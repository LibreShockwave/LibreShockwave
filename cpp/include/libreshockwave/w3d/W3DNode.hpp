#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace libreshockwave::w3d {

struct W3DNode {
    std::string name;
    std::string parentName;
    int flags;
    std::optional<std::array<float, 16>> transform;
    std::string resourceName;
    std::string refName;
    std::string shaderName;

    [[nodiscard]] float posX() const;
    [[nodiscard]] float posY() const;
    [[nodiscard]] float posZ() const;

    [[nodiscard]] static W3DNode parse(const std::vector<std::uint8_t>& data);
};

} // namespace libreshockwave::w3d
