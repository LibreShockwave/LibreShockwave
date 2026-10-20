#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace libreshockwave::w3d {

struct W3DNode {
    std::string name;
    std::string parent_name;
    int flags;
    std::optional<std::array<float, 16>> transform;
    std::string resource_name;
    std::string ref_name;
    std::string shader_name;

    [[nodiscard]] float posX() const;
    [[nodiscard]] float posY() const;
    [[nodiscard]] float posZ() const;

    [[nodiscard]] static W3DNode parse(const std::vector<std::uint8_t>& data);
};

} // namespace libreshockwave::w3d
