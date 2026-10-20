#pragma once

#include <cstdint>
#include <vector>

#include "libreshockwave/w3d/w3d_entry_type.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::w3d {

struct W3DEntry {
    W3DEntryType type;
    int parent_ref;
    std::vector<std::uint8_t> data;

    [[nodiscard]] static W3DEntry read(io::BinaryReader& reader);
};

} // namespace libreshockwave::w3d
