#include "libreshockwave/font/BitmapFont.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "libreshockwave/font/Pfr1Font.hpp"

namespace libreshockwave::font {
namespace {

int javaRound(float value) {
    return static_cast<int>(std::floor(value + 0.5F));
}

struct Point {
    float x = 0.0F;
    float y = 0.0F;
};

bool cellHasInk(const std::vector<std::uint32_t>& argb,
                int bitmapWidth,
                int cx,
                int cy,
                int cw,
                int ch) {
    for (int y = 0; y < ch; ++y) {
        for (int x = 0; x < cw; ++x) {
            const int idx = (cy + y) * bitmapWidth + (cx + x);
            if (idx >= 0 && idx < static_cast<int>(argb.size()) &&
                ((argb[static_cast<std::size_t>(idx)] >> 24U) & 0xFFU) > 0) {
                return true;
            }
        }
    }
    return false;
}

void copyCell(std::vector<std::uint32_t>& argb,
              int bitmapWidth,
              int srcIdx,
              int dstIdx,
              int cellWidth,
              int cellHeight) {
    const int srcCol = srcIdx % BitmapFont::GRID_COLUMNS;
    const int srcRow = srcIdx / BitmapFont::GRID_COLUMNS;
    const int dstCol = dstIdx % BitmapFont::GRID_COLUMNS;
    const int dstRow = dstIdx / BitmapFont::GRID_COLUMNS;
    const int srcX = srcCol * cellWidth;
    const int srcY = srcRow * cellHeight;
    const int dstX = dstCol * cellWidth;
    const int dstY = dstRow * cellHeight;

    for (int y = 0; y < cellHeight; ++y) {
        for (int x = 0; x < cellWidth; ++x) {
            const int si = (srcY + y) * bitmapWidth + (srcX + x);
            const int di = (dstY + y) * bitmapWidth + (dstX + x);
            if (si >= 0 && si < static_cast<int>(argb.size()) &&
                di >= 0 && di < static_cast<int>(argb.size())) {
                argb[static_cast<std::size_t>(di)] = argb[static_cast<std::size_t>(si)];
            }
        }
    }
}

float scaledDistance(Point lhs, Point rhs, float scaleX, float scaleY) {
    const float dx = (rhs.x - lhs.x) * scaleX;
    const float dy = (rhs.y - lhs.y) * scaleY;
    return std::sqrt(dx * dx + dy * dy);
}

Point cubicPoint(Point p0, Point p1, Point p2, Point p3, float t) {
    const float mt = 1.0F - t;
    const float mt2 = mt * mt;
    const float t2 = t * t;
    return Point{
        mt2 * mt * p0.x + 3.0F * mt2 * t * p1.x + 3.0F * mt * t2 * p2.x + t2 * t * p3.x,
        mt2 * mt * p0.y + 3.0F * mt2 * t * p1.y + 3.0F * mt * t2 * p2.y + t2 * t * p3.y,
    };
}

void appendCubicFlattened(std::vector<Point>& points,
                          Point start,
                          const Pfr1Font::Contour::Command& command,
                          float scaleX,
                          float scaleY) {
    const Point control1{command.x1, command.y1};
    const Point control2{command.x2, command.y2};
    const Point end{command.x, command.y};
    const float controlLength = scaledDistance(start, control1, scaleX, scaleY) +
                                scaledDistance(control1, control2, scaleX, scaleY) +
                                scaledDistance(control2, end, scaleX, scaleY);
    const int segments = std::clamp(static_cast<int>(std::ceil(controlLength / 3.0F)), 4, 64);
    for (int i = 1; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        points.push_back(cubicPoint(start, control1, control2, end, t));
    }
}

void rasterizeGlyph(const Pfr1Font::OutlineGlyph& glyph,
                    std::vector<std::uint32_t>& argb,
                    int bitmapWidth,
                    int bitmapHeight,
                    int cellX,
                    int cellY,
                    int cellWidth,
                    int cellHeight,
                    float scaleX,
                    float scaleY,
                    float offsetX,
                    float offsetY) {
    std::vector<std::vector<Point>> polygons;
    for (const auto& contour : glyph.contours) {
        std::vector<Point> points;
        Point current;
        bool hasCurrent = false;
        for (const auto& command : contour.commands) {
            if (command.type == 0) {
                if (points.size() >= 3) {
                    polygons.push_back(std::move(points));
                    points = {};
                } else {
                    points.clear();
                }
                current = Point{command.x, command.y};
                points.push_back(current);
                hasCurrent = true;
            } else if (command.type == 1) {
                current = Point{command.x, command.y};
                points.push_back(current);
                hasCurrent = true;
            } else if (command.type == 2) {
                if (!hasCurrent) {
                    current = Point{command.x, command.y};
                    points.push_back(current);
                    hasCurrent = true;
                    continue;
                }
                appendCubicFlattened(points, current, command, scaleX, scaleY);
                current = Point{command.x, command.y};
            }
        }
        if (points.size() >= 3) {
            polygons.push_back(std::move(points));
        }
    }

    if (polygons.empty()) {
        return;
    }

    for (int y = 0; y < cellHeight; ++y) {
        const float scanY = static_cast<float>(y) + 0.5F;
        std::vector<std::pair<float, int>> crossings;

        for (const auto& polygon : polygons) {
            const auto n = static_cast<int>(polygon.size());
            for (int i = 0; i < n; ++i) {
                const auto& p0 = polygon[static_cast<std::size_t>(i)];
                const auto& p1 = polygon[static_cast<std::size_t>((i + 1) % n)];
                const float x0 = p0.x * scaleX + offsetX;
                const float y0 = p0.y * scaleY + offsetY;
                const float x1 = p1.x * scaleX + offsetX;
                const float y1 = p1.y * scaleY + offsetY;

                if (std::abs(y0 - y1) < 0.001F) {
                    continue;
                }
                if ((y0 <= scanY && y1 > scanY) || (y1 <= scanY && y0 > scanY)) {
                    const float t = (scanY - y0) / (y1 - y0);
                    const float xCross = x0 + t * (x1 - x0);
                    const int dir = y0 < y1 ? 1 : -1;
                    crossings.emplace_back(xCross, dir);
                }
            }
        }

        std::sort(crossings.begin(), crossings.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });

        int winding = 0;
        for (std::size_t i = 0; i < crossings.size(); ++i) {
            winding += crossings[i].second;
            if (i + 1 >= crossings.size() || winding == 0) {
                continue;
            }

            const int xStart = std::max(0, std::min(cellWidth, javaRound(crossings[i].first)));
            const int xEnd = std::max(0, std::min(cellWidth, javaRound(crossings[i + 1].first)));
            for (int bx = xStart; bx < xEnd; ++bx) {
                const int px = cellX + bx;
                const int py = cellY + y;
                if (px < 0 || px >= bitmapWidth || py < 0 || py >= bitmapHeight) {
                    continue;
                }
                const int idx = py * bitmapWidth + px;
                if (idx >= 0 && idx < static_cast<int>(argb.size())) {
                    argb[static_cast<std::size_t>(idx)] = 0xFF000000U;
                }
            }
        }
    }
}

} // namespace

std::shared_ptr<BitmapFont> BitmapFont::create(std::vector<std::uint32_t> bitmap,
                                               int bitmapWidth,
                                               int bitmapHeight,
                                               int cellWidth,
                                               int cellHeight,
                                               std::vector<int> charWidths,
                                               std::string fontName,
                                               int fontSize,
                                               int metricsAscent,
                                               int metricsLineHeight,
                                               GlyphMap overflowGlyphs,
                                               MetricMap overflowWidths) {
    return create(std::move(bitmap),
                  bitmapWidth,
                  bitmapHeight,
                  cellWidth,
                  cellHeight,
                  std::move(charWidths),
                  std::vector<int>(NUM_CHARS, 0),
                  std::move(fontName),
                  fontSize,
                  metricsAscent,
                  metricsLineHeight,
                  std::move(overflowGlyphs),
                  std::move(overflowWidths),
                  {});
}

std::shared_ptr<BitmapFont> BitmapFont::create(std::vector<std::uint32_t> bitmap,
                                               int bitmapWidth,
                                               int bitmapHeight,
                                               int cellWidth,
                                               int cellHeight,
                                               std::vector<int> charWidths,
                                               std::vector<int> charOffsetsX,
                                               std::string fontName,
                                               int fontSize,
                                               int metricsAscent,
                                               int metricsLineHeight,
                                               GlyphMap overflowGlyphs,
                                               MetricMap overflowWidths,
                                               MetricMap overflowOffsetsX) {
    return std::shared_ptr<BitmapFont>(new BitmapFont(std::move(bitmap),
                                                      bitmapWidth,
                                                      bitmapHeight,
                                                      cellWidth,
                                                      cellHeight,
                                                      std::move(charWidths),
                                                      std::move(charOffsetsX),
                                                      std::move(fontName),
                                                      fontSize,
                                                      metricsAscent,
                                                      metricsLineHeight,
                                                      std::move(overflowGlyphs),
                                                      std::move(overflowWidths),
                                                      std::move(overflowOffsetsX)));
}

std::shared_ptr<BitmapFont> BitmapFont::fromPfr1(const Pfr1Font& font, int targetHeight) {
    if (targetHeight <= 0) {
        return nullptr;
    }

    const auto& fm = font.metrics;
    int outlineRes = fm.outlineResolution;
    if (outlineRes <= 0) {
        outlineRes = 2048;
    }

    const float metricHeight = std::abs(static_cast<float>(fm.ascender - fm.descender));
    const float scale = metricHeight > 0.0F
                            ? static_cast<float>(targetHeight) / metricHeight
                            : static_cast<float>(targetHeight) / static_cast<float>(outlineRes);
    const float matrixScaleX = static_cast<float>(font.fontMatrix[0]) / 256.0F;
    const float matrixScaleY = static_cast<float>(font.fontMatrix[3]) / 256.0F;
    const float scaleX = scale * std::abs(matrixScaleX);
    const float scaleY = scale * std::abs(matrixScaleY);

    float maxSetWidth = 0.0F;
    for (const auto& [_, glyph] : font.glyphs) {
        maxSetWidth = std::max(maxSetWidth, glyph.setWidth);
    }

    float maxBboxWidth = 0.0F;
    for (const auto& [_, glyph] : font.glyphs) {
        float minX = std::numeric_limits<float>::max();
        float maxX = -std::numeric_limits<float>::max();
        for (const auto& contour : glyph.contours) {
            for (const auto& command : contour.commands) {
                minX = std::min(minX, command.x);
                maxX = std::max(maxX, command.x);
                if (command.type == 2) {
                    minX = std::min({minX, command.x1, command.x2});
                    maxX = std::max({maxX, command.x1, command.x2});
                }
            }
        }
        if (minX < maxX) {
            maxBboxWidth = std::max(maxBboxWidth, maxX - minX);
        }
    }

    const int cellWidth = std::max(1,
                                   std::max(static_cast<int>(std::ceil(maxSetWidth * scaleX)),
                                            static_cast<int>(std::ceil(maxBboxWidth * scaleX))));
    const float pixelScale = static_cast<float>(targetHeight) / static_cast<float>(outlineRes);
    const int pfrAscPx = javaRound(std::abs(static_cast<float>(fm.ascender)) * pixelScale);
    const int pfrDescPx = javaRound(std::abs(static_cast<float>(fm.descender)) * pixelScale);
    const int pfrLineHeight = pfrAscPx + pfrDescPx;

    int cellHeight = std::max(targetHeight, pfrLineHeight + 1);
    if (fm.descender < 0) {
        const int descPx = static_cast<int>(std::ceil(std::abs(static_cast<float>(fm.descender)) * pixelScale));
        const int baselineRow = static_cast<int>(std::floor(static_cast<float>(fm.ascender) * pixelScale));
        cellHeight = std::max(cellHeight, baselineRow + descPx + 1);
    }

    const int bitmapWidth = cellWidth * GRID_COLUMNS;
    const int bitmapHeight = cellHeight * GRID_ROWS;
    std::vector<std::uint32_t> argb(static_cast<std::size_t>(bitmapWidth * bitmapHeight), 0);
    std::vector<int> charWidths(NUM_CHARS, cellWidth);
    GlyphMap overflowGlyphs;
    MetricMap overflowWidths;

    const float fontMinX = static_cast<float>(fm.xMin);
    const float fontAsc = static_cast<float>(fm.ascender);
    const float setWidthScale = scaleX;

    for (const auto& [charCode, glyph] : font.glyphs) {
        const int glyphPixelWidth = javaRound(glyph.setWidth * setWidthScale);
        const float glyphScaleX = scaleX;
        const float glyphScaleY = matrixScaleY < 0.0F ? scaleY : -scaleY;
        const float glyphOffsetX = -fontMinX * scaleX;
        const float glyphOffsetY = fontAsc > 0.0F ? fontAsc * pixelScale : 0.0F;

        if (charCode >= 0 && charCode < NUM_CHARS) {
            if (glyphPixelWidth > 0) {
                charWidths[static_cast<std::size_t>(charCode)] = glyphPixelWidth;
            }
            if (glyph.contours.empty()) {
                continue;
            }
            const int col = charCode % GRID_COLUMNS;
            const int row = charCode / GRID_COLUMNS;
            rasterizeGlyph(glyph,
                           argb,
                           bitmapWidth,
                           bitmapHeight,
                           col * cellWidth,
                           row * cellHeight,
                           cellWidth,
                           cellHeight,
                           glyphScaleX,
                           glyphScaleY,
                           glyphOffsetX,
                           glyphOffsetY);
        } else {
            if (glyphPixelWidth > 0) {
                overflowWidths[charCode] = glyphPixelWidth;
            }
            if (glyph.contours.empty()) {
                continue;
            }
            std::vector<std::uint32_t> cellBuf(static_cast<std::size_t>(cellWidth * cellHeight), 0);
            rasterizeGlyph(glyph,
                           cellBuf,
                           cellWidth,
                           cellHeight,
                           0,
                           0,
                           cellWidth,
                           cellHeight,
                           glyphScaleX,
                           glyphScaleY,
                           glyphOffsetX,
                           glyphOffsetY);
            overflowGlyphs[charCode] = std::move(cellBuf);
        }
    }

    for (const auto& [charCode, bitmapGlyph] : font.bitmapGlyphs) {
        if (charCode < 0 || charCode >= NUM_CHARS) {
            continue;
        }
        if (const auto outline = font.glyphs.find(charCode);
            outline != font.glyphs.end() && !outline->second.contours.empty()) {
            continue;
        }

        const int col = charCode % GRID_COLUMNS;
        const int row = charCode / GRID_COLUMNS;
        const int cellXPos = col * cellWidth;
        const int cellYPos = row * cellHeight;
        const int bmpAdv = std::max(1, javaRound(static_cast<float>(bitmapGlyph.setWidth) * setWidthScale));
        charWidths[static_cast<std::size_t>(charCode)] = bmpAdv;

        for (int gy = 0; gy < bitmapGlyph.ySize; ++gy) {
            for (int gx = 0; gx < bitmapGlyph.xSize; ++gx) {
                const int bitIndex = gy * bitmapGlyph.xSize + gx;
                const int byteIdx = bitIndex / 8;
                const int bitIdx = 7 - (bitIndex % 8);
                if (byteIdx < 0 || byteIdx >= static_cast<int>(bitmapGlyph.imageData.size())) {
                    continue;
                }
                bool bit = (bitmapGlyph.imageData[static_cast<std::size_t>(byteIdx)] & (1U << bitIdx)) != 0;
                if (!font.pfrBlackPixel) {
                    bit = !bit;
                }
                if (!bit) {
                    continue;
                }

                const int px = cellXPos + gx + std::max(0, bitmapGlyph.xPos);
                const int py = cellYPos + gy + std::max(0, bitmapGlyph.yPos);
                if (px >= bitmapWidth || py >= bitmapHeight ||
                    px >= cellXPos + cellWidth || py >= cellYPos + cellHeight) {
                    continue;
                }
                const int idx = py * bitmapWidth + px;
                if (idx >= 0 && idx < static_cast<int>(argb.size())) {
                    argb[static_cast<std::size_t>(idx)] = 0xFF000000U;
                }
            }
        }
    }

    for (int lc = 'a'; lc <= 'z'; ++lc) {
        const int ui = lc - 32;
        const int lowerX = (lc % GRID_COLUMNS) * cellWidth;
        const int lowerY = (lc / GRID_COLUMNS) * cellHeight;
        const int upperX = (ui % GRID_COLUMNS) * cellWidth;
        const int upperY = (ui / GRID_COLUMNS) * cellHeight;
        if (cellHasInk(argb, bitmapWidth, lowerX, lowerY, cellWidth, cellHeight) ||
            !cellHasInk(argb, bitmapWidth, upperX, upperY, cellWidth, cellHeight)) {
            continue;
        }
        copyCell(argb, bitmapWidth, ui, lc, cellWidth, cellHeight);
        charWidths[static_cast<std::size_t>(lc)] = charWidths[static_cast<std::size_t>(ui)];
    }

    return create(std::move(argb),
                  bitmapWidth,
                  bitmapHeight,
                  cellWidth,
                  cellHeight,
                  std::move(charWidths),
                  std::vector<int>(NUM_CHARS, 0),
                  font.fontName,
                  targetHeight,
                  pfrAscPx,
                  pfrLineHeight,
                  std::move(overflowGlyphs),
                  std::move(overflowWidths),
                  {});
}

BitmapFont::BitmapFont(std::vector<std::uint32_t> bitmap,
                       int bitmapWidth,
                       int bitmapHeight,
                       int cellWidth,
                       int cellHeight,
                       std::vector<int> charWidths,
                       std::vector<int> charOffsetsX,
                       std::string fontName,
                       int fontSize,
                       int metricsAscent,
                       int metricsLineHeight,
                       GlyphMap overflowGlyphs,
                       MetricMap overflowWidths,
                       MetricMap overflowOffsetsX)
    : bitmap_(std::move(bitmap)),
      bitmapWidth_(bitmapWidth),
      bitmapHeight_(bitmapHeight),
      cellWidth_(cellWidth),
      cellHeight_(cellHeight),
      charWidths_(std::move(charWidths)),
      charOffsetsX_(std::move(charOffsetsX)),
      fontName_(std::move(fontName)),
      fontSize_(fontSize),
      metricsAscent_(metricsAscent),
      metricsLineHeight_(metricsLineHeight),
      overflowGlyphs_(std::move(overflowGlyphs)),
      overflowWidths_(std::move(overflowWidths)),
      overflowOffsetsX_(std::move(overflowOffsetsX)) {
    if (bitmapWidth_ < 0 || bitmapHeight_ < 0 || cellWidth_ <= 0 || cellHeight_ <= 0) {
        throw std::invalid_argument("BitmapFont dimensions are invalid");
    }
    const auto expected = static_cast<std::size_t>(bitmapWidth_) * static_cast<std::size_t>(bitmapHeight_);
    if (bitmap_.size() != expected) {
        throw std::invalid_argument("BitmapFont pixel count does not match dimensions");
    }
    if (charWidths_.size() < NUM_CHARS) {
        charWidths_.resize(NUM_CHARS, cellWidth_);
    }
    if (charOffsetsX_.size() < NUM_CHARS) {
        charOffsetsX_.resize(NUM_CHARS, 0);
    }
}

int BitmapFont::bitmapWidth() const { return bitmapWidth_; }
int BitmapFont::bitmapHeight() const { return bitmapHeight_; }
int BitmapFont::cellWidth() const { return cellWidth_; }
int BitmapFont::cellHeight() const { return cellHeight_; }

int BitmapFont::getCharWidth(int charCode) const {
    if (charCode >= 0 && charCode < static_cast<int>(charWidths_.size())) {
        return charWidths_[static_cast<std::size_t>(charCode)];
    }
    if (const auto it = overflowWidths_.find(charCode); it != overflowWidths_.end()) {
        return it->second;
    }
    return cellWidth_;
}

int BitmapFont::getCharOffsetX(int charCode) const {
    if (charCode >= 0 && charCode < static_cast<int>(charOffsetsX_.size())) {
        return charOffsetsX_[static_cast<std::size_t>(charCode)];
    }
    if (const auto it = overflowOffsetsX_.find(charCode); it != overflowOffsetsX_.end()) {
        return it->second;
    }
    return 0;
}

int BitmapFont::getStringWidth(std::string_view text) const {
    int width = 0;
    for (const unsigned char ch : text) {
        width += getCharWidth(ch);
    }
    return width;
}

int BitmapFont::getLineHeight() const { return metricsLineHeight_; }
int BitmapFont::getAscent() const { return metricsAscent_; }
const std::string& BitmapFont::getFontName() const { return fontName_; }
int BitmapFont::getFontSize() const { return fontSize_; }

void BitmapFont::drawChar(int charCode,
                          std::vector<std::uint32_t>& dst,
                          int dstW,
                          int dstH,
                          int dstX,
                          int dstY,
                          std::uint32_t color) const {
    const int drawX = dstX + getCharOffsetX(charCode);
    const int r = static_cast<int>((color >> 16U) & 0xFFU);
    const int g = static_cast<int>((color >> 8U) & 0xFFU);
    const int b = static_cast<int>(color & 0xFFU);

    if (dstW <= 0 || dstH <= 0 || dst.size() < static_cast<std::size_t>(dstW * dstH)) {
        return;
    }

    if (charCode >= 0 && charCode < NUM_CHARS) {
        const int col = charCode % GRID_COLUMNS;
        const int row = charCode / GRID_COLUMNS;
        const int cellX = col * cellWidth_;
        const int cellY = row * cellHeight_;

        for (int cy = 0; cy < cellHeight_; ++cy) {
            const int py = dstY + cy;
            if (py < 0 || py >= dstH) {
                continue;
            }
            for (int cx = 0; cx < cellWidth_; ++cx) {
                const int px = drawX + cx;
                if (px < 0 || px >= dstW) {
                    continue;
                }
                const int srcIdx = (cellY + cy) * bitmapWidth_ + (cellX + cx);
                if (srcIdx < 0 || srcIdx >= static_cast<int>(bitmap_.size())) {
                    continue;
                }
                blendPixel(dst, dstW, px, py, bitmap_[static_cast<std::size_t>(srcIdx)], r, g, b);
            }
        }
        return;
    }

    const auto glyph = overflowGlyphs_.find(charCode);
    if (glyph == overflowGlyphs_.end()) {
        return;
    }
    const auto& glyphPixels = glyph->second;
    for (int cy = 0; cy < cellHeight_; ++cy) {
        const int py = dstY + cy;
        if (py < 0 || py >= dstH) {
            continue;
        }
        for (int cx = 0; cx < cellWidth_; ++cx) {
            const int px = drawX + cx;
            if (px < 0 || px >= dstW) {
                continue;
            }
            const int srcIdx = cy * cellWidth_ + cx;
            if (srcIdx < 0 || srcIdx >= static_cast<int>(glyphPixels.size())) {
                continue;
            }
            blendPixel(dst, dstW, px, py, glyphPixels[static_cast<std::size_t>(srcIdx)], r, g, b);
        }
    }
}

void BitmapFont::blendPixel(std::vector<std::uint32_t>& dst,
                            int dstW,
                            int px,
                            int py,
                            std::uint32_t srcPixel,
                            int r,
                            int g,
                            int b) {
    const int srcA = static_cast<int>((srcPixel >> 24U) & 0xFFU);
    if (srcA == 0) {
        return;
    }

    const auto dstIdx = static_cast<std::size_t>(py * dstW + px);
    if (dstIdx >= dst.size()) {
        return;
    }

    if (srcA == 255) {
        dst[dstIdx] = 0xFF000000U |
                      (static_cast<std::uint32_t>(r & 0xFF) << 16U) |
                      (static_cast<std::uint32_t>(g & 0xFF) << 8U) |
                      static_cast<std::uint32_t>(b & 0xFF);
        return;
    }

    const auto existing = dst[dstIdx];
    const int ea = static_cast<int>((existing >> 24U) & 0xFFU);
    const int er = static_cast<int>((existing >> 16U) & 0xFFU);
    const int eg = static_cast<int>((existing >> 8U) & 0xFFU);
    const int eb = static_cast<int>(existing & 0xFFU);
    const int outA = srcA + (ea * (255 - srcA)) / 255;
    const int outR = (r * srcA + er * (255 - srcA)) / 255;
    const int outG = (g * srcA + eg * (255 - srcA)) / 255;
    const int outB = (b * srcA + eb * (255 - srcA)) / 255;
    dst[dstIdx] = (static_cast<std::uint32_t>(outA & 0xFF) << 24U) |
                  (static_cast<std::uint32_t>(outR & 0xFF) << 16U) |
                  (static_cast<std::uint32_t>(outG & 0xFF) << 8U) |
                  static_cast<std::uint32_t>(outB & 0xFF);
}

} // namespace libreshockwave::font
