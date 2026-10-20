#include "libreshockwave/w3d/w3d_texture.hpp"

#include "libreshockwave/io/binary_reader.hpp"

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

    std::vector<std::uint8_t> image_data;
    if (reader.bytesLeft() > 0) {
        image_data = reader.readBytes(reader.bytesLeft());
    }

    std::string format = "raw";
    if (image_data.size() >= 2 && image_data[0] == 0xFF && image_data[1] == 0xD8) {
        format = "jpeg";
    }

    return W3DTexture{std::move(name), std::move(image_data), std::move(format)};
}

} // namespace libreshockwave::w3d
