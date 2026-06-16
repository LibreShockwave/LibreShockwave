#include "libreshockwave/w3d/W3DTexture.hpp"

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::w3d {

W3DTexture W3DTexture::parse(const std::vector<std::uint8_t>& data) {
    if (data.size() < 4) {
        return W3DTexture{"", {}, "raw"};
    }

    io::BinaryReader reader(data, io::ByteOrder::LittleEndian);
    std::string name = reader.readPString16();
    if (reader.bytesLeft() >= 1) {
        reader.skip(1);
    }

    std::vector<std::uint8_t> imageData;
    if (reader.bytesLeft() > 0) {
        imageData = reader.readBytes(reader.bytesLeft());
    }

    std::string format = "raw";
    if (imageData.size() >= 2 && imageData[0] == 0xFF && imageData[1] == 0xD8) {
        format = "jpeg";
    }

    return W3DTexture{std::move(name), std::move(imageData), std::move(format)};
}

} // namespace libreshockwave::w3d
