#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace libreshockwave::w3d {

struct W3DShape {
    std::string name;
    std::string parent_name;
    int flags;
    std::optional<std::array<float, 16>> transform;
    std::vector<std::uint8_t> shape_data;

    [[nodiscard]] static W3DShape parse(const std::vector<std::uint8_t>& data);
};

} // namespace libreshockwave::w3d
