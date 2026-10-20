#include "libreshockwave/font/bitmap_font.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "libreshockwave/font/pfr1_font.hpp"

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
                int bitmap_width,
                int cx,
                int cy,
                int cw,
                int ch) {
    for (int y = 0; y < ch; ++y) {
        for (int x = 0; x < cw; ++x) {
            const int idx = (cy + y) * bitmap_width + (cx + x);
            if (idx >= 0 && idx < static_cast<int>(argb.size()) &&
                ((argb[static_cast<std::size_t>(idx)] >> 24U) & 0xFFU) > 0) {
                return true;
            }
        }
    }
    return false;
}

void copyCell(std::vector<std::uint32_t>& argb,
              int bitmap_width,
              int src_idx,
              int dst_idx,
              int cell_width,
              int cell_height) {
    const int src_col = src_idx % BitmapFont::GRID_COLUMNS;
    const int src_row = src_idx / BitmapFont::GRID_COLUMNS;
    const int dst_col = dst_idx % BitmapFont::GRID_COLUMNS;
    const int dst_row = dst_idx / BitmapFont::GRID_COLUMNS;
    const int src_x = src_col * cell_width;
    const int src_y = src_row * cell_height;
    const int dst_x = dst_col * cell_width;
    const int dst_y = dst_row * cell_height;

    for (int y = 0; y < cell_height; ++y) {
        for (int x = 0; x < cell_width; ++x) {
            const int si = (src_y + y) * bitmap_width + (src_x + x);
            const int di = (dst_y + y) * bitmap_width + (dst_x + x);
            if (si >= 0 && si < static_cast<int>(argb.size()) &&
                di >= 0 && di < static_cast<int>(argb.size())) {
                argb[static_cast<std::size_t>(di)] = argb[static_cast<std::size_t>(si)];
            }
        }
    }
}

float scaledDistance(Point lhs, Point rhs, float scale_x, float scale_y) {
    const float dx = (rhs.x - lhs.x) * scale_x;
    const float dy = (rhs.y - lhs.y) * scale_y;
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
                          float scale_x,
                          float scale_y) {
    const Point control1{command.x1, command.y1};
    const Point control2{command.x2, command.y2};
    const Point end{command.x, command.y};
    const float control_length = scaledDistance(start, control1, scale_x, scale_y) +
                                scaledDistance(control1, control2, scale_x, scale_y) +
                                scaledDistance(control2, end, scale_x, scale_y);
    const int segments = std::clamp(static_cast<int>(std::ceil(control_length / 3.0F)), 4, 64);
    for (int i = 1; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        points.push_back(cubicPoint(start, control1, control2, end, t));
    }
}

void rasterizeGlyph(const Pfr1Font::OutlineGlyph& glyph,
                    std::vector<std::uint32_t>& argb,
                    int bitmap_width,
                    int bitmap_height,
                    int cell_x,
                    int cell_y,
                    int cell_width,
                    int cell_height,
                    float scale_x,
                    float scale_y,
                    float offset_x,
                    float offset_y) {
    std::vector<std::vector<Point>> polygons;
    for (const auto& contour : glyph.contours) {
        std::vector<Point> points;
        Point current;
        bool has_current = false;
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
                has_current = true;
            } else if (command.type == 1) {
                current = Point{command.x, command.y};
                points.push_back(current);
                has_current = true;
            } else if (command.type == 2) {
                if (!has_current) {
                    current = Point{command.x, command.y};
                    points.push_back(current);
                    has_current = true;
                    continue;
                }
                appendCubicFlattened(points, current, command, scale_x, scale_y);
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

    for (int y = 0; y < cell_height; ++y) {
        const float scan_y = static_cast<float>(y) + 0.5F;
        std::vector<std::pair<float, int>> crossings;

        for (const auto& polygon : polygons) {
            const auto n = static_cast<int>(polygon.size());
            for (int i = 0; i < n; ++i) {
                const auto& p0 = polygon[static_cast<std::size_t>(i)];
                const auto& p1 = polygon[static_cast<std::size_t>((i + 1) % n)];
                const float x0 = p0.x * scale_x + offset_x;
                const float y0 = p0.y * scale_y + offset_y;
                const float x1 = p1.x * scale_x + offset_x;
                const float y1 = p1.y * scale_y + offset_y;

                if (std::abs(y0 - y1) < 0.001F) {
                    continue;
                }
                if ((y0 <= scan_y && y1 > scan_y) || (y1 <= scan_y && y0 > scan_y)) {
                    const float t = (scan_y - y0) / (y1 - y0);
                    const float x_cross = x0 + t * (x1 - x0);
                    const int dir = y0 < y1 ? 1 : -1;
                    crossings.emplace_back(x_cross, dir);
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
                if (px < 0 || px >= bitmap_width || py < 0 || py >= bitmap_height) {
                    continue;
                }
                const int idx = py * bitmap_width + px;
                if (idx >= 0 && idx < static_cast<int>(argb.size())) {
                    argb[static_cast<std::size_t>(idx)] = 0xFF000000U;
                }
            }
        }
    }
}

} // namespace

std::shared_ptr<BitmapFont> BitmapFont::create(std::vector<std::uint32_t> bitmap,
                                               int bitmap_width,
                                               int bitmap_height,
                                               int cell_width,
                                               int cell_height,
                                               std::vector<int> char_widths,
                                               std::string font_name,
                                               int font_size,
                                               int metrics_ascent,
                                               int metrics_line_height,
                                               GlyphMap overflow_glyphs,
                                               MetricMap overflow_widths) {
    return create(std::move(bitmap),
                  bitmap_width,
                  bitmap_height,
                  cell_width,
                  cell_height,
                  std::move(char_widths),
                  std::vector<int>(NUM_CHARS, 0),
                  std::move(font_name),
                  font_size,
                  metrics_ascent,
                  metrics_line_height,
                  std::move(overflow_glyphs),
                  std::move(overflow_widths),
                  {});
}

std::shared_ptr<BitmapFont> BitmapFont::create(std::vector<std::uint32_t> bitmap,
                                               int bitmap_width,
                                               int bitmap_height,
                                               int cell_width,
                                               int cell_height,
                                               std::vector<int> char_widths,
                                               std::vector<int> char_offsets_x,
                                               std::string font_name,
                                               int font_size,
                                               int metrics_ascent,
                                               int metrics_line_height,
                                               GlyphMap overflow_glyphs,
                                               MetricMap overflow_widths,
                                               MetricMap overflow_offsets_x) {
    return std::shared_ptr<BitmapFont>(new BitmapFont(std::move(bitmap),
                                                      bitmap_width,
                                                      bitmap_height,
                                                      cell_width,
                                                      cell_height,
                                                      std::move(char_widths),
                                                      std::move(char_offsets_x),
                                                      std::move(font_name),
                                                      font_size,
                                                      metrics_ascent,
                                                      metrics_line_height,
                                                      std::move(overflow_glyphs),
                                                      std::move(overflow_widths),
                                                      std::move(overflow_offsets_x)));
}

std::shared_ptr<BitmapFont> BitmapFont::fromPfr1(const Pfr1Font& font, int target_height) {
    if (target_height <= 0) {
        return nullptr;
    }

    const auto& fm = font.metrics;
    int outline_res = fm.outline_resolution;
    if (outline_res <= 0) {
        outline_res = 2048;
    }

    // Director's PFR font size scales glyph coordinates from outlineResolution.
    // Do not derive the glyph scale from the ascender/descender span: that is a
    // separate metric and can produce a different vertical extent.
    const float scale = static_cast<float>(target_height) / static_cast<float>(outline_res);
    const float matrix_scale_x = static_cast<float>(font.font_matrix[0]) / 256.0F;
    const float matrix_scale_y = static_cast<float>(font.font_matrix[3]) / 256.0F;
    const float scale_x = scale * std::abs(matrix_scale_x);
    const float scale_y = scale * std::abs(matrix_scale_y);

    float max_set_width = 0.0F;
    for (const auto& [_, glyph] : font.glyphs) {
        max_set_width = std::max(max_set_width, glyph.set_width);
    }

    float max_bbox_width = 0.0F;
    for (const auto& [_, glyph] : font.glyphs) {
        float min_x = std::numeric_limits<float>::max();
        float max_x = -std::numeric_limits<float>::max();
        for (const auto& contour : glyph.contours) {
            for (const auto& command : contour.commands) {
                min_x = std::min(min_x, command.x);
                max_x = std::max(max_x, command.x);
                if (command.type == 2) {
                    min_x = std::min({min_x, command.x1, command.x2});
                    max_x = std::max({max_x, command.x1, command.x2});
                }
            }
        }
        if (min_x < max_x) {
            max_bbox_width = std::max(max_bbox_width, max_x - min_x);
        }
    }

    const int cell_width = std::max(1,
                                   std::max(static_cast<int>(std::ceil(max_set_width * scale_x)),
                                            static_cast<int>(std::ceil(max_bbox_width * scale_x))));
    const float pixel_scale = static_cast<float>(target_height) / static_cast<float>(outline_res);
    const int pfr_asc_px = javaRound(std::abs(static_cast<float>(fm.ascender)) * pixel_scale);
    const int pfr_desc_px = javaRound(std::abs(static_cast<float>(fm.descender)) * pixel_scale);
    const int pfr_line_height = pfr_asc_px + pfr_desc_px;

    int cell_height = std::max(target_height, pfr_line_height + 1);
    if (fm.descender < 0) {
        const int desc_px = static_cast<int>(std::ceil(std::abs(static_cast<float>(fm.descender)) * pixel_scale));
        const int baseline_row = static_cast<int>(std::floor(static_cast<float>(fm.ascender) * pixel_scale));
        cell_height = std::max(cell_height, baseline_row + desc_px + 1);
    }

    const int bitmap_width = cell_width * GRID_COLUMNS;
    const int bitmap_height = cell_height * GRID_ROWS;
    std::vector<std::uint32_t> argb(static_cast<std::size_t>(bitmap_width * bitmap_height), 0);
    std::vector<int> char_widths(NUM_CHARS, cell_width);
    GlyphMap overflow_glyphs;
    MetricMap overflow_widths;

    const float font_min_x = static_cast<float>(fm.x_min);
    const float font_asc = static_cast<float>(fm.ascender);
    const float set_width_scale = scale_x;

    for (const auto& [char_code, glyph] : font.glyphs) {
        const int glyph_pixel_width = javaRound(glyph.set_width * set_width_scale);
        const float glyph_scale_x = scale_x;
        const float glyph_scale_y = matrix_scale_y < 0.0F ? scale_y : -scale_y;
        const float glyph_offset_x = -font_min_x * scale_x;
        const float glyph_offset_y = font_asc > 0.0F ? font_asc * pixel_scale : 0.0F;

        if (char_code >= 0 && char_code < NUM_CHARS) {
            if (glyph_pixel_width > 0) {
                char_widths[static_cast<std::size_t>(char_code)] = glyph_pixel_width;
            }
            if (glyph.contours.empty()) {
                continue;
            }
            const int col = char_code % GRID_COLUMNS;
            const int row = char_code / GRID_COLUMNS;
            rasterizeGlyph(glyph,
                           argb,
                           bitmap_width,
                           bitmap_height,
                           col * cell_width,
                           row * cell_height,
                           cell_width,
                           cell_height,
                           glyph_scale_x,
                           glyph_scale_y,
                           glyph_offset_x,
                           glyph_offset_y);
        } else {
            if (glyph_pixel_width > 0) {
                overflow_widths[char_code] = glyph_pixel_width;
            }
            if (glyph.contours.empty()) {
                continue;
            }
            std::vector<std::uint32_t> cell_buf(static_cast<std::size_t>(cell_width * cell_height), 0);
            rasterizeGlyph(glyph,
                           cell_buf,
                           cell_width,
                           cell_height,
                           0,
                           0,
                           cell_width,
                           cell_height,
                           glyph_scale_x,
                           glyph_scale_y,
                           glyph_offset_x,
                           glyph_offset_y);
            overflow_glyphs[char_code] = std::move(cell_buf);
        }
    }

    for (const auto& [char_code, bitmap_glyph] : font.bitmap_glyphs) {
        if (char_code < 0 || char_code >= NUM_CHARS) {
            continue;
        }
        if (const auto outline = font.glyphs.find(char_code);
            outline != font.glyphs.end() && !outline->second.contours.empty()) {
            continue;
        }

        const int col = char_code % GRID_COLUMNS;
        const int row = char_code / GRID_COLUMNS;
        const int cell_x_pos = col * cell_width;
        const int cell_y_pos = row * cell_height;
        const int bmp_adv = std::max(1, javaRound(static_cast<float>(bitmap_glyph.set_width) * set_width_scale));
        char_widths[static_cast<std::size_t>(char_code)] = bmp_adv;

        for (int gy = 0; gy < bitmap_glyph.y_size; ++gy) {
            for (int gx = 0; gx < bitmap_glyph.x_size; ++gx) {
                const int bit_index = gy * bitmap_glyph.x_size + gx;
                const int byte_idx = bit_index / 8;
                const int bit_idx = 7 - (bit_index % 8);
                if (byte_idx < 0 || byte_idx >= static_cast<int>(bitmap_glyph.image_data.size())) {
                    continue;
                }
                bool bit = (bitmap_glyph.image_data[static_cast<std::size_t>(byte_idx)] & (1U << bit_idx)) != 0;
                if (!font.pfr_black_pixel) {
                    bit = !bit;
                }
                if (!bit) {
                    continue;
                }

                const int px = cell_x_pos + gx + std::max(0, bitmap_glyph.x_pos);
                const int py = cell_y_pos + gy + std::max(0, bitmap_glyph.y_pos);
                if (px >= bitmap_width || py >= bitmap_height ||
                    px >= cell_x_pos + cell_width || py >= cell_y_pos + cell_height) {
                    continue;
                }
                const int idx = py * bitmap_width + px;
                if (idx >= 0 && idx < static_cast<int>(argb.size())) {
                    argb[static_cast<std::size_t>(idx)] = 0xFF000000U;
                }
            }
        }
    }

    for (int lc = 'a'; lc <= 'z'; ++lc) {
        const int ui = lc - 32;
        const int lower_x = (lc % GRID_COLUMNS) * cell_width;
        const int lower_y = (lc / GRID_COLUMNS) * cell_height;
        const int upper_x = (ui % GRID_COLUMNS) * cell_width;
        const int upper_y = (ui / GRID_COLUMNS) * cell_height;
        if (cellHasInk(argb, bitmap_width, lower_x, lower_y, cell_width, cell_height) ||
            !cellHasInk(argb, bitmap_width, upper_x, upper_y, cell_width, cell_height)) {
            continue;
        }
        copyCell(argb, bitmap_width, ui, lc, cell_width, cell_height);
        char_widths[static_cast<std::size_t>(lc)] = char_widths[static_cast<std::size_t>(ui)];
    }

    return create(std::move(argb),
                  bitmap_width,
                  bitmap_height,
                  cell_width,
                  cell_height,
                  std::move(char_widths),
                  std::vector<int>(NUM_CHARS, 0),
                  font.font_name,
                  target_height,
                  pfr_asc_px,
                  pfr_line_height,
                  std::move(overflow_glyphs),
                  std::move(overflow_widths),
                  {});
}

BitmapFont::BitmapFont(std::vector<std::uint32_t> bitmap,
                       int bitmap_width,
                       int bitmap_height,
                       int cell_width,
                       int cell_height,
                       std::vector<int> char_widths,
                       std::vector<int> char_offsets_x,
                       std::string font_name,
                       int font_size,
                       int metrics_ascent,
                       int metrics_line_height,
                       GlyphMap overflow_glyphs,
                       MetricMap overflow_widths,
                       MetricMap overflow_offsets_x)
    : bitmap_(std::move(bitmap)),
      bitmap_width_(bitmap_width),
      bitmap_height_(bitmap_height),
      cell_width_(cell_width),
      cell_height_(cell_height),
      char_widths_(std::move(char_widths)),
      char_offsets_x_(std::move(char_offsets_x)),
      font_name_(std::move(font_name)),
      font_size_(font_size),
      metrics_ascent_(metrics_ascent),
      metrics_line_height_(metrics_line_height),
      overflow_glyphs_(std::move(overflow_glyphs)),
      overflow_widths_(std::move(overflow_widths)),
      overflow_offsets_x_(std::move(overflow_offsets_x)) {
    if (bitmap_width_ < 0 || bitmap_height_ < 0 || cell_width_ <= 0 || cell_height_ <= 0) {
        throw std::invalid_argument("BitmapFont dimensions are invalid");
    }
    const auto expected = static_cast<std::size_t>(bitmap_width_) * static_cast<std::size_t>(bitmap_height_);
    if (bitmap_.size() != expected) {
        throw std::invalid_argument("BitmapFont pixel count does not match dimensions");
    }
    if (char_widths_.size() < NUM_CHARS) {
        char_widths_.resize(NUM_CHARS, cell_width_);
    }
    if (char_offsets_x_.size() < NUM_CHARS) {
        char_offsets_x_.resize(NUM_CHARS, 0);
    }
}

int BitmapFont::bitmapWidth() const { return bitmap_width_; }
int BitmapFont::bitmapHeight() const { return bitmap_height_; }
int BitmapFont::cellWidth() const { return cell_width_; }
int BitmapFont::cellHeight() const { return cell_height_; }

int BitmapFont::getCharWidth(int char_code) const {
    if (char_code >= 0 && char_code < static_cast<int>(char_widths_.size())) {
        return char_widths_[static_cast<std::size_t>(char_code)];
    }
    if (const auto it = overflow_widths_.find(char_code); it != overflow_widths_.end()) {
        return it->second;
    }
    return cell_width_;
}

int BitmapFont::getCharOffsetX(int char_code) const {
    if (char_code >= 0 && char_code < static_cast<int>(char_offsets_x_.size())) {
        return char_offsets_x_[static_cast<std::size_t>(char_code)];
    }
    if (const auto it = overflow_offsets_x_.find(char_code); it != overflow_offsets_x_.end()) {
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

int BitmapFont::getLineHeight() const { return metrics_line_height_; }
int BitmapFont::getAscent() const { return metrics_ascent_; }
const std::string& BitmapFont::getFontName() const { return font_name_; }
int BitmapFont::getFontSize() const { return font_size_; }

void BitmapFont::drawChar(int char_code,
                          std::vector<std::uint32_t>& dst,
                          int dst_w,
                          int dst_h,
                          int dst_x,
                          int dst_y,
                          std::uint32_t color) const {
    const int draw_x = dst_x + getCharOffsetX(char_code);
    const int r = static_cast<int>((color >> 16U) & 0xFFU);
    const int g = static_cast<int>((color >> 8U) & 0xFFU);
    const int b = static_cast<int>(color & 0xFFU);

    if (dst_w <= 0 || dst_h <= 0 || dst.size() < static_cast<std::size_t>(dst_w * dst_h)) {
        return;
    }

    if (char_code >= 0 && char_code < NUM_CHARS) {
        const int col = char_code % GRID_COLUMNS;
        const int row = char_code / GRID_COLUMNS;
        const int cell_x = col * cell_width_;
        const int cell_y = row * cell_height_;

        for (int cy = 0; cy < cell_height_; ++cy) {
            const int py = dst_y + cy;
            if (py < 0 || py >= dst_h) {
                continue;
            }
            for (int cx = 0; cx < cell_width_; ++cx) {
                const int px = draw_x + cx;
                if (px < 0 || px >= dst_w) {
                    continue;
                }
                const int src_idx = (cell_y + cy) * bitmap_width_ + (cell_x + cx);
                if (src_idx < 0 || src_idx >= static_cast<int>(bitmap_.size())) {
                    continue;
                }
                blendPixel(dst, dst_w, px, py, bitmap_[static_cast<std::size_t>(src_idx)], r, g, b);
            }
        }
        return;
    }

    const auto glyph = overflow_glyphs_.find(char_code);
    if (glyph == overflow_glyphs_.end()) {
        return;
    }
    const auto& glyph_pixels = glyph->second;
    for (int cy = 0; cy < cell_height_; ++cy) {
        const int py = dst_y + cy;
        if (py < 0 || py >= dst_h) {
            continue;
        }
        for (int cx = 0; cx < cell_width_; ++cx) {
            const int px = draw_x + cx;
            if (px < 0 || px >= dst_w) {
                continue;
            }
            const int src_idx = cy * cell_width_ + cx;
            if (src_idx < 0 || src_idx >= static_cast<int>(glyph_pixels.size())) {
                continue;
            }
            blendPixel(dst, dst_w, px, py, glyph_pixels[static_cast<std::size_t>(src_idx)], r, g, b);
        }
    }
}

void BitmapFont::blendPixel(std::vector<std::uint32_t>& dst,
                            int dst_w,
                            int px,
                            int py,
                            std::uint32_t src_pixel,
                            int r,
                            int g,
                            int b) {
    const int src_a = static_cast<int>((src_pixel >> 24U) & 0xFFU);
    if (src_a == 0) {
        return;
    }

    const auto dst_idx = static_cast<std::size_t>(py * dst_w + px);
    if (dst_idx >= dst.size()) {
        return;
    }

    if (src_a == 255) {
        dst[dst_idx] = 0xFF000000U |
                      (static_cast<std::uint32_t>(r & 0xFF) << 16U) |
                      (static_cast<std::uint32_t>(g & 0xFF) << 8U) |
                      static_cast<std::uint32_t>(b & 0xFF);
        return;
    }

    const auto existing = dst[dst_idx];
    const int ea = static_cast<int>((existing >> 24U) & 0xFFU);
    const int er = static_cast<int>((existing >> 16U) & 0xFFU);
    const int eg = static_cast<int>((existing >> 8U) & 0xFFU);
    const int eb = static_cast<int>(existing & 0xFFU);
    const int out_a = src_a + (ea * (255 - src_a)) / 255;
    const int out_r = (r * src_a + er * (255 - src_a)) / 255;
    const int out_g = (g * src_a + eg * (255 - src_a)) / 255;
    const int out_b = (b * src_a + eb * (255 - src_a)) / 255;
    dst[dst_idx] = (static_cast<std::uint32_t>(out_a & 0xFF) << 24U) |
                  (static_cast<std::uint32_t>(out_r & 0xFF) << 16U) |
                  (static_cast<std::uint32_t>(out_g & 0xFF) << 8U) |
                  static_cast<std::uint32_t>(out_b & 0xFF);
}

} // namespace libreshockwave::font
