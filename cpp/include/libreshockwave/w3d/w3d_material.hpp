#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace libreshockwave::w3d {

struct W3DMaterial {
    std::string name;
    std::vector<std::uint8_t> material_data;
    std::optional<std::array<float, 4>> diffuse_color;
    std::optional<std::array<float, 4>> ambient_color;
    std::optional<std::array<float, 4>> specular_color;
    std::optional<float> shininess;
    std::string texture_name;

    [[nodiscard]] static W3DMaterial parse(const std::vector<std::uint8_t>& data);
};

} // namespace libreshockwave::w3d
