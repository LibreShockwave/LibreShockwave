#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace libreshockwave::w3d {

struct W3DResourceRef {
    std::string name;
    int refType;
    std::vector<std::uint8_t> refData;

    [[nodiscard]] static W3DResourceRef parse(const std::vector<std::uint8_t>& data);
};

} // namespace libreshockwave::w3d
