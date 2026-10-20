#include "libreshockwave/w3d/w3d_shape.hpp"

#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::w3d {

W3DShape W3DShape::parse(const std::vector<std::uint8_t>& data) {
    if (data.size() < 4) {
        return W3DShape{"", "", 0, std::nullopt, {}};
    }

    io::BinaryReader reader(data, io::ByteOrder::LittleEndian);
    std::string name = reader.readPString16();
    std::string parent_name = reader.bytesLeft() >= 2 ? reader.readPString16() : "";
    const int flags = reader.bytesLeft() >= 4 ? reader.readI32() : 0;

    std::optional<std::array<float, 16>> transform;
    if (reader.bytesLeft() >= 64) {
        std::array<float, 16> values{};
        for (float& value : values) {
            value = reader.readF32();
        }
        transform = values;
    }

    std::vector<std::uint8_t> shape_data;
    if (reader.bytesLeft() > 0) {
        shape_data = reader.readBytes(reader.bytesLeft());
    }

    return W3DShape{std::move(name), std::move(parent_name), flags, transform, std::move(shape_data)};
}

} // namespace libreshockwave::w3d
