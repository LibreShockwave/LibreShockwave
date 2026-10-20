#include "libreshockwave/w3d/w3d_resource_ref.hpp"

#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::w3d {

W3DResourceRef W3DResourceRef::parse(const std::vector<std::uint8_t>& data) {
    if (data.size() < 4) {
        return W3DResourceRef{"", 0, {}};
    }

    io::BinaryReader reader(data, io::ByteOrder::LittleEndian);
    std::string name = reader.readPString16();
    const int ref_type = reader.bytesLeft() >= 4 ? reader.readI32() : 0;

    std::vector<std::uint8_t> ref_data;
    if (reader.bytesLeft() > 0) {
        ref_data = reader.readBytes(reader.bytesLeft());
    }
    return W3DResourceRef{std::move(name), ref_type, std::move(ref_data)};
}

} // namespace libreshockwave::w3d
