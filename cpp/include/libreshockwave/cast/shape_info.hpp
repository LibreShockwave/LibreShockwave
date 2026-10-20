#pragma once

#include <cstdint>
#include <vector>

namespace libreshockwave::cast {

enum class ShapeType {
    Rect = 0x01,
    OvalRect = 0x02,
    Oval = 0x03,
    Line = 0x08,
    Unknown = 0
};

[[nodiscard]] ShapeType shapeTypeFromCode(int code);

struct ShapeInfo {
    ShapeType shape_type;
    int reg_x;
    int reg_y;
    int width;
    int height;
    int color;
    int back_color;
    int fill_type;
    int line_thickness;
    int line_direction;

    [[nodiscard]] bool isFilled() const;
    [[nodiscard]] bool isOutlineInvisible() const;

    [[nodiscard]] static ShapeInfo parse(const std::vector<std::uint8_t>& data);
};

} // namespace libreshockwave::cast
