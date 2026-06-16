#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace libreshockwave::w3d {

struct W3DTexture {
    std::string name;
    std::vector<std::uint8_t> imageData;
    std::string format;

    [[nodiscard]] static W3DTexture parse(const std::vector<std::uint8_t>& data);
};

} // namespace libreshockwave::w3d
