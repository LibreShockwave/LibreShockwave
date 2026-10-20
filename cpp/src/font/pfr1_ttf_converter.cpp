#include "libreshockwave/font/pfr1_ttf_converter.hpp"

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
    bool on_curve = true;
};

struct GlyphEntry {
    int char_code = 0;
    int advance_width = 0;
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

std::vector<std::uint8_t> buildHead(int units_per_em, const Pfr1Font::FontMetrics& metrics) {
    ByteWriter writer;
    writer.writeU32(0x00010000U);
    writer.writeU32(0x00005000U);
    writer.writeU32(0);
    writer.writeU32(0x5F0F3CF5U);
    writer.writeU16(0x000B);
    writer.writeU16(units_per_em);
    writer.writeI64Zero();
    writer.writeI64Zero();
    writer.writeU16(metrics.x_min);
    writer.writeU16(metrics.y_min);
    writer.writeU16(metrics.x_max);
    writer.writeU16(metrics.y_max);
    writer.writeU16(0);
    writer.writeU16(8);
    writer.writeU16(2);
    writer.writeU16(1);
    writer.writeU16(0);
    return std::move(writer.bytes);
}

std::vector<std::uint8_t> buildHhea(const Pfr1Font::FontMetrics& metrics,
                                    const std::vector<GlyphEntry>& entries) {
    int max_advance_width = 0;
    for (const auto& entry : entries) {
        max_advance_width = std::max(max_advance_width, entry.advance_width);
    }

    ByteWriter writer;
    writer.writeU32(0x00010000U);
    writer.writeU16(metrics.ascender);
    writer.writeU16(metrics.descender);
    writer.writeU16(0);
    writer.writeU16(max_advance_width);
    writer.writeU16(metrics.x_min);
    writer.writeU16(metrics.x_min);
    writer.writeU16(metrics.x_max);
    writer.writeU16(1);
    writer.writeU16(0);
    writer.writeU16(0);
    writer.writeI64Zero();
    writer.writeU16(0);
    writer.writeU16(static_cast<int>(entries.size()));
    return std::move(writer.bytes);
}

std::vector<std::uint8_t> buildMaxp(int num_glyphs) {
    ByteWriter writer;
    writer.writeU32(0x00010000U);
    writer.writeU16(num_glyphs);
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
                                   int units_per_em,
                                   const std::map<int, int>& cmap_entries) {
    int first_char = 0x0020;
    int last_char = 0x00FF;
    if (!cmap_entries.empty()) {
        first_char = cmap_entries.begin()->first;
        last_char = cmap_entries.rbegin()->first;
    }

    ByteWriter writer;
    writer.writeU16(4);
    writer.writeU16(units_per_em / 2);
    writer.writeU16(400);
    writer.writeU16(5);
    writer.writeU16(0);
    writer.writeU16(units_per_em / 10);
    writer.writeU16(units_per_em / 10);
    writer.writeU16(0);
    writer.writeU16(units_per_em / 5);
    writer.writeU16(units_per_em / 10);
    writer.writeU16(units_per_em / 10);
    writer.writeU16(0);
    writer.writeU16(units_per_em / 3);
    writer.writeU16(metrics.std_vw > 0 ? metrics.std_vw : units_per_em / 20);
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
    writer.writeU16(first_char);
    writer.writeU16(std::min(last_char, 0xFFFF));
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

std::vector<std::uint8_t> buildName(const std::string& family_name) {
    const std::vector<std::string> names{
        "",
        family_name,
        "Regular",
        family_name + "-Regular",
        family_name,
        "Version 1.0",
        removeSpaces(family_name),
    };

    std::vector<std::vector<std::uint8_t>> encoded;
    encoded.reserve(names.size());
    for (const auto& name : names) {
        encoded.push_back(utf16beAscii(name));
    }

    const int count = static_cast<int>(encoded.size());
    const int storage_offset = 6 + count * 12;
    ByteWriter writer;
    writer.writeU16(0);
    writer.writeU16(count);
    writer.writeU16(storage_offset);

    int string_offset = 0;
    for (int i = 0; i < count; ++i) {
        writer.writeU16(3);
        writer.writeU16(1);
        writer.writeU16(0x0409);
        writer.writeU16(i);
        writer.writeU16(static_cast<int>(encoded[static_cast<std::size_t>(i)].size()));
        writer.writeU16(string_offset);
        string_offset += static_cast<int>(encoded[static_cast<std::size_t>(i)].size());
    }

    for (const auto& text : encoded) {
        writer.writeBytes(text);
    }
    return std::move(writer.bytes);
}

std::vector<std::uint8_t> buildCmap(const std::map<int, int>& char_to_glyph) {
    std::vector<std::pair<int, int>> segments;
    if (!char_to_glyph.empty()) {
        int start = char_to_glyph.begin()->first;
        int end = start;
        for (auto it = std::next(char_to_glyph.begin()); it != char_to_glyph.end(); ++it) {
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

    const int seg_count = static_cast<int>(segments.size());
    const int search_range = highestOneBit(seg_count) * 2;
    const int entry_selector = trailingZeroCount(highestOneBit(seg_count));
    const int range_shift = seg_count * 2 - search_range;

    ByteWriter subtable;
    for (const auto& segment : segments) {
        subtable.writeU16(segment.second);
    }
    subtable.writeU16(0);
    for (const auto& segment : segments) {
        subtable.writeU16(segment.first);
    }

    std::vector<int> glyph_id_array_entries;
    std::vector<int> id_range_offsets(static_cast<std::size_t>(seg_count), 0);
    for (int i = 0; i < seg_count; ++i) {
        const auto& segment = segments[static_cast<std::size_t>(i)];
        if (segment.first == 0xFFFF) {
            id_range_offsets[static_cast<std::size_t>(i)] = 0;
        } else {
            const int array_start_index = static_cast<int>(glyph_id_array_entries.size());
            const int remaining_offsets = seg_count - i;
            id_range_offsets[static_cast<std::size_t>(i)] = (remaining_offsets + array_start_index) * 2;
            for (int code = segment.first; code <= segment.second; ++code) {
                const auto mapped = char_to_glyph.find(code);
                glyph_id_array_entries.push_back(mapped != char_to_glyph.end() ? mapped->second : 0);
            }
        }
    }

    for (int i = 0; i < seg_count; ++i) {
        subtable.writeU16(segments[static_cast<std::size_t>(i)].first == 0xFFFF ? 1 : 0);
    }
    for (const int offset : id_range_offsets) {
        subtable.writeU16(offset);
    }
    for (const int glyph_id : glyph_id_array_entries) {
        subtable.writeU16(glyph_id);
    }

    const auto subtable_data = std::move(subtable.bytes);
    const int subtable_length = 14 + static_cast<int>(subtable_data.size());

    ByteWriter writer;
    writer.writeU16(0);
    writer.writeU16(1);
    writer.writeU16(3);
    writer.writeU16(1);
    writer.writeU32(12);
    writer.writeU16(4);
    writer.writeU16(subtable_length);
    writer.writeU16(0);
    writer.writeU16(seg_count * 2);
    writer.writeU16(search_range);
    writer.writeU16(entry_selector);
    writer.writeU16(range_shift);
    writer.writeBytes(subtable_data);
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
        writer.writeU16(entry.advance_width);
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
    const float mid_x = (m012x + m123x) / 2.0F;
    const float mid_y = (m012y + m123y) / 2.0F;

    const float q1x = (m01x + m012x) / 2.0F;
    const float q1y = (m01y + m012y) / 2.0F;
    points.push_back(TtPoint{roundToInt(q1x), roundToInt(q1y), false});
    points.push_back(TtPoint{roundToInt(mid_x), roundToInt(mid_y), true});

    const float q2x = (m123x + m23x) / 2.0F;
    const float q2y = (m123y + m23y) / 2.0F;
    points.push_back(TtPoint{roundToInt(q2x), roundToInt(q2y), false});
    points.push_back(TtPoint{roundToInt(ex), roundToInt(ey), true});
}

std::vector<std::uint8_t> buildGlyf(const GlyphEntry& entry) {
    if (entry.glyph.contours.empty()) {
        return {};
    }

    std::vector<std::vector<TtPoint>> tt_contours;
    for (const auto& contour : entry.glyph.contours) {
        std::vector<TtPoint> points;
        float current_x = 0.0F;
        float current_y = 0.0F;

        for (const auto& command : contour.commands) {
            if (command.type == 0) {
                if (!points.empty()) {
                    tt_contours.push_back(std::move(points));
                    points = {};
                }
                current_x = command.x;
                current_y = command.y;
                points.push_back(TtPoint{roundToInt(current_x), roundToInt(current_y), true});
            } else if (command.type == 1) {
                current_x = command.x;
                current_y = command.y;
                points.push_back(TtPoint{roundToInt(current_x), roundToInt(current_y), true});
            } else if (command.type == 2) {
                cubicToQuadratic(points,
                                 current_x,
                                 current_y,
                                 command.x1,
                                 command.y1,
                                 command.x2,
                                 command.y2,
                                 command.x,
                                 command.y);
                current_x = command.x;
                current_y = command.y;
            }
        }
        if (!points.empty()) {
            tt_contours.push_back(std::move(points));
        }
    }

    if (tt_contours.empty()) {
        return {};
    }

    int x_min = std::numeric_limits<int>::max();
    int y_min = std::numeric_limits<int>::max();
    int x_max = std::numeric_limits<int>::min();
    int y_max = std::numeric_limits<int>::min();
    for (const auto& contour : tt_contours) {
        for (const auto& point : contour) {
            x_min = std::min(x_min, point.x);
            y_min = std::min(y_min, point.y);
            x_max = std::max(x_max, point.x);
            y_max = std::max(y_max, point.y);
        }
    }

    ByteWriter writer;
    writer.writeU16(static_cast<int>(tt_contours.size()));
    writer.writeU16(x_min);
    writer.writeU16(y_min);
    writer.writeU16(x_max);
    writer.writeU16(y_max);

    int index = -1;
    for (const auto& contour : tt_contours) {
        index += static_cast<int>(contour.size());
        writer.writeU16(index);
    }
    writer.writeU16(0);

    std::vector<int> flags;
    std::vector<int> x_coords;
    std::vector<int> y_coords;
    int previous_x = 0;
    int previous_y = 0;
    for (const auto& contour : tt_contours) {
        for (const auto& point : contour) {
            const int dx = point.x - previous_x;
            const int dy = point.y - previous_y;
            int flag = point.on_curve ? 1 : 0;

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
            x_coords.push_back(dx);
            y_coords.push_back(dy);
            previous_x = point.x;
            previous_y = point.y;
        }
    }

    for (const int flag : flags) {
        writer.writeU8(flag);
    }

    for (std::size_t i = 0; i < x_coords.size(); ++i) {
        const int dx = x_coords[i];
        const int flag = flags[i];
        if ((flag & 0x02) != 0) {
            writer.writeU8(std::abs(dx));
        } else if ((flag & 0x10) == 0) {
            writer.writeU16(dx);
        }
    }

    for (std::size_t i = 0; i < y_coords.size(); ++i) {
        const int dy = y_coords[i];
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
    int min_x = std::numeric_limits<int>::max();
    for (const auto& contour : glyph.contours) {
        for (const auto& command : contour.commands) {
            min_x = std::min(min_x, roundToInt(command.x));
        }
    }
    return min_x == std::numeric_limits<int>::max() ? 0 : min_x;
}

std::vector<std::uint8_t> assembleTtf(const std::vector<std::string>& tags,
                                      const std::vector<std::vector<std::uint8_t>>& tables) {
    const int num_tables = static_cast<int>(tags.size());
    const int search_range = highestOneBit(num_tables) * 16;
    const int entry_selector = trailingZeroCount(highestOneBit(num_tables));
    const int range_shift = num_tables * 16 - search_range;

    const int header_size = 12 + num_tables * 16;
    std::vector<int> offsets(static_cast<std::size_t>(num_tables), 0);
    int current_offset = header_size;
    for (int i = 0; i < num_tables; ++i) {
        offsets[static_cast<std::size_t>(i)] = current_offset;
        current_offset += (static_cast<int>(tables[static_cast<std::size_t>(i)].size()) + 3) & ~3;
    }

    ByteWriter writer;
    writer.writeU32(0x00010000U);
    writer.writeU16(num_tables);
    writer.writeU16(search_range);
    writer.writeU16(entry_selector);
    writer.writeU16(range_shift);

    for (int i = 0; i < num_tables; ++i) {
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
                                                    const std::string& family_name) {
    std::vector<GlyphEntry> entries;
    entries.push_back(GlyphEntry{0, 0, 0, Pfr1Font::OutlineGlyph{}});

    const int units_per_em = font.metrics.outline_resolution > 0 ? font.metrics.outline_resolution : 2048;
    for (const auto& record : font.char_records) {
        const auto glyph = font.glyphs.find(record.char_code);
        if (glyph == font.glyphs.end()) {
            continue;
        }
        entries.push_back(GlyphEntry{
            record.char_code,
            record.set_width,
            glyphLsb(glyph->second),
            glyph->second,
        });
    }

    std::map<int, int> cmap_entries;
    for (std::size_t i = 1; i < entries.size(); ++i) {
        cmap_entries[entries[i].char_code] = static_cast<int>(i);
    }

    const auto head_table = buildHead(units_per_em, font.metrics);
    const auto hhea_table = buildHhea(font.metrics, entries);
    const auto maxp_table = buildMaxp(static_cast<int>(entries.size()));
    const auto os2_table = buildOs2(font.metrics, units_per_em, cmap_entries);
    const auto name_table = buildName(family_name);
    const auto cmap_table = buildCmap(cmap_entries);
    const auto post_table = buildPost();

    ByteWriter glyf_writer;
    std::vector<int> loca_offsets;
    for (const auto& entry : entries) {
        while (glyf_writer.bytes.size() % 2 != 0) {
            glyf_writer.writeU8(0);
        }
        loca_offsets.push_back(static_cast<int>(glyf_writer.bytes.size() / 2));
        glyf_writer.writeBytes(buildGlyf(entry));
    }
    while (glyf_writer.bytes.size() % 2 != 0) {
        glyf_writer.writeU8(0);
    }
    loca_offsets.push_back(static_cast<int>(glyf_writer.bytes.size() / 2));

    const auto glyf_table = std::move(glyf_writer.bytes);
    const auto loca_table = buildLoca(loca_offsets);
    const auto hmtx_table = buildHmtx(entries);

    return assembleTtf(
        {"cmap", "glyf", "head", "hhea", "hmtx", "loca", "maxp", "name", "OS/2", "post"},
        {cmap_table, glyf_table, head_table, hhea_table, hmtx_table, loca_table, maxp_table, name_table, os2_table, post_table});
}

} // namespace libreshockwave::font
