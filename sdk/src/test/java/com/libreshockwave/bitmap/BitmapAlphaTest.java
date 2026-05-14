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
}
