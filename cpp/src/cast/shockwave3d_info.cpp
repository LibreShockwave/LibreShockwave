#include "libreshockwave/cast/shockwave3d_info.hpp"

#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::cast {

bool Shockwave3DInfo::isShockwave3D(const std::vector<std::uint8_t>& specific_data) {
    return specific_data.size() >= 40;
}

Shockwave3DInfo Shockwave3DInfo::parse(const std::vector<std::uint8_t>& specific_data) {
    if (specific_data.size() < 40) {
        return Shockwave3DInfo{"", "", "", "", 0.0F, {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, 0, 0, 0, 0, 0, 0, {}};
    }

    io::BinaryReader reader(specific_data, io::ByteOrder::BigEndian);
    std::vector<int> header_flags{reader.readI32(), reader.readI32()};
    const float draw_distance = reader.readF32();
    std::array<float, 3> camera_position{reader.readF32(), reader.readF32(), reader.readF32()};
    std::array<float, 3> camera_target{reader.readF32(), reader.readF32(), reader.readF32()};

    const int ambient_r = reader.bytesLeft() >= 1 ? reader.readU8() : 0;
    const int ambient_g = reader.bytesLeft() >= 1 ? reader.readU8() : 0;
    const int ambient_b = reader.bytesLeft() >= 1 ? reader.readU8() : 0;
    const int bg_color_r = reader.bytesLeft() >= 1 ? reader.readU8() : 0;
    const int bg_color_g = reader.bytesLeft() >= 1 ? reader.readU8() : 0;
    const int bg_color_b = reader.bytesLeft() >= 1 ? reader.readU8() : 0;

    auto read_pascal = [&reader]() {
        std::string value;
        if (reader.bytesLeft() >= 1) {
            const int length = reader.readU8();
            if (length > 0 && reader.bytesLeft() >= static_cast<std::size_t>(length)) {
                value = reader.readString(static_cast<std::size_t>(length));
            }
        }
        return value;
    };

    return Shockwave3DInfo{
        read_pascal(),
        read_pascal(),
        read_pascal(),
        read_pascal(),
        draw_distance,
        camera_position,
        camera_target,
        ambient_r,
        ambient_g,
        ambient_b,
        bg_color_r,
        bg_color_g,
        bg_color_b,
        std::move(header_flags)
    };
}

} // namespace libreshockwave::cast
