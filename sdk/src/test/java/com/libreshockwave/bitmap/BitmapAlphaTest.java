package com.libreshockwave.bitmap;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;

class BitmapAlphaTest {

    @Test
    void nonNativeThirtyTwoBitAlphaIsExposedAsOpaqueAuthoredColor() {
        Bitmap bitmap = new Bitmap(2, 1, 32, new int[] {
                0x00F0F0F0,
                0x80123456
        });

        Bitmap opaque = bitmap.copyWithNonNativeAlphaOpaque();

        assertEquals(0xFFF0F0F0, opaque.getPixel(0, 0));
        assertEquals(0xFF123456, opaque.getPixel(1, 0));
    }

    @Test
    void nativeAlphaBitmapKeepsTransparentPixels() {
        Bitmap bitmap = new Bitmap(1, 1, 32, new int[] {0x00F0F0F0});
        bitmap.setNativeAlpha(true);

        Bitmap result = bitmap.copyWithNonNativeAlphaOpaque();

        assertSame(bitmap, result);
        assertEquals(0x00F0F0F0, result.getPixel(0, 0));
    }

    @Test
    void eightBitRgbFillQuantizesToImagePalette() {
        Bitmap bitmap = new Bitmap(2, 1, 8);
        bitmap.setImagePalette(Palette.SYSTEM_WIN_PALETTE);

        bitmap.fillRect(0, 0, 2, 1, 0xFFEEEEEE);

        assertEquals(0xFFF0F0F0, bitmap.getPixel(0, 0));
        assertEquals(0xFFF0F0F0, bitmap.getPixel(1, 0));
    }

    @Test
    void firstPaletteAssignmentQuantizesExistingEightBitRgbPixels() {
        Bitmap bitmap = new Bitmap(2, 1, 8);
        bitmap.fillRect(0, 0, 2, 1, 0xFFEEEEEE);

        int changed = bitmap.remapImagePalette(Palette.SYSTEM_WIN_PALETTE);

        assertEquals(2, changed);
        assertEquals(0xFFF0F0F0, bitmap.getPixel(0, 0));
        assertEquals(0xFFF0F0F0, bitmap.getPixel(1, 0));
    }

    @Test
    void systemPaletteThirtyTwoBitFillKeepsAuthoredRgb() {
        Bitmap bitmap = new Bitmap(2, 1, 32);
        bitmap.setImagePalette(Palette.SYSTEM_WIN_PALETTE);

        bitmap.fillRect(0, 0, 2, 1, 0xFFEEEEEE);

        assertEquals(0xFFEEEEEE, bitmap.getPixel(0, 0));
        assertEquals(0xFFEEEEEE, bitmap.getPixel(1, 0));
    }

    @Test
    void customPaletteThirtyTwoBitFillKeepsAuthoredRgb() {
        Bitmap bitmap = new Bitmap(2, 1, 32);
        bitmap.setImagePalette(new Palette(new int[] {0x000000, 0xF0F0F0}, "custom"));

        bitmap.fillRect(0, 0, 2, 1, 0xFFEEEEEE);

        assertEquals(0xFFEEEEEE, bitmap.getPixel(0, 0));
        assertEquals(0xFFEEEEEE, bitmap.getPixel(1, 0));
    }
}
