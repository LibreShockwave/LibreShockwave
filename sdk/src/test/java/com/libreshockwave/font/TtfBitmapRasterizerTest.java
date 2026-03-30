package com.libreshockwave.font;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertTrue;

class TtfBitmapRasterizerTest {

    @Test
    void rasterizedGlyphsPreserveLeftSideBearingAndAdvanceWidth() {
        BitmapFont font = TtfBitmapRasterizer.rasterize(
                com.libreshockwave.fonts.windows.Verdana.getData(), 9, "Verdana");

        int canvasW = 32;
        int canvasH = 32;
        int[] pixels = new int[canvasW * canvasH];
        font.drawChar('H', pixels, canvasW, canvasH, 0, 0, 0xFF000000);

        int minX = canvasW;
        int maxX = -1;
        for (int y = 0; y < canvasH; y++) {
            for (int x = 0; x < canvasW; x++) {
                if (((pixels[y * canvasW + x] >>> 24) & 0xFF) != 0) {
                    minX = Math.min(minX, x);
                    maxX = Math.max(maxX, x);
                }
            }
        }

        int inkWidth = maxX - minX + 1;
        assertTrue(maxX >= 0, "glyph should render visible pixels");
        assertTrue(minX > 0, "glyph should preserve its left-side bearing");
        assertTrue(font.getCharWidth('H') > inkWidth,
                "advance width should preserve the font metrics, not collapse to the ink bounds");
    }
}
