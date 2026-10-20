#include "libreshockwave/player/render/output/simple_text_renderer.hpp"

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

#include "libreshockwave/cast/styled_span.hpp"
#include "libreshockwave/cast/xmed_styled_text.hpp"
#include "libreshockwave/player/cast/font_registry.hpp"
#include "libreshockwave/player/cast/mac_font_bundle.hpp"
#include "libreshockwave/player/cast/windows_font_bundle.hpp"

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
    bool synthetic_bold = false;
    bool underline = false;
    std::uint32_t color = 0xFF000000U;
};

struct StyledLine {
    int start = 0;
    int end = 0;
    int width = 0;
    int max_line_height = 0;
    bool paragraph_break_before = false;
};

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

int alignmentOffset(std::string_view alignment, int field_width, int line_width) {
    if (field_width <= 0) {
        return 0;
    }
    if (alignment == "center") {
        return std::max(0, (field_width - line_width) / 2);
    }
    if (alignment == "right") {
        return std::max(0, field_width - line_width);
    }
    return 0;
}

int renderAlignmentX(std::string_view alignment, int width, int line_width) {
    if (alignment == "center") {
        return (width - line_width) / 2;
    }
    if (alignment == "right") {
        return width - line_width;
    }
    return 0;
}

bool isDefaultLeading(const BitmapFont* font, int line_height, int top_spacing) {
    return top_spacing == 1 && font != nullptr && line_height == font->getFontSize();
}

int bitmapLineAdvance(const BitmapFont* font, int line_height, int top_spacing) {
    return line_height + (isDefaultLeading(font, line_height, top_spacing) ? 0 : top_spacing);
}

int excludedLeading(const BitmapFont* font, int line_height, int top_spacing) {
    return isDefaultLeading(font, line_height, top_spacing) ? top_spacing : 0;
}

int initialBitmapTextY(const BitmapFont* font, int line_height, int top_spacing) {
    return top_spacing + (top_spacing > 1 ? 1 : 0);
}

int findInkBottom(const std::vector<std::uint32_t>& pixels,
                  int width,
                  int height,
                  int start_x,
                  int end_x,
                  int start_y,
                  int end_y) {
    const int clamped_start_x = std::max(0, start_x);
    const int clamped_end_x = std::min(width, end_x);
    const int clamped_start_y = std::max(0, start_y);
    const int clamped_end_y = std::min(height - 1, end_y);
    for (int y = clamped_end_y; y >= clamped_start_y; --y) {
        for (int x = clamped_start_x; x < clamped_end_x; ++x) {
            if (((pixels[static_cast<std::size_t>(y * width + x)] >> 24U) & 0xFFU) != 0) {
                return y;
            }
        }
    }
    return clamped_start_y;
}

std::array<int, 2> findHorizontalInkBounds(const std::vector<std::uint32_t>& pixels,
                                           int width,
                                           int height,
                                           int start_x,
                                           int end_x,
                                           int start_y,
                                           int end_y) {
    const int clamped_start_x = std::max(0, start_x);
    const int clamped_end_x = std::min(width, end_x);
    const int clamped_start_y = std::max(0, start_y);
    const int clamped_end_y = std::min(height - 1, end_y);
    int left = clamped_end_x;
    int right = clamped_start_x;
    for (int y = clamped_start_y; y <= clamped_end_y; ++y) {
        for (int x = clamped_start_x; x < clamped_end_x; ++x) {
            if (((pixels[static_cast<std::size_t>(y * width + x)] >> 24U) & 0xFFU) != 0) {
                left = std::min(left, x);
                right = std::max(right, x + 1);
            }
        }
    }
    if (left >= right) {
        return {clamped_start_x, clamped_end_x};
    }
    return {left, right};
}

int extendUnderlineRightEdge(int ink_right_x, int span_end_x, int width) {
    return std::min(width, std::min(span_end_x, ink_right_x + 1));
}

void drawUnderline(std::vector<std::uint32_t>& pixels,
                   int width,
                   int height,
                   int y,
                   int line_start_x,
                   int line_end_x,
                   std::uint32_t text_color) {
    if (y < 0 || y >= height) {
        return;
    }
    for (int x = std::max(0, line_start_x); x < std::min(width, line_end_x); ++x) {
        pixels[static_cast<std::size_t>(y * width + x)] = text_color;
    }
}

bool isLineBreak(char ch) {
    return ch == '\r' || ch == '\n';
}

bool xmedDisplayNameImpliesBold(std::string_view font_name) {
    return lowerAscii(std::string(font_name)).find("bold") != std::string::npos;
}

bool isPreferredDirectorPixelDisplayName(std::string_view font_name) {
    const auto preferred = FontRegistry::getPreferredDirectorPixelFont();
    return preferred.has_value() && !preferred->empty() &&
           FontRegistry::canonicalFontName(std::string(font_name))
               .starts_with(FontRegistry::canonicalFontName(*preferred));
}

bool isPlatformFontRequest(const std::string& font_name) {
    return WindowsFontBundle::hasWindowsFont(font_name) || MacFontBundle::hasMacFont(font_name);
}

std::shared_ptr<BitmapFont> preferredDirectorFontAtSize(const std::string& font_name,
                                                        int font_size,
                                                        bool bold,
                                                        bool* used_real_bold,
                                                        bool cache) {
    if (auto font = FontRegistry::getEmbeddedBitmapFont(font_name, font_size, bold, false, cache); font != nullptr) {
        if (used_real_bold != nullptr) {
            *used_real_bold = bold && FontRegistry::hasEmbeddedBoldVariant(font_name);
        }
        return font;
    }
    if (auto font = FontRegistry::getPfrBitmapFont(font_name, font_size, cache); font != nullptr) {
        if (used_real_bold != nullptr) {
            *used_real_bold = false;
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

std::shared_ptr<BitmapFont> resolvePreferredDirectorPixelFont(const std::string& font_name,
                                                              int font_size,
                                                              bool bold,
                                                              bool italic,
                                                              bool* used_real_bold) {
    if (italic) {
        return nullptr;
    }

    std::vector<std::string> candidates;
    const auto normalized_name = FontRegistry::canonicalFontName(font_name);
    if (const auto preferred = FontRegistry::getPreferredDirectorPixelFont();
        preferred.has_value() && !preferred->empty()) {
        const auto preferred_name = FontRegistry::canonicalFontName(*preferred);
        if (normalized_name.starts_with(preferred_name)) {
            candidates.push_back(*preferred);
        }
    }

    const bool resolved_bold = bold || xmedDisplayNameImpliesBold(font_name);
    for (const auto& candidate : candidates) {
        if (auto font = FontRegistry::getBitmapFont(candidate, font_size, resolved_bold, false); font != nullptr) {
            if (used_real_bold != nullptr) {
                *used_real_bold = resolved_bold && FontRegistry::hasEmbeddedBoldVariant(candidate);
            }
            return font;
        }
        if (auto font = FontRegistry::getBitmapFont(candidate, font_size, false, false); font != nullptr) {
            if (used_real_bold != nullptr) {
                *used_real_bold = false;
            }
            return font;
        }
    }
    return nullptr;
}

std::shared_ptr<BitmapFont> resolvePreferredDirectorPixelFallback(int font_size,
                                                                  bool bold,
                                                                  bool italic,
                                                                  bool* used_real_bold) {
    if (italic) {
        return nullptr;
    }
    const auto preferred = FontRegistry::getPreferredDirectorPixelFont();
    if (!preferred.has_value() || preferred->empty()) {
        return nullptr;
    }

    // The probe below rasterizes the preferred pixel font at every candidate size 1..fontSize+2
    // to find the closest ink-height match. For a large requested size (e.g. a corrupt 256) that
    // is hundreds of rasterizations; caching each would retain ~15GB of atlases. Memoize the
    // selected font per (preferred, fontSize, bold) so the probe runs once per distinct size, and
    // run the probe itself with caching disabled so only the single selected atlas is retained.
    if (auto memoized = FontRegistry::getFallbackSelection(*preferred, font_size, bold);
        memoized != nullptr) {
        if (used_real_bold != nullptr) {
            *used_real_bold = bold && FontRegistry::hasEmbeddedBoldVariant(*preferred);
        }
        return memoized;
    }

    std::shared_ptr<BitmapFont> best_fit;
    int best_fit_size = -1;
    int best_fit_ink_height = -1;
    std::shared_ptr<BitmapFont> nearest;
    int nearest_distance = std::numeric_limits<int>::max();
    int nearest_size = -1;
    const int max_candidate_size = std::max(1, font_size + 2);
    for (int candidate_size = 1; candidate_size <= max_candidate_size; ++candidate_size) {
        // cache=false: probe rasterizations must not pollute the persistent font cache. Each
        // candidate is freed the next iteration unless it is the one finally selected (memoized).
        auto candidate = preferredDirectorFontAtSize(*preferred, candidate_size, bold, nullptr, /*cache=*/false);
        if (candidate == nullptr) {
            continue;
        }
        const int ink_height = representativeInkHeight(*candidate);
        const int distance = std::abs(ink_height - font_size);
        if (distance < nearest_distance || (distance == nearest_distance && candidate_size > nearest_size)) {
            nearest = candidate;
            nearest_distance = distance;
            nearest_size = candidate_size;
        }
        if (ink_height > 0 && ink_height < font_size &&
            (ink_height > best_fit_ink_height ||
             (ink_height == best_fit_ink_height && candidate_size > best_fit_size))) {
            best_fit = candidate;
            best_fit_ink_height = ink_height;
            best_fit_size = candidate_size;
        }
    }

    auto selected = best_fit != nullptr ? best_fit : nearest;
    if (selected != nullptr) {
        if (used_real_bold != nullptr) {
            *used_real_bold = bold && FontRegistry::hasEmbeddedBoldVariant(*preferred);
        }
        FontRegistry::setFallbackSelection(*preferred, font_size, bold, selected);
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
                            bool paragraph_break_before) {
    return StyledLine{start,
                      end,
                      measureStyledWidth(text, spans, start, end),
                      measureMaxLineHeight(spans, start, end),
                      paragraph_break_before};
}

void wrapStyledRange(const std::string& text,
                     const std::vector<ResolvedXmedSpan>& spans,
                     int start,
                     int end,
                     int max_width,
                     std::vector<StyledLine>& out,
                     bool paragraph_break_before) {
    int current_start = start;
    bool first_wrapped_line = true;
    while (current_start < end) {
        int pos = current_start;
        int width = 0;
        int last_break = -1;
        while (pos < end) {
            const char ch = text[static_cast<std::size_t>(pos)];
            const int char_width = widthForChar(spans, pos, ch);
            if (width + char_width > max_width && pos > current_start) {
                break;
            }
            width += char_width;
            if (std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == '-') {
                last_break = pos + 1;
            }
            ++pos;
        }
        if (pos >= end) {
            out.push_back(createStyledLine(text, spans, current_start, end, paragraph_break_before && first_wrapped_line));
            return;
        }

        int break_pos = last_break > current_start ? last_break : pos;
        int trimmed_end = break_pos;
        while (trimmed_end > current_start &&
               std::isspace(static_cast<unsigned char>(text[static_cast<std::size_t>(trimmed_end - 1)])) != 0) {
            --trimmed_end;
        }
        out.push_back(createStyledLine(text,
                                       spans,
                                       current_start,
                                       trimmed_end,
                                       paragraph_break_before && first_wrapped_line));
        first_wrapped_line = false;
        current_start = break_pos;
        while (current_start < end &&
               std::isspace(static_cast<unsigned char>(text[static_cast<std::size_t>(current_start)])) != 0) {
            ++current_start;
        }
        if (current_start == break_pos && break_pos == pos) {
            current_start = std::min(end, pos + 1);
        }
    }
}

std::vector<StyledLine> layoutStyledLines(const std::string& text,
                                          const std::vector<ResolvedXmedSpan>& spans,
                                          int max_width,
                                          bool word_wrap) {
    std::vector<StyledLine> lines;
    const int text_length = static_cast<int>(text.size());
    int line_start = 0;
    while (line_start <= text_length) {
        int line_end = line_start;
        while (line_end < text_length && !isLineBreak(text[static_cast<std::size_t>(line_end)])) {
            ++line_end;
        }
        if (!word_wrap || measureStyledWidth(text, spans, line_start, line_end) <= max_width) {
            lines.push_back(createStyledLine(text, spans, line_start, line_end, line_start > 0));
        } else {
            wrapStyledRange(text, spans, line_start, line_end, max_width, lines, line_start > 0);
        }
        if (line_end >= text_length) {
            break;
        }
        if (text[static_cast<std::size_t>(line_end)] == '\r' &&
            line_end + 1 < text_length &&
            text[static_cast<std::size_t>(line_end + 1)] == '\n') {
            line_start = line_end + 2;
        } else {
            line_start = line_end + 1;
        }
        if (line_start == text_length) {
            lines.push_back(createStyledLine(text, spans, text_length, text_length, true));
            break;
        }
    }
    if (lines.empty()) {
        lines.push_back(StyledLine{});
    }
    return lines;
}

bool needsPerSpanBitmapRendering(const XmedStyledText& styled_text,
                                 const std::vector<ResolvedXmedSpan>& spans) {
    if (styled_text.text.empty() || spans.empty()) {
        return false;
    }

    const ResolvedXmedSpan* first = nullptr;
    for (int index = 0; index < static_cast<int>(spans.size()); ++index) {
        if (!isLineBreak(styled_text.text[static_cast<std::size_t>(index)])) {
            first = &spans[static_cast<std::size_t>(index)];
            break;
        }
    }
    if (first == nullptr) {
        return false;
    }

    for (int index = 0; index < static_cast<int>(spans.size()); ++index) {
        if (isLineBreak(styled_text.text[static_cast<std::size_t>(index)])) {
            continue;
        }
        const auto& current = spans[static_cast<std::size_t>(index)];
        if (current.font != first->font ||
            current.synthetic_bold != first->synthetic_bold ||
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
                         int glyph_y,
                         int start_x,
                         int end_x,
                         int line_height,
                         std::uint32_t color) {
    const int bottom = findInkBottom(pixels,
                                     width,
                                     height,
                                     start_x,
                                     end_x,
                                     glyph_y,
                                     std::min(height - 1, glyph_y + std::max(0, line_height - 1)));
    const auto bounds = findHorizontalInkBounds(pixels,
                                                width,
                                                height,
                                                start_x,
                                                end_x,
                                                glyph_y,
                                                std::min(height - 1, glyph_y + std::max(0, line_height - 1)));
    const int underline_y = std::min(height - 1, std::max(glyph_y, bottom + 2));
    drawUnderline(pixels, width, height, underline_y, bounds[0], extendUnderlineRightEdge(bounds[1], end_x, width), color);
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
    result->markTextRendered();
    return result;
}

int builtinScale(int font_size) {
    return std::max(1, font_size / 8);
}

int builtinCharWidth(int font_size) {
    return 6 * builtinScale(font_size);
}

int builtinLineHeight(int font_size) {
    return 9 * builtinScale(font_size);
}

int builtinAscent(int font_size) {
    return 7 * builtinScale(font_size);
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
                     int baseline_y,
                     int scale,
                     std::uint32_t color) {
    int index = static_cast<int>(static_cast<unsigned char>(ch)) - 32;
    if (index < 0 || index >= static_cast<int>(FONT_5X7.size())) {
        index = 0;
    }
    const auto& glyph = FONT_5X7[static_cast<std::size_t>(index)];
    const int top_y = baseline_y - 7 * scale;
    for (int col = 0; col < 5; ++col) {
        const int bits = glyph[static_cast<std::size_t>(col)];
        for (int row = 0; row < 7; ++row) {
            if ((bits & (1 << row)) == 0) {
                continue;
            }
            for (int sy = 0; sy < scale; ++sy) {
                for (int sx = 0; sx < scale; ++sx) {
                    const int px = x + col * scale + sx;
                    const int py = top_y + row * scale + sy;
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
                                             int wrap_width,
                                             int height,
                                             std::string_view alignment,
                                             std::uint32_t text_color,
                                             std::uint32_t bg_color,
                                             bool word_wrap,
                                             int fixed_line_space,
                                             int top_spacing,
                                             bool synthetic_bold,
                                             bool underline) {
    const int line_height = fixed_line_space > 0 ? fixed_line_space : font->getLineHeight();
    std::vector<std::string> lines;
    for (const auto& raw_line : TextRenderer::splitLines(text)) {
        if (word_wrap) {
            TextRenderer::wrapLine(raw_line,
                                   [&font](std::string_view value) {
                                       return font->getStringWidth(value);
                                   },
                                   wrap_width,
                                   lines);
        } else {
            lines.push_back(raw_line);
        }
    }

    const int line_advance = bitmapLineAdvance(font.get(), line_height, top_spacing);
    const int needed_height = static_cast<int>(lines.size()) * line_advance +
                             excludedLeading(font.get(), line_height, top_spacing);
    height = std::max(height, needed_height);
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width * height), bg_color);

    const int leading = std::max(0, line_height - font->getLineHeight());
    const int vertical_overflow = std::max(0, font->getLineHeight() - line_height);
    int y = initialBitmapTextY(font.get(), line_height, top_spacing);
    for (const auto& line : lines) {
        if (y >= height) {
            break;
        }
        const int line_width = font->getStringWidth(line);
        int x = renderAlignmentX(alignment, width, line_width);
        const int line_start_x = x;
        const int glyph_y = y + leading - vertical_overflow;
        for (char ch : line) {
            font->drawChar(static_cast<unsigned char>(ch), pixels, width, height, x, glyph_y, text_color);
            if (synthetic_bold) {
                font->drawChar(static_cast<unsigned char>(ch), pixels, width, height, x + 1, glyph_y, text_color);
            }
            x += font->getCharWidth(static_cast<unsigned char>(ch));
        }
        if (underline && !line.empty()) {
            const int bottom = findInkBottom(pixels,
                                             width,
                                             height,
                                             line_start_x,
                                             x,
                                             glyph_y,
                                             std::min(height - 1, glyph_y + font->getLineHeight() - 1));
            const auto bounds = findHorizontalInkBounds(pixels,
                                                        width,
                                                        height,
                                                        line_start_x,
                                                        x,
                                                        glyph_y,
                                                        std::min(height - 1, glyph_y + font->getLineHeight() - 1));
            const int underline_y = std::min(height - 1, std::max(glyph_y, bottom + 1));
            drawUnderline(pixels,
                          width,
                          height,
                          underline_y,
                          bounds[0],
                          extendUnderlineRightEdge(bounds[1], x, width),
                          text_color);
        }
        y += line_advance;
    }

    auto bitmap = std::make_shared<Bitmap>(width, height, 32, std::move(pixels));
    bitmap->markScriptModified();
    bitmap->markTextRendered();
    return bitmap;
}

void underlineStyledSpans(const std::shared_ptr<Bitmap>& bitmap,
                          const std::shared_ptr<BitmapFont>& font,
                          const XmedStyledText& styled_text,
                          int top_spacing,
                          std::uint32_t text_color) {
    if (bitmap == nullptr || font == nullptr || styled_text.styled_spans.empty()) {
        return;
    }

    bool has_underline = false;
    for (const auto& span : styled_text.styled_spans) {
        if (span.underline) {
            has_underline = true;
            break;
        }
    }
    if (!has_underline || styled_text.text.empty()) {
        return;
    }

    auto& pixels = bitmap->pixels();
    const int width = bitmap->width();
    const int height = bitmap->height();
    const int line_height = styled_text.fixed_line_space > 0 ? styled_text.fixed_line_space : font->getLineHeight();

    int y = top_spacing;
    int line_start = 0;
    while (line_start <= static_cast<int>(styled_text.text.size()) && y < height) {
        int line_end = line_start;
        while (line_end < static_cast<int>(styled_text.text.size()) &&
               !isLineBreak(styled_text.text[static_cast<std::size_t>(line_end)])) {
            ++line_end;
        }

        const auto line = std::string_view(styled_text.text).substr(
            static_cast<std::size_t>(line_start),
            static_cast<std::size_t>(line_end - line_start));
        const int line_width = font->getStringWidth(line);
        const int line_x = renderAlignmentX(styled_text.alignment, width, line_width);
        const int glyph_y = y;

        for (const auto& span : styled_text.styled_spans) {
            if (!span.underline) {
                continue;
            }
            const int start = std::max(line_start, span.start_offset);
            const int end = std::min(line_end, span.end_offset);
            if (start >= end) {
                continue;
            }
            const auto prefix_to_start = std::string_view(styled_text.text).substr(
                static_cast<std::size_t>(line_start),
                static_cast<std::size_t>(start - line_start));
            const auto prefix_to_end = std::string_view(styled_text.text).substr(
                static_cast<std::size_t>(line_start),
                static_cast<std::size_t>(end - line_start));
            const int start_x = line_x + font->getStringWidth(prefix_to_start);
            const int end_x = line_x + font->getStringWidth(prefix_to_end);
            const int bottom = findInkBottom(pixels,
                                             width,
                                             height,
                                             start_x,
                                             end_x,
                                             glyph_y,
                                             std::min(height - 1, glyph_y + font->getLineHeight() - 1));
            const auto bounds = findHorizontalInkBounds(pixels,
                                                        width,
                                                        height,
                                                        start_x,
                                                        end_x,
                                                        glyph_y,
                                                        std::min(height - 1, glyph_y + font->getLineHeight() - 1));
            const int underline_y = std::min(height - 1, std::max(glyph_y, bottom + 1));
            drawUnderline(pixels,
                          width,
                          height,
                          underline_y,
                          bounds[0],
                          extendUnderlineRightEdge(bounds[1], end_x, width),
                          text_color);
        }

        if (line_end >= static_cast<int>(styled_text.text.size())) {
            break;
        }
        if (styled_text.text[static_cast<std::size_t>(line_end)] == '\r' &&
            line_end + 1 < static_cast<int>(styled_text.text.size()) &&
            styled_text.text[static_cast<std::size_t>(line_end + 1)] == '\n') {
            line_start = line_end + 2;
        } else {
            line_start = line_end + 1;
        }
        y += line_height;
    }
}

std::shared_ptr<Bitmap> renderWithBuiltinFont(const std::string& text,
                                              int width,
                                              int wrap_width,
                                              int height,
                                              int font_size,
                                              std::string_view alignment,
                                              std::uint32_t text_color,
                                              std::uint32_t bg_color,
                                              bool word_wrap,
                                              int fixed_line_space,
                                              int top_spacing,
                                              bool underline) {
    const int char_width = builtinCharWidth(font_size);
    const int line_height = fixed_line_space > 0 ? fixed_line_space : builtinLineHeight(font_size);
    const int ascent = builtinAscent(font_size);
    const int scale = builtinScale(font_size);

    std::vector<std::string> lines;
    for (const auto& raw_line : TextRenderer::splitLines(text)) {
        if (word_wrap) {
            TextRenderer::wrapLine(raw_line,
                                   [char_width](std::string_view value) {
                                       return static_cast<int>(value.size()) * char_width;
                                   },
                                   wrap_width,
                                   lines);
        } else {
            lines.push_back(raw_line);
        }
    }

    const int line_advance = line_height + top_spacing;
    const int needed_height = static_cast<int>(lines.size()) * line_advance;
    height = std::max(height, needed_height);
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width * height), bg_color);

    int y = top_spacing;
    for (const auto& line : lines) {
        if (y >= height) {
            break;
        }
        int x = renderAlignmentX(alignment, width, static_cast<int>(line.size()) * char_width);
        const int line_start_x = x;
        for (char ch : line) {
            drawBuiltinChar(ch, pixels, width, height, x, y + ascent, scale, text_color);
            x += char_width;
        }
        if (underline && !line.empty()) {
            const int glyph_top = std::max(0, y);
            const int glyph_bottom = findInkBottom(pixels,
                                                  width,
                                                  height,
                                                  line_start_x,
                                                  x,
                                                  glyph_top,
                                                  std::min(height - 1, y + ascent));
            drawUnderline(pixels, width, height, std::min(height - 1, glyph_bottom + 1), line_start_x, x, text_color);
        }
        y += line_advance;
    }

    auto bitmap = std::make_shared<Bitmap>(width, height, 32, std::move(pixels));
    bitmap->markScriptModified();
    bitmap->markTextRendered();
    return bitmap;
}

std::shared_ptr<Bitmap> renderStyledXmedText(const std::vector<ResolvedXmedSpan>& spans,
                                             const XmedStyledText& styled_text,
                                             int width,
                                             int height,
                                             std::uint32_t bg_color) {
    const auto lines = layoutStyledLines(styled_text.text, spans, width, styled_text.word_wrap);
    int max_font_line_height = 0;
    for (const auto& span : spans) {
        if (span.font != nullptr) {
            max_font_line_height = std::max(max_font_line_height, span.font->getLineHeight());
        }
    }
    if (max_font_line_height <= 0) {
        max_font_line_height = styled_text.font_size;
    }
    const int line_height = styled_text.fixed_line_space > 0 ? styled_text.fixed_line_space : max_font_line_height;
    const int safe_line_height = std::max(1, line_height);
    int needed_height = std::max(height, std::max(1, static_cast<int>(lines.size())) * safe_line_height);
    int paragraph_break_count = 0;
    for (const auto& line : lines) {
        if (line.paragraph_break_before) {
            ++paragraph_break_count;
        }
    }
    const int paragraph_gap = paragraph_break_count > 0
        ? std::max(0, (height - std::max(1, static_cast<int>(lines.size())) * safe_line_height) / (paragraph_break_count + 1))
        : 0;

    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width * needed_height), bg_color);
    int y = 0;
    for (const auto& line : lines) {
        if (y >= needed_height) {
            break;
        }
        if (line.paragraph_break_before) {
            y += paragraph_gap;
            if (y >= needed_height) {
                break;
            }
        }

        const int line_x = alignmentOffset(styled_text.alignment, width, line.width);
        int x = line_x;
        for (int index = line.start; index < line.end; ++index) {
            const char ch = styled_text.text[static_cast<std::size_t>(index)];
            if (isLineBreak(ch)) {
                continue;
            }
            const auto& span = spans[static_cast<std::size_t>(index)];
            if (span.font == nullptr) {
                continue;
            }
            span.font->drawChar(static_cast<unsigned char>(ch), pixels, width, needed_height, x, y, span.color);
            if (span.synthetic_bold) {
                span.font->drawChar(static_cast<unsigned char>(ch), pixels, width, needed_height, x + 1, y, span.color);
            }
            x += span.font->getCharWidth(static_cast<unsigned char>(ch));
        }

        int run_start = -1;
        int run_start_x = 0;
        x = line_x;
        for (int index = line.start; index < line.end; ++index) {
            const char ch = styled_text.text[static_cast<std::size_t>(index)];
            if (isLineBreak(ch)) {
                continue;
            }
            const auto& span = spans[static_cast<std::size_t>(index)];
            const int char_width = span.font != nullptr ? span.font->getCharWidth(static_cast<unsigned char>(ch)) : 0;
            if (span.underline) {
                if (run_start < 0) {
                    run_start = index;
                    run_start_x = x;
                }
            } else if (run_start >= 0) {
                drawStyledUnderline(pixels,
                                    width,
                                    needed_height,
                                    y,
                                    run_start_x,
                                    x,
                                    line.max_line_height,
                                    spans[static_cast<std::size_t>(run_start)].color);
                run_start = -1;
            }
            x += char_width;
        }
        if (run_start >= 0) {
            drawStyledUnderline(pixels,
                                width,
                                needed_height,
                                y,
                                run_start_x,
                                x,
                                line.max_line_height,
                                spans[static_cast<std::size_t>(run_start)].color);
        }
        y += safe_line_height;
    }

    auto bitmap = std::make_shared<Bitmap>(width, needed_height, 32, std::move(pixels));
    bitmap->markScriptModified();
    bitmap->markTextRendered();
    return bitmap;
}

} // namespace

std::shared_ptr<Bitmap> SimpleTextRenderer::renderText(std::string text,
                                                       int width,
                                                       int height,
                                                       std::string font_name,
                                                       int font_size,
                                                       std::string font_style,
                                                       std::string alignment,
                                                       int text_color,
                                                       int bg_color,
                                                       bool word_wrap,
                                                       bool antialias,
                                                       int fixed_line_space,
                                                       int top_spacing) {
    return renderTextInternal(std::move(text),
                              width,
                              height,
                              std::move(font_name),
                              font_size,
                              std::move(font_style),
                              std::move(alignment),
                              text_color,
                              bg_color,
                              word_wrap,
                              antialias,
                              fixed_line_space,
                              top_spacing,
                              false);
}

std::shared_ptr<Bitmap> SimpleTextRenderer::renderTextInField(std::string text,
                                                              int width,
                                                              int height,
                                                              std::string font_name,
                                                              int font_size,
                                                              std::string font_style,
                                                              std::string alignment,
                                                              int text_color,
                                                              int bg_color,
                                                              bool word_wrap,
                                                              bool antialias,
                                                              int fixed_line_space,
                                                              int top_spacing,
                                                              int text_gutter) {
    const int field_width = width > 0 ? width : 200;
    const int content_width = std::max(1, field_width - std::max(0, text_gutter) * 2);
    // Director text fields leave a one-pixel leading inset before their first
    // glyph; keep the general text renderer's direct-output origin unchanged.
    const int field_top_spacing = std::max(1, top_spacing);
    return renderTextInternal(std::move(text),
                              width,
                              height,
                              std::move(font_name),
                              font_size,
                              std::move(font_style),
                              std::move(alignment),
                              text_color,
                              bg_color,
                              word_wrap,
                              antialias,
                              fixed_line_space,
                              field_top_spacing,
                              false,
                              content_width);
}

std::shared_ptr<Bitmap> SimpleTextRenderer::renderLegacyStxtText(std::string text,
                                                                 int width,
                                                                 int height,
                                                                 std::string font_name,
                                                                 int font_size,
                                                                 std::string font_style,
                                                                 std::string alignment,
                                                                 int text_color,
                                                                 int bg_color,
                                                                 bool word_wrap,
                                                                 bool antialias,
                                                                 int fixed_line_space,
                                                                 int top_spacing) {
    return renderTextInternal(std::move(text),
                              width,
                              height,
                              std::move(font_name),
                              font_size,
                              std::move(font_style),
                              std::move(alignment),
                              text_color,
                              bg_color,
                              word_wrap,
                              antialias,
                              fixed_line_space,
                              top_spacing,
                              true);
}

std::shared_ptr<Bitmap> SimpleTextRenderer::renderTextInternal(std::string text,
                                                              int width,
                                                              int height,
                                                              std::string font_name,
                                                              int font_size,
                                                              std::string font_style,
                                                              std::string alignment,
                                                              int text_color,
                                                              int bg_color,
                                                              bool word_wrap,
                                                              bool antialias,
                                                              int fixed_line_space,
                                                              int top_spacing,
                                                              bool prefer_registered_director_fonts,
                                                              int wrap_width) {
    if (width <= 0) {
        width = 200;
    }
    if (height <= 0) {
        height = 1;
    }
    if (wrap_width <= 0) {
        wrap_width = width;
    }
    const auto style = lowerAscii(std::move(font_style));
    const bool wants_bold = style.find("bold") != std::string::npos;
    const bool wants_italic = style.find("italic") != std::string::npos;
    const bool underline = style.find("underline") != std::string::npos;

    bool used_real_bold = false;
    auto font = resolveBitmapFont(font_name,
                                  font_size,
                                  wants_bold,
                                  wants_italic,
                                  &used_real_bold,
                                  prefer_registered_director_fonts);
    std::shared_ptr<Bitmap> bitmap;
    if (font != nullptr) {
        bitmap = renderWithBitmapFont(font,
                                      text,
                                      width,
                                      wrap_width,
                                      height,
                                      alignment,
                                      static_cast<std::uint32_t>(text_color),
                                      static_cast<std::uint32_t>(bg_color),
                                      word_wrap,
                                      fixed_line_space,
                                      top_spacing,
                                      wants_bold && !used_real_bold,
                                      underline);
    } else {
        bitmap = renderWithBuiltinFont(text,
                                       width,
                                       wrap_width,
                                       height,
                                       font_size,
                                       alignment,
                                       static_cast<std::uint32_t>(text_color),
                                       static_cast<std::uint32_t>(bg_color),
                                       word_wrap,
                                       fixed_line_space,
                                       top_spacing,
                                       underline);
    }
    return antialias && bitmap != nullptr ? applyTextAA(*bitmap) : bitmap;
}

std::vector<int> SimpleTextRenderer::charPosToLoc(std::string text,
                                                  int char_index,
                                                  std::string font_name,
                                                  int font_size,
                                                  std::string,
                                                  int fixed_line_space,
                                                  std::string alignment,
                                                  int field_width) {
    auto font = resolveBitmapFont(font_name, font_size);
    if (font != nullptr) {
        const int line_height = fixed_line_space > 0 ? fixed_line_space : font->getLineHeight();
        if (text.empty() || char_index <= 0) {
            const auto lines = TextRenderer::splitLines(text);
            const int first_line_width = lines.empty() ? 0 : font->getStringWidth(lines.front());
            return {alignmentOffset(alignment, field_width, first_line_width), 0};
        }
        const auto line_info = TextRenderer::findCharLine(text, char_index);
        const auto lines = TextRenderer::splitLines(text);
        const int line_num = line_info.empty() ? 0 : line_info[0];
        const int chars_on_line = line_info.size() > 1 ? line_info[1] : 0;
        const std::string full_line = line_num < static_cast<int>(lines.size()) ? lines[static_cast<std::size_t>(line_num)] : "";
        const std::string line_prefix = full_line.substr(0, std::min(chars_on_line, static_cast<int>(full_line.size())));
        const int x = font->getStringWidth(line_prefix) + (chars_on_line > 0 ? 1 : 0);
        const int align_x = alignmentOffset(alignment, field_width, font->getStringWidth(full_line));
        return {x + align_x, line_num * line_height};
    }

    const int char_width = builtinCharWidth(font_size);
    const int line_height = fixed_line_space > 0 ? fixed_line_space : builtinLineHeight(font_size);
    if (text.empty() || char_index <= 0) {
        return {alignmentOffset(alignment, field_width, 0), 0};
    }
    const auto line_info = TextRenderer::findCharLine(text, char_index);
    const auto lines = TextRenderer::splitLines(text);
    const int line_num = line_info.empty() ? 0 : line_info[0];
    const int chars_on_line = line_info.size() > 1 ? line_info[1] : 0;
    const std::string full_line = line_num < static_cast<int>(lines.size()) ? lines[static_cast<std::size_t>(line_num)] : "";
    const int x = chars_on_line * char_width + (chars_on_line > 0 ? 1 : 0);
    const int align_x = alignmentOffset(alignment, field_width, static_cast<int>(full_line.size()) * char_width);
    return {x + align_x, line_num * line_height};
}

int SimpleTextRenderer::locToCharPos(std::string text,
                                     int x,
                                     int y,
                                     std::string font_name,
                                     int font_size,
                                     std::string,
                                     int fixed_line_space,
                                     std::string alignment,
                                     int field_width) {
    if (text.empty()) {
        return 0;
    }

    const auto lines = TextRenderer::splitLines(text);
    auto font = resolveBitmapFont(font_name, font_size);
    if (font != nullptr) {
        const int line_height = fixed_line_space > 0 ? fixed_line_space : font->getLineHeight();
        const int line_index = std::max(0,
                                       std::min(y / std::max(1, line_height),
                                                static_cast<int>(lines.size()) - 1));
        const int chars_before = TextRenderer::lineStartIndex(text, line_index);
        const auto& line = lines[static_cast<std::size_t>(line_index)];
        const int local_x = x - alignmentOffset(alignment, field_width, font->getStringWidth(line));
        int cursor_x = 0;
        for (int index = 0; index < static_cast<int>(line.size()); ++index) {
            const int width = font->getCharWidth(static_cast<unsigned char>(line[static_cast<std::size_t>(index)]));
            if (cursor_x + width / 2 >= local_x) {
                return chars_before + index;
            }
            cursor_x += width;
        }
        return chars_before + static_cast<int>(line.size());
    }

    const int char_width = builtinCharWidth(font_size);
    const int line_height = fixed_line_space > 0 ? fixed_line_space : builtinLineHeight(font_size);
    const int line_index = std::max(0,
                                   std::min(y / std::max(1, line_height),
                                            static_cast<int>(lines.size()) - 1));
    const int chars_before = TextRenderer::lineStartIndex(text, line_index);
    const auto& line = lines[static_cast<std::size_t>(line_index)];
    const int local_x = x - alignmentOffset(alignment, field_width, static_cast<int>(line.size()) * char_width);
    const int char_on_line = std::min(static_cast<int>(line.size()), (local_x + char_width / 2) / std::max(1, char_width));
    return chars_before + std::max(0, char_on_line);
}

int SimpleTextRenderer::getLineHeight(std::string font_name,
                                      int font_size,
                                      std::string,
                                      int fixed_line_space) {
    if (fixed_line_space > 0) {
        return fixed_line_space;
    }
    auto font = resolveBitmapFont(font_name, font_size);
    return font != nullptr ? font->getLineHeight() : builtinLineHeight(font_size);
}

std::shared_ptr<Bitmap> SimpleTextRenderer::renderXmedText(
    const XmedStyledText* styled_text,
    int width,
    int height,
    int text_color,
    int bg_color) {
    if (styled_text == nullptr) {
        return nullptr;
    }
    if (width <= 0) {
        width = 200;
    }
    if (height <= 0) {
        height = 1;
    }

    const auto style = lowerAscii(styled_text->fontStyleString());
    const bool wants_bold = style.find("bold") != std::string::npos;
    const bool wants_italic = style.find("italic") != std::string::npos;
    const bool underline = style.find("underline") != std::string::npos;

    bool used_real_bold = false;
    auto font = resolveXmedFont(*styled_text, styled_text->font_size, wants_bold, wants_italic, &used_real_bold);
    if (font != nullptr) {
        const std::uint32_t default_color = text_color != 0
            ? static_cast<std::uint32_t>(text_color)
            : styled_text->textColorARGB();
        std::vector<ResolvedXmedSpan> spans(styled_text->text.size());
        for (const auto& source_span : styled_text->styled_spans) {
            const bool uses_primary_font = source_span.font_name.empty() ||
                lowerAscii(source_span.font_name) == lowerAscii(styled_text->font_name);
            bool span_used_real_bold = uses_primary_font ? used_real_bold : false;
            auto span_font = uses_primary_font
                ? font
                : resolveXmedFontByName(source_span.font_name,
                                        nullptr,
                                        source_span.font_size,
                                        source_span.bold,
                                        source_span.italic,
                                        &span_used_real_bold);
            if (span_font == nullptr) {
                span_font = font;
            }
            const bool synthetic_bold = source_span.bold && !span_used_real_bold;
            const std::uint32_t span_color = text_color != 0
                ? static_cast<std::uint32_t>(text_color)
                : (0xFF000000U |
                   (static_cast<std::uint32_t>(source_span.color_r & 0xFF) << 16U) |
                   (static_cast<std::uint32_t>(source_span.color_g & 0xFF) << 8U) |
                   static_cast<std::uint32_t>(source_span.color_b & 0xFF));
            const int start = std::max(0, source_span.start_offset);
            const int end = std::min(static_cast<int>(styled_text->text.size()), source_span.end_offset);
            for (int index = start; index < end; ++index) {
                spans[static_cast<std::size_t>(index)] = ResolvedXmedSpan{span_font,
                                                                          synthetic_bold,
                                                                          source_span.underline,
                                                                          span_color};
            }
        }

        const ResolvedXmedSpan fallback{font,
                                        false,
                                        underline,
                                        default_color};
        for (auto& span : spans) {
            if (span.font == nullptr) {
                span = fallback;
            }
        }

        std::shared_ptr<Bitmap> bitmap;
        if (needsPerSpanBitmapRendering(*styled_text, spans)) {
            bitmap = renderStyledXmedText(spans, *styled_text, width, height, static_cast<std::uint32_t>(bg_color));
        } else {
            bitmap = renderWithBitmapFont(font,
                                          styled_text->text,
                                          width,
                                          width,
                                          height,
                                          styled_text->alignment,
                                          default_color,
                                          static_cast<std::uint32_t>(bg_color),
                                          styled_text->word_wrap,
                                          styled_text->fixed_line_space,
                                          0,
                                          wants_bold && !used_real_bold,
                                          underline);
            underlineStyledSpans(bitmap,
                                 font,
                                 *styled_text,
                                 0,
                                 static_cast<std::uint32_t>(text_color));
        }
        return bitmap;
    }

    return renderWithBuiltinFont(styled_text->text,
                                 width,
                                 width,
                                 height,
                                 styled_text->font_size,
                                 styled_text->alignment,
                                 text_color != 0 ? static_cast<std::uint32_t>(text_color) : styled_text->textColorARGB(),
                                 static_cast<std::uint32_t>(bg_color),
                                 styled_text->word_wrap,
                                 styled_text->fixed_line_space,
                                 0,
                                 underline);
}

std::shared_ptr<BitmapFont> SimpleTextRenderer::resolveBitmapFont(const std::string& font_name,
                                                                  int font_size,
                                                                  bool bold,
                                                                  bool italic,
                                                                  bool* used_real_bold,
                                                                  bool prefer_registered_director_fonts) {
    if (font_name.empty()) {
        return nullptr;
    }
    auto alias_font = resolveDirectorFontAlias(font_name,
                                              font_size,
                                              bold,
                                              italic,
                                              used_real_bold,
                                              false,
                                              prefer_registered_director_fonts);
    if (alias_font != nullptr) {
        return alias_font;
    }

    auto resolve_embedded_or_pfr = [&](const std::string& candidate) -> std::shared_ptr<BitmapFont> {
        if (auto embedded = FontRegistry::getEmbeddedBitmapFont(candidate, font_size, bold, italic);
            embedded != nullptr) {
            if (used_real_bold != nullptr) {
                *used_real_bold = bold && FontRegistry::hasEmbeddedBoldVariant(candidate);
            }
            return embedded;
        }
        if (auto pfr = FontRegistry::getPfrBitmapFont(candidate, font_size); pfr != nullptr) {
            if (used_real_bold != nullptr) {
                *used_real_bold = false;
            }
            return pfr;
        }
        return nullptr;
    };

    if (auto exact_director_font = resolve_embedded_or_pfr(font_name); exact_director_font != nullptr) {
        return exact_director_font;
    }

    if (const auto resolved = FontRegistry::resolveFont(font_name); resolved.has_value()) {
        if (auto resolved_director_font = resolve_embedded_or_pfr(*resolved); resolved_director_font != nullptr) {
            return resolved_director_font;
        }
    }

    if (isPlatformFontRequest(font_name)) {
        if (auto preferred = resolvePreferredDirectorPixelFallback(font_size, bold, italic, used_real_bold);
            preferred != nullptr) {
            return preferred;
        }
    }

    if (const auto fallback = FontRegistry::getFirstRegisteredFont(); fallback.has_value()) {
        const int fallback_size = font_size > 1 ? font_size - 1 : font_size;
        return FontRegistry::getBitmapFont(*fallback, fallback_size);
    }

    if (auto platform_font = FontRegistry::getBitmapFont(font_name, font_size, bold, italic); platform_font != nullptr) {
        if (used_real_bold != nullptr) {
            *used_real_bold = bold && FontRegistry::hasEmbeddedBoldVariant(font_name);
        }
        return platform_font;
    }
    return nullptr;
}

std::shared_ptr<BitmapFont> SimpleTextRenderer::resolveDirectorFontAlias(
    const std::string& font_name,
    int font_size,
    bool bold,
    bool italic,
    bool* used_real_bold,
    bool prefer_mac_fonts,
    bool prefer_registered_director_fonts) {
    const auto alias = FontRegistry::getFontAlias(font_name);
    const std::string resolved_name = alias.has_value() ? alias->font_name : font_name;
    const bool resolved_bold = bold || (alias.has_value() && alias->bold);
    const int alias_size = directorAliasFontSize(font_size);

    auto embedded_director_font = [&]() -> std::shared_ptr<BitmapFont> {
        auto embedded = FontRegistry::getEmbeddedBitmapFont(resolved_name, alias_size, resolved_bold, italic);
        if (embedded != nullptr && used_real_bold != nullptr) {
            *used_real_bold = resolved_bold && FontRegistry::hasEmbeddedBoldVariant(resolved_name);
        }
        return embedded;
    };

    if (alias.has_value()) {
        if (auto embedded = embedded_director_font(); embedded != nullptr) {
            return embedded;
        }
    }

    if (prefer_registered_director_fonts) {
        auto registered = resolveRegisteredDirectorFont(font_name,
                                                        resolved_name,
                                                        alias_size,
                                                        resolved_bold,
                                                        italic,
                                                        used_real_bold);
        if (registered != nullptr) {
            return registered;
        }
    }

    if (!alias.has_value()) {
        return embedded_director_font();
    }

    if (!prefer_registered_director_fonts) {
        const std::string& registered_original_name = resolved_name;
        auto registered = resolveRegisteredDirectorFont(registered_original_name,
                                                        resolved_name,
                                                        alias_size,
                                                        resolved_bold,
                                                        italic,
                                                        used_real_bold);
        if (registered != nullptr) {
            return registered;
        }
    }

    auto first_platform_font = prefer_mac_fonts
        ? MacFontBundle::getFont(resolved_name, alias_size, resolved_bold, italic)
        : WindowsFontBundle::getFont(resolved_name, alias_size, resolved_bold, italic);
    if (first_platform_font != nullptr) {
        if (used_real_bold != nullptr) {
            *used_real_bold = resolved_bold && (prefer_mac_fonts
                ? MacFontBundle::hasBoldVariant(resolved_name)
                : WindowsFontBundle::hasBoldVariant(resolved_name));
        }
        return first_platform_font;
    }

    auto second_platform_font = prefer_mac_fonts
        ? WindowsFontBundle::getFont(resolved_name, alias_size, resolved_bold, italic)
        : MacFontBundle::getFont(resolved_name, alias_size, resolved_bold, italic);
    if (second_platform_font != nullptr) {
        if (used_real_bold != nullptr) {
            *used_real_bold = resolved_bold && (prefer_mac_fonts
                ? WindowsFontBundle::hasBoldVariant(resolved_name)
                : MacFontBundle::hasBoldVariant(resolved_name));
        }
        return second_platform_font;
    }

    if (auto registered = FontRegistry::getBitmapFont(resolved_name, alias_size); registered != nullptr) {
        return registered;
    }
    const auto resolved = FontRegistry::resolveFont(resolved_name);
    return resolved.has_value() ? FontRegistry::getBitmapFont(*resolved, alias_size) : nullptr;
}

std::shared_ptr<BitmapFont> SimpleTextRenderer::resolveRegisteredDirectorFont(
    const std::string& original_name,
    const std::string& resolved_name,
    int font_size,
    bool bold,
    bool italic,
    bool* used_real_bold) {
    if (italic) {
        return nullptr;
    }
    if (bold) {
        auto bold_font = resolveRegisteredPfrCandidate(original_name + "-Bold", font_size);
        if (bold_font == nullptr) {
            bold_font = resolveRegisteredPfrCandidate(original_name + " Bold", font_size);
        }
        if (bold_font == nullptr && original_name != resolved_name) {
            bold_font = resolveRegisteredPfrCandidate(resolved_name + "-Bold", font_size);
            if (bold_font == nullptr) {
                bold_font = resolveRegisteredPfrCandidate(resolved_name + " Bold", font_size);
            }
        }
        if (bold_font != nullptr) {
            if (used_real_bold != nullptr) {
                *used_real_bold = true;
            }
            return bold_font;
        }
    }

    auto exact = resolveRegisteredPfrCandidate(original_name, font_size);
    if (exact == nullptr && original_name != resolved_name) {
        exact = resolveRegisteredPfrCandidate(resolved_name, font_size);
    }
    return exact;
}

std::shared_ptr<BitmapFont> SimpleTextRenderer::resolveRegisteredPfrCandidate(
    const std::string& font_name,
    int font_size) {
    const auto resolved = FontRegistry::resolveFont(font_name);
    if (!resolved.has_value() || !FontRegistry::hasPfrFont(*resolved)) {
        return nullptr;
    }
    return FontRegistry::getPfrBitmapFont(*resolved, font_size);
}

std::shared_ptr<BitmapFont> SimpleTextRenderer::resolveXmedFont(
    const XmedStyledText& styled_text,
    int font_size,
    bool bold,
    bool italic,
    bool* used_real_bold) {
    return resolveXmedFontByName(styled_text.font_name,
                                 &styled_text.font_candidates,
                                 font_size,
                                 bold,
                                 italic,
                                 used_real_bold);
}

std::shared_ptr<BitmapFont> SimpleTextRenderer::resolveXmedFontByName(
    const std::string& font_name,
    const std::vector<std::string>* font_candidates,
    int font_size,
    bool bold,
    bool italic,
    bool* used_real_bold) {
    if (font_name.empty()) {
        return nullptr;
    }

    if (isPreferredDirectorPixelDisplayName(font_name)) {
        if (auto preferred = resolvePreferredDirectorPixelFont(font_name, font_size, bold, italic, used_real_bold);
            preferred != nullptr) {
            return preferred;
        }
    }

    if (auto alias_font = resolveDirectorFontAlias(font_name, font_size, bold, italic, used_real_bold, true, false);
        alias_font != nullptr) {
        return alias_font;
    }

    if (auto movie_font = resolveMovieFontCandidate(font_candidates, font_name, font_size, bold, italic, used_real_bold);
        movie_font != nullptr) {
        return movie_font;
    }

    if (auto mac_font = MacFontBundle::getFont(font_name, font_size, bold, italic); mac_font != nullptr) {
        if (used_real_bold != nullptr) {
            *used_real_bold = bold && MacFontBundle::hasBoldVariant(font_name);
        }
        return mac_font;
    }

    if (auto windows_font = WindowsFontBundle::getFont(font_name, font_size, bold, italic); windows_font != nullptr) {
        if (used_real_bold != nullptr) {
            *used_real_bold = bold && WindowsFontBundle::hasBoldVariant(font_name);
        }
        return windows_font;
    }

    if (auto exact = FontRegistry::getBitmapFont(font_name, font_size, bold, italic); exact != nullptr) {
        if (used_real_bold != nullptr) {
            *used_real_bold = bold && FontRegistry::hasEmbeddedBoldVariant(font_name);
        }
        return exact;
    }

    if (const auto resolved = FontRegistry::resolveFont(font_name); resolved.has_value()) {
        if (auto font = FontRegistry::getBitmapFont(*resolved, font_size, bold, italic); font != nullptr) {
            if (used_real_bold != nullptr) {
                *used_real_bold = bold && FontRegistry::hasEmbeddedBoldVariant(*resolved);
            }
            return font;
        }
    }

    return resolvePreferredDirectorPixelFont(font_name, font_size, bold, italic, used_real_bold);
}

std::shared_ptr<BitmapFont> SimpleTextRenderer::resolveMovieFontCandidate(
    const std::vector<std::string>* font_candidates,
    const std::string& primary_font_name,
    int font_size,
    bool bold,
    bool italic,
    bool* used_real_bold) {
    if (italic || font_candidates == nullptr || font_candidates->empty()) {
        return nullptr;
    }
    const auto primary_lower = lowerAscii(primary_font_name);
    for (const auto& candidate : *font_candidates) {
        if (candidate.empty() || lowerAscii(candidate) == primary_lower) {
            continue;
        }
        if (auto registered = resolveRegisteredDirectorFont(candidate, candidate, font_size, bold, false, used_real_bold);
            registered != nullptr) {
            return registered;
        }
        if (auto exact = FontRegistry::getBitmapFont(candidate, font_size, bold, false); exact != nullptr) {
            if (used_real_bold != nullptr) {
                *used_real_bold = bold && FontRegistry::hasEmbeddedBoldVariant(candidate);
            }
            return exact;
        }
    }
    return nullptr;
}

int SimpleTextRenderer::directorAliasFontSize(int font_size) {
    return font_size >= 11 ? font_size - 1 : font_size;
}

} // namespace libreshockwave::player::render::output
