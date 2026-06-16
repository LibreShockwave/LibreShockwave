#include "libreshockwave/w3d/W3DResourceRef.hpp"

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::w3d {

W3DResourceRef W3DResourceRef::parse(const std::vector<std::uint8_t>& data) {
    if (data.size() < 4) {
        return W3DResourceRef{"", 0, {}};
    }

    io::BinaryReader reader(data, io::ByteOrder::LittleEndian);
    std::string name = reader.readPString16();
    const int refType = reader.bytesLeft() >= 4 ? reader.readI32() : 0;

    std::vector<std::uint8_t> refData;
    if (reader.bytesLeft() > 0) {
        refData = reader.readBytes(reader.bytesLeft());
    }
    return W3DResourceRef{std::move(name), refType, std::move(refData)};
}

} // namespace libreshockwave::w3d
