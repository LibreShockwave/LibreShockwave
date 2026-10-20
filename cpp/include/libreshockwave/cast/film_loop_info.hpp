#pragma once

#include <cstdint>
#include <vector>

namespace libreshockwave::cast {

struct FilmLoopInfo {
    int rect_top;
    int rect_left;
    int rect_bottom;
    int rect_right;
    bool center;
    bool crop;
    bool sound;
    bool loops;

    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] int regX() const;
    [[nodiscard]] int regY() const;

    [[nodiscard]] static FilmLoopInfo parse(const std::vector<std::uint8_t>& data);
};

} // namespace libreshockwave::cast
