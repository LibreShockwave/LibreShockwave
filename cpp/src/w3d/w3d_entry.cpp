#include "libreshockwave/w3d/w3d_entry.hpp"

#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::w3d {

W3DEntry W3DEntry::read(io::BinaryReader& reader) {
    const int type_code = reader.readU16();
    const int data_len = reader.readI32();
    const int parent_ref = reader.readI32();

    std::vector<std::uint8_t> data;
    if (data_len > 0 && reader.bytesLeft() >= static_cast<std::size_t>(data_len)) {
        data = reader.readBytes(static_cast<std::size_t>(data_len));
    }

    return W3DEntry{w3dEntryTypeFromCode(type_code), parent_ref, std::move(data)};
}

} // namespace libreshockwave::w3d
