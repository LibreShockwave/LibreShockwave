package com.libreshockwave.cast;

import com.libreshockwave.bitmap.Palette;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

class BitmapInfoTest {

    @Test
    void parseDirectorFiveBitmapInfoUsesD5PaletteFields() {
        byte[] data = new byte[] {
                0x00, 0x18, // pitch
                0x00, 0x02, // top
                0x00, 0x03, // left
                0x00, 0x12, // bottom
                0x00, 0x13, // right
                0x00, 0x00, 0x00, 0x00, // bounding rect
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x07, // regY
                0x00, 0x08, // regX
                0x00,       // padding
                0x08,       // bits per pixel
                0x00, 0x00, // clut cast lib
                (byte) 0xFF, (byte) 0xFA // clut id: -6, palette id -7
        };

        BitmapInfo info = BitmapInfo.parse(data, 1100);

        assertEquals(16, info.width());
        assertEquals(16, info.height());
        assertEquals(8, info.regX());
        assertEquals(7, info.regY());
        assertEquals(8, info.bitDepth());
        assertEquals(Palette.METALLIC, info.paletteId());
        assertEquals(24, info.pitch());
    }
}
