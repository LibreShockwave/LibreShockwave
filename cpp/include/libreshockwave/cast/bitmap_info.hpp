#pragma once

#include <cstdint>
#include <vector>

namespace libreshockwave::cast {

struct BitmapInfo {
    int width;
    int height;
    int reg_x;
    int reg_y;
    int initial_left;
    int initial_top;
    int bit_depth;
    int palette_id;
    int palette_cast_lib;
    int pitch;
    int alpha_threshold;
    bool use_alpha;

    [[nodiscard]] int regXLocal() const;
    [[nodiscard]] int regYLocal() const;
    [[nodiscard]] int bytesPerPixel() const;
    [[nodiscard]] bool isPaletted() const;

    [[nodiscard]] static BitmapInfo parse(const std::vector<std::uint8_t>& data, int director_version = 1200);
};

} // namespace libreshockwave::cast
