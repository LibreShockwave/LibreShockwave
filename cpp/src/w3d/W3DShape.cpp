#include "libreshockwave/w3d/W3DShape.hpp"

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::w3d {

W3DShape W3DShape::parse(const std::vector<std::uint8_t>& data) {
    if (data.size() < 4) {
        return W3DShape{"", "", 0, std::nullopt, {}};
    }

    io::BinaryReader reader(data, io::ByteOrder::LittleEndian);
    std::string name = reader.readPString16();
    std::string parentName = reader.bytesLeft() >= 2 ? reader.readPString16() : "";
    const int flags = reader.bytesLeft() >= 4 ? reader.readI32() : 0;

    std::optional<std::array<float, 16>> transform;
    if (reader.bytesLeft() >= 64) {
        std::array<float, 16> values{};
        for (float& value : values) {
            value = reader.readF32();
        }
        transform = values;
    }

    std::vector<std::uint8_t> shapeData;
    if (reader.bytesLeft() > 0) {
        shapeData = reader.readBytes(reader.bytesLeft());
    }

    return W3DShape{std::move(name), std::move(parentName), flags, transform, std::move(shapeData)};
}

} // namespace libreshockwave::w3d
