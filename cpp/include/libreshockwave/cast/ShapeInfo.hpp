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
    ShapeType shapeType;
    int regX;
    int regY;
    int width;
    int height;
    int color;
    int backColor;
    int fillType;
    int lineThickness;
    int lineDirection;

    [[nodiscard]] bool isFilled() const;
    [[nodiscard]] bool isOutlineInvisible() const;

    [[nodiscard]] static ShapeInfo parse(const std::vector<std::uint8_t>& data);
};

} // namespace libreshockwave::cast
