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
        int bitmapWidth,
        int bitmapHeight,
        int cellWidth,
        int cellHeight,
        std::vector<int> charWidths,
        std::string fontName,
        int fontSize,
        int metricsAscent,
        int metricsLineHeight,
        GlyphMap overflowGlyphs = {},
        MetricMap overflowWidths = {});

    [[nodiscard]] static std::shared_ptr<BitmapFont> create(
        std::vector<std::uint32_t> bitmap,
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
        GlyphMap overflowGlyphs = {},
        MetricMap overflowWidths = {},
        MetricMap overflowOffsetsX = {});

    [[nodiscard]] static std::shared_ptr<BitmapFont> fromPfr1(const Pfr1Font& font, int targetHeight);

    [[nodiscard]] int bitmapWidth() const;
    [[nodiscard]] int bitmapHeight() const;
    [[nodiscard]] int cellWidth() const;
    [[nodiscard]] int cellHeight() const;
    [[nodiscard]] int getCharWidth(int charCode) const;
    [[nodiscard]] int getCharOffsetX(int charCode) const;
    [[nodiscard]] int getStringWidth(std::string_view text) const;
    [[nodiscard]] int getLineHeight() const;
    [[nodiscard]] int getAscent() const;
    [[nodiscard]] const std::string& getFontName() const;
    [[nodiscard]] int getFontSize() const;

    void drawChar(int charCode,
                  std::vector<std::uint32_t>& dst,
                  int dstW,
                  int dstH,
                  int dstX,
                  int dstY,
                  std::uint32_t color) const;

private:
    BitmapFont(std::vector<std::uint32_t> bitmap,
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
               MetricMap overflowOffsetsX);

    static void blendPixel(std::vector<std::uint32_t>& dst,
                           int dstW,
                           int px,
                           int py,
                           std::uint32_t srcPixel,
                           int r,
                           int g,
                           int b);

    std::vector<std::uint32_t> bitmap_;
    int bitmapWidth_;
    int bitmapHeight_;
    int cellWidth_;
    int cellHeight_;
    std::vector<int> charWidths_;
    std::vector<int> charOffsetsX_;
    std::string fontName_;
    int fontSize_;
    int metricsAscent_;
    int metricsLineHeight_;
    GlyphMap overflowGlyphs_;
    MetricMap overflowWidths_;
    MetricMap overflowOffsetsX_;
};

} // namespace libreshockwave::font
