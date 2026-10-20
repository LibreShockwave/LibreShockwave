#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace libreshockwave::cast {

struct Shockwave3DInfo {
    std::string default_shader_name;
    std::string world_name;
    std::string texture_name;
    std::string camera_name;
    float draw_distance;
    std::array<float, 3> camera_position;
    std::array<float, 3> camera_target;
    int ambient_r;
    int ambient_g;
    int ambient_b;
    int bg_color_r;
    int bg_color_g;
    int bg_color_b;
    std::vector<int> header_flags;

    [[nodiscard]] static bool isShockwave3D(const std::vector<std::uint8_t>& specific_data);
    [[nodiscard]] static Shockwave3DInfo parse(const std::vector<std::uint8_t>& specific_data);
};

} // namespace libreshockwave::cast
