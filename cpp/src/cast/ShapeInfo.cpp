#include "libreshockwave/cast/ShapeInfo.hpp"

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::cast {

ShapeType shapeTypeFromCode(int value) {
    switch (value) {
        case 0x01: return ShapeType::Rect;
        case 0x02: return ShapeType::OvalRect;
        case 0x03: return ShapeType::Oval;
        case 0x08: return ShapeType::Line;
        default: return ShapeType::Unknown;
    }
}

bool ShapeInfo::isFilled() const {
    return fillType != 0;
}

bool ShapeInfo::isOutlineInvisible() const {
    return !isFilled() && lineThickness <= 1;
}

ShapeInfo ShapeInfo::parse(const std::vector<std::uint8_t>& data) {
    if (data.size() < 14) {
        return ShapeInfo{ShapeType::Unknown, 0, 0, 0, 0, 0, 0, 0, 1, 0};
    }

    io::BinaryReader reader(data, io::ByteOrder::BigEndian);
    const int shapeTypeRaw = reader.readU16();
    const int regY = reader.readU16();
    const int regX = reader.readU16();
    const int height = reader.readU16();
    const int width = reader.readU16();
    reader.skip(2);
    const int color = reader.readU8();
    const int backColor = data.size() >= 14 ? data[13] : 0;
    const int fillType = data.size() >= 15 ? data[14] : 1;
    const int lineThickness = data.size() >= 16 ? data[15] : 1;
    const int lineDirection = data.size() >= 17 ? data[16] : 0;

    return ShapeInfo{shapeTypeFromCode(shapeTypeRaw), regX, regY, width, height, color, backColor, fillType, lineThickness, lineDirection};
}

} // namespace libreshockwave::cast
