package com.libreshockwave.player.render.output;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.cast.StyledSpan;
import com.libreshockwave.cast.XmedStyledText;
import com.libreshockwave.player.cast.FontRegistry;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
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

        int underlineRow = findLastOpaqueRow(underlined);
        int plainPixelsOnUnderlineRow = countOpaquePixelsOnRow(plain, underlineRow);
        int underlinedPixelsOnUnderlineRow = countOpaquePixelsOnRow(underlined, underlineRow);

        assertEquals(plain.getHeight(), underlined.getHeight());
        assertTrue(underlinedPixelsOnUnderlineRow > plainPixelsOnUnderlineRow,
                "expected underline to add pixels on the glyph-bottom row, plain=" + plainPixelsOnUnderlineRow
                        + " underlined=" + underlinedPixelsOnUnderlineRow);
        assertTrue(underlinedPixelsOnUnderlineRow >= 20,
                "expected visible underline coverage on glyph-bottom row, got " + underlinedPixelsOnUnderlineRow);
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

        assertEquals(underlineRow - 1, glyphBottom,
                "expected underline to sit directly below the prior glyph ink row");
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
    void onePixelDefaultLeadingDoesNotExpandBitmapFontLineAdvance() {
        FontRegistry.clear();
        try {
            FontRegistry.registerFontAlias("V", "Volter", false);
            SimpleTextRenderer renderer = new SimpleTextRenderer();

            Bitmap text = renderer.renderText("Public Spaces\n(0/0)",
                    160, 0,
                    "V", 9, "plain",
                    "left", 0xFF000000, 0x00FFFFFF,
                    false, false, 9, 1);

            assertEquals(19, text.getHeight(),
                    "expected one-pixel default leading to affect image height without adding interline spacing");
        } finally {
            FontRegistry.clear();
        }
    }

    @Test
    void explicitBitmapFontLineSpacingStillExpandsLineAdvance() {
        FontRegistry.clear();
        try {
            FontRegistry.registerFontAlias("V", "Volter", false);
            SimpleTextRenderer renderer = new SimpleTextRenderer();

            Bitmap text = renderer.renderText("Public Spaces\n(0/0)",
                    160, 0,
                    "V", 9, "plain",
                    "left", 0xFF000000, 0x00FFFFFF,
                    false, false, 9, 9);

            assertEquals(36, text.getHeight(),
                    "expected explicit extra leading to keep the larger Director line advance");
        } finally {
            FontRegistry.clear();
        }
    }

    @Test
    void explicitBitmapFontTopSpacingAddsDirectorLeadingBeforeGlyphInk() {
        FontRegistry.clear();
        try {
            FontRegistry.registerFontAlias("vb", "Volter", true);
            SimpleTextRenderer renderer = new SimpleTextRenderer();

            Bitmap text = renderer.renderText("Copyright Habbo Ltd 2001",
                    170, 54,
                    "vb", 9, "plain",
                    "left", 0xFFFFFFFF, 0xFF000000,
                    true, false, 9, 2);

            assertEquals(3, findFirstNonBackgroundRow(text, 0xFF000000),
                    "expected explicit two-pixel topSpacing to reserve Director leading above glyph ink");
        } finally {
            FontRegistry.clear();
        }
    }

    @Test
    void registeredPfrVariantTakesPrecedenceOverBundledDirectorFallback() throws Exception {
        Path v1BoldVolter = Path.of("/opt/git/v1_assets/habbo_entry/raw_chunks/03731_Volter-Bold_GoldFish__3968_XMED.bin");
        if (!Files.isRegularFile(v1BoldVolter)) {
            return;
        }

        FontRegistry.clear();
        try {
            FontRegistry.registerPfr1Font("Volter-Bold (GoldFish)", Files.readAllBytes(v1BoldVolter));
            SimpleTextRenderer renderer = new SimpleTextRenderer();

            Bitmap text = renderer.renderLegacyStxtText("Copyright Habbo Ltd 2001",
                    170, 54,
                    "Volter", 9, "bold",
                    "left", 0xFFFFFFFF, 0xFF000000,
                    true, false, 9, 2);

            assertEquals(153, findLastNonBackgroundColumn(text, 0xFF000000),
                    "expected legacy Volter bold text to use the wider movie-registered PFR metrics");
        } finally {
            FontRegistry.clear();
        }
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
    void charPosToLocUsesDirectorOneBasedCharacterPositions() {
        SimpleTextRenderer renderer = new SimpleTextRenderer();

        int[] first = renderer.charPosToLoc("Go", 1,
                "Courier", 9, "plain",
                10, "left", 0);
        int[] second = renderer.charPosToLoc("Go", 2,
                "Courier", 9, "plain",
                10, "left", 0);
        int[] afterEnd = renderer.charPosToLoc("Go", 3,
                "Courier", 9, "plain",
                10, "left", 0);

        assertEquals(0, first[0]);
        assertEquals(6, second[0]);
        assertEquals(11, afterEnd[0]);
    }

    @Test
    void charPosToLocDoesNotReturnNegativeAlignedOverflow() {
        FontRegistry.clear();
        SimpleTextRenderer renderer = new SimpleTextRenderer();

        int[] afterTitle = renderer.charPosToLoc("Hotel Navigator", 16,
                "VB", 9, "plain",
                10, "center", 50);

        assertTrue(afterTitle[0] >= 90,
                "expected overflowing centered text to measure full content width");
    }

    @Test
    void findCharLineTreatsCrLfAsSingleLineBreak() {
        assertArrayEquals(new int[]{0, 1}, TextRenderer.findCharLine("A\r\nB", 2));
        assertArrayEquals(new int[]{1, 0}, TextRenderer.findCharLine("A\r\nB", 4));
        assertEquals(3, TextRenderer.lineStartIndex("A\r\nB", 1));
    }

    @Test
    void wordWrapCanBreakHyphenatedWordsAtFittingHyphen() {
        java.util.List<String> lines = new java.util.ArrayList<>();

        TextRenderer.wrapLine("Relax! It's faux-fur",
                s -> s.length(),
                "Relax! It's faux-".length(),
                lines);

        assertArrayEquals(new String[]{"Relax! It's faux-", "fur"}, lines.toArray(String[]::new));
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

    @Test
    void directorFontAliasUsesEmbeddedBoldMetrics() {
        FontRegistry.clear();
        try {
            FontRegistry.registerFontAlias("vb", "Volter", true);
            SimpleTextRenderer renderer = new SimpleTextRenderer();

            int[] afterTitle = renderer.charPosToLoc("Hotel Navigator", 16,
                    "vb", 9, "plain",
                    10, "left", 200);

            assertTrue(afterTitle[0] >= 90,
                    "expected Director alias vb to resolve to the wider embedded bold metrics");
        } finally {
            FontRegistry.clear();
        }
    }

    @Test
    void directorFontAliasCanResolveBundledBoldFace() {
        FontRegistry.clear();
        try {
            FontRegistry.registerFontAlias("VB", "Volter", true);
            SimpleTextRenderer renderer = new SimpleTextRenderer();

            Bitmap title = renderer.renderText("Hotel Navigator", 200, 15,
                    "VB", 9, "plain",
                    "left", 0xFFEEEEEE, 0xFF6794A7,
                    false, false, 10, 0);

            assertEquals(284, countPixels(title, 0xFFEEEEEE),
                    "expected Director font alias VB to use the bundled bold face");
        } finally {
            FontRegistry.clear();
        }
    }

    @Test
    void directorFontAliasUsesDirectorSizedMetricsForWrapping() {
        FontRegistry.clear();
        try {
            FontRegistry.registerFontAlias("Volter (goldfish)", "Volter", false);
            SimpleTextRenderer renderer = new SimpleTextRenderer();

            Bitmap text = renderer.renderText("Haven't got a Habbo yet?\rYou can create one here.",
                    175, 24,
                    "Volter (goldfish)", 12, "plain",
                    "center", 0xFF000000, 0x00FFFFFF,
                    true, false, 12, 0);

            assertEquals(24, text.getHeight(),
                    "expected Director alias size 12 to fit the v1 two-line panel text");
        } finally {
            FontRegistry.clear();
        }
    }

    @Test
    void xmedFontCandidatesPreferRegisteredMovieFontOverWindowsFallback() throws Exception {
        Path v1Volter = Path.of("/opt/git/v1_assets/habbo_entry/raw_chunks/03732_Volter_GoldFish__3966_XMED.bin");
        if (!Files.isRegularFile(v1Volter)) {
            return;
        }

        FontRegistry.clear();
        try {
            FontRegistry.registerPfr1Font("Volter (GoldFish)", Files.readAllBytes(v1Volter));
            SimpleTextRenderer renderer = new SimpleTextRenderer();
            XmedStyledText styled = new XmedStyledText(
                    "You can create one here.",
                    List.of(new StyledSpan(0, 24, "Verdana", 9, false, false, false, 0, 0, 0)),
                    List.of("Geneva", "Verdana", "Volter Neue", "Volter (goldfish)"),
                    "left",
                    1,
                    1,
                    2,
                    true,
                    0,
                    175,
                    24,
                    "Verdana",
                    9,
                    false,
                    14,
                    false,
                    0,
                    0,
                    0
            );

            Bitmap text = renderer.renderXmedText(styled, 175, 24, 0xFF000000, 0x00FFFFFF);

            assertEquals(132, findLastNonBackgroundColumn(text, 0x00FFFFFF),
                    "expected the XMED font table to prefer the registered GoldFish pixel font over Verdana fallback");
        } finally {
            FontRegistry.clear();
        }
    }

    @Test
    void xmedMovieFontCandidateMatchesExplicitGoldfishRender() throws Exception {
        Path v1Volter = Path.of("/opt/git/v1_assets/habbo_entry/raw_chunks/03732_Volter_GoldFish__3966_XMED.bin");
        if (!Files.isRegularFile(v1Volter)) {
            return;
        }

        FontRegistry.clear();
        try {
            FontRegistry.registerPfr1Font("Volter (GoldFish)", Files.readAllBytes(v1Volter));
            SimpleTextRenderer renderer = new SimpleTextRenderer();

            XmedStyledText current = new XmedStyledText(
                    "Haven't got a Habbo yet?\rYou can create one here.",
                    List.of(
                            new StyledSpan(0, 44, "Verdana", 9, false, false, false, 0, 0, 0),
                            new StyledSpan(44, 48, "Verdana", 9, false, false, true, 0, 0, 0),
                            new StyledSpan(48, 49, "Verdana", 9, false, false, false, 0, 0, 0)
                    ),
                    List.of("Geneva", "Verdana", "Volter Neue", "Volter (goldfish)"),
                    "center",
                    1,
                    1,
                    2,
                    true,
                    0,
                    175,
                    24,
                    "Verdana",
                    9,
                    false,
                    14,
                    false,
                    0,
                    0,
                    0
            );

            XmedStyledText goldfish = new XmedStyledText(
                    current.text(),
                    List.of(
                            new StyledSpan(0, 44, "Volter (goldfish)", 9, false, false, false, 0, 0, 0),
                            new StyledSpan(44, 48, "Volter (goldfish)", 9, false, false, true, 0, 0, 0),
                            new StyledSpan(48, 49, "Volter (goldfish)", 9, false, false, false, 0, 0, 0)
                    ),
                    List.of("Volter (goldfish)"),
                    "center",
                    1,
                    1,
                    2,
                    true,
                    0,
                    175,
                    24,
                    "Volter (goldfish)",
                    9,
                    false,
                    14,
                    false,
                    0,
                    0,
                    0
            );

            XmedStyledText verdana = new XmedStyledText(
                    current.text(),
                    List.of(
                            new StyledSpan(0, 44, "Verdana", 9, false, false, false, 0, 0, 0),
                            new StyledSpan(44, 48, "Verdana", 9, false, false, true, 0, 0, 0),
                            new StyledSpan(48, 49, "Verdana", 9, false, false, false, 0, 0, 0)
                    ),
                    List.of("Verdana"),
                    "center",
                    1,
                    1,
                    2,
                    true,
                    0,
                    175,
                    24,
                    "Verdana",
                    9,
                    false,
                    14,
                    false,
                    0,
                    0,
                    0
            );

            Bitmap currentBitmap = renderer.renderXmedText(current, 175, 24, 0xFF000000, 0x00FFFFFF);
            Bitmap goldfishBitmap = renderer.renderXmedText(goldfish, 175, 24, 0xFF000000, 0x00FFFFFF);
            Bitmap verdanaBitmap = renderer.renderXmedText(verdana, 175, 24, 0xFF000000, 0x00FFFFFF);

            assertArrayEquals(goldfishBitmap.getPixels(), currentBitmap.getPixels(),
                    "expected XMED movie-font candidate resolution to match the explicit GoldFish render");
            assertFalse(java.util.Arrays.equals(verdanaBitmap.getPixels(), currentBitmap.getPixels()),
                    "expected the resolved render to differ from pure Verdana fallback");
        } finally {
            FontRegistry.clear();
        }
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

    private static int findFirstNonBackgroundRow(Bitmap bitmap, int bgColor) {
        for (int y = 0; y < bitmap.getHeight(); y++) {
            for (int x = 0; x < bitmap.getWidth(); x++) {
                if (bitmap.getPixel(x, y) != bgColor) {
                    return y;
                }
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

    private static int findLastNonBackgroundColumn(Bitmap bitmap, int bgColor) {
        for (int x = bitmap.getWidth() - 1; x >= 0; x--) {
            for (int y = 0; y < bitmap.getHeight(); y++) {
                if (bitmap.getPixel(x, y) != bgColor) {
                    return x;
                }
            }
        }
        return -1;
    }

    private static int countPixels(Bitmap bitmap, int color) {
        int count = 0;
        for (int y = 0; y < bitmap.getHeight(); y++) {
            for (int x = 0; x < bitmap.getWidth(); x++) {
                if (bitmap.getPixel(x, y) == color) {
                    count++;
                }
            }
        }
        return count;
    }
}
