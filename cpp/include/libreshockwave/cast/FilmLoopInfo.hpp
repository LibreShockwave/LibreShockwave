#pragma once

#include <cstdint>
#include <vector>

namespace libreshockwave::cast {

struct FilmLoopInfo {
    int rectTop;
    int rectLeft;
    int rectBottom;
    int rectRight;
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
