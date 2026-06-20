#include "libreshockwave/font/TtfBitmapRasterizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace libreshockwave::font {
namespace {

struct TtfPoint {
    int x = 0;
    int y = 0;
    bool onCurve = false;
};

struct RasterPoint {
    float x = 0.0F;
    float y = 0.0F;
};

struct TtfGlyph {
    int numContours = 0;
    int xMin = 0;
    int yMin = 0;
    int xMax = 0;
    int yMax = 0;
    std::vector<std::vector<TtfPoint>> contours;
};

struct TtfTable {
    int offset = 0;
    int length = 0;
};

struct TtfData {
    int unitsPerEm = 0;
    int ascender = 0;
    int descender = 0;
    int indexToLocFormat = 0;
    std::map<int, int> cmap;
    std::vector<int> advanceWidths;
    std::vector<int> leftSideBearings;
    std::vector<int> locaOffsets;
    std::vector<std::uint8_t> glyfTable;
    int numGlyphs = 0;
};

int javaRound(float value) {
    return static_cast<int>(std::floor(value + 0.5F));
}

float gridFit(float value) {
    return static_cast<float>(javaRound(value));
}

RasterPoint scalePoint(const TtfPoint& point, float scale, int baselineY) {
    return RasterPoint{
        gridFit(static_cast<float>(point.x) * scale),
        gridFit(static_cast<float>(baselineY) - static_cast<float>(point.y) * scale),
    };
}

int readU16(const std::vector<std::uint8_t>& data, int offset) {
    if (offset < 0 || offset + 2 > static_cast<int>(data.size())) {
        return 0;
    }
    return ((data[static_cast<std::size_t>(offset)] & 0xFF) << 8) |
           (data[static_cast<std::size_t>(offset + 1)] & 0xFF);
}

int readI16(const std::vector<std::uint8_t>& data, int offset) {
    const int value = readU16(data, offset);
    return value >= 0x8000 ? value - 0x10000 : value;
}

int readI32(const std::vector<std::uint8_t>& data, int offset) {
    if (offset < 0 || offset + 4 > static_cast<int>(data.size())) {
        return 0;
    }
    const std::uint32_t value =
        (static_cast<std::uint32_t>(data[static_cast<std::size_t>(offset)] & 0xFF) << 24U) |
        (static_cast<std::uint32_t>(data[static_cast<std::size_t>(offset + 1)] & 0xFF) << 16U) |
        (static_cast<std::uint32_t>(data[static_cast<std::size_t>(offset + 2)] & 0xFF) << 8U) |
        static_cast<std::uint32_t>(data[static_cast<std::size_t>(offset + 3)] & 0xFF);
    return static_cast<int>(static_cast<std::int32_t>(value));
}

void parseCmapFormat4(const std::vector<std::uint8_t>& data, int offset, TtfData& ttf) {
    if (offset < 0 || offset + 14 > static_cast<int>(data.size())) {
        return;
    }
    const int segCount = readU16(data, offset + 6) / 2;
    const int endCodesOffset = offset + 14;
    const int startCodesOffset = endCodesOffset + segCount * 2 + 2;
    const int idDeltaOffset = startCodesOffset + segCount * 2;
    const int idRangeOffsetOffset = idDeltaOffset + segCount * 2;

    for (int i = 0; i < segCount; ++i) {
        const int endCode = readU16(data, endCodesOffset + i * 2);
        const int startCode = readU16(data, startCodesOffset + i * 2);
        const int idDelta = readI16(data, idDeltaOffset + i * 2);
        const int idRangeOffset = readU16(data, idRangeOffsetOffset + i * 2);
        if (startCode == 0xFFFF) {
            break;
        }
        if (startCode > endCode) {
            continue;
        }

        for (int code = startCode; code <= endCode; ++code) {
            int glyphIndex = 0;
            if (idRangeOffset == 0) {
                glyphIndex = (code + idDelta) & 0xFFFF;
            } else {
                const int rangeAddr = idRangeOffsetOffset + i * 2 + idRangeOffset + (code - startCode) * 2;
                if (rangeAddr + 2 > static_cast<int>(data.size())) {
                    continue;
                }
                glyphIndex = readU16(data, rangeAddr);
                if (glyphIndex != 0) {
                    glyphIndex = (glyphIndex + idDelta) & 0xFFFF;
                }
            }
            if (glyphIndex != 0 && glyphIndex < ttf.numGlyphs) {
                ttf.cmap[code] = glyphIndex;
            }
        }
    }
}

void parseCmap(const std::vector<std::uint8_t>& data, int cmapOffset, TtfData& ttf) {
    const int numSubtables = readU16(data, cmapOffset + 2);
    for (int i = 0; i < numSubtables; ++i) {
        const int base = cmapOffset + 4 + i * 8;
        if (base + 8 > static_cast<int>(data.size())) {
            break;
        }
        const int platformId = readU16(data, base);
        const int encodingId = readU16(data, base + 2);
        const int subtableOffset = readI32(data, base + 4);
        const int absoluteOffset = cmapOffset + subtableOffset;
        if (platformId == 3 && encodingId == 1 && readU16(data, absoluteOffset) == 4) {
            parseCmapFormat4(data, absoluteOffset, ttf);
            return;
        }
    }

    if (numSubtables > 0) {
        const int subtableOffset = readI32(data, cmapOffset + 8);
        const int absoluteOffset = cmapOffset + subtableOffset;
        if (readU16(data, absoluteOffset) == 4) {
            parseCmapFormat4(data, absoluteOffset, ttf);
        }
    }
}

bool tableAvailable(const std::vector<std::uint8_t>& data, const TtfTable& table, int minLength) {
    return table.offset >= 0 &&
           table.length >= minLength &&
           table.offset + minLength <= static_cast<int>(data.size());
}

bool parseTtf(const std::vector<std::uint8_t>& data, TtfData& ttf) {
    if (data.size() < 12) {
        return false;
    }

    const int numTables = readU16(data, 4);
    std::unordered_map<std::string, TtfTable> tables;
    for (int i = 0; i < numTables; ++i) {
        const int base = 12 + i * 16;
        if (base + 16 > static_cast<int>(data.size())) {
            break;
        }
        const std::string tag(reinterpret_cast<const char*>(data.data() + base), 4);
        tables[tag] = TtfTable{readI32(data, base + 8), readI32(data, base + 12)};
    }

    const auto headIt = tables.find("head");
    const auto hheaIt = tables.find("hhea");
    const auto maxpIt = tables.find("maxp");
    const auto hmtxIt = tables.find("hmtx");
    const auto cmapIt = tables.find("cmap");
    const auto locaIt = tables.find("loca");
    const auto glyfIt = tables.find("glyf");
    if (headIt == tables.end() || hheaIt == tables.end() || maxpIt == tables.end() ||
        hmtxIt == tables.end() || cmapIt == tables.end() || locaIt == tables.end() ||
        glyfIt == tables.end()) {
        return false;
    }
    if (!tableAvailable(data, headIt->second, 54) ||
        !tableAvailable(data, hheaIt->second, 36) ||
        !tableAvailable(data, maxpIt->second, 6)) {
        return false;
    }

    ttf.unitsPerEm = readU16(data, headIt->second.offset + 18);
    ttf.indexToLocFormat = readI16(data, headIt->second.offset + 50);
    ttf.ascender = readI16(data, hheaIt->second.offset + 4);
    ttf.descender = readI16(data, hheaIt->second.offset + 6);
    const int numHMetrics = readU16(data, hheaIt->second.offset + 34);
    ttf.numGlyphs = readU16(data, maxpIt->second.offset + 4);
    if (ttf.unitsPerEm <= 0 || ttf.numGlyphs <= 0) {
        return false;
    }

    ttf.advanceWidths.assign(static_cast<std::size_t>(ttf.numGlyphs), 0);
    ttf.leftSideBearings.assign(static_cast<std::size_t>(ttf.numGlyphs), 0);
    int lastAdvanceWidth = 0;
    for (int i = 0; i < ttf.numGlyphs; ++i) {
        if (i < numHMetrics) {
            const int offset = hmtxIt->second.offset + i * 4;
            if (offset + 4 > static_cast<int>(data.size())) {
                break;
            }
            ttf.advanceWidths[static_cast<std::size_t>(i)] = readU16(data, offset);
            ttf.leftSideBearings[static_cast<std::size_t>(i)] = readI16(data, offset + 2);
            lastAdvanceWidth = ttf.advanceWidths[static_cast<std::size_t>(i)];
        } else {
            ttf.advanceWidths[static_cast<std::size_t>(i)] = lastAdvanceWidth;
            const int offset = hmtxIt->second.offset + numHMetrics * 4 + (i - numHMetrics) * 2;
            if (offset + 2 <= static_cast<int>(data.size())) {
                ttf.leftSideBearings[static_cast<std::size_t>(i)] = readI16(data, offset);
            }
        }
    }

    parseCmap(data, cmapIt->second.offset, ttf);

    ttf.locaOffsets.assign(static_cast<std::size_t>(ttf.numGlyphs + 1), 0);
    for (int i = 0; i <= ttf.numGlyphs; ++i) {
        if (ttf.indexToLocFormat == 0) {
            const int offset = locaIt->second.offset + i * 2;
            if (offset + 2 <= static_cast<int>(data.size())) {
                ttf.locaOffsets[static_cast<std::size_t>(i)] = readU16(data, offset) * 2;
            }
        } else {
            const int offset = locaIt->second.offset + i * 4;
            if (offset + 4 <= static_cast<int>(data.size())) {
                ttf.locaOffsets[static_cast<std::size_t>(i)] = readI32(data, offset);
            }
        }
    }

    const int glyfOffset = glyfIt->second.offset;
    const int glyfLength = glyfIt->second.length;
    if (glyfOffset < 0 || glyfLength <= 0 || glyfOffset >= static_cast<int>(data.size())) {
        return false;
    }
    const int copyLength = std::min(glyfLength, static_cast<int>(data.size()) - glyfOffset);
    ttf.glyfTable.assign(data.begin() + glyfOffset, data.begin() + glyfOffset + copyLength);
    return true;
}

std::unique_ptr<TtfGlyph> parseGlyph(const TtfData& ttf, int glyphIndex) {
    if (glyphIndex < 0 || glyphIndex >= ttf.numGlyphs) {
        return nullptr;
    }
    const int offset = ttf.locaOffsets[static_cast<std::size_t>(glyphIndex)];
    const int nextOffset = ttf.locaOffsets[static_cast<std::size_t>(glyphIndex + 1)];
    if (offset == nextOffset || offset < 0 || offset + 10 > static_cast<int>(ttf.glyfTable.size())) {
        return nullptr;
    }

    auto glyph = std::make_unique<TtfGlyph>();
    glyph->numContours = readI16(ttf.glyfTable, offset);
    glyph->xMin = readI16(ttf.glyfTable, offset + 2);
    glyph->yMin = readI16(ttf.glyfTable, offset + 4);
    glyph->xMax = readI16(ttf.glyfTable, offset + 6);
    glyph->yMax = readI16(ttf.glyfTable, offset + 8);
    if (glyph->numContours < 0) {
        return nullptr;
    }
    if (glyph->numContours == 0) {
        return glyph;
    }

    int pos = offset + 10;
    std::vector<int> endPoints(static_cast<std::size_t>(glyph->numContours), 0);
    for (int i = 0; i < glyph->numContours; ++i) {
        if (pos + 2 > static_cast<int>(ttf.glyfTable.size())) {
            return nullptr;
        }
        endPoints[static_cast<std::size_t>(i)] = readU16(ttf.glyfTable, pos);
        pos += 2;
    }

    const int numPoints = endPoints.empty() ? 0 : endPoints.back() + 1;
    if (numPoints <= 0) {
        return glyph;
    }
    if (pos + 2 > static_cast<int>(ttf.glyfTable.size())) {
        return nullptr;
    }
    const int instructionLength = readU16(ttf.glyfTable, pos);
    pos += 2 + instructionLength;
    if (pos > static_cast<int>(ttf.glyfTable.size())) {
        return nullptr;
    }

    std::vector<int> flags(static_cast<std::size_t>(numPoints), 0);
    for (int i = 0; i < numPoints; ++i) {
        if (pos >= static_cast<int>(ttf.glyfTable.size())) {
            return nullptr;
        }
        flags[static_cast<std::size_t>(i)] = ttf.glyfTable[static_cast<std::size_t>(pos++)] & 0xFF;
        if ((flags[static_cast<std::size_t>(i)] & 0x08) != 0) {
            if (pos >= static_cast<int>(ttf.glyfTable.size())) {
                return nullptr;
            }
            const int repeatCount = ttf.glyfTable[static_cast<std::size_t>(pos++)] & 0xFF;
            const int repeatedFlag = flags[static_cast<std::size_t>(i)];
            for (int r = 0; r < repeatCount && i + 1 < numPoints; ++r) {
                flags[static_cast<std::size_t>(++i)] = repeatedFlag;
            }
        }
    }

    std::vector<int> xCoords(static_cast<std::size_t>(numPoints), 0);
    int x = 0;
    for (int i = 0; i < numPoints; ++i) {
        const int flag = flags[static_cast<std::size_t>(i)];
        if ((flag & 0x02) != 0) {
            if (pos >= static_cast<int>(ttf.glyfTable.size())) {
                return nullptr;
            }
            const int dx = ttf.glyfTable[static_cast<std::size_t>(pos++)] & 0xFF;
            x += (flag & 0x10) != 0 ? dx : -dx;
        } else if ((flag & 0x10) == 0) {
            if (pos + 2 > static_cast<int>(ttf.glyfTable.size())) {
                return nullptr;
            }
            x += readI16(ttf.glyfTable, pos);
            pos += 2;
        }
        xCoords[static_cast<std::size_t>(i)] = x;
    }

    std::vector<int> yCoords(static_cast<std::size_t>(numPoints), 0);
    int y = 0;
    for (int i = 0; i < numPoints; ++i) {
        const int flag = flags[static_cast<std::size_t>(i)];
        if ((flag & 0x04) != 0) {
            if (pos >= static_cast<int>(ttf.glyfTable.size())) {
                return nullptr;
            }
            const int dy = ttf.glyfTable[static_cast<std::size_t>(pos++)] & 0xFF;
            y += (flag & 0x20) != 0 ? dy : -dy;
        } else if ((flag & 0x20) == 0) {
            if (pos + 2 > static_cast<int>(ttf.glyfTable.size())) {
                return nullptr;
            }
            y += readI16(ttf.glyfTable, pos);
            pos += 2;
        }
        yCoords[static_cast<std::size_t>(i)] = y;
    }

    int pointIndex = 0;
    for (int contourIndex = 0; contourIndex < glyph->numContours; ++contourIndex) {
        const int endPoint = endPoints[static_cast<std::size_t>(contourIndex)];
        std::vector<TtfPoint> contour;
        for (int i = pointIndex; i <= endPoint && i < numPoints; ++i) {
            contour.push_back(TtfPoint{
                xCoords[static_cast<std::size_t>(i)],
                yCoords[static_cast<std::size_t>(i)],
                (flags[static_cast<std::size_t>(i)] & 0x01) != 0,
            });
        }
        if (contour.size() >= 2) {
            glyph->contours.push_back(std::move(contour));
        }
        pointIndex = endPoint + 1;
    }
    return glyph;
}

void subdivideQuadratic(std::vector<RasterPoint>& result,
                        float x0,
                        float y0,
                        float cpx,
                        float cpy,
                        float endX,
                        float endY,
                        int levels) {
    if (levels <= 0) {
        result.push_back(RasterPoint{endX, endY});
        return;
    }
    const float midX = (x0 + 2.0F * cpx + endX) / 4.0F;
    const float midY = (y0 + 2.0F * cpy + endY) / 4.0F;
    const float cp1X = (x0 + cpx) / 2.0F;
    const float cp1Y = (y0 + cpy) / 2.0F;
    const float cp2X = (cpx + endX) / 2.0F;
    const float cp2Y = (cpy + endY) / 2.0F;
    subdivideQuadratic(result, x0, y0, cp1X, cp1Y, midX, midY, levels - 1);
    subdivideQuadratic(result, midX, midY, cp2X, cp2Y, endX, endY, levels - 1);
}

std::vector<RasterPoint> flattenTtfContour(const std::vector<TtfPoint>& contour,
                                           float scale,
                                           int baselineY) {
    std::vector<RasterPoint> result;
    const int n = static_cast<int>(contour.size());
    if (n < 2) {
        return result;
    }

    std::vector<TtfPoint> expanded;
    expanded.reserve(contour.size() * 2);
    for (int i = 0; i < n; ++i) {
        const auto& current = contour[static_cast<std::size_t>(i)];
        const auto& next = contour[static_cast<std::size_t>((i + 1) % n)];
        expanded.push_back(current);
        if (!current.onCurve && !next.onCurve) {
            expanded.push_back(TtfPoint{(current.x + next.x) / 2, (current.y + next.y) / 2, true});
        }
    }

    int startIndex = -1;
    for (int i = 0; i < static_cast<int>(expanded.size()); ++i) {
        if (expanded[static_cast<std::size_t>(i)].onCurve) {
            startIndex = i;
            break;
        }
    }
    if (startIndex < 0) {
        return result;
    }

    const int size = static_cast<int>(expanded.size());
    const auto& start = expanded[static_cast<std::size_t>(startIndex)];
    result.push_back(scalePoint(start, scale, baselineY));

    int index = (startIndex + 1) % size;
    int count = 0;
    while (count < size) {
        const auto& point = expanded[static_cast<std::size_t>(index)];
        if (point.onCurve) {
            result.push_back(scalePoint(point, scale, baselineY));
        } else {
            const auto& next = expanded[static_cast<std::size_t>((index + 1) % size)];
            if (!next.onCurve || result.empty()) {
                return {};
            }
            const auto control = scalePoint(point, scale, baselineY);
            const auto end = scalePoint(next, scale, baselineY);
            const auto& last = result.back();
            subdivideQuadratic(result, last.x, last.y, control.x, control.y, end.x, end.y, 3);

            index = (index + 1) % size;
            ++count;
        }

        index = (index + 1) % size;
        ++count;
        if (index == startIndex) {
            break;
        }
    }
    return result;
}

bool rasterizeContours(const std::vector<std::vector<TtfPoint>>& contours,
                       std::vector<std::uint32_t>& argb,
                       int bufferWidth,
                       int bufferHeight,
                       int cellX,
                       int cellY,
                       int cellWidth,
                       int cellHeight,
                       float scale,
                       int baselineY) {
    std::vector<std::vector<RasterPoint>> polygons;
    for (const auto& contour : contours) {
        auto points = flattenTtfContour(contour, scale, baselineY);
        if (points.size() >= 3) {
            polygons.push_back(std::move(points));
        }
    }

    if (polygons.empty()) {
        return false;
    }

    bool wrotePixel = false;
    for (int y = 0; y < cellHeight; ++y) {
        const float scanY = static_cast<float>(y) + 0.5F;
        std::vector<std::pair<float, int>> crossings;
        for (const auto& polygon : polygons) {
            const int n = static_cast<int>(polygon.size());
            for (int i = 0; i < n; ++i) {
                const auto& p0 = polygon[static_cast<std::size_t>(i)];
                const auto& p1 = polygon[static_cast<std::size_t>((i + 1) % n)];
                if (std::abs(p0.y - p1.y) < 0.001F) {
                    continue;
                }
                if ((p0.y <= scanY && p1.y > scanY) || (p1.y <= scanY && p0.y > scanY)) {
                    const float t = (scanY - p0.y) / (p1.y - p0.y);
                    crossings.emplace_back(p0.x + t * (p1.x - p0.x), p0.y < p1.y ? 1 : -1);
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
                if (px < 0 || px >= bufferWidth || py < 0 || py >= bufferHeight) {
                    continue;
                }
                const int index = py * bufferWidth + px;
                if (index >= 0 && index < static_cast<int>(argb.size())) {
                    argb[static_cast<std::size_t>(index)] = 0xFF000000U;
                    wrotePixel = true;
                }
            }
        }
    }
    return wrotePixel;
}

std::shared_ptr<BitmapFont> buildBitmapFont(const TtfData& ttf,
                                            int targetSize,
                                            const std::string& fontName) {
    const float scale = static_cast<float>(targetSize) / static_cast<float>(ttf.unitsPerEm);
    int maxAdvancePx = 0;
    int maxGlyphWidthPx = 0;
    for (const auto& [_, glyphIndex] : ttf.cmap) {
        const int advancePx = javaRound(static_cast<float>(ttf.advanceWidths[static_cast<std::size_t>(glyphIndex)]) *
                                        scale);
        maxAdvancePx = std::max(maxAdvancePx, advancePx);
        const auto glyph = parseGlyph(ttf, glyphIndex);
        if (glyph != nullptr) {
            const int glyphWidthPx =
                std::max(1, javaRound(static_cast<float>(glyph->xMax - glyph->xMin) * scale) + 1);
            maxGlyphWidthPx = std::max(maxGlyphWidthPx, glyphWidthPx);
        }
    }

    const int ascentPx = javaRound(std::abs(static_cast<float>(ttf.ascender)) * scale);
    const int descentPx = javaRound(std::abs(static_cast<float>(ttf.descender)) * scale);
    const int metricsLineHeight = ascentPx + descentPx;
    const int baselinePx = std::max(0, ascentPx - 1);
    const int cellHeight = metricsLineHeight + 1;
    const int cellWidth = std::max(1, std::max(maxAdvancePx, maxGlyphWidthPx));
    const int bitmapWidth = cellWidth * BitmapFont::GRID_COLUMNS;
    const int bitmapHeight = cellHeight * BitmapFont::GRID_ROWS;

    std::vector<std::uint32_t> argb(static_cast<std::size_t>(bitmapWidth * bitmapHeight), 0);
    std::vector<int> charWidths(BitmapFont::NUM_CHARS, cellWidth);
    std::vector<int> charOffsetsX(BitmapFont::NUM_CHARS, 0);
    BitmapFont::GlyphMap overflowGlyphs;
    BitmapFont::MetricMap overflowWidths;
    BitmapFont::MetricMap overflowOffsetsX;
    bool hasRasterizedInk = false;

    for (const auto& [charCode, glyphIndex] : ttf.cmap) {
        const auto glyphIndexSize = static_cast<std::size_t>(glyphIndex);
        const int advancePx = std::max(1, javaRound(static_cast<float>(ttf.advanceWidths[glyphIndexSize]) * scale));
        const auto glyph = parseGlyph(ttf, glyphIndex);
        int glyphOffsetX = 0;
        int glyphDrawOffsetX = 0;
        if (glyph != nullptr) {
            glyphDrawOffsetX = javaRound(static_cast<float>(ttf.leftSideBearings[glyphIndexSize]) * scale);
            glyphOffsetX = -javaRound(static_cast<float>(glyph->xMin) * scale);
        }

        if (charCode >= 0 && charCode < BitmapFont::NUM_CHARS) {
            charWidths[static_cast<std::size_t>(charCode)] = advancePx;
            charOffsetsX[static_cast<std::size_t>(charCode)] = glyphDrawOffsetX;
            if (glyph != nullptr && !glyph->contours.empty()) {
                const int column = charCode % BitmapFont::GRID_COLUMNS;
                const int row = charCode / BitmapFont::GRID_COLUMNS;
                hasRasterizedInk = rasterizeContours(glyph->contours,
                                                      argb,
                                                      bitmapWidth,
                                                      bitmapHeight,
                                                      column * cellWidth + glyphOffsetX,
                                                      row * cellHeight,
                                                      cellWidth,
                                                      cellHeight,
                                                      scale,
                                                      baselinePx) ||
                                   hasRasterizedInk;
            }
        } else {
            overflowWidths[charCode] = advancePx;
            overflowOffsetsX[charCode] = glyphDrawOffsetX;
            if (glyph != nullptr && !glyph->contours.empty()) {
                std::vector<std::uint32_t> cellBuffer(static_cast<std::size_t>(cellWidth * cellHeight), 0);
                if (rasterizeContours(glyph->contours,
                                      cellBuffer,
                                      cellWidth,
                                      cellHeight,
                                      glyphOffsetX,
                                      0,
                                      cellWidth,
                                      cellHeight,
                                      scale,
                                      baselinePx)) {
                    hasRasterizedInk = true;
                    overflowGlyphs[charCode] = std::move(cellBuffer);
                }
            }
        }
    }

    if (!hasRasterizedInk) {
        return nullptr;
    }

    if (!ttf.cmap.contains(' ')) {
        charWidths[static_cast<std::size_t>(' ')] =
            std::max(1, javaRound(static_cast<float>(ttf.unitsPerEm) * scale / 4.0F));
    }

    return BitmapFont::create(std::move(argb),
                              bitmapWidth,
                              bitmapHeight,
                              cellWidth,
                              cellHeight,
                              std::move(charWidths),
                              std::move(charOffsetsX),
                              fontName,
                              targetSize,
                              ascentPx,
                              metricsLineHeight,
                              std::move(overflowGlyphs),
                              std::move(overflowWidths),
                              std::move(overflowOffsetsX));
}

} // namespace

std::shared_ptr<BitmapFont> TtfBitmapRasterizer::rasterize(const std::vector<std::uint8_t>& ttfBytes,
                                                          int targetSize,
                                                          const std::string& fontName) {
    if (ttfBytes.size() < 12 || targetSize <= 0) {
        return nullptr;
    }

    try {
        TtfData ttf;
        if (!parseTtf(ttfBytes, ttf)) {
            return nullptr;
        }
        return buildBitmapFont(ttf, targetSize, fontName);
    } catch (const std::exception&) {
        return nullptr;
    }
}

} // namespace libreshockwave::font
