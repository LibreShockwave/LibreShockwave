package com.libreshockwave.cast;

import com.libreshockwave.io.BinaryReader;

import java.nio.ByteOrder;

/**
 * Shape-specific cast member data.
 */
public record ShapeInfo(
    ShapeType shapeType,
    int regX,
    int regY,
    int width,
    int height,
    int color,
    int backColor,
    int fillType,
    int lineThickness,
    int lineDirection
) implements Dimensioned {

    public enum ShapeType {
        RECT(0x01),
        OVAL_RECT(0x02),
        OVAL(0x03),
        LINE(0x08),
        UNKNOWN(0);

        private final int code;

        ShapeType(int code) {
            this.code = code;
        }

        public static ShapeType fromCode(int code) {
            for (ShapeType type : values()) {
                if (type.code == code) return type;
            }
            return UNKNOWN;
        }
    }

    public static ShapeInfo parse(byte[] data) {
        if (data == null || data.length < 14) {
            return new ShapeInfo(ShapeType.UNKNOWN, 0, 0, 0, 0, 0, 0, 0, 1, 0);
        }

        BinaryReader reader = new BinaryReader(data, ByteOrder.BIG_ENDIAN);

        int shapeTypeRaw = reader.readU16();
        int regY = reader.readU16();
        int regX = reader.readU16();
        int height = reader.readU16();
        int width = reader.readU16();
        reader.skip(2); // unknown
        int color = reader.readU8();
        int backColor = data.length >= 14 ? data[13] & 0xFF : 0;
        int fillType = data.length >= 15 ? data[14] & 0xFF : 1;
        int lineThickness = data.length >= 16 ? data[15] & 0xFF : 1;
        int lineDirection = data.length >= 17 ? data[16] & 0xFF : 0;

        return new ShapeInfo(
            ShapeType.fromCode(shapeTypeRaw),
            regX, regY,
            width, height,
            color,
            backColor,
            fillType,
            lineThickness,
            lineDirection
        );
    }

    public boolean isFilled() {
        return fillType != 0;
    }

    public boolean isOutlineInvisible() {
        return !isFilled() && lineThickness <= 1;
    }
}
