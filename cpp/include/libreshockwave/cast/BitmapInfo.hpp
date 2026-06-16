#pragma once

#include <cstdint>
#include <vector>

namespace libreshockwave::cast {

struct BitmapInfo {
    int width;
    int height;
    int regX;
    int regY;
    int initialLeft;
    int initialTop;
    int bitDepth;
    int paletteId;
    int paletteCastLib;
    int pitch;
    int alphaThreshold;
    bool useAlpha;

    [[nodiscard]] int regXLocal() const;
    [[nodiscard]] int regYLocal() const;
    [[nodiscard]] int bytesPerPixel() const;
    [[nodiscard]] bool isPaletted() const;

    [[nodiscard]] static BitmapInfo parse(const std::vector<std::uint8_t>& data, int directorVersion = 1200);
};

} // namespace libreshockwave::cast
