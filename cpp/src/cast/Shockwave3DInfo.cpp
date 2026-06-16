#include "libreshockwave/cast/Shockwave3DInfo.hpp"

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::cast {

bool Shockwave3DInfo::isShockwave3D(const std::vector<std::uint8_t>& specificData) {
    return specificData.size() >= 40;
}

Shockwave3DInfo Shockwave3DInfo::parse(const std::vector<std::uint8_t>& specificData) {
    if (specificData.size() < 40) {
        return Shockwave3DInfo{"", "", "", "", 0.0F, {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, 0, 0, 0, 0, 0, 0, {}};
    }

    io::BinaryReader reader(specificData, io::ByteOrder::BigEndian);
    std::vector<int> headerFlags{reader.readI32(), reader.readI32()};
    const float drawDistance = reader.readF32();
    std::array<float, 3> cameraPosition{reader.readF32(), reader.readF32(), reader.readF32()};
    std::array<float, 3> cameraTarget{reader.readF32(), reader.readF32(), reader.readF32()};

    const int ambientR = reader.bytesLeft() >= 1 ? reader.readU8() : 0;
    const int ambientG = reader.bytesLeft() >= 1 ? reader.readU8() : 0;
    const int ambientB = reader.bytesLeft() >= 1 ? reader.readU8() : 0;
    const int bgColorR = reader.bytesLeft() >= 1 ? reader.readU8() : 0;
    const int bgColorG = reader.bytesLeft() >= 1 ? reader.readU8() : 0;
    const int bgColorB = reader.bytesLeft() >= 1 ? reader.readU8() : 0;

    auto readPascal = [&reader]() {
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
        readPascal(),
        readPascal(),
        readPascal(),
        readPascal(),
        drawDistance,
        cameraPosition,
        cameraTarget,
        ambientR,
        ambientG,
        ambientB,
        bgColorR,
        bgColorG,
        bgColorB,
        std::move(headerFlags)
    };
}

} // namespace libreshockwave::cast
