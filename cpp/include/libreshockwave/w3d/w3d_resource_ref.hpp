#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace libreshockwave::w3d {

struct W3DResourceRef {
    std::string name;
    int ref_type;
    std::vector<std::uint8_t> ref_data;

    [[nodiscard]] static W3DResourceRef parse(const std::vector<std::uint8_t>& data);
};

} // namespace libreshockwave::w3d
