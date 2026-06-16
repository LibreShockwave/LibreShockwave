#include "libreshockwave/font/Pfr1TtfConverter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace libreshockwave::font {
namespace {

struct ByteWriter {
    std::vector<std::uint8_t> bytes;

    void writeU8(int value) {
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
    }

    void writeU16(int value) {
        const auto raw = static_cast<std::uint16_t>(value);
        bytes.push_back(static_cast<std::uint8_t>((raw >> 8U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>(raw & 0xFFU));
    }

    void writeU32(std::uint32_t value) {
        bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    }

    void writeI64Zero() {
        for (int i = 0; i < 8; ++i) {
            writeU8(0);
        }
    }

    void writeBytes(const std::vector<std::uint8_t>& value) {
        bytes.insert(bytes.end(), value.begin(), value.end());
    }

    void writeAsciiTag(std::string_view tag) {
        for (std::size_t i = 0; i < 4; ++i) {
            writeU8(i < tag.size() ? static_cast<unsigned char>(tag[i]) : 0);
        }
    }
};

struct TtPoint {
    int x = 0;
    int y = 0;
    bool onCurve = true;
};

struct GlyphEntry {
    int charCode = 0;
    int advanceWidth = 0;
    int lsb = 0;
    Pfr1Font::OutlineGlyph glyph;
};

int highestOneBit(int value) {
    int result = 1;
    while (result <= value / 2) {
        result <<= 1;
    }
    return result;
}

int trailingZeroCount(int value) {
    int result = 0;
    while (value > 1 && (value & 1) == 0) {
        ++result;
        value >>= 1;
    }
    return result;
}

std::uint32_t checksum(const std::vector<std::uint8_t>& data) {
    std::uint32_t sum = 0;
    const std::size_t len = (data.size() + 3U) & ~std::size_t{3U};
    for (std::size_t i = 0; i < len; i += 4) {
        const auto b0 = i < data.size() ? static_cast<std::uint32_t>(data[i]) : 0U;
        const auto b1 = i + 1 < data.size() ? static_cast<std::uint32_t>(data[i + 1]) : 0U;
        const auto b2 = i + 2 < data.size() ? static_cast<std::uint32_t>(data[i + 2]) : 0U;
        const auto b3 = i + 3 < data.size() ? static_cast<std::uint32_t>(data[i + 3]) : 0U;
        sum += (b0 << 24U) | (b1 << 16U) | (b2 << 8U) | b3;
    }
    return sum;
}

std::vector<std::uint8_t> buildHead(int unitsPerEm, const Pfr1Font::FontMetrics& metrics) {
    ByteWriter writer;
    writer.writeU32(0x00010000U);
    writer.writeU32(0x00005000U);
    writer.writeU32(0);
    writer.writeU32(0x5F0F3CF5U);
    writer.writeU16(0x000B);
    writer.writeU16(unitsPerEm);
    writer.writeI64Zero();
    writer.writeI64Zero();
    writer.writeU16(metrics.xMin);
    writer.writeU16(metrics.yMin);
    writer.writeU16(metrics.xMax);
    writer.writeU16(metrics.yMax);
    writer.writeU16(0);
    writer.writeU16(8);
    writer.writeU16(2);
    writer.writeU16(1);
    writer.writeU16(0);
    return std::move(writer.bytes);
}

std::vector<std::uint8_t> buildHhea(const Pfr1Font::FontMetrics& metrics,
                                    const std::vector<GlyphEntry>& entries) {
    int maxAdvanceWidth = 0;
    for (const auto& entry : entries) {
        maxAdvanceWidth = std::max(maxAdvanceWidth, entry.advanceWidth);
    }

    ByteWriter writer;
    writer.writeU32(0x00010000U);
    writer.writeU16(metrics.ascender);
    writer.writeU16(metrics.descender);
    writer.writeU16(0);
    writer.writeU16(maxAdvanceWidth);
    writer.writeU16(metrics.xMin);
    writer.writeU16(metrics.xMin);
    writer.writeU16(metrics.xMax);
    writer.writeU16(1);
    writer.writeU16(0);
    writer.writeU16(0);
    writer.writeI64Zero();
    writer.writeU16(0);
    writer.writeU16(static_cast<int>(entries.size()));
    return std::move(writer.bytes);
}

std::vector<std::uint8_t> buildMaxp(int numGlyphs) {
    ByteWriter writer;
    writer.writeU32(0x00010000U);
    writer.writeU16(numGlyphs);
    writer.writeU16(256);
    writer.writeU16(32);
    writer.writeU16(0);
    writer.writeU16(0);
    writer.writeU16(1);
    writer.writeU16(0);
    writer.writeU16(0);
    writer.writeU16(0);
    writer.writeU16(0);
    writer.writeU16(0);
    writer.writeU16(0);
    writer.writeU16(0);
    writer.writeU16(0);
    return std::move(writer.bytes);
}

std::vector<std::uint8_t> buildOs2(const Pfr1Font::FontMetrics& metrics,
                                   int unitsPerEm,
                                   const std::map<int, int>& cmapEntries) {
    int firstChar = 0x0020;
    int lastChar = 0x00FF;
    if (!cmapEntries.empty()) {
        firstChar = cmapEntries.begin()->first;
        lastChar = cmapEntries.rbegin()->first;
    }

    ByteWriter writer;
    writer.writeU16(4);
    writer.writeU16(unitsPerEm / 2);
    writer.writeU16(400);
    writer.writeU16(5);
    writer.writeU16(0);
    writer.writeU16(unitsPerEm / 10);
    writer.writeU16(unitsPerEm / 10);
    writer.writeU16(0);
    writer.writeU16(unitsPerEm / 5);
    writer.writeU16(unitsPerEm / 10);
    writer.writeU16(unitsPerEm / 10);
    writer.writeU16(0);
    writer.writeU16(unitsPerEm / 3);
    writer.writeU16(metrics.stdVW > 0 ? metrics.stdVW : unitsPerEm / 20);
    writer.writeU16(metrics.ascender / 2);
    writer.writeU16(0);
    for (int i = 0; i < 10; ++i) {
        writer.writeU8(0);
    }
    writer.writeU32(0);
    writer.writeU32(0);
    writer.writeU32(0);
    writer.writeU32(0);
    writer.writeAsciiTag("    ");
    writer.writeU16(0x0040);
    writer.writeU16(firstChar);
    writer.writeU16(std::min(lastChar, 0xFFFF));
    writer.writeU16(metrics.ascender);
    writer.writeU16(metrics.descender);
    writer.writeU16(0);
    writer.writeU16(std::max(metrics.ascender, 0));
    writer.writeU16(std::abs(std::min(metrics.descender, 0)));
    writer.writeU32(1);
    writer.writeU32(0);
    writer.writeU16(metrics.ascender * 8 / 10);
    writer.writeU16(metrics.ascender);
    writer.writeU16(0);
    writer.writeU16(0x0020);
    writer.writeU16(1);
    return std::move(writer.bytes);
}

std::vector<std::uint8_t> utf16beAscii(std::string_view value) {
    std::vector<std::uint8_t> result;
    result.reserve(value.size() * 2);
    for (unsigned char ch : value) {
        result.push_back(0);
        result.push_back(ch);
    }
    return result;
}

std::string removeSpaces(std::string value) {
    value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
    return value;
}

std::vector<std::uint8_t> buildName(const std::string& familyName) {
    const std::vector<std::string> names{
        "",
        familyName,
        "Regular",
        familyName + "-Regular",
        familyName,
        "Version 1.0",
        removeSpaces(familyName),
    };

    std::vector<std::vector<std::uint8_t>> encoded;
    encoded.reserve(names.size());
    for (const auto& name : names) {
        encoded.push_back(utf16beAscii(name));
    }

    const int count = static_cast<int>(encoded.size());
    const int storageOffset = 6 + count * 12;
    ByteWriter writer;
    writer.writeU16(0);
    writer.writeU16(count);
    writer.writeU16(storageOffset);

    int stringOffset = 0;
    for (int i = 0; i < count; ++i) {
        writer.writeU16(3);
        writer.writeU16(1);
        writer.writeU16(0x0409);
        writer.writeU16(i);
        writer.writeU16(static_cast<int>(encoded[static_cast<std::size_t>(i)].size()));
        writer.writeU16(stringOffset);
        stringOffset += static_cast<int>(encoded[static_cast<std::size_t>(i)].size());
    }

    for (const auto& text : encoded) {
        writer.writeBytes(text);
    }
    return std::move(writer.bytes);
}

std::vector<std::uint8_t> buildCmap(const std::map<int, int>& charToGlyph) {
    std::vector<std::pair<int, int>> segments;
    if (!charToGlyph.empty()) {
        int start = charToGlyph.begin()->first;
        int end = start;
        for (auto it = std::next(charToGlyph.begin()); it != charToGlyph.end(); ++it) {
            if (it->first == end + 1) {
                end = it->first;
            } else {
                segments.emplace_back(start, end);
                start = it->first;
                end = it->first;
            }
        }
        segments.emplace_back(start, end);
    }
    segments.emplace_back(0xFFFF, 0xFFFF);

    const int segCount = static_cast<int>(segments.size());
    const int searchRange = highestOneBit(segCount) * 2;
    const int entrySelector = trailingZeroCount(highestOneBit(segCount));
    const int rangeShift = segCount * 2 - searchRange;

    ByteWriter subtable;
    for (const auto& segment : segments) {
        subtable.writeU16(segment.second);
    }
    subtable.writeU16(0);
    for (const auto& segment : segments) {
        subtable.writeU16(segment.first);
    }

    std::vector<int> glyphIdArrayEntries;
    std::vector<int> idRangeOffsets(static_cast<std::size_t>(segCount), 0);
    for (int i = 0; i < segCount; ++i) {
        const auto& segment = segments[static_cast<std::size_t>(i)];
        if (segment.first == 0xFFFF) {
            idRangeOffsets[static_cast<std::size_t>(i)] = 0;
        } else {
            const int arrayStartIndex = static_cast<int>(glyphIdArrayEntries.size());
            const int remainingOffsets = segCount - i;
            idRangeOffsets[static_cast<std::size_t>(i)] = (remainingOffsets + arrayStartIndex) * 2;
            for (int code = segment.first; code <= segment.second; ++code) {
                const auto mapped = charToGlyph.find(code);
                glyphIdArrayEntries.push_back(mapped != charToGlyph.end() ? mapped->second : 0);
            }
        }
    }

    for (int i = 0; i < segCount; ++i) {
        subtable.writeU16(segments[static_cast<std::size_t>(i)].first == 0xFFFF ? 1 : 0);
    }
    for (const int offset : idRangeOffsets) {
        subtable.writeU16(offset);
    }
    for (const int glyphId : glyphIdArrayEntries) {
        subtable.writeU16(glyphId);
    }

    const auto subtableData = std::move(subtable.bytes);
    const int subtableLength = 14 + static_cast<int>(subtableData.size());

    ByteWriter writer;
    writer.writeU16(0);
    writer.writeU16(1);
    writer.writeU16(3);
    writer.writeU16(1);
    writer.writeU32(12);
    writer.writeU16(4);
    writer.writeU16(subtableLength);
    writer.writeU16(0);
    writer.writeU16(segCount * 2);
    writer.writeU16(searchRange);
    writer.writeU16(entrySelector);
    writer.writeU16(rangeShift);
    writer.writeBytes(subtableData);
    return std::move(writer.bytes);
}

std::vector<std::uint8_t> buildPost() {
    ByteWriter writer;
    writer.writeU32(0x00030000U);
    writer.writeU32(0);
    writer.writeU16(-100);
    writer.writeU16(50);
    writer.writeU32(0);
    writer.writeU32(0);
    writer.writeU32(0);
    writer.writeU32(0);
    writer.writeU32(0);
    return std::move(writer.bytes);
}

std::vector<std::uint8_t> buildHmtx(const std::vector<GlyphEntry>& entries) {
    ByteWriter writer;
    for (const auto& entry : entries) {
        writer.writeU16(entry.advanceWidth);
        writer.writeU16(entry.lsb);
    }
    return std::move(writer.bytes);
}

std::vector<std::uint8_t> buildLoca(const std::vector<int>& offsets) {
    ByteWriter writer;
    for (const int offset : offsets) {
        writer.writeU16(offset);
    }
    return std::move(writer.bytes);
}

int roundToInt(float value) {
    return static_cast<int>(std::lround(value));
}

void cubicToQuadratic(std::vector<TtPoint>& points,
                      float x0,
                      float y0,
                      float c1x,
                      float c1y,
                      float c2x,
                      float c2y,
                      float ex,
                      float ey) {
    const float m01x = (x0 + c1x) / 2.0F;
    const float m01y = (y0 + c1y) / 2.0F;
    const float m12x = (c1x + c2x) / 2.0F;
    const float m12y = (c1y + c2y) / 2.0F;
    const float m23x = (c2x + ex) / 2.0F;
    const float m23y = (c2y + ey) / 2.0F;
    const float m012x = (m01x + m12x) / 2.0F;
    const float m012y = (m01y + m12y) / 2.0F;
    const float m123x = (m12x + m23x) / 2.0F;
    const float m123y = (m12y + m23y) / 2.0F;
    const float midX = (m012x + m123x) / 2.0F;
    const float midY = (m012y + m123y) / 2.0F;

    const float q1x = (m01x + m012x) / 2.0F;
    const float q1y = (m01y + m012y) / 2.0F;
    points.push_back(TtPoint{roundToInt(q1x), roundToInt(q1y), false});
    points.push_back(TtPoint{roundToInt(midX), roundToInt(midY), true});

    const float q2x = (m123x + m23x) / 2.0F;
    const float q2y = (m123y + m23y) / 2.0F;
    points.push_back(TtPoint{roundToInt(q2x), roundToInt(q2y), false});
    points.push_back(TtPoint{roundToInt(ex), roundToInt(ey), true});
}

std::vector<std::uint8_t> buildGlyf(const GlyphEntry& entry) {
    if (entry.glyph.contours.empty()) {
        return {};
    }

    std::vector<std::vector<TtPoint>> ttContours;
    for (const auto& contour : entry.glyph.contours) {
        std::vector<TtPoint> points;
        float currentX = 0.0F;
        float currentY = 0.0F;

        for (const auto& command : contour.commands) {
            if (command.type == 0) {
                if (!points.empty()) {
                    ttContours.push_back(std::move(points));
                    points = {};
                }
                currentX = command.x;
                currentY = command.y;
                points.push_back(TtPoint{roundToInt(currentX), roundToInt(currentY), true});
            } else if (command.type == 1) {
                currentX = command.x;
                currentY = command.y;
                points.push_back(TtPoint{roundToInt(currentX), roundToInt(currentY), true});
            } else if (command.type == 2) {
                cubicToQuadratic(points,
                                 currentX,
                                 currentY,
                                 command.x1,
                                 command.y1,
                                 command.x2,
                                 command.y2,
                                 command.x,
                                 command.y);
                currentX = command.x;
                currentY = command.y;
            }
        }
        if (!points.empty()) {
            ttContours.push_back(std::move(points));
        }
    }

    if (ttContours.empty()) {
        return {};
    }

    int xMin = std::numeric_limits<int>::max();
    int yMin = std::numeric_limits<int>::max();
    int xMax = std::numeric_limits<int>::min();
    int yMax = std::numeric_limits<int>::min();
    for (const auto& contour : ttContours) {
        for (const auto& point : contour) {
            xMin = std::min(xMin, point.x);
            yMin = std::min(yMin, point.y);
            xMax = std::max(xMax, point.x);
            yMax = std::max(yMax, point.y);
        }
    }

    ByteWriter writer;
    writer.writeU16(static_cast<int>(ttContours.size()));
    writer.writeU16(xMin);
    writer.writeU16(yMin);
    writer.writeU16(xMax);
    writer.writeU16(yMax);

    int index = -1;
    for (const auto& contour : ttContours) {
        index += static_cast<int>(contour.size());
        writer.writeU16(index);
    }
    writer.writeU16(0);

    std::vector<int> flags;
    std::vector<int> xCoords;
    std::vector<int> yCoords;
    int previousX = 0;
    int previousY = 0;
    for (const auto& contour : ttContours) {
        for (const auto& point : contour) {
            const int dx = point.x - previousX;
            const int dy = point.y - previousY;
            int flag = point.onCurve ? 1 : 0;

            if (dx == 0) {
                flag |= 0x10;
            } else if (dx >= -255 && dx <= 255) {
                flag |= 0x02;
                if (dx > 0) {
                    flag |= 0x10;
                }
            }

            if (dy == 0) {
                flag |= 0x20;
            } else if (dy >= -255 && dy <= 255) {
                flag |= 0x04;
                if (dy > 0) {
                    flag |= 0x20;
                }
            }

            flags.push_back(flag);
            xCoords.push_back(dx);
            yCoords.push_back(dy);
            previousX = point.x;
            previousY = point.y;
        }
    }

    for (const int flag : flags) {
        writer.writeU8(flag);
    }

    for (std::size_t i = 0; i < xCoords.size(); ++i) {
        const int dx = xCoords[i];
        const int flag = flags[i];
        if ((flag & 0x02) != 0) {
            writer.writeU8(std::abs(dx));
        } else if ((flag & 0x10) == 0) {
            writer.writeU16(dx);
        }
    }

    for (std::size_t i = 0; i < yCoords.size(); ++i) {
        const int dy = yCoords[i];
        const int flag = flags[i];
        if ((flag & 0x04) != 0) {
            writer.writeU8(std::abs(dy));
        } else if ((flag & 0x20) == 0) {
            writer.writeU16(dy);
        }
    }

    return std::move(writer.bytes);
}

int glyphLsb(const Pfr1Font::OutlineGlyph& glyph) {
    int minX = std::numeric_limits<int>::max();
    for (const auto& contour : glyph.contours) {
        for (const auto& command : contour.commands) {
            minX = std::min(minX, roundToInt(command.x));
        }
    }
    return minX == std::numeric_limits<int>::max() ? 0 : minX;
}

std::vector<std::uint8_t> assembleTtf(const std::vector<std::string>& tags,
                                      const std::vector<std::vector<std::uint8_t>>& tables) {
    const int numTables = static_cast<int>(tags.size());
    const int searchRange = highestOneBit(numTables) * 16;
    const int entrySelector = trailingZeroCount(highestOneBit(numTables));
    const int rangeShift = numTables * 16 - searchRange;

    const int headerSize = 12 + numTables * 16;
    std::vector<int> offsets(static_cast<std::size_t>(numTables), 0);
    int currentOffset = headerSize;
    for (int i = 0; i < numTables; ++i) {
        offsets[static_cast<std::size_t>(i)] = currentOffset;
        currentOffset += (static_cast<int>(tables[static_cast<std::size_t>(i)].size()) + 3) & ~3;
    }

    ByteWriter writer;
    writer.writeU32(0x00010000U);
    writer.writeU16(numTables);
    writer.writeU16(searchRange);
    writer.writeU16(entrySelector);
    writer.writeU16(rangeShift);

    for (int i = 0; i < numTables; ++i) {
        const auto& table = tables[static_cast<std::size_t>(i)];
        writer.writeAsciiTag(tags[static_cast<std::size_t>(i)]);
        writer.writeU32(checksum(table));
        writer.writeU32(static_cast<std::uint32_t>(offsets[static_cast<std::size_t>(i)]));
        writer.writeU32(static_cast<std::uint32_t>(table.size()));
    }

    for (const auto& table : tables) {
        writer.writeBytes(table);
        const int pad = (4 - (static_cast<int>(table.size()) % 4)) % 4;
        for (int i = 0; i < pad; ++i) {
            writer.writeU8(0);
        }
    }

    return std::move(writer.bytes);
}

} // namespace

std::vector<std::uint8_t> Pfr1TtfConverter::convert(const Pfr1Font& font,
                                                    const std::string& familyName) {
    std::vector<GlyphEntry> entries;
    entries.push_back(GlyphEntry{0, 0, 0, Pfr1Font::OutlineGlyph{}});

    const int unitsPerEm = font.metrics.outlineResolution > 0 ? font.metrics.outlineResolution : 2048;
    for (const auto& record : font.charRecords) {
        const auto glyph = font.glyphs.find(record.charCode);
        if (glyph == font.glyphs.end()) {
            continue;
        }
        entries.push_back(GlyphEntry{
            record.charCode,
            record.setWidth,
            glyphLsb(glyph->second),
            glyph->second,
        });
    }

    std::map<int, int> cmapEntries;
    for (std::size_t i = 1; i < entries.size(); ++i) {
        cmapEntries[entries[i].charCode] = static_cast<int>(i);
    }

    const auto headTable = buildHead(unitsPerEm, font.metrics);
    const auto hheaTable = buildHhea(font.metrics, entries);
    const auto maxpTable = buildMaxp(static_cast<int>(entries.size()));
    const auto os2Table = buildOs2(font.metrics, unitsPerEm, cmapEntries);
    const auto nameTable = buildName(familyName);
    const auto cmapTable = buildCmap(cmapEntries);
    const auto postTable = buildPost();

    ByteWriter glyfWriter;
    std::vector<int> locaOffsets;
    for (const auto& entry : entries) {
        while (glyfWriter.bytes.size() % 2 != 0) {
            glyfWriter.writeU8(0);
        }
        locaOffsets.push_back(static_cast<int>(glyfWriter.bytes.size() / 2));
        glyfWriter.writeBytes(buildGlyf(entry));
    }
    while (glyfWriter.bytes.size() % 2 != 0) {
        glyfWriter.writeU8(0);
    }
    locaOffsets.push_back(static_cast<int>(glyfWriter.bytes.size() / 2));

    const auto glyfTable = std::move(glyfWriter.bytes);
    const auto locaTable = buildLoca(locaOffsets);
    const auto hmtxTable = buildHmtx(entries);

    return assembleTtf(
        {"cmap", "glyf", "head", "hhea", "hmtx", "loca", "maxp", "name", "OS/2", "post"},
        {cmapTable, glyfTable, headTable, hheaTable, hmtxTable, locaTable, maxpTable, nameTable, os2Table, postTable});
}

} // namespace libreshockwave::font
