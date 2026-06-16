#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace libreshockwave::cast {

struct Shockwave3DInfo {
    std::string defaultShaderName;
    std::string worldName;
    std::string textureName;
    std::string cameraName;
    float drawDistance;
    std::array<float, 3> cameraPosition;
    std::array<float, 3> cameraTarget;
    int ambientR;
    int ambientG;
    int ambientB;
    int bgColorR;
    int bgColorG;
    int bgColorB;
    std::vector<int> headerFlags;

    [[nodiscard]] static bool isShockwave3D(const std::vector<std::uint8_t>& specificData);
    [[nodiscard]] static Shockwave3DInfo parse(const std::vector<std::uint8_t>& specificData);
};

} // namespace libreshockwave::cast
