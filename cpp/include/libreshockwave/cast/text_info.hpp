#pragma once

#include <cstdint>
#include <vector>

namespace libreshockwave::cast {

struct TextInfo {
    int text_align;
    int bg_red;
    int bg_green;
    int bg_blue;
    int width;
    int height;
    int border_size;
    int gutter_size;
    int text_height;
    bool is_word_wrap;

    [[nodiscard]] static TextInfo parse(const std::vector<std::uint8_t>& data);
};

} // namespace libreshockwave::cast
