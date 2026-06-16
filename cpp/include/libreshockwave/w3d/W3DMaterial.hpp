#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace libreshockwave::w3d {

struct W3DMaterial {
    std::string name;
    std::vector<std::uint8_t> materialData;
    std::optional<std::array<float, 4>> diffuseColor;
    std::optional<std::array<float, 4>> ambientColor;
    std::optional<std::array<float, 4>> specularColor;
    std::optional<float> shininess;
    std::string textureName;

    [[nodiscard]] static W3DMaterial parse(const std::vector<std::uint8_t>& data);
};

} // namespace libreshockwave::w3d
