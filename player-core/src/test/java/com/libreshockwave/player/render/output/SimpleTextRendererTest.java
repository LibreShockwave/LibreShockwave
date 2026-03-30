package com.libreshockwave.player.render.output;

import com.libreshockwave.bitmap.Bitmap;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class SimpleTextRendererTest {

    @Test
    void underlineFitsWithinAutosizedBitmapFontText() {
        SimpleTextRenderer renderer = new SimpleTextRenderer();

        Bitmap plain = renderer.renderText("Open", 33, 0,
                "Verdana", 9, "plain",
                "left", 0xFF000000, 0x00FFFFFF,
                false, false, 9, 1);
        Bitmap underlined = renderer.renderText("Open", 33, 0,
                "Verdana", 9, "underline",
                "left", 0xFF000000, 0x00FFFFFF,
                false, false, 9, 1);

        int plainLastRow = countOpaquePixelsOnRow(plain, underlined.getHeight() - 1);
        int underlinedLastRow = countOpaquePixelsOnRow(underlined, underlined.getHeight() - 1);

        assertTrue(underlined.getHeight() >= plain.getHeight());
        assertTrue(underlinedLastRow > plainLastRow,
                "expected underline to add pixels on the last row, plain=" + plainLastRow
                        + " underlined=" + underlinedLastRow);
        assertTrue(underlinedLastRow >= 20,
                "expected visible underline coverage on last row, got " + underlinedLastRow);
    }

    @Test
    void underlineSitsBelowBitmapFontGlyphInk() {
        SimpleTextRenderer renderer = new SimpleTextRenderer();

        Bitmap underlined = renderer.renderText("Open", 33, 0,
                "Verdana", 9, "underline",
                "left", 0xFF000000, 0x00FFFFFF,
                false, false, 9, 1);

        int underlineRow = findLastOpaqueRow(underlined);
        int glyphBottom = findOpaqueRowBefore(underlined, underlineRow);

        assertEquals(underlined.getHeight() - 1, underlineRow,
                "expected underline on the bottom row of the auto-sized line box");
        assertEquals(underlineRow - 2, glyphBottom,
                "expected one clear row between glyph ink and underline");
    }

    @Test
    void preservesBlankLinesWhenAutosizingText() {
        SimpleTextRenderer renderer = new SimpleTextRenderer();

        Bitmap singleBreak = renderer.renderText("A\r\nB", 20, 0,
                "Verdana", 9, "plain",
                "left", 0xFF000000, 0x00FFFFFF,
                false, false, 9, 0);
        Bitmap blankLine = renderer.renderText("A\r\n\r\nB", 20, 0,
                "Verdana", 9, "plain",
                "left", 0xFF000000, 0x00FFFFFF,
                false, false, 9, 0);

        assertEquals(singleBreak.getHeight() + 9, blankLine.getHeight(),
                "expected preserved empty line to add one full line advance");
    }

    @Test
    void locToCharPosTreatsLfAsLineBreak() {
        SimpleTextRenderer renderer = new SimpleTextRenderer();

        int charPos = renderer.locToCharPos("A\nB", 0, 9,
                "Verdana", 9, "plain",
                9, "left", 20);

        assertEquals(2, charPos,
                "expected click on second line to map after the LF break");
    }

    @Test
    void findCharLineTreatsCrLfAsSingleLineBreak() {
        assertArrayEquals(new int[]{1, 0}, TextRenderer.findCharLine("A\r\nB", 2));
        assertArrayEquals(new int[]{1, 1}, TextRenderer.findCharLine("A\r\nB", 4));
        assertEquals(3, TextRenderer.lineStartIndex("A\r\nB", 1));
    }

    @Test
    void leftAlignedBitmapFontTextKeepsInkOffTheImageEdge() {
        SimpleTextRenderer renderer = new SimpleTextRenderer();

        Bitmap text = renderer.renderText("How To Get?", 80, 0,
                "Verdana", 9, "plain",
                "left", 0xFFFFFFFF, 0xFF000000,
                false, false, 9, 0);

        assertTrue(findFirstNonBackgroundColumn(text, 0xFF000000) > 0,
                "expected the first glyph to preserve its font bearing instead of touching the image edge");
    }

    private static int countOpaquePixelsOnRow(Bitmap bitmap, int y) {
        int count = 0;
        for (int x = 0; x < bitmap.getWidth(); x++) {
            if (((bitmap.getPixel(x, y) >>> 24) & 0xFF) != 0) {
                count++;
            }
        }
        return count;
    }

    private static int findLastOpaqueRow(Bitmap bitmap) {
        for (int y = bitmap.getHeight() - 1; y >= 0; y--) {
            if (countOpaquePixelsOnRow(bitmap, y) > 0) {
                return y;
            }
        }
        return -1;
    }

    private static int findOpaqueRowBefore(Bitmap bitmap, int beforeY) {
        for (int y = beforeY - 1; y >= 0; y--) {
            if (countOpaquePixelsOnRow(bitmap, y) > 0) {
                return y;
            }
        }
        return -1;
    }

    private static int findFirstNonBackgroundColumn(Bitmap bitmap, int bgColor) {
        for (int x = 0; x < bitmap.getWidth(); x++) {
            for (int y = 0; y < bitmap.getHeight(); y++) {
                if (bitmap.getPixel(x, y) != bgColor) {
                    return x;
                }
            }
        }
        return -1;
    }
}
