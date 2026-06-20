#include "libreshockwave/player/render/output/SimpleTextRenderer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "libreshockwave/cast/StyledSpan.hpp"
#include "libreshockwave/cast/XmedStyledText.hpp"
#include "libreshockwave/player/cast/FontRegistry.hpp"
#include "libreshockwave/player/cast/MacFontBundle.hpp"
#include "libreshockwave/player/cast/WindowsFontBundle.hpp"

namespace libreshockwave::player::render::output {
namespace {

using bitmap::Bitmap;
using font::BitmapFont;
using ::libreshockwave::cast::StyledSpan;
using ::libreshockwave::cast::XmedStyledText;
using ::libreshockwave::player::cast::FontRegistry;
using ::libreshockwave::player::cast::MacFontBundle;
using ::libreshockwave::player::cast::WindowsFontBundle;

struct ResolvedXmedSpan {
    std::shared_ptr<BitmapFont> font;
    bool syntheticBold = false;
    bool underline = false;
    std::uint32_t color = 0xFF000000U;
};

struct StyledLine {
    int start = 0;
    int end = 0;
    int width = 0;
    int maxLineHeight = 0;
    bool paragraphBreakBefore = false;
};

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

int alignmentOffset(std::string_view alignment, int fieldWidth, int lineWidth) {
    if (fieldWidth <= 0) {
        return 0;
    }
    if (alignment == "center") {
        return std::max(0, (fieldWidth - lineWidth) / 2);
    }
    if (alignment == "right") {
        return std::max(0, fieldWidth - lineWidth);
    }
    return 0;
}

int renderAlignmentX(std::string_view alignment, int width, int lineWidth) {
    if (alignment == "center") {
        return (width - lineWidth) / 2;
    }
    if (alignment == "right") {
        return width - lineWidth;
    }
    return 0;
}

bool isDefaultLeading(const BitmapFont* font, int lineHeight, int topSpacing) {
    return topSpacing == 1 && font != nullptr && lineHeight == font->getFontSize();
}

int bitmapLineAdvance(const BitmapFont* font, int lineHeight, int topSpacing) {
    return lineHeight + (isDefaultLeading(font, lineHeight, topSpacing) ? 0 : topSpacing);
}

int excludedLeading(const BitmapFont* font, int lineHeight, int topSpacing) {
    return isDefaultLeading(font, lineHeight, topSpacing) ? topSpacing : 0;
}

int initialBitmapTextY(const BitmapFont* font, int lineHeight, int topSpacing) {
    return topSpacing + (topSpacing > 1 ? 1 : 0);
}

int findInkBottom(const std::vector<std::uint32_t>& pixels,
                  int width,
                  int height,
                  int startX,
                  int endX,
                  int startY,
                  int endY) {
    const int clampedStartX = std::max(0, startX);
    const int clampedEndX = std::min(width, endX);
    const int clampedStartY = std::max(0, startY);
    const int clampedEndY = std::min(height - 1, endY);
    for (int y = clampedEndY; y >= clampedStartY; --y) {
        for (int x = clampedStartX; x < clampedEndX; ++x) {
            if (((pixels[static_cast<std::size_t>(y * width + x)] >> 24U) & 0xFFU) != 0) {
                return y;
            }
        }
    }
    return clampedStartY;
}

std::array<int, 2> findHorizontalInkBounds(const std::vector<std::uint32_t>& pixels,
                                           int width,
                                           int height,
                                           int startX,
                                           int endX,
                                           int startY,
                                           int endY) {
    const int clampedStartX = std::max(0, startX);
    const int clampedEndX = std::min(width, endX);
    const int clampedStartY = std::max(0, startY);
    const int clampedEndY = std::min(height - 1, endY);
    int left = clampedEndX;
    int right = clampedStartX;
    for (int y = clampedStartY; y <= clampedEndY; ++y) {
        for (int x = clampedStartX; x < clampedEndX; ++x) {
            if (((pixels[static_cast<std::size_t>(y * width + x)] >> 24U) & 0xFFU) != 0) {
                left = std::min(left, x);
                right = std::max(right, x + 1);
            }
        }
    }
    if (left >= right) {
        return {clampedStartX, clampedEndX};
    }
    return {left, right};
}

int extendUnderlineRightEdge(int inkRightX, int spanEndX, int width) {
    return std::min(width, std::min(spanEndX, inkRightX + 1));
}

void drawUnderline(std::vector<std::uint32_t>& pixels,
                   int width,
                   int height,
                   int y,
                   int lineStartX,
                   int lineEndX,
                   std::uint32_t textColor) {
    if (y < 0 || y >= height) {
        return;
    }
    for (int x = std::max(0, lineStartX); x < std::min(width, lineEndX); ++x) {
        pixels[static_cast<std::size_t>(y * width + x)] = textColor;
    }
}

bool isLineBreak(char ch) {
    return ch == '\r' || ch == '\n';
}

bool xmedDisplayNameImpliesBold(std::string_view fontName) {
    return lowerAscii(std::string(fontName)).find("bold") != std::string::npos;
}

bool isPreferredDirectorPixelDisplayName(std::string_view fontName) {
    return FontRegistry::canonicalFontName(std::string(fontName)).starts_with("volter");
}

bool isPlatformFontRequest(const std::string& fontName) {
    return WindowsFontBundle::hasWindowsFont(fontName) || MacFontBundle::hasMacFont(fontName);
}

std::shared_ptr<BitmapFont> preferredDirectorFontAtSize(const std::string& fontName,
                                                        int fontSize,
                                                        bool bold,
                                                        bool* usedRealBold) {
    if (auto font = FontRegistry::getEmbeddedBitmapFont(fontName, fontSize, bold, false); font != nullptr) {
        if (usedRealBold != nullptr) {
            *usedRealBold = bold && FontRegistry::hasEmbeddedBoldVariant(fontName);
        }
        return font;
    }
    if (auto font = FontRegistry::getPfrBitmapFont(fontName, fontSize); font != nullptr) {
        if (usedRealBold != nullptr) {
            *usedRealBold = false;
        }
        return font;
    }
    return nullptr;
}

int representativeInkHeight(const BitmapFont& font) {
    constexpr std::string_view sample = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    const int width = std::max(1, font.getStringWidth(sample));
    const int height = std::max(1, font.cellHeight());
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width * height), 0);
    int x = 0;
    for (const auto ch : sample) {
        font.drawChar(static_cast<unsigned char>(ch), pixels, width, height, x, 0, 0xFF000000U);
        x += font.getCharWidth(static_cast<unsigned char>(ch));
    }

    int top = height;
    int bottom = -1;
    for (int y = 0; y < height; ++y) {
        for (int px = 0; px < width; ++px) {
            if (((pixels[static_cast<std::size_t>(y * width + px)] >> 24U) & 0xFFU) != 0) {
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    }
    return bottom >= top ? bottom - top + 1 : 0;
}

std::shared_ptr<BitmapFont> resolvePreferredDirectorPixelFont(const std::string& fontName,
                                                              int fontSize,
                                                              bool bold,
                                                              bool italic,
                                                              bool* usedRealBold) {
    if (italic) {
        return nullptr;
    }

    std::vector<std::string> candidates;
    const auto normalizedName = FontRegistry::canonicalFontName(fontName);
    if (normalizedName.starts_with("volter")) {
        candidates.emplace_back("Volter");
    }
    if (const auto preferred = FontRegistry::getPreferredDirectorPixelFont();
        preferred.has_value() && !preferred->empty() &&
        std::find(candidates.begin(), candidates.end(), *preferred) == candidates.end()) {
        candidates.push_back(*preferred);
    }

    const bool resolvedBold = bold || xmedDisplayNameImpliesBold(fontName);
    for (const auto& candidate : candidates) {
        if (auto font = FontRegistry::getBitmapFont(candidate, fontSize, resolvedBold, false); font != nullptr) {
            if (usedRealBold != nullptr) {
                *usedRealBold = resolvedBold && FontRegistry::hasEmbeddedBoldVariant(candidate);
            }
            return font;
        }
        if (auto font = FontRegistry::getBitmapFont(candidate, fontSize, false, false); font != nullptr) {
            if (usedRealBold != nullptr) {
                *usedRealBold = false;
            }
            return font;
        }
    }
    return nullptr;
}

std::shared_ptr<BitmapFont> resolvePreferredDirectorPixelFallback(int fontSize,
                                                                  bool bold,
                                                                  bool italic,
                                                                  bool* usedRealBold) {
    if (italic) {
        return nullptr;
    }
    const auto preferred = FontRegistry::getPreferredDirectorPixelFont();
    if (!preferred.has_value() || preferred->empty()) {
        return nullptr;
    }

    std::shared_ptr<BitmapFont> bestFit;
    int bestFitSize = -1;
    int bestFitInkHeight = -1;
    std::shared_ptr<BitmapFont> nearest;
    int nearestDistance = std::numeric_limits<int>::max();
    int nearestSize = -1;
    const int maxCandidateSize = std::max(1, fontSize + 2);
    for (int candidateSize = 1; candidateSize <= maxCandidateSize; ++candidateSize) {
        auto candidate = preferredDirectorFontAtSize(*preferred, candidateSize, bold, nullptr);
        if (candidate == nullptr) {
            continue;
        }
        const int inkHeight = representativeInkHeight(*candidate);
        const int distance = std::abs(inkHeight - fontSize);
        if (distance < nearestDistance || (distance == nearestDistance && candidateSize > nearestSize)) {
            nearest = candidate;
            nearestDistance = distance;
            nearestSize = candidateSize;
        }
        if (inkHeight > 0 && inkHeight < fontSize &&
            (inkHeight > bestFitInkHeight ||
             (inkHeight == bestFitInkHeight && candidateSize > bestFitSize))) {
            bestFit = candidate;
            bestFitInkHeight = inkHeight;
            bestFitSize = candidateSize;
        }
    }

    auto selected = bestFit != nullptr ? bestFit : nearest;
    if (selected != nullptr && usedRealBold != nullptr) {
        *usedRealBold = bold && FontRegistry::hasEmbeddedBoldVariant(*preferred);
    }
    return selected;
}

int widthForChar(const std::vector<ResolvedXmedSpan>& spans, int index, char ch) {
    if (index < 0 || index >= static_cast<int>(spans.size()) || spans[static_cast<std::size_t>(index)].font == nullptr) {
        return 0;
    }
    return spans[static_cast<std::size_t>(index)].font->getCharWidth(static_cast<unsigned char>(ch));
}

int measureStyledWidth(const std::string& text,
                       const std::vector<ResolvedXmedSpan>& spans,
                       int start,
                       int end) {
    int width = 0;
    for (int index = start; index < end; ++index) {
        const char ch = text[static_cast<std::size_t>(index)];
        if (!isLineBreak(ch)) {
            width += widthForChar(spans, index, ch);
        }
    }
    return width;
}

int measureMaxLineHeight(const std::vector<ResolvedXmedSpan>& spans, int start, int end) {
    int maximum = 0;
    for (int index = start; index < end; ++index) {
        const auto& span = spans[static_cast<std::size_t>(index)];
        if (span.font != nullptr) {
            maximum = std::max(maximum, span.font->getLineHeight());
        }
    }
    return maximum;
}

StyledLine createStyledLine(const std::string& text,
                            const std::vector<ResolvedXmedSpan>& spans,
                            int start,
                            int end,
                            bool paragraphBreakBefore) {
    return StyledLine{start,
                      end,
                      measureStyledWidth(text, spans, start, end),
                      measureMaxLineHeight(spans, start, end),
                      paragraphBreakBefore};
}

void wrapStyledRange(const std::string& text,
                     const std::vector<ResolvedXmedSpan>& spans,
                     int start,
                     int end,
                     int maxWidth,
                     std::vector<StyledLine>& out,
                     bool paragraphBreakBefore) {
    int currentStart = start;
    bool firstWrappedLine = true;
    while (currentStart < end) {
        int pos = currentStart;
        int width = 0;
        int lastBreak = -1;
        while (pos < end) {
            const char ch = text[static_cast<std::size_t>(pos)];
            const int charWidth = widthForChar(spans, pos, ch);
            if (width + charWidth > maxWidth && pos > currentStart) {
                break;
            }
            width += charWidth;
            if (std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == '-') {
                lastBreak = pos + 1;
            }
            ++pos;
        }
        if (pos >= end) {
            out.push_back(createStyledLine(text, spans, currentStart, end, paragraphBreakBefore && firstWrappedLine));
            return;
        }

        int breakPos = lastBreak > currentStart ? lastBreak : pos;
        int trimmedEnd = breakPos;
        while (trimmedEnd > currentStart &&
               std::isspace(static_cast<unsigned char>(text[static_cast<std::size_t>(trimmedEnd - 1)])) != 0) {
            --trimmedEnd;
        }
        out.push_back(createStyledLine(text,
                                       spans,
                                       currentStart,
                                       trimmedEnd,
                                       paragraphBreakBefore && firstWrappedLine));
        firstWrappedLine = false;
        currentStart = breakPos;
        while (currentStart < end &&
               std::isspace(static_cast<unsigned char>(text[static_cast<std::size_t>(currentStart)])) != 0) {
            ++currentStart;
        }
        if (currentStart == breakPos && breakPos == pos) {
            currentStart = std::min(end, pos + 1);
        }
    }
}

std::vector<StyledLine> layoutStyledLines(const std::string& text,
                                          const std::vector<ResolvedXmedSpan>& spans,
                                          int maxWidth,
                                          bool wordWrap) {
    std::vector<StyledLine> lines;
    const int textLength = static_cast<int>(text.size());
    int lineStart = 0;
    while (lineStart <= textLength) {
        int lineEnd = lineStart;
        while (lineEnd < textLength && !isLineBreak(text[static_cast<std::size_t>(lineEnd)])) {
            ++lineEnd;
        }
        if (!wordWrap || measureStyledWidth(text, spans, lineStart, lineEnd) <= maxWidth) {
            lines.push_back(createStyledLine(text, spans, lineStart, lineEnd, lineStart > 0));
        } else {
            wrapStyledRange(text, spans, lineStart, lineEnd, maxWidth, lines, lineStart > 0);
        }
        if (lineEnd >= textLength) {
            break;
        }
        if (text[static_cast<std::size_t>(lineEnd)] == '\r' &&
            lineEnd + 1 < textLength &&
            text[static_cast<std::size_t>(lineEnd + 1)] == '\n') {
            lineStart = lineEnd + 2;
        } else {
            lineStart = lineEnd + 1;
        }
        if (lineStart == textLength) {
            lines.push_back(createStyledLine(text, spans, textLength, textLength, true));
            break;
        }
    }
    if (lines.empty()) {
        lines.push_back(StyledLine{});
    }
    return lines;
}

bool needsPerSpanBitmapRendering(const XmedStyledText& styledText,
                                 const std::vector<ResolvedXmedSpan>& spans) {
    if (styledText.text.empty() || spans.empty()) {
        return false;
    }

    const ResolvedXmedSpan* first = nullptr;
    for (int index = 0; index < static_cast<int>(spans.size()); ++index) {
        if (!isLineBreak(styledText.text[static_cast<std::size_t>(index)])) {
            first = &spans[static_cast<std::size_t>(index)];
            break;
        }
    }
    if (first == nullptr) {
        return false;
    }

    for (int index = 0; index < static_cast<int>(spans.size()); ++index) {
        if (isLineBreak(styledText.text[static_cast<std::size_t>(index)])) {
            continue;
        }
        const auto& current = spans[static_cast<std::size_t>(index)];
        if (current.font != first->font ||
            current.syntheticBold != first->syntheticBold ||
            current.underline != first->underline ||
            current.color != first->color) {
            return true;
        }
    }
    return false;
}

void drawStyledUnderline(std::vector<std::uint32_t>& pixels,
                         int width,
                         int height,
                         int glyphY,
                         int startX,
                         int endX,
                         int lineHeight,
                         std::uint32_t color) {
    const int bottom = findInkBottom(pixels,
                                     width,
                                     height,
                                     startX,
                                     endX,
                                     glyphY,
                                     std::min(height - 1, glyphY + std::max(0, lineHeight - 1)));
    const auto bounds = findHorizontalInkBounds(pixels,
                                                width,
                                                height,
                                                startX,
                                                endX,
                                                glyphY,
                                                std::min(height - 1, glyphY + std::max(0, lineHeight - 1)));
    const int underlineY = std::min(height - 1, std::max(glyphY, bottom + 2));
    drawUnderline(pixels, width, height, underlineY, bounds[0], extendUnderlineRightEdge(bounds[1], endX, width), color);
}

std::shared_ptr<Bitmap> applyTextAA(const Bitmap& bitmap) {
    const int width = bitmap.width();
    const int height = bitmap.height();
    const auto& src = bitmap.pixels();
    auto dst = src;
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            const auto index = static_cast<std::size_t>(y * width + x);
            const auto current = src[index];
            bool boundary = false;
            for (int dy = -1; dy <= 1 && !boundary; ++dy) {
                for (int dx = -1; dx <= 1 && !boundary; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    if (src[static_cast<std::size_t>((y + dy) * width + (x + dx))] != current) {
                        boundary = true;
                    }
                }
            }
            if (!boundary) {
                continue;
            }

            int a = 0;
            int r = 0;
            int g = 0;
            int b = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const auto pixel = src[static_cast<std::size_t>((y + dy) * width + (x + dx))];
                    a += static_cast<int>((pixel >> 24U) & 0xFFU);
                    r += static_cast<int>((pixel >> 16U) & 0xFFU);
                    g += static_cast<int>((pixel >> 8U) & 0xFFU);
                    b += static_cast<int>(pixel & 0xFFU);
                }
            }
            dst[index] = (static_cast<std::uint32_t>(a / 9) << 24U) |
                         (static_cast<std::uint32_t>(r / 9) << 16U) |
                         (static_cast<std::uint32_t>(g / 9) << 8U) |
                         static_cast<std::uint32_t>(b / 9);
        }
    }
    auto result = std::make_shared<Bitmap>(width, height, 32, std::move(dst));
    result->markScriptModified();
    return result;
}

int builtinScale(int fontSize) {
    return std::max(1, fontSize / 8);
}

int builtinCharWidth(int fontSize) {
    return 6 * builtinScale(fontSize);
}

int builtinLineHeight(int fontSize) {
    return 9 * builtinScale(fontSize);
}

int builtinAscent(int fontSize) {
    return 7 * builtinScale(fontSize);
}

constexpr std::array<std::array<std::uint8_t, 5>, 95> FONT_5X7{{
    {{0x00, 0x00, 0x00, 0x00, 0x00}}, // 32 space
    {{0x00, 0x00, 0x5F, 0x00, 0x00}}, // 33 !
    {{0x00, 0x07, 0x00, 0x07, 0x00}}, // 34 "
    {{0x14, 0x7F, 0x14, 0x7F, 0x14}}, // 35 #
    {{0x24, 0x2A, 0x7F, 0x2A, 0x12}}, // 36 $
    {{0x23, 0x13, 0x08, 0x64, 0x62}}, // 37 %
    {{0x36, 0x49, 0x55, 0x22, 0x50}}, // 38 &
    {{0x00, 0x05, 0x03, 0x00, 0x00}}, // 39 '
    {{0x00, 0x1C, 0x22, 0x41, 0x00}}, // 40 (
    {{0x00, 0x41, 0x22, 0x1C, 0x00}}, // 41 )
    {{0x14, 0x08, 0x3E, 0x08, 0x14}}, // 42 *
    {{0x08, 0x08, 0x3E, 0x08, 0x08}}, // 43 +
    {{0x00, 0x50, 0x30, 0x00, 0x00}}, // 44 ,
    {{0x08, 0x08, 0x08, 0x08, 0x08}}, // 45 -
    {{0x00, 0x60, 0x60, 0x00, 0x00}}, // 46 .
    {{0x20, 0x10, 0x08, 0x04, 0x02}}, // 47 /
    {{0x3E, 0x51, 0x49, 0x45, 0x3E}}, // 48 0
    {{0x00, 0x42, 0x7F, 0x40, 0x00}}, // 49 1
    {{0x42, 0x61, 0x51, 0x49, 0x46}}, // 50 2
    {{0x21, 0x41, 0x45, 0x4B, 0x31}}, // 51 3
    {{0x18, 0x14, 0x12, 0x7F, 0x10}}, // 52 4
    {{0x27, 0x45, 0x45, 0x45, 0x39}}, // 53 5
    {{0x3C, 0x4A, 0x49, 0x49, 0x30}}, // 54 6
    {{0x01, 0x71, 0x09, 0x05, 0x03}}, // 55 7
    {{0x36, 0x49, 0x49, 0x49, 0x36}}, // 56 8
    {{0x06, 0x49, 0x49, 0x29, 0x1E}}, // 57 9
    {{0x00, 0x36, 0x36, 0x00, 0x00}}, // 58 :
    {{0x00, 0x56, 0x36, 0x00, 0x00}}, // 59 ;
    {{0x08, 0x14, 0x22, 0x41, 0x00}}, // 60 <
    {{0x14, 0x14, 0x14, 0x14, 0x14}}, // 61 =
    {{0x00, 0x41, 0x22, 0x14, 0x08}}, // 62 >
    {{0x02, 0x01, 0x51, 0x09, 0x06}}, // 63 ?
    {{0x32, 0x49, 0x79, 0x41, 0x3E}}, // 64 @
    {{0x7E, 0x11, 0x11, 0x11, 0x7E}}, // 65 A
    {{0x7F, 0x49, 0x49, 0x49, 0x36}}, // 66 B
    {{0x3E, 0x41, 0x41, 0x41, 0x22}}, // 67 C
    {{0x7F, 0x41, 0x41, 0x22, 0x1C}}, // 68 D
    {{0x7F, 0x49, 0x49, 0x49, 0x41}}, // 69 E
    {{0x7F, 0x09, 0x09, 0x09, 0x01}}, // 70 F
    {{0x3E, 0x41, 0x49, 0x49, 0x7A}}, // 71 G
    {{0x7F, 0x08, 0x08, 0x08, 0x7F}}, // 72 H
    {{0x00, 0x41, 0x7F, 0x41, 0x00}}, // 73 I
    {{0x20, 0x40, 0x41, 0x3F, 0x01}}, // 74 J
    {{0x7F, 0x08, 0x14, 0x22, 0x41}}, // 75 K
    {{0x7F, 0x40, 0x40, 0x40, 0x40}}, // 76 L
    {{0x7F, 0x02, 0x0C, 0x02, 0x7F}}, // 77 M
    {{0x7F, 0x04, 0x08, 0x10, 0x7F}}, // 78 N
    {{0x3E, 0x41, 0x41, 0x41, 0x3E}}, // 79 O
    {{0x7F, 0x09, 0x09, 0x09, 0x06}}, // 80 P
    {{0x3E, 0x41, 0x51, 0x21, 0x5E}}, // 81 Q
    {{0x7F, 0x09, 0x19, 0x29, 0x46}}, // 82 R
    {{0x46, 0x49, 0x49, 0x49, 0x31}}, // 83 S
    {{0x01, 0x01, 0x7F, 0x01, 0x01}}, // 84 T
    {{0x3F, 0x40, 0x40, 0x40, 0x3F}}, // 85 U
    {{0x1F, 0x20, 0x40, 0x20, 0x1F}}, // 86 V
    {{0x3F, 0x40, 0x38, 0x40, 0x3F}}, // 87 W
    {{0x63, 0x14, 0x08, 0x14, 0x63}}, // 88 X
    {{0x07, 0x08, 0x70, 0x08, 0x07}}, // 89 Y
    {{0x61, 0x51, 0x49, 0x45, 0x43}}, // 90 Z
    {{0x00, 0x7F, 0x41, 0x41, 0x00}}, // 91 [
    {{0x02, 0x04, 0x08, 0x10, 0x20}}, // 92 backslash
    {{0x00, 0x41, 0x41, 0x7F, 0x00}}, // 93 ]
    {{0x04, 0x02, 0x01, 0x02, 0x04}}, // 94 ^
    {{0x40, 0x40, 0x40, 0x40, 0x40}}, // 95 _
    {{0x00, 0x01, 0x02, 0x04, 0x00}}, // 96 `
    {{0x20, 0x54, 0x54, 0x54, 0x78}}, // 97 a
    {{0x7F, 0x48, 0x44, 0x44, 0x38}}, // 98 b
    {{0x38, 0x44, 0x44, 0x44, 0x20}}, // 99 c
    {{0x38, 0x44, 0x44, 0x48, 0x7F}}, // 100 d
    {{0x38, 0x54, 0x54, 0x54, 0x18}}, // 101 e
    {{0x08, 0x7E, 0x09, 0x01, 0x02}}, // 102 f
    {{0x0C, 0x52, 0x52, 0x52, 0x3E}}, // 103 g
    {{0x7F, 0x08, 0x04, 0x04, 0x78}}, // 104 h
    {{0x00, 0x44, 0x7D, 0x40, 0x00}}, // 105 i
    {{0x20, 0x40, 0x44, 0x3D, 0x00}}, // 106 j
    {{0x7F, 0x10, 0x28, 0x44, 0x00}}, // 107 k
    {{0x00, 0x41, 0x7F, 0x40, 0x00}}, // 108 l
    {{0x7C, 0x04, 0x18, 0x04, 0x78}}, // 109 m
    {{0x7C, 0x08, 0x04, 0x04, 0x78}}, // 110 n
    {{0x38, 0x44, 0x44, 0x44, 0x38}}, // 111 o
    {{0x7C, 0x14, 0x14, 0x14, 0x08}}, // 112 p
    {{0x08, 0x14, 0x14, 0x18, 0x7C}}, // 113 q
    {{0x7C, 0x08, 0x04, 0x04, 0x08}}, // 114 r
    {{0x48, 0x54, 0x54, 0x54, 0x20}}, // 115 s
    {{0x04, 0x3F, 0x44, 0x40, 0x20}}, // 116 t
    {{0x3C, 0x40, 0x40, 0x20, 0x7C}}, // 117 u
    {{0x1C, 0x20, 0x40, 0x20, 0x1C}}, // 118 v
    {{0x3C, 0x40, 0x30, 0x40, 0x3C}}, // 119 w
    {{0x44, 0x28, 0x10, 0x28, 0x44}}, // 120 x
    {{0x0C, 0x50, 0x50, 0x50, 0x3C}}, // 121 y
    {{0x44, 0x64, 0x54, 0x4C, 0x44}}, // 122 z
    {{0x00, 0x08, 0x36, 0x41, 0x00}}, // 123 {
    {{0x00, 0x00, 0x7F, 0x00, 0x00}}, // 124 |
    {{0x00, 0x41, 0x36, 0x08, 0x00}}, // 125 }
    {{0x10, 0x08, 0x08, 0x10, 0x08}}, // 126 ~
}};

void drawBuiltinChar(char ch,
                     std::vector<std::uint32_t>& pixels,
                     int width,
                     int height,
                     int x,
                     int baselineY,
                     int scale,
                     std::uint32_t color) {
    int index = static_cast<int>(static_cast<unsigned char>(ch)) - 32;
    if (index < 0 || index >= static_cast<int>(FONT_5X7.size())) {
        index = 0;
    }
    const auto& glyph = FONT_5X7[static_cast<std::size_t>(index)];
    const int topY = baselineY - 7 * scale;
    for (int col = 0; col < 5; ++col) {
        const int bits = glyph[static_cast<std::size_t>(col)];
        for (int row = 0; row < 7; ++row) {
            if ((bits & (1 << row)) == 0) {
                continue;
            }
            for (int sy = 0; sy < scale; ++sy) {
                for (int sx = 0; sx < scale; ++sx) {
                    const int px = x + col * scale + sx;
                    const int py = topY + row * scale + sy;
                    if (px >= 0 && px < width && py >= 0 && py < height) {
                        pixels[static_cast<std::size_t>(py * width + px)] = color;
                    }
                }
            }
        }
    }
}

std::shared_ptr<Bitmap> renderWithBitmapFont(const std::shared_ptr<BitmapFont>& font,
                                             const std::string& text,
                                             int width,
                                             int height,
                                             std::string_view alignment,
                                             std::uint32_t textColor,
                                             std::uint32_t bgColor,
                                             bool wordWrap,
                                             int fixedLineSpace,
                                             int topSpacing,
                                             bool syntheticBold,
                                             bool underline) {
    const int lineHeight = fixedLineSpace > 0 ? fixedLineSpace : font->getLineHeight();
    std::vector<std::string> lines;
    for (const auto& rawLine : TextRenderer::splitLines(text)) {
        if (wordWrap) {
            TextRenderer::wrapLine(rawLine,
                                   [&font](std::string_view value) {
                                       return font->getStringWidth(value);
                                   },
                                   width,
                                   lines);
        } else {
            lines.push_back(rawLine);
        }
    }

    const int lineAdvance = bitmapLineAdvance(font.get(), lineHeight, topSpacing);
    const int neededHeight = static_cast<int>(lines.size()) * lineAdvance +
                             excludedLeading(font.get(), lineHeight, topSpacing);
    height = std::max(height, neededHeight);
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width * height), bgColor);

    const int leading = std::max(0, lineHeight - font->getLineHeight());
    const int verticalOverflow = std::max(0, font->getLineHeight() - lineHeight);
    int y = initialBitmapTextY(font.get(), lineHeight, topSpacing);
    if (fixedLineSpace <= 0 && topSpacing == 0) {
        ++y;
    }
    for (const auto& line : lines) {
        if (y >= height) {
            break;
        }
        const int lineWidth = font->getStringWidth(line);
        int x = renderAlignmentX(alignment, width, lineWidth);
        const int lineStartX = x;
        const int glyphY = y + leading - verticalOverflow;
        for (char ch : line) {
            font->drawChar(static_cast<unsigned char>(ch), pixels, width, height, x, glyphY, textColor);
            if (syntheticBold) {
                font->drawChar(static_cast<unsigned char>(ch), pixels, width, height, x + 1, glyphY, textColor);
            }
            x += font->getCharWidth(static_cast<unsigned char>(ch));
        }
        if (underline && !line.empty()) {
            const int bottom = findInkBottom(pixels,
                                             width,
                                             height,
                                             lineStartX,
                                             x,
                                             glyphY,
                                             std::min(height - 1, glyphY + font->getLineHeight() - 1));
            const auto bounds = findHorizontalInkBounds(pixels,
                                                        width,
                                                        height,
                                                        lineStartX,
                                                        x,
                                                        glyphY,
                                                        std::min(height - 1, glyphY + font->getLineHeight() - 1));
            const int underlineY = std::min(height - 1, std::max(glyphY, bottom + 1));
            drawUnderline(pixels,
                          width,
                          height,
                          underlineY,
                          bounds[0],
                          extendUnderlineRightEdge(bounds[1], x, width),
                          textColor);
        }
        y += lineAdvance;
    }

    auto bitmap = std::make_shared<Bitmap>(width, height, 32, std::move(pixels));
    bitmap->markScriptModified();
    return bitmap;
}

void underlineStyledSpans(const std::shared_ptr<Bitmap>& bitmap,
                          const std::shared_ptr<BitmapFont>& font,
                          const XmedStyledText& styledText,
                          int topSpacing,
                          std::uint32_t textColor) {
    if (bitmap == nullptr || font == nullptr || styledText.styledSpans.empty()) {
        return;
    }

    bool hasUnderline = false;
    for (const auto& span : styledText.styledSpans) {
        if (span.underline) {
            hasUnderline = true;
            break;
        }
    }
    if (!hasUnderline || styledText.text.empty()) {
        return;
    }

    auto& pixels = bitmap->pixels();
    const int width = bitmap->width();
    const int height = bitmap->height();
    const int lineHeight = styledText.fixedLineSpace > 0 ? styledText.fixedLineSpace : font->getLineHeight();

    int y = topSpacing;
    int lineStart = 0;
    while (lineStart <= static_cast<int>(styledText.text.size()) && y < height) {
        int lineEnd = lineStart;
        while (lineEnd < static_cast<int>(styledText.text.size()) &&
               !isLineBreak(styledText.text[static_cast<std::size_t>(lineEnd)])) {
            ++lineEnd;
        }

        const auto line = std::string_view(styledText.text).substr(
            static_cast<std::size_t>(lineStart),
            static_cast<std::size_t>(lineEnd - lineStart));
        const int lineWidth = font->getStringWidth(line);
        const int lineX = renderAlignmentX(styledText.alignment, width, lineWidth);
        const int glyphY = y;

        for (const auto& span : styledText.styledSpans) {
            if (!span.underline) {
                continue;
            }
            const int start = std::max(lineStart, span.startOffset);
            const int end = std::min(lineEnd, span.endOffset);
            if (start >= end) {
                continue;
            }
            const auto prefixToStart = std::string_view(styledText.text).substr(
                static_cast<std::size_t>(lineStart),
                static_cast<std::size_t>(start - lineStart));
            const auto prefixToEnd = std::string_view(styledText.text).substr(
                static_cast<std::size_t>(lineStart),
                static_cast<std::size_t>(end - lineStart));
            const int startX = lineX + font->getStringWidth(prefixToStart);
            const int endX = lineX + font->getStringWidth(prefixToEnd);
            const int bottom = findInkBottom(pixels,
                                             width,
                                             height,
                                             startX,
                                             endX,
                                             glyphY,
                                             std::min(height - 1, glyphY + font->getLineHeight() - 1));
            const auto bounds = findHorizontalInkBounds(pixels,
                                                        width,
                                                        height,
                                                        startX,
                                                        endX,
                                                        glyphY,
                                                        std::min(height - 1, glyphY + font->getLineHeight() - 1));
            const int underlineY = std::min(height - 1, std::max(glyphY, bottom + 1));
            drawUnderline(pixels,
                          width,
                          height,
                          underlineY,
                          bounds[0],
                          extendUnderlineRightEdge(bounds[1], endX, width),
                          textColor);
        }

        if (lineEnd >= static_cast<int>(styledText.text.size())) {
            break;
        }
        if (styledText.text[static_cast<std::size_t>(lineEnd)] == '\r' &&
            lineEnd + 1 < static_cast<int>(styledText.text.size()) &&
            styledText.text[static_cast<std::size_t>(lineEnd + 1)] == '\n') {
            lineStart = lineEnd + 2;
        } else {
            lineStart = lineEnd + 1;
        }
        y += lineHeight;
    }
}

std::shared_ptr<Bitmap> renderWithBuiltinFont(const std::string& text,
                                              int width,
                                              int height,
                                              int fontSize,
                                              std::string_view alignment,
                                              std::uint32_t textColor,
                                              std::uint32_t bgColor,
                                              bool wordWrap,
                                              int fixedLineSpace,
                                              int topSpacing,
                                              bool underline) {
    const int charWidth = builtinCharWidth(fontSize);
    const int lineHeight = fixedLineSpace > 0 ? fixedLineSpace : builtinLineHeight(fontSize);
    const int ascent = builtinAscent(fontSize);
    const int scale = builtinScale(fontSize);

    std::vector<std::string> lines;
    for (const auto& rawLine : TextRenderer::splitLines(text)) {
        if (wordWrap) {
            TextRenderer::wrapLine(rawLine,
                                   [charWidth](std::string_view value) {
                                       return static_cast<int>(value.size()) * charWidth;
                                   },
                                   width,
                                   lines);
        } else {
            lines.push_back(rawLine);
        }
    }

    const int lineAdvance = lineHeight + topSpacing;
    const int neededHeight = static_cast<int>(lines.size()) * lineAdvance;
    height = std::max(height, neededHeight);
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width * height), bgColor);

    int y = topSpacing;
    for (const auto& line : lines) {
        if (y >= height) {
            break;
        }
        int x = renderAlignmentX(alignment, width, static_cast<int>(line.size()) * charWidth);
        const int lineStartX = x;
        for (char ch : line) {
            drawBuiltinChar(ch, pixels, width, height, x, y + ascent, scale, textColor);
            x += charWidth;
        }
        if (underline && !line.empty()) {
            const int glyphTop = std::max(0, y);
            const int glyphBottom = findInkBottom(pixels,
                                                  width,
                                                  height,
                                                  lineStartX,
                                                  x,
                                                  glyphTop,
                                                  std::min(height - 1, y + ascent));
            drawUnderline(pixels, width, height, std::min(height - 1, glyphBottom + 1), lineStartX, x, textColor);
        }
        y += lineAdvance;
    }

    auto bitmap = std::make_shared<Bitmap>(width, height, 32, std::move(pixels));
    bitmap->markScriptModified();
    return bitmap;
}

std::shared_ptr<Bitmap> renderStyledXmedText(const std::vector<ResolvedXmedSpan>& spans,
                                             const XmedStyledText& styledText,
                                             int width,
                                             int height,
                                             std::uint32_t bgColor) {
    const auto lines = layoutStyledLines(styledText.text, spans, width, styledText.wordWrap);
    int maxFontLineHeight = 0;
    for (const auto& span : spans) {
        if (span.font != nullptr) {
            maxFontLineHeight = std::max(maxFontLineHeight, span.font->getLineHeight());
        }
    }
    if (maxFontLineHeight <= 0) {
        maxFontLineHeight = styledText.fontSize;
    }
    const int lineHeight = styledText.fixedLineSpace > 0 ? styledText.fixedLineSpace : maxFontLineHeight;
    const int safeLineHeight = std::max(1, lineHeight);
    int neededHeight = std::max(height, std::max(1, static_cast<int>(lines.size())) * safeLineHeight);
    int paragraphBreakCount = 0;
    for (const auto& line : lines) {
        if (line.paragraphBreakBefore) {
            ++paragraphBreakCount;
        }
    }
    const int paragraphGap = paragraphBreakCount > 0
        ? std::max(0, (height - std::max(1, static_cast<int>(lines.size())) * safeLineHeight) / (paragraphBreakCount + 1))
        : 0;

    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width * neededHeight), bgColor);
    int y = styledText.fixedLineSpace > 0 ? 0 : 1;
    for (const auto& line : lines) {
        if (y >= neededHeight) {
            break;
        }
        if (line.paragraphBreakBefore) {
            y += paragraphGap;
            if (y >= neededHeight) {
                break;
            }
        }

        const int lineX = alignmentOffset(styledText.alignment, width, line.width);
        int x = lineX;
        for (int index = line.start; index < line.end; ++index) {
            const char ch = styledText.text[static_cast<std::size_t>(index)];
            if (isLineBreak(ch)) {
                continue;
            }
            const auto& span = spans[static_cast<std::size_t>(index)];
            if (span.font == nullptr) {
                continue;
            }
            span.font->drawChar(static_cast<unsigned char>(ch), pixels, width, neededHeight, x, y, span.color);
            if (span.syntheticBold) {
                span.font->drawChar(static_cast<unsigned char>(ch), pixels, width, neededHeight, x + 1, y, span.color);
            }
            x += span.font->getCharWidth(static_cast<unsigned char>(ch));
        }

        int runStart = -1;
        int runStartX = 0;
        x = lineX;
        for (int index = line.start; index < line.end; ++index) {
            const char ch = styledText.text[static_cast<std::size_t>(index)];
            if (isLineBreak(ch)) {
                continue;
            }
            const auto& span = spans[static_cast<std::size_t>(index)];
            const int charWidth = span.font != nullptr ? span.font->getCharWidth(static_cast<unsigned char>(ch)) : 0;
            if (span.underline) {
                if (runStart < 0) {
                    runStart = index;
                    runStartX = x;
                }
            } else if (runStart >= 0) {
                drawStyledUnderline(pixels,
                                    width,
                                    neededHeight,
                                    y,
                                    runStartX,
                                    x,
                                    line.maxLineHeight,
                                    spans[static_cast<std::size_t>(runStart)].color);
                runStart = -1;
            }
            x += charWidth;
        }
        if (runStart >= 0) {
            drawStyledUnderline(pixels,
                                width,
                                neededHeight,
                                y,
                                runStartX,
                                x,
                                line.maxLineHeight,
                                spans[static_cast<std::size_t>(runStart)].color);
        }
        y += safeLineHeight;
    }

    auto bitmap = std::make_shared<Bitmap>(width, neededHeight, 32, std::move(pixels));
    bitmap->markScriptModified();
    return bitmap;
}

} // namespace

std::shared_ptr<Bitmap> SimpleTextRenderer::renderText(std::string text,
                                                       int width,
                                                       int height,
                                                       std::string fontName,
                                                       int fontSize,
                                                       std::string fontStyle,
                                                       std::string alignment,
                                                       int textColor,
                                                       int bgColor,
                                                       bool wordWrap,
                                                       bool antialias,
                                                       int fixedLineSpace,
                                                       int topSpacing) {
    return renderTextInternal(std::move(text),
                              width,
                              height,
                              std::move(fontName),
                              fontSize,
                              std::move(fontStyle),
                              std::move(alignment),
                              textColor,
                              bgColor,
                              wordWrap,
                              antialias,
                              fixedLineSpace,
                              topSpacing,
                              false);
}

std::shared_ptr<Bitmap> SimpleTextRenderer::renderLegacyStxtText(std::string text,
                                                                 int width,
                                                                 int height,
                                                                 std::string fontName,
                                                                 int fontSize,
                                                                 std::string fontStyle,
                                                                 std::string alignment,
                                                                 int textColor,
                                                                 int bgColor,
                                                                 bool wordWrap,
                                                                 bool antialias,
                                                                 int fixedLineSpace,
                                                                 int topSpacing) {
    return renderTextInternal(std::move(text),
                              width,
                              height,
                              std::move(fontName),
                              fontSize,
                              std::move(fontStyle),
                              std::move(alignment),
                              textColor,
                              bgColor,
                              wordWrap,
                              antialias,
                              fixedLineSpace,
                              topSpacing,
                              true);
}

std::shared_ptr<Bitmap> SimpleTextRenderer::renderTextInternal(std::string text,
                                                              int width,
                                                              int height,
                                                              std::string fontName,
                                                              int fontSize,
                                                              std::string fontStyle,
                                                              std::string alignment,
                                                              int textColor,
                                                              int bgColor,
                                                              bool wordWrap,
                                                              bool antialias,
                                                              int fixedLineSpace,
                                                              int topSpacing,
                                                              bool preferRegisteredDirectorFonts) {
    if (width <= 0) {
        width = 200;
    }
    if (height <= 0) {
        height = 1;
    }
    const auto style = lowerAscii(std::move(fontStyle));
    const bool wantsBold = style.find("bold") != std::string::npos;
    const bool wantsItalic = style.find("italic") != std::string::npos;
    const bool underline = style.find("underline") != std::string::npos;

    bool usedRealBold = false;
    auto font = resolveBitmapFont(fontName,
                                  fontSize,
                                  wantsBold,
                                  wantsItalic,
                                  &usedRealBold,
                                  preferRegisteredDirectorFonts);
    std::shared_ptr<Bitmap> bitmap;
    if (font != nullptr) {
        bitmap = renderWithBitmapFont(font,
                                      text,
                                      width,
                                      height,
                                      alignment,
                                      static_cast<std::uint32_t>(textColor),
                                      static_cast<std::uint32_t>(bgColor),
                                      wordWrap,
                                      fixedLineSpace,
                                      topSpacing,
                                      wantsBold && !usedRealBold,
                                      underline);
    } else {
        bitmap = renderWithBuiltinFont(text,
                                       width,
                                       height,
                                       fontSize,
                                       alignment,
                                       static_cast<std::uint32_t>(textColor),
                                       static_cast<std::uint32_t>(bgColor),
                                       wordWrap,
                                       fixedLineSpace,
                                       topSpacing,
                                       underline);
    }
    return antialias && bitmap != nullptr ? applyTextAA(*bitmap) : bitmap;
}

std::vector<int> SimpleTextRenderer::charPosToLoc(std::string text,
                                                  int charIndex,
                                                  std::string fontName,
                                                  int fontSize,
                                                  std::string,
                                                  int fixedLineSpace,
                                                  std::string alignment,
                                                  int fieldWidth) {
    auto font = resolveBitmapFont(fontName, fontSize);
    if (font != nullptr) {
        const int lineHeight = fixedLineSpace > 0 ? fixedLineSpace : font->getLineHeight();
        if (text.empty() || charIndex <= 0) {
            const auto lines = TextRenderer::splitLines(text);
            const int firstLineWidth = lines.empty() ? 0 : font->getStringWidth(lines.front());
            return {alignmentOffset(alignment, fieldWidth, firstLineWidth), 0};
        }
        const auto lineInfo = TextRenderer::findCharLine(text, charIndex);
        const auto lines = TextRenderer::splitLines(text);
        const int lineNum = lineInfo.empty() ? 0 : lineInfo[0];
        const int charsOnLine = lineInfo.size() > 1 ? lineInfo[1] : 0;
        const std::string fullLine = lineNum < static_cast<int>(lines.size()) ? lines[static_cast<std::size_t>(lineNum)] : "";
        const std::string linePrefix = fullLine.substr(0, std::min(charsOnLine, static_cast<int>(fullLine.size())));
        const int x = font->getStringWidth(linePrefix) + (charsOnLine > 0 ? 1 : 0);
        const int alignX = alignmentOffset(alignment, fieldWidth, font->getStringWidth(fullLine));
        return {x + alignX, lineNum * lineHeight};
    }

    const int charWidth = builtinCharWidth(fontSize);
    const int lineHeight = fixedLineSpace > 0 ? fixedLineSpace : builtinLineHeight(fontSize);
    if (text.empty() || charIndex <= 0) {
        return {alignmentOffset(alignment, fieldWidth, 0), 0};
    }
    const auto lineInfo = TextRenderer::findCharLine(text, charIndex);
    const auto lines = TextRenderer::splitLines(text);
    const int lineNum = lineInfo.empty() ? 0 : lineInfo[0];
    const int charsOnLine = lineInfo.size() > 1 ? lineInfo[1] : 0;
    const std::string fullLine = lineNum < static_cast<int>(lines.size()) ? lines[static_cast<std::size_t>(lineNum)] : "";
    const int x = charsOnLine * charWidth + (charsOnLine > 0 ? 1 : 0);
    const int alignX = alignmentOffset(alignment, fieldWidth, static_cast<int>(fullLine.size()) * charWidth);
    return {x + alignX, lineNum * lineHeight};
}

int SimpleTextRenderer::locToCharPos(std::string text,
                                     int x,
                                     int y,
                                     std::string fontName,
                                     int fontSize,
                                     std::string,
                                     int fixedLineSpace,
                                     std::string alignment,
                                     int fieldWidth) {
    if (text.empty()) {
        return 0;
    }

    const auto lines = TextRenderer::splitLines(text);
    auto font = resolveBitmapFont(fontName, fontSize);
    if (font != nullptr) {
        const int lineHeight = fixedLineSpace > 0 ? fixedLineSpace : font->getLineHeight();
        const int lineIndex = std::max(0,
                                       std::min(y / std::max(1, lineHeight),
                                                static_cast<int>(lines.size()) - 1));
        const int charsBefore = TextRenderer::lineStartIndex(text, lineIndex);
        const auto& line = lines[static_cast<std::size_t>(lineIndex)];
        const int localX = x - alignmentOffset(alignment, fieldWidth, font->getStringWidth(line));
        int cursorX = 0;
        for (int index = 0; index < static_cast<int>(line.size()); ++index) {
            const int width = font->getCharWidth(static_cast<unsigned char>(line[static_cast<std::size_t>(index)]));
            if (cursorX + width / 2 >= localX) {
                return charsBefore + index;
            }
            cursorX += width;
        }
        return charsBefore + static_cast<int>(line.size());
    }

    const int charWidth = builtinCharWidth(fontSize);
    const int lineHeight = fixedLineSpace > 0 ? fixedLineSpace : builtinLineHeight(fontSize);
    const int lineIndex = std::max(0,
                                   std::min(y / std::max(1, lineHeight),
                                            static_cast<int>(lines.size()) - 1));
    const int charsBefore = TextRenderer::lineStartIndex(text, lineIndex);
    const auto& line = lines[static_cast<std::size_t>(lineIndex)];
    const int localX = x - alignmentOffset(alignment, fieldWidth, static_cast<int>(line.size()) * charWidth);
    const int charOnLine = std::min(static_cast<int>(line.size()), (localX + charWidth / 2) / std::max(1, charWidth));
    return charsBefore + std::max(0, charOnLine);
}

int SimpleTextRenderer::getLineHeight(std::string fontName,
                                      int fontSize,
                                      std::string,
                                      int fixedLineSpace) {
    if (fixedLineSpace > 0) {
        return fixedLineSpace;
    }
    auto font = resolveBitmapFont(fontName, fontSize);
    return font != nullptr ? font->getLineHeight() : builtinLineHeight(fontSize);
}

std::shared_ptr<Bitmap> SimpleTextRenderer::renderXmedText(
    const XmedStyledText* styledText,
    int width,
    int height,
    int textColor,
    int bgColor) {
    if (styledText == nullptr) {
        return nullptr;
    }
    if (width <= 0) {
        width = 200;
    }
    if (height <= 0) {
        height = 1;
    }

    const auto style = lowerAscii(styledText->fontStyleString());
    const bool wantsBold = style.find("bold") != std::string::npos;
    const bool wantsItalic = style.find("italic") != std::string::npos;
    const bool underline = style.find("underline") != std::string::npos;

    bool usedRealBold = false;
    auto font = resolveXmedFont(*styledText, styledText->fontSize, wantsBold, wantsItalic, &usedRealBold);
    if (font != nullptr) {
        const std::uint32_t defaultColor = textColor != 0
            ? static_cast<std::uint32_t>(textColor)
            : styledText->textColorARGB();
        std::vector<ResolvedXmedSpan> spans(styledText->text.size());
        for (const auto& sourceSpan : styledText->styledSpans) {
            bool spanUsedRealBold = false;
            auto spanFont = sourceSpan.fontName.empty() || lowerAscii(sourceSpan.fontName) == lowerAscii(styledText->fontName)
                ? font
                : resolveXmedFontByName(sourceSpan.fontName,
                                        nullptr,
                                        sourceSpan.fontSize,
                                        sourceSpan.bold,
                                        sourceSpan.italic,
                                        &spanUsedRealBold);
            if (spanFont == nullptr) {
                spanFont = font;
            }
            const bool syntheticBold = sourceSpan.bold && !spanUsedRealBold;
            const std::uint32_t spanColor = textColor != 0
                ? static_cast<std::uint32_t>(textColor)
                : (0xFF000000U |
                   (static_cast<std::uint32_t>(sourceSpan.colorR & 0xFF) << 16U) |
                   (static_cast<std::uint32_t>(sourceSpan.colorG & 0xFF) << 8U) |
                   static_cast<std::uint32_t>(sourceSpan.colorB & 0xFF));
            const int start = std::max(0, sourceSpan.startOffset);
            const int end = std::min(static_cast<int>(styledText->text.size()), sourceSpan.endOffset);
            for (int index = start; index < end; ++index) {
                spans[static_cast<std::size_t>(index)] = ResolvedXmedSpan{spanFont,
                                                                          syntheticBold,
                                                                          sourceSpan.underline,
                                                                          spanColor};
            }
        }

        const ResolvedXmedSpan fallback{font, false, underline, defaultColor};
        for (auto& span : spans) {
            if (span.font == nullptr) {
                span = fallback;
            }
        }

        std::shared_ptr<Bitmap> bitmap;
        if (needsPerSpanBitmapRendering(*styledText, spans)) {
            bitmap = renderStyledXmedText(spans, *styledText, width, height, static_cast<std::uint32_t>(bgColor));
        } else {
            bitmap = renderWithBitmapFont(font,
                                          styledText->text,
                                          width,
                                          height,
                                          styledText->alignment,
                                          defaultColor,
                                          static_cast<std::uint32_t>(bgColor),
                                          styledText->wordWrap,
                                          styledText->fixedLineSpace,
                                          0,
                                          wantsBold && !usedRealBold,
                                          underline);
            underlineStyledSpans(bitmap,
                                 font,
                                 *styledText,
                                 0,
                                 static_cast<std::uint32_t>(textColor));
        }
        return bitmap;
    }

    return renderWithBuiltinFont(styledText->text,
                                 width,
                                 height,
                                 styledText->fontSize,
                                 styledText->alignment,
                                 textColor != 0 ? static_cast<std::uint32_t>(textColor) : styledText->textColorARGB(),
                                 static_cast<std::uint32_t>(bgColor),
                                 styledText->wordWrap,
                                 styledText->fixedLineSpace,
                                 0,
                                 underline);
}

std::shared_ptr<BitmapFont> SimpleTextRenderer::resolveBitmapFont(const std::string& fontName,
                                                                  int fontSize,
                                                                  bool bold,
                                                                  bool italic,
                                                                  bool* usedRealBold,
                                                                  bool preferRegisteredDirectorFonts) {
    if (fontName.empty()) {
        return nullptr;
    }
    auto aliasFont = resolveDirectorFontAlias(fontName,
                                              fontSize,
                                              bold,
                                              italic,
                                              usedRealBold,
                                              false,
                                              preferRegisteredDirectorFonts);
    if (aliasFont != nullptr) {
        return aliasFont;
    }

    auto resolveEmbeddedOrPfr = [&](const std::string& candidate) -> std::shared_ptr<BitmapFont> {
        if (auto embedded = FontRegistry::getEmbeddedBitmapFont(candidate, fontSize, bold, italic);
            embedded != nullptr) {
            if (usedRealBold != nullptr) {
                *usedRealBold = bold && FontRegistry::hasEmbeddedBoldVariant(candidate);
            }
            return embedded;
        }
        if (auto pfr = FontRegistry::getPfrBitmapFont(candidate, fontSize); pfr != nullptr) {
            if (usedRealBold != nullptr) {
                *usedRealBold = false;
            }
            return pfr;
        }
        return nullptr;
    };

    if (auto exactDirectorFont = resolveEmbeddedOrPfr(fontName); exactDirectorFont != nullptr) {
        return exactDirectorFont;
    }

    if (const auto resolved = FontRegistry::resolveFont(fontName); resolved.has_value()) {
        if (auto resolvedDirectorFont = resolveEmbeddedOrPfr(*resolved); resolvedDirectorFont != nullptr) {
            return resolvedDirectorFont;
        }
    }

    if (isPlatformFontRequest(fontName)) {
        if (auto preferred = resolvePreferredDirectorPixelFallback(fontSize, bold, italic, usedRealBold);
            preferred != nullptr) {
            return preferred;
        }
    }

    if (const auto fallback = FontRegistry::getFirstRegisteredFont(); fallback.has_value()) {
        const int fallbackSize = fontSize > 1 ? fontSize - 1 : fontSize;
        return FontRegistry::getBitmapFont(*fallback, fallbackSize);
    }

    if (auto platformFont = FontRegistry::getBitmapFont(fontName, fontSize, bold, italic); platformFont != nullptr) {
        if (usedRealBold != nullptr) {
            *usedRealBold = bold && FontRegistry::hasEmbeddedBoldVariant(fontName);
        }
        return platformFont;
    }
    return nullptr;
}

std::shared_ptr<BitmapFont> SimpleTextRenderer::resolveDirectorFontAlias(
    const std::string& fontName,
    int fontSize,
    bool bold,
    bool italic,
    bool* usedRealBold,
    bool preferMacFonts,
    bool preferRegisteredDirectorFonts) {
    const auto alias = FontRegistry::getFontAlias(fontName);
    const std::string resolvedName = alias.has_value() ? alias->fontName : fontName;
    const bool resolvedBold = bold || (alias.has_value() && alias->bold);
    const int aliasSize = directorAliasFontSize(fontSize);

    auto embeddedDirectorFont = [&]() -> std::shared_ptr<BitmapFont> {
        auto embedded = FontRegistry::getEmbeddedBitmapFont(resolvedName, aliasSize, resolvedBold, italic);
        if (embedded != nullptr && usedRealBold != nullptr) {
            *usedRealBold = resolvedBold && FontRegistry::hasEmbeddedBoldVariant(resolvedName);
        }
        return embedded;
    };

    if (alias.has_value()) {
        if (auto embedded = embeddedDirectorFont(); embedded != nullptr) {
            return embedded;
        }
    }

    if (preferRegisteredDirectorFonts) {
        auto registered = resolveRegisteredDirectorFont(fontName,
                                                        resolvedName,
                                                        aliasSize,
                                                        resolvedBold,
                                                        italic,
                                                        usedRealBold);
        if (registered != nullptr) {
            return registered;
        }
    }

    if (!alias.has_value()) {
        return embeddedDirectorFont();
    }

    if (!preferRegisteredDirectorFonts) {
        const std::string& registeredOriginalName = resolvedName;
        auto registered = resolveRegisteredDirectorFont(registeredOriginalName,
                                                        resolvedName,
                                                        aliasSize,
                                                        resolvedBold,
                                                        italic,
                                                        usedRealBold);
        if (registered != nullptr) {
            return registered;
        }
    }

    auto firstPlatformFont = preferMacFonts
        ? MacFontBundle::getFont(resolvedName, aliasSize, resolvedBold, italic)
        : WindowsFontBundle::getFont(resolvedName, aliasSize, resolvedBold, italic);
    if (firstPlatformFont != nullptr) {
        if (usedRealBold != nullptr) {
            *usedRealBold = resolvedBold && (preferMacFonts
                ? MacFontBundle::hasBoldVariant(resolvedName)
                : WindowsFontBundle::hasBoldVariant(resolvedName));
        }
        return firstPlatformFont;
    }

    auto secondPlatformFont = preferMacFonts
        ? WindowsFontBundle::getFont(resolvedName, aliasSize, resolvedBold, italic)
        : MacFontBundle::getFont(resolvedName, aliasSize, resolvedBold, italic);
    if (secondPlatformFont != nullptr) {
        if (usedRealBold != nullptr) {
            *usedRealBold = resolvedBold && (preferMacFonts
                ? WindowsFontBundle::hasBoldVariant(resolvedName)
                : MacFontBundle::hasBoldVariant(resolvedName));
        }
        return secondPlatformFont;
    }

    if (auto registered = FontRegistry::getBitmapFont(resolvedName, aliasSize); registered != nullptr) {
        return registered;
    }
    const auto resolved = FontRegistry::resolveFont(resolvedName);
    return resolved.has_value() ? FontRegistry::getBitmapFont(*resolved, aliasSize) : nullptr;
}

std::shared_ptr<BitmapFont> SimpleTextRenderer::resolveRegisteredDirectorFont(
    const std::string& originalName,
    const std::string& resolvedName,
    int fontSize,
    bool bold,
    bool italic,
    bool* usedRealBold) {
    if (italic) {
        return nullptr;
    }
    if (bold) {
        auto boldFont = resolveRegisteredPfrCandidate(originalName + "-Bold", fontSize);
        if (boldFont == nullptr) {
            boldFont = resolveRegisteredPfrCandidate(originalName + " Bold", fontSize);
        }
        if (boldFont == nullptr && originalName != resolvedName) {
            boldFont = resolveRegisteredPfrCandidate(resolvedName + "-Bold", fontSize);
            if (boldFont == nullptr) {
                boldFont = resolveRegisteredPfrCandidate(resolvedName + " Bold", fontSize);
            }
        }
        if (boldFont != nullptr) {
            if (usedRealBold != nullptr) {
                *usedRealBold = true;
            }
            return boldFont;
        }
    }

    auto exact = resolveRegisteredPfrCandidate(originalName, fontSize);
    if (exact == nullptr && originalName != resolvedName) {
        exact = resolveRegisteredPfrCandidate(resolvedName, fontSize);
    }
    return exact;
}

std::shared_ptr<BitmapFont> SimpleTextRenderer::resolveRegisteredPfrCandidate(
    const std::string& fontName,
    int fontSize) {
    const auto resolved = FontRegistry::resolveFont(fontName);
    if (!resolved.has_value() || !FontRegistry::hasPfrFont(*resolved)) {
        return nullptr;
    }
    return FontRegistry::getPfrBitmapFont(*resolved, fontSize);
}

std::shared_ptr<BitmapFont> SimpleTextRenderer::resolveXmedFont(
    const XmedStyledText& styledText,
    int fontSize,
    bool bold,
    bool italic,
    bool* usedRealBold) {
    return resolveXmedFontByName(styledText.fontName,
                                 &styledText.fontCandidates,
                                 fontSize,
                                 bold,
                                 italic,
                                 usedRealBold);
}

std::shared_ptr<BitmapFont> SimpleTextRenderer::resolveXmedFontByName(
    const std::string& fontName,
    const std::vector<std::string>* fontCandidates,
    int fontSize,
    bool bold,
    bool italic,
    bool* usedRealBold) {
    if (fontName.empty()) {
        return nullptr;
    }

    if (isPreferredDirectorPixelDisplayName(fontName)) {
        if (auto preferred = resolvePreferredDirectorPixelFont(fontName, fontSize, bold, italic, usedRealBold);
            preferred != nullptr) {
            return preferred;
        }
    }

    if (auto aliasFont = resolveDirectorFontAlias(fontName, fontSize, bold, italic, usedRealBold, true, false);
        aliasFont != nullptr) {
        return aliasFont;
    }

    if (auto movieFont = resolveMovieFontCandidate(fontCandidates, fontName, fontSize, bold, italic, usedRealBold);
        movieFont != nullptr) {
        return movieFont;
    }

    if (auto macFont = MacFontBundle::getFont(fontName, fontSize, bold, italic); macFont != nullptr) {
        if (usedRealBold != nullptr) {
            *usedRealBold = bold && MacFontBundle::hasBoldVariant(fontName);
        }
        return macFont;
    }

    if (auto windowsFont = WindowsFontBundle::getFont(fontName, fontSize, bold, italic); windowsFont != nullptr) {
        if (usedRealBold != nullptr) {
            *usedRealBold = bold && WindowsFontBundle::hasBoldVariant(fontName);
        }
        return windowsFont;
    }

    if (auto exact = FontRegistry::getBitmapFont(fontName, fontSize, bold, italic); exact != nullptr) {
        if (usedRealBold != nullptr) {
            *usedRealBold = bold && FontRegistry::hasEmbeddedBoldVariant(fontName);
        }
        return exact;
    }

    if (const auto resolved = FontRegistry::resolveFont(fontName); resolved.has_value()) {
        if (auto font = FontRegistry::getBitmapFont(*resolved, fontSize, bold, italic); font != nullptr) {
            if (usedRealBold != nullptr) {
                *usedRealBold = bold && FontRegistry::hasEmbeddedBoldVariant(*resolved);
            }
            return font;
        }
    }

    return resolvePreferredDirectorPixelFont(fontName, fontSize, bold, italic, usedRealBold);
}

std::shared_ptr<BitmapFont> SimpleTextRenderer::resolveMovieFontCandidate(
    const std::vector<std::string>* fontCandidates,
    const std::string& primaryFontName,
    int fontSize,
    bool bold,
    bool italic,
    bool* usedRealBold) {
    if (italic || fontCandidates == nullptr || fontCandidates->empty()) {
        return nullptr;
    }
    const auto primaryLower = lowerAscii(primaryFontName);
    for (const auto& candidate : *fontCandidates) {
        if (candidate.empty() || lowerAscii(candidate) == primaryLower) {
            continue;
        }
        if (auto registered = resolveRegisteredDirectorFont(candidate, candidate, fontSize, bold, false, usedRealBold);
            registered != nullptr) {
            return registered;
        }
        if (auto exact = FontRegistry::getBitmapFont(candidate, fontSize, bold, false); exact != nullptr) {
            if (usedRealBold != nullptr) {
                *usedRealBold = bold && FontRegistry::hasEmbeddedBoldVariant(candidate);
            }
            return exact;
        }
    }
    return nullptr;
}

int SimpleTextRenderer::directorAliasFontSize(int fontSize) {
    return fontSize >= 11 ? fontSize - 1 : fontSize;
}

} // namespace libreshockwave::player::render::output
