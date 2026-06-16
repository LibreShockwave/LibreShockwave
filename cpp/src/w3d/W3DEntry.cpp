#include "libreshockwave/w3d/W3DEntry.hpp"

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::w3d {

W3DEntry W3DEntry::read(io::BinaryReader& reader) {
    const int typeCode = reader.readU16();
    const int dataLen = reader.readI32();
    const int parentRef = reader.readI32();

    std::vector<std::uint8_t> data;
    if (dataLen > 0 && reader.bytesLeft() >= static_cast<std::size_t>(dataLen)) {
        data = reader.readBytes(static_cast<std::size_t>(dataLen));
    }

    return W3DEntry{w3dEntryTypeFromCode(typeCode), parentRef, std::move(data)};
}

} // namespace libreshockwave::w3d
