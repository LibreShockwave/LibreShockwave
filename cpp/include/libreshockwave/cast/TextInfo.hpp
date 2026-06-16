#pragma once

#include <cstdint>
#include <vector>

namespace libreshockwave::cast {

struct TextInfo {
    int textAlign;
    int bgRed;
    int bgGreen;
    int bgBlue;
    int width;
    int height;
    int borderSize;
    int gutterSize;
    int textHeight;
    bool isWordWrap;

    [[nodiscard]] static TextInfo parse(const std::vector<std::uint8_t>& data);
};

} // namespace libreshockwave::cast
