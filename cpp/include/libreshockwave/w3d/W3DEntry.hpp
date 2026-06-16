#pragma once

#include <cstdint>
#include <vector>

#include "libreshockwave/w3d/W3DEntryType.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::w3d {

struct W3DEntry {
    W3DEntryType type;
    int parentRef;
    std::vector<std::uint8_t> data;

    [[nodiscard]] static W3DEntry read(io::BinaryReader& reader);
};

} // namespace libreshockwave::w3d
