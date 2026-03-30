package com.libreshockwave.cast;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ShapeInfoTest {

    @Test
    void parseDirectorExitShapeRetainsOutlineMetadata() {
        byte[] data = new byte[] {
                0x00, 0x01,
                0x00, 0x00,
                0x00, 0x00,
                0x00, 0x39,
                0x00, 0x39,
                0x00, 0x01,
                (byte) 0xF9,
                0x00,
                0x00,
                0x01,
                0x05
        };

        ShapeInfo shapeInfo = ShapeInfo.parse(data);

        assertEquals(ShapeInfo.ShapeType.RECT, shapeInfo.shapeType());
        assertEquals(57, shapeInfo.width());
        assertEquals(57, shapeInfo.height());
        assertEquals(0xF9, shapeInfo.color());
        assertEquals(0, shapeInfo.backColor());
        assertEquals(0, shapeInfo.fillType());
        assertEquals(1, shapeInfo.lineThickness());
        assertEquals(5, shapeInfo.lineDirection());
        assertFalse(shapeInfo.isFilled());
        assertTrue(shapeInfo.isOutlineInvisible());
    }
}
