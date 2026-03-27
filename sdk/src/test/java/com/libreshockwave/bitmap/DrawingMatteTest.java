package com.libreshockwave.bitmap;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

class DrawingMatteTest {

    @Test
    void createMatteUsesSourceAlphaLayer() {
        Bitmap src = new Bitmap(4, 1, 32, new int[] {
            0xFFFFFFFF,
            0x00DDDDDD,
            0xFF000000,
            0x80AA0000
        });

        Bitmap matte = Drawing.createMatte(src);

        assertEquals(0xFFFFFFFF, matte.getPixel(0, 0));
        assertEquals(0x00FFFFFF, matte.getPixel(1, 0));
        assertEquals(0xFFFFFFFF, matte.getPixel(2, 0));
        assertEquals(0x80FFFFFF, matte.getPixel(3, 0));
    }

    @Test
    void createMatteHonorsAlphaThreshold() {
        Bitmap src = new Bitmap(3, 1, 32, new int[] {
            0x7FFFFFFF,
            0x80FFFFFF,
            0xFFFFFFFF
        });

        Bitmap matte = Drawing.createMatte(src, 0x80);

        assertEquals(0x00FFFFFF, matte.getPixel(0, 0));
        assertEquals(0x80FFFFFF, matte.getPixel(1, 0));
        assertEquals(0xFFFFFFFF, matte.getPixel(2, 0));
    }

    @Test
    void matteCopyPixelsKeepsSolidColoredTileContent() {
        Bitmap dest = new Bitmap(1, 1, 32);
        Bitmap src = new Bitmap(1, 1, 32, new int[] { 0xFFD4DDE1 });

        Drawing.copyPixels(dest, src, 0, 0, 0, 0, 1, 1, Palette.InkMode.MATTE, 255);

        assertEquals(0xFFD4DDE1, dest.getPixel(0, 0));
    }

    @Test
    void matteCopyPixelsOnlyRemovesWhiteBoundingPixels() {
        Bitmap dest = new Bitmap(3, 3, 32);
        Bitmap src = new Bitmap(3, 3, 32, new int[] {
            0xFF2A6883, 0xFF2A6883, 0xFF2A6883,
            0xFF2A6883, 0xFFFFFFFF, 0xFF2A6883,
            0xFF2A6883, 0xFF2A6883, 0xFF2A6883
        });

        Drawing.copyPixels(dest, src, 0, 0, 0, 0, 3, 3, Palette.InkMode.MATTE, 255);

        assertEquals(0xFF2A6883, dest.getPixel(0, 0));
        assertEquals(0xFFFFFFFF, dest.getPixel(1, 1));
    }

    @Test
    void matteCopyPixelsKeepsMixed32BitNoWhiteEdgeStripOpaque() {
        Bitmap dest = new Bitmap(5, 1, 32);
        Bitmap src = new Bitmap(5, 1, 32, new int[] {
            0xFF88ADBD, 0xFF88ADBD, 0xFF88ADBD, 0xFF88ADBD, 0xFF000000
        });

        Drawing.copyPixels(dest, src, 0, 0, 0, 0, 5, 1, Palette.InkMode.MATTE, 255);

        assertEquals(0xFF88ADBD, dest.getPixel(0, 0));
        assertEquals(0xFF000000, dest.getPixel(4, 0));
    }
}
