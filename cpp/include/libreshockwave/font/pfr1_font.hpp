#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace libreshockwave::font {

class Pfr1Font {
public:
    struct CharacterRecord {
        int char_code = 0;
        int set_width = 0;
        int gps_size = 0;
        int gps_offset = 0;
    };

    struct FontMetrics {
        int outline_resolution = 2048;
        int metrics_resolution = 2048;
        int x_min = 0;
        int y_min = 0;
        int x_max = 0;
        int y_max = 0;
        int ascender = 0;
        int descender = 0;
        int std_vw = 0;
        int std_hw = 0;
        bool has_bitmap_section = false;
        std::string font_id;
    };

    struct Contour {
        struct Command {
            int type = 0;
            float x = 0.0F;
            float y = 0.0F;
            float x1 = 0.0F;
            float y1 = 0.0F;
            float x2 = 0.0F;
            float y2 = 0.0F;
        };

        std::vector<Command> commands;
        void moveTo(float x, float y);
        void lineTo(float x, float y);
        void curveTo(float x1, float y1, float x2, float y2, float x, float y);
    };

    struct OutlineGlyph {
        int char_code = 0;
        float set_width = 0.0F;
        std::vector<Contour> contours;
    };

    struct BitmapGlyph {
        int char_code = 0;
        int image_format = 0;
        int x_pos = 0;
        int y_pos = 0;
        int x_size = 0;
        int y_size = 0;
        int set_width = 0;
        std::vector<std::uint8_t> image_data;
    };

    [[nodiscard]] static std::shared_ptr<Pfr1Font> parse(const std::vector<std::uint8_t>& data);

    std::string font_name;
    FontMetrics metrics;
    std::vector<CharacterRecord> char_records;
    std::unordered_map<int, OutlineGlyph> glyphs;
    std::unordered_map<int, BitmapGlyph> bitmap_glyphs;
    std::array<int, 4> font_matrix{256, 0, 0, 256};
    bool pfr_black_pixel = false;
    int gps_offset = 0;
    int gps_size = 0;

private:
    void parseHeader(const std::vector<std::uint8_t>& data);
    void parseLogicalFontDirectory(const std::vector<std::uint8_t>& data,
                                   int dir_size,
                                   int dir_offset,
                                   int section_size,
                                   int section_offset);
    void parsePhysicalFont(const std::vector<std::uint8_t>& data);
    void parseDeltaEncodedCharRecords(class PfrBitReader& reader,
                                      int n_characters,
                                      int standard_set_width);
    void parseGlyphStubsAndBitmaps(const std::vector<std::uint8_t>& data);
    [[nodiscard]] BitmapGlyph parseBitmapGlyph(const std::vector<std::uint8_t>& data,
                                               int start,
                                               int size,
                                               int char_code) const;

    int stored_max_chars_ = 0;
};

} // namespace libreshockwave::font
