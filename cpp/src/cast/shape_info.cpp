#include "libreshockwave/cast/shape_info.hpp"

#include "libreshockwave/io/binary_reader.hpp"

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
    return fill_type != 0;
}

bool ShapeInfo::isOutlineInvisible() const {
    return !isFilled() && line_thickness <= 1;
}

ShapeInfo ShapeInfo::parse(const std::vector<std::uint8_t>& data) {
    if (data.size() < 14) {
        return ShapeInfo{ShapeType::Unknown, 0, 0, 0, 0, 0, 0, 0, 1, 0};
    }

    io::BinaryReader reader(data, io::ByteOrder::BigEndian);
    const int shape_type_raw = reader.readU16();
    const int reg_y = reader.readU16();
    const int reg_x = reader.readU16();
    const int height = reader.readU16();
    const int width = reader.readU16();
    reader.skip(2);
    const int color = reader.readU8();
    const int back_color = data.size() >= 14 ? data[13] : 0;
    const int fill_type = data.size() >= 15 ? data[14] : 1;
    const int line_thickness = data.size() >= 16 ? data[15] : 1;
    const int line_direction = data.size() >= 17 ? data[16] : 0;

    return ShapeInfo{shapeTypeFromCode(shape_type_raw), reg_x, reg_y, width, height, color, back_color, fill_type, line_thickness, line_direction};
}

} // namespace libreshockwave::cast
