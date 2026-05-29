package com.libreshockwave.cast;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

class TextInfoTest {

    @Test
    void parseDirectorFiveCompactTextInfoUsesGutterAlignmentAndBackgroundFields() {
        byte[] data = new byte[] {
                0x00, 0x05, // border, gutter
                0x00, 0x02, // shadow, text type
                0x00, 0x01, // alignment
                (byte) 0xFF, (byte) 0xCC, // background red word
                (byte) 0xFF, (byte) 0xDD, // background green word
                (byte) 0xFF, (byte) 0xEE, // background blue word
                0x00, 0x00, // scroll
                0x00, 0x00, 0x00, 0x00, // initial rect top/left
                0x00, 0x14, 0x00, (byte) 0xAA, // initial rect bottom/right
                0x00, 0x0B, // max height
                0x00, 0x00, 0x00, 0x00 // shadow/flags/text height
        };

        TextInfo info = TextInfo.parse(data);

        assertEquals(1, info.textAlign());
        assertEquals(0xCC, info.bgRed());
        assertEquals(0xDD, info.bgGreen());
        assertEquals(0xEE, info.bgBlue());
        assertEquals(0, info.borderSize());
        assertEquals(5, info.gutterSize());
        assertEquals(11, info.textHeight());
    }
}
