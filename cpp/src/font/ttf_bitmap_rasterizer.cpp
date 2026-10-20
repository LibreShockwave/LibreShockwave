#include "libreshockwave/font/ttf_bitmap_rasterizer.hpp"

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
    bool on_curve = false;
};

struct RasterPoint {
    float x = 0.0F;
    float y = 0.0F;
};

struct TtfGlyph {
    int num_contours = 0;
    int x_min = 0;
    int y_min = 0;
    int x_max = 0;
    int y_max = 0;
    std::vector<std::vector<TtfPoint>> contours;
};

struct TtfTable {
    int offset = 0;
    int length = 0;
};

struct TtfData {
    int units_per_em = 0;
    int ascender = 0;
    int descender = 0;
    int index_to_loc_format = 0;
    std::map<int, int> cmap;
    std::vector<int> advance_widths;
    std::vector<int> left_side_bearings;
    std::vector<int> loca_offsets;
    std::vector<std::uint8_t> glyf_table;
    int num_glyphs = 0;
};

int javaRound(float value) {
    return static_cast<int>(std::floor(value + 0.5F));
}

float gridFit(float value) {
    return static_cast<float>(javaRound(value));
}

RasterPoint scalePoint(const TtfPoint& point, float scale, int baseline_y) {
    return RasterPoint{
        gridFit(static_cast<float>(point.x) * scale),
        gridFit(static_cast<float>(baseline_y) - static_cast<float>(point.y) * scale),
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
    const int seg_count = readU16(data, offset + 6) / 2;
    const int end_codes_offset = offset + 14;
    const int start_codes_offset = end_codes_offset + seg_count * 2 + 2;
    const int id_delta_offset = start_codes_offset + seg_count * 2;
    const int id_range_offset_offset = id_delta_offset + seg_count * 2;

    for (int i = 0; i < seg_count; ++i) {
        const int end_code = readU16(data, end_codes_offset + i * 2);
        const int start_code = readU16(data, start_codes_offset + i * 2);
        const int id_delta = readI16(data, id_delta_offset + i * 2);
        const int id_range_offset = readU16(data, id_range_offset_offset + i * 2);
        if (start_code == 0xFFFF) {
            break;
        }
        if (start_code > end_code) {
            continue;
        }

        for (int code = start_code; code <= end_code; ++code) {
            int glyph_index = 0;
            if (id_range_offset == 0) {
                glyph_index = (code + id_delta) & 0xFFFF;
            } else {
                const int range_addr = id_range_offset_offset + i * 2 + id_range_offset + (code - start_code) * 2;
                if (range_addr + 2 > static_cast<int>(data.size())) {
                    continue;
                }
                glyph_index = readU16(data, range_addr);
                if (glyph_index != 0) {
                    glyph_index = (glyph_index + id_delta) & 0xFFFF;
                }
            }
            if (glyph_index != 0 && glyph_index < ttf.num_glyphs) {
                ttf.cmap[code] = glyph_index;
            }
        }
    }
}

void parseCmap(const std::vector<std::uint8_t>& data, int cmap_offset, TtfData& ttf) {
    const int num_subtables = readU16(data, cmap_offset + 2);
    for (int i = 0; i < num_subtables; ++i) {
        const int base = cmap_offset + 4 + i * 8;
        if (base + 8 > static_cast<int>(data.size())) {
            break;
        }
        const int platform_id = readU16(data, base);
        const int encoding_id = readU16(data, base + 2);
        const int subtable_offset = readI32(data, base + 4);
        const int absolute_offset = cmap_offset + subtable_offset;
        if (platform_id == 3 && encoding_id == 1 && readU16(data, absolute_offset) == 4) {
            parseCmapFormat4(data, absolute_offset, ttf);
            return;
        }
    }

    if (num_subtables > 0) {
        const int subtable_offset = readI32(data, cmap_offset + 8);
        const int absolute_offset = cmap_offset + subtable_offset;
        if (readU16(data, absolute_offset) == 4) {
            parseCmapFormat4(data, absolute_offset, ttf);
        }
    }
}

bool tableAvailable(const std::vector<std::uint8_t>& data, const TtfTable& table, int min_length) {
    return table.offset >= 0 &&
           table.length >= min_length &&
           table.offset + min_length <= static_cast<int>(data.size());
}

bool parseTtf(const std::vector<std::uint8_t>& data, TtfData& ttf) {
    if (data.size() < 12) {
        return false;
    }

    const int num_tables = readU16(data, 4);
    std::unordered_map<std::string, TtfTable> tables;
    for (int i = 0; i < num_tables; ++i) {
        const int base = 12 + i * 16;
        if (base + 16 > static_cast<int>(data.size())) {
            break;
        }
        const std::string tag(reinterpret_cast<const char*>(data.data() + base), 4);
        tables[tag] = TtfTable{readI32(data, base + 8), readI32(data, base + 12)};
    }

    const auto head_it = tables.find("head");
    const auto hhea_it = tables.find("hhea");
    const auto maxp_it = tables.find("maxp");
    const auto hmtx_it = tables.find("hmtx");
    const auto cmap_it = tables.find("cmap");
    const auto loca_it = tables.find("loca");
    const auto glyf_it = tables.find("glyf");
    if (head_it == tables.end() || hhea_it == tables.end() || maxp_it == tables.end() ||
        hmtx_it == tables.end() || cmap_it == tables.end() || loca_it == tables.end() ||
        glyf_it == tables.end()) {
        return false;
    }
    if (!tableAvailable(data, head_it->second, 54) ||
        !tableAvailable(data, hhea_it->second, 36) ||
        !tableAvailable(data, maxp_it->second, 6)) {
        return false;
    }

    ttf.units_per_em = readU16(data, head_it->second.offset + 18);
    ttf.index_to_loc_format = readI16(data, head_it->second.offset + 50);
    ttf.ascender = readI16(data, hhea_it->second.offset + 4);
    ttf.descender = readI16(data, hhea_it->second.offset + 6);
    const int num_h_metrics = readU16(data, hhea_it->second.offset + 34);
    ttf.num_glyphs = readU16(data, maxp_it->second.offset + 4);
    if (ttf.units_per_em <= 0 || ttf.num_glyphs <= 0) {
        return false;
    }

    ttf.advance_widths.assign(static_cast<std::size_t>(ttf.num_glyphs), 0);
    ttf.left_side_bearings.assign(static_cast<std::size_t>(ttf.num_glyphs), 0);
    int last_advance_width = 0;
    for (int i = 0; i < ttf.num_glyphs; ++i) {
        if (i < num_h_metrics) {
            const int offset = hmtx_it->second.offset + i * 4;
            if (offset + 4 > static_cast<int>(data.size())) {
                break;
            }
            ttf.advance_widths[static_cast<std::size_t>(i)] = readU16(data, offset);
            ttf.left_side_bearings[static_cast<std::size_t>(i)] = readI16(data, offset + 2);
            last_advance_width = ttf.advance_widths[static_cast<std::size_t>(i)];
        } else {
            ttf.advance_widths[static_cast<std::size_t>(i)] = last_advance_width;
            const int offset = hmtx_it->second.offset + num_h_metrics * 4 + (i - num_h_metrics) * 2;
            if (offset + 2 <= static_cast<int>(data.size())) {
                ttf.left_side_bearings[static_cast<std::size_t>(i)] = readI16(data, offset);
            }
        }
    }

    parseCmap(data, cmap_it->second.offset, ttf);

    ttf.loca_offsets.assign(static_cast<std::size_t>(ttf.num_glyphs + 1), 0);
    for (int i = 0; i <= ttf.num_glyphs; ++i) {
        if (ttf.index_to_loc_format == 0) {
            const int offset = loca_it->second.offset + i * 2;
            if (offset + 2 <= static_cast<int>(data.size())) {
                ttf.loca_offsets[static_cast<std::size_t>(i)] = readU16(data, offset) * 2;
            }
        } else {
            const int offset = loca_it->second.offset + i * 4;
            if (offset + 4 <= static_cast<int>(data.size())) {
                ttf.loca_offsets[static_cast<std::size_t>(i)] = readI32(data, offset);
            }
        }
    }

    const int glyf_offset = glyf_it->second.offset;
    const int glyf_length = glyf_it->second.length;
    if (glyf_offset < 0 || glyf_length <= 0 || glyf_offset >= static_cast<int>(data.size())) {
        return false;
    }
    const int copy_length = std::min(glyf_length, static_cast<int>(data.size()) - glyf_offset);
    ttf.glyf_table.assign(data.begin() + glyf_offset, data.begin() + glyf_offset + copy_length);
    return true;
}

std::unique_ptr<TtfGlyph> parseGlyph(const TtfData& ttf, int glyph_index) {
    if (glyph_index < 0 || glyph_index >= ttf.num_glyphs) {
        return nullptr;
    }
    const int offset = ttf.loca_offsets[static_cast<std::size_t>(glyph_index)];
    const int next_offset = ttf.loca_offsets[static_cast<std::size_t>(glyph_index + 1)];
    if (offset == next_offset || offset < 0 || offset + 10 > static_cast<int>(ttf.glyf_table.size())) {
        return nullptr;
    }

    auto glyph = std::make_unique<TtfGlyph>();
    glyph->num_contours = readI16(ttf.glyf_table, offset);
    glyph->x_min = readI16(ttf.glyf_table, offset + 2);
    glyph->y_min = readI16(ttf.glyf_table, offset + 4);
    glyph->x_max = readI16(ttf.glyf_table, offset + 6);
    glyph->y_max = readI16(ttf.glyf_table, offset + 8);
    if (glyph->num_contours < 0) {
        return nullptr;
    }
    if (glyph->num_contours == 0) {
        return glyph;
    }

    int pos = offset + 10;
    std::vector<int> end_points(static_cast<std::size_t>(glyph->num_contours), 0);
    for (int i = 0; i < glyph->num_contours; ++i) {
        if (pos + 2 > static_cast<int>(ttf.glyf_table.size())) {
            return nullptr;
        }
        end_points[static_cast<std::size_t>(i)] = readU16(ttf.glyf_table, pos);
        pos += 2;
    }

    const int num_points = end_points.empty() ? 0 : end_points.back() + 1;
    if (num_points <= 0) {
        return glyph;
    }
    if (pos + 2 > static_cast<int>(ttf.glyf_table.size())) {
        return nullptr;
    }
    const int instruction_length = readU16(ttf.glyf_table, pos);
    pos += 2 + instruction_length;
    if (pos > static_cast<int>(ttf.glyf_table.size())) {
        return nullptr;
    }

    std::vector<int> flags(static_cast<std::size_t>(num_points), 0);
    for (int i = 0; i < num_points; ++i) {
        if (pos >= static_cast<int>(ttf.glyf_table.size())) {
            return nullptr;
        }
        flags[static_cast<std::size_t>(i)] = ttf.glyf_table[static_cast<std::size_t>(pos++)] & 0xFF;
        if ((flags[static_cast<std::size_t>(i)] & 0x08) != 0) {
            if (pos >= static_cast<int>(ttf.glyf_table.size())) {
                return nullptr;
            }
            const int repeat_count = ttf.glyf_table[static_cast<std::size_t>(pos++)] & 0xFF;
            const int repeated_flag = flags[static_cast<std::size_t>(i)];
            for (int r = 0; r < repeat_count && i + 1 < num_points; ++r) {
                flags[static_cast<std::size_t>(++i)] = repeated_flag;
            }
        }
    }

    std::vector<int> x_coords(static_cast<std::size_t>(num_points), 0);
    int x = 0;
    for (int i = 0; i < num_points; ++i) {
        const int flag = flags[static_cast<std::size_t>(i)];
        if ((flag & 0x02) != 0) {
            if (pos >= static_cast<int>(ttf.glyf_table.size())) {
                return nullptr;
            }
            const int dx = ttf.glyf_table[static_cast<std::size_t>(pos++)] & 0xFF;
            x += (flag & 0x10) != 0 ? dx : -dx;
        } else if ((flag & 0x10) == 0) {
            if (pos + 2 > static_cast<int>(ttf.glyf_table.size())) {
                return nullptr;
            }
            x += readI16(ttf.glyf_table, pos);
            pos += 2;
        }
        x_coords[static_cast<std::size_t>(i)] = x;
    }

    std::vector<int> y_coords(static_cast<std::size_t>(num_points), 0);
    int y = 0;
    for (int i = 0; i < num_points; ++i) {
        const int flag = flags[static_cast<std::size_t>(i)];
        if ((flag & 0x04) != 0) {
            if (pos >= static_cast<int>(ttf.glyf_table.size())) {
                return nullptr;
            }
            const int dy = ttf.glyf_table[static_cast<std::size_t>(pos++)] & 0xFF;
            y += (flag & 0x20) != 0 ? dy : -dy;
        } else if ((flag & 0x20) == 0) {
            if (pos + 2 > static_cast<int>(ttf.glyf_table.size())) {
                return nullptr;
            }
            y += readI16(ttf.glyf_table, pos);
            pos += 2;
        }
        y_coords[static_cast<std::size_t>(i)] = y;
    }

    int point_index = 0;
    for (int contour_index = 0; contour_index < glyph->num_contours; ++contour_index) {
        const int end_point = end_points[static_cast<std::size_t>(contour_index)];
        std::vector<TtfPoint> contour;
        for (int i = point_index; i <= end_point && i < num_points; ++i) {
            contour.push_back(TtfPoint{
                x_coords[static_cast<std::size_t>(i)],
                y_coords[static_cast<std::size_t>(i)],
                (flags[static_cast<std::size_t>(i)] & 0x01) != 0,
            });
        }
        if (contour.size() >= 2) {
            glyph->contours.push_back(std::move(contour));
        }
        point_index = end_point + 1;
    }
    return glyph;
}

void subdivideQuadratic(std::vector<RasterPoint>& result,
                        float x0,
                        float y0,
                        float cpx,
                        float cpy,
                        float end_x,
                        float end_y,
                        int levels) {
    if (levels <= 0) {
        result.push_back(RasterPoint{end_x, end_y});
        return;
    }
    const float mid_x = (x0 + 2.0F * cpx + end_x) / 4.0F;
    const float mid_y = (y0 + 2.0F * cpy + end_y) / 4.0F;
    const float cp1x = (x0 + cpx) / 2.0F;
    const float cp1y = (y0 + cpy) / 2.0F;
    const float cp2x = (cpx + end_x) / 2.0F;
    const float cp2y = (cpy + end_y) / 2.0F;
    subdivideQuadratic(result, x0, y0, cp1x, cp1y, mid_x, mid_y, levels - 1);
    subdivideQuadratic(result, mid_x, mid_y, cp2x, cp2y, end_x, end_y, levels - 1);
}

std::vector<RasterPoint> flattenTtfContour(const std::vector<TtfPoint>& contour,
                                           float scale,
                                           int baseline_y) {
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
        if (!current.on_curve && !next.on_curve) {
            expanded.push_back(TtfPoint{(current.x + next.x) / 2, (current.y + next.y) / 2, true});
        }
    }

    int start_index = -1;
    for (int i = 0; i < static_cast<int>(expanded.size()); ++i) {
        if (expanded[static_cast<std::size_t>(i)].on_curve) {
            start_index = i;
            break;
        }
    }
    if (start_index < 0) {
        return result;
    }

    const int size = static_cast<int>(expanded.size());
    const auto& start = expanded[static_cast<std::size_t>(start_index)];
    result.push_back(scalePoint(start, scale, baseline_y));

    int index = (start_index + 1) % size;
    int count = 0;
    while (count < size) {
        const auto& point = expanded[static_cast<std::size_t>(index)];
        if (point.on_curve) {
            result.push_back(scalePoint(point, scale, baseline_y));
        } else {
            const auto& next = expanded[static_cast<std::size_t>((index + 1) % size)];
            if (!next.on_curve || result.empty()) {
                return {};
            }
            const auto control = scalePoint(point, scale, baseline_y);
            const auto end = scalePoint(next, scale, baseline_y);
            const auto& last = result.back();
            subdivideQuadratic(result, last.x, last.y, control.x, control.y, end.x, end.y, 3);

            index = (index + 1) % size;
            ++count;
        }

        index = (index + 1) % size;
        ++count;
        if (index == start_index) {
            break;
        }
    }
    return result;
}

bool rasterizeContours(const std::vector<std::vector<TtfPoint>>& contours,
                       std::vector<std::uint32_t>& argb,
                       int buffer_width,
                       int buffer_height,
                       int cell_x,
                       int cell_y,
                       int cell_width,
                       int cell_height,
                       float scale,
                       int baseline_y) {
    std::vector<std::vector<RasterPoint>> polygons;
    for (const auto& contour : contours) {
        auto points = flattenTtfContour(contour, scale, baseline_y);
        if (points.size() >= 3) {
            polygons.push_back(std::move(points));
        }
    }

    if (polygons.empty()) {
        return false;
    }

    bool wrote_pixel = false;
    for (int y = 0; y < cell_height; ++y) {
        const float scan_y = static_cast<float>(y) + 0.5F;
        std::vector<std::pair<float, int>> crossings;
        for (const auto& polygon : polygons) {
            const int n = static_cast<int>(polygon.size());
            for (int i = 0; i < n; ++i) {
                const auto& p0 = polygon[static_cast<std::size_t>(i)];
                const auto& p1 = polygon[static_cast<std::size_t>((i + 1) % n)];
                if (std::abs(p0.y - p1.y) < 0.001F) {
                    continue;
                }
                if ((p0.y <= scan_y && p1.y > scan_y) || (p1.y <= scan_y && p0.y > scan_y)) {
                    const float t = (scan_y - p0.y) / (p1.y - p0.y);
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
            const int x_start = std::max(0, std::min(cell_width, javaRound(crossings[i].first)));
            const int x_end = std::max(0, std::min(cell_width, javaRound(crossings[i + 1].first)));
            for (int bx = x_start; bx < x_end; ++bx) {
                const int px = cell_x + bx;
                const int py = cell_y + y;
                if (px < 0 || px >= buffer_width || py < 0 || py >= buffer_height) {
                    continue;
                }
                const int index = py * buffer_width + px;
                if (index >= 0 && index < static_cast<int>(argb.size())) {
                    argb[static_cast<std::size_t>(index)] = 0xFF000000U;
                    wrote_pixel = true;
                }
            }
        }
    }
    return wrote_pixel;
}

std::shared_ptr<BitmapFont> buildBitmapFont(const TtfData& ttf,
                                            int target_size,
                                            const std::string& font_name) {
    const float scale = static_cast<float>(target_size) / static_cast<float>(ttf.units_per_em);
    int max_advance_px = 0;
    int max_glyph_width_px = 0;
    for (const auto& [_, glyph_index] : ttf.cmap) {
        const int advance_px = javaRound(static_cast<float>(ttf.advance_widths[static_cast<std::size_t>(glyph_index)]) *
                                        scale);
        max_advance_px = std::max(max_advance_px, advance_px);
        const auto glyph = parseGlyph(ttf, glyph_index);
        if (glyph != nullptr) {
            const int glyph_width_px =
                std::max(1, javaRound(static_cast<float>(glyph->x_max - glyph->x_min) * scale) + 1);
            max_glyph_width_px = std::max(max_glyph_width_px, glyph_width_px);
        }
    }

    const int ascent_px = javaRound(std::abs(static_cast<float>(ttf.ascender)) * scale);
    const int descent_px = javaRound(std::abs(static_cast<float>(ttf.descender)) * scale);
    const int metrics_line_height = ascent_px + descent_px;
    const int baseline_px = std::max(0, ascent_px - 1);
    const int cell_height = metrics_line_height + 1;
    const int cell_width = std::max(1, std::max(max_advance_px, max_glyph_width_px));
    const int bitmap_width = cell_width * BitmapFont::GRID_COLUMNS;
    const int bitmap_height = cell_height * BitmapFont::GRID_ROWS;

    std::vector<std::uint32_t> argb(static_cast<std::size_t>(bitmap_width * bitmap_height), 0);
    std::vector<int> char_widths(BitmapFont::NUM_CHARS, cell_width);
    std::vector<int> char_offsets_x(BitmapFont::NUM_CHARS, 0);
    BitmapFont::GlyphMap overflow_glyphs;
    BitmapFont::MetricMap overflow_widths;
    BitmapFont::MetricMap overflow_offsets_x;
    bool has_rasterized_ink = false;

    for (const auto& [char_code, glyph_index] : ttf.cmap) {
        const auto glyph_index_size = static_cast<std::size_t>(glyph_index);
        const int advance_px = std::max(1, javaRound(static_cast<float>(ttf.advance_widths[glyph_index_size]) * scale));
        const auto glyph = parseGlyph(ttf, glyph_index);
        int glyph_offset_x = 0;
        int glyph_draw_offset_x = 0;
        if (glyph != nullptr) {
            glyph_draw_offset_x = javaRound(static_cast<float>(ttf.left_side_bearings[glyph_index_size]) * scale);
            glyph_offset_x = -javaRound(static_cast<float>(glyph->x_min) * scale);
        }

        if (char_code >= 0 && char_code < BitmapFont::NUM_CHARS) {
            char_widths[static_cast<std::size_t>(char_code)] = advance_px;
            char_offsets_x[static_cast<std::size_t>(char_code)] = glyph_draw_offset_x;
            if (glyph != nullptr && !glyph->contours.empty()) {
                const int column = char_code % BitmapFont::GRID_COLUMNS;
                const int row = char_code / BitmapFont::GRID_COLUMNS;
                has_rasterized_ink = rasterizeContours(glyph->contours,
                                                      argb,
                                                      bitmap_width,
                                                      bitmap_height,
                                                      column * cell_width + glyph_offset_x,
                                                      row * cell_height,
                                                      cell_width,
                                                      cell_height,
                                                      scale,
                                                      baseline_px) ||
                                   has_rasterized_ink;
            }
        } else {
            overflow_widths[char_code] = advance_px;
            overflow_offsets_x[char_code] = glyph_draw_offset_x;
            if (glyph != nullptr && !glyph->contours.empty()) {
                std::vector<std::uint32_t> cell_buffer(static_cast<std::size_t>(cell_width * cell_height), 0);
                if (rasterizeContours(glyph->contours,
                                      cell_buffer,
                                      cell_width,
                                      cell_height,
                                      glyph_offset_x,
                                      0,
                                      cell_width,
                                      cell_height,
                                      scale,
                                      baseline_px)) {
                    has_rasterized_ink = true;
                    overflow_glyphs[char_code] = std::move(cell_buffer);
                }
            }
        }
    }

    if (!has_rasterized_ink) {
        return nullptr;
    }

    if (!ttf.cmap.contains(' ')) {
        char_widths[static_cast<std::size_t>(' ')] =
            std::max(1, javaRound(static_cast<float>(ttf.units_per_em) * scale / 4.0F));
    }

    return BitmapFont::create(std::move(argb),
                              bitmap_width,
                              bitmap_height,
                              cell_width,
                              cell_height,
                              std::move(char_widths),
                              std::move(char_offsets_x),
                              font_name,
                              target_size,
                              ascent_px,
                              metrics_line_height,
                              std::move(overflow_glyphs),
                              std::move(overflow_widths),
                              std::move(overflow_offsets_x));
}

} // namespace

std::shared_ptr<BitmapFont> TtfBitmapRasterizer::rasterize(const std::vector<std::uint8_t>& ttf_bytes,
                                                          int target_size,
                                                          const std::string& font_name) {
    if (ttf_bytes.size() < 12 || target_size <= 0) {
        return nullptr;
    }

    try {
        TtfData ttf;
        if (!parseTtf(ttf_bytes, ttf)) {
            return nullptr;
        }
        return buildBitmapFont(ttf, target_size, font_name);
    } catch (const std::exception&) {
        return nullptr;
    }
}

} // namespace libreshockwave::font
