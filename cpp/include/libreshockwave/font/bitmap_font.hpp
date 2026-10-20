#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace libreshockwave::font {

class Pfr1Font;

class BitmapFont {
public:
    static constexpr int GRID_COLUMNS = 16;
    static constexpr int GRID_ROWS = 8;
    static constexpr int NUM_CHARS = 128;

    using GlyphMap = std::unordered_map<int, std::vector<std::uint32_t>>;
    using MetricMap = std::unordered_map<int, int>;

    [[nodiscard]] static std::shared_ptr<BitmapFont> create(
        std::vector<std::uint32_t> bitmap,
        int bitmap_width,
        int bitmap_height,
        int cell_width,
        int cell_height,
        std::vector<int> char_widths,
        std::string font_name,
        int font_size,
        int metrics_ascent,
        int metrics_line_height,
        GlyphMap overflow_glyphs = {},
        MetricMap overflow_widths = {});

    [[nodiscard]] static std::shared_ptr<BitmapFont> create(
        std::vector<std::uint32_t> bitmap,
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
        GlyphMap overflow_glyphs = {},
        MetricMap overflow_widths = {},
        MetricMap overflow_offsets_x = {});

    [[nodiscard]] static std::shared_ptr<BitmapFont> fromPfr1(const Pfr1Font& font, int target_height);

    [[nodiscard]] int bitmapWidth() const;
    [[nodiscard]] int bitmapHeight() const;
    [[nodiscard]] int cellWidth() const;
    [[nodiscard]] int cellHeight() const;
    [[nodiscard]] int getCharWidth(int char_code) const;
    [[nodiscard]] int getCharOffsetX(int char_code) const;
    [[nodiscard]] int getStringWidth(std::string_view text) const;
    [[nodiscard]] int getLineHeight() const;
    [[nodiscard]] int getAscent() const;
    [[nodiscard]] const std::string& getFontName() const;
    [[nodiscard]] int getFontSize() const;

    void drawChar(int char_code,
                  std::vector<std::uint32_t>& dst,
                  int dst_w,
                  int dst_h,
                  int dst_x,
                  int dst_y,
                  std::uint32_t color) const;

private:
    BitmapFont(std::vector<std::uint32_t> bitmap,
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
               MetricMap overflow_offsets_x);

    static void blendPixel(std::vector<std::uint32_t>& dst,
                           int dst_w,
                           int px,
                           int py,
                           std::uint32_t src_pixel,
                           int r,
                           int g,
                           int b);

    std::vector<std::uint32_t> bitmap_;
    int bitmap_width_;
    int bitmap_height_;
    int cell_width_;
    int cell_height_;
    std::vector<int> char_widths_;
    std::vector<int> char_offsets_x_;
    std::string font_name_;
    int font_size_;
    int metrics_ascent_;
    int metrics_line_height_;
    GlyphMap overflow_glyphs_;
    MetricMap overflow_widths_;
    MetricMap overflow_offsets_x_;
};

} // namespace libreshockwave::font
