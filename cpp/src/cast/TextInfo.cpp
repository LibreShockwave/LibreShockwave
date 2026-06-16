#include "libreshockwave/cast/TextInfo.hpp"

namespace libreshockwave::cast {

TextInfo TextInfo::parse(const std::vector<std::uint8_t>& data) {
    if (data.size() < 4) {
        return TextInfo{0, 255, 255, 255, 0, 0, 0, 0, 0, true};
    }

    int textAlign = 0;
    int bgR = 255;
    int bgG = 255;
    int bgB = 255;
    int width = 0;
    int height = 0;
    int borderSize = 0;
    int gutterSize = 0;
    int textHeight = 0;
    bool wordWrap = true;

    if (data.size() >= 48) {
        textAlign = static_cast<std::int16_t>((data[0] << 8) | data[1]);
        bgR = data[2];
        bgG = data[4];
        bgB = data[6];
        width = (data[38] << 8) | data[39];
        height = (data[40] << 8) | data[41];
    } else if (data.size() >= 28) {
        borderSize = data[0];
        gutterSize = data[1];
        textAlign = static_cast<std::int16_t>((data[4] << 8) | data[5]);
        bgR = data[7];
        bgG = data[9];
        bgB = data[11];
        textHeight = (data[22] << 8) | data[23];
    } else {
        textAlign = static_cast<std::int16_t>((data[0] << 8) | data[1]);
        if (data.size() >= 8) {
            bgR = data[2];
            bgG = data[4];
            bgB = data[6];
        }
    }

    return TextInfo{textAlign, bgR, bgG, bgB, width, height, borderSize, gutterSize, textHeight, wordWrap};
}

} // namespace libreshockwave::cast
