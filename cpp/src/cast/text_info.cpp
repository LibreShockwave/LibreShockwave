#include "libreshockwave/cast/text_info.hpp"

namespace libreshockwave::cast {

TextInfo TextInfo::parse(const std::vector<std::uint8_t>& data) {
    if (data.size() < 4) {
        return TextInfo{0, 255, 255, 255, 0, 0, 0, 0, 0, true};
    }

    int text_align = 0;
    int bg_r = 255;
    int bg_g = 255;
    int bg_b = 255;
    int width = 0;
    int height = 0;
    int border_size = 0;
    int gutter_size = 0;
    int text_height = 0;
    bool word_wrap = true;

    if (data.size() >= 48) {
        text_align = static_cast<std::int16_t>((data[0] << 8) | data[1]);
        bg_r = data[2];
        bg_g = data[4];
        bg_b = data[6];
        width = (data[38] << 8) | data[39];
        height = (data[40] << 8) | data[41];
    } else if (data.size() >= 28) {
        border_size = data[0];
        gutter_size = data[1];
        text_align = static_cast<std::int16_t>((data[4] << 8) | data[5]);
        bg_r = data[7];
        bg_g = data[9];
        bg_b = data[11];
        text_height = (data[22] << 8) | data[23];
    } else {
        text_align = static_cast<std::int16_t>((data[0] << 8) | data[1]);
        if (data.size() >= 8) {
            bg_r = data[2];
            bg_g = data[4];
            bg_b = data[6];
        }
    }

    return TextInfo{text_align, bg_r, bg_g, bg_b, width, height, border_size, gutter_size, text_height, word_wrap};
}

} // namespace libreshockwave::cast
