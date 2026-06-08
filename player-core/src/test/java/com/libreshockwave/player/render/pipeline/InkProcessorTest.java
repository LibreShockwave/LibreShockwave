package com.libreshockwave.player.render.pipeline;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.id.InkMode;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

class InkProcessorTest {

    @Test
    void maskInkIsHandledBySpriteInkPipeline() {
        assertEquals(true, InkProcessor.shouldProcessInk(InkMode.MASK));
    }

    @Test
    void maskInkTurnsPoolWaterColorIntoTranslucentAlpha() {
        Bitmap src = new Bitmap(3, 1, 32, new int[] {
            0xFF009999,
            0xFFFFFFFF,
            0x80009999
        });

        Bitmap result = InkProcessor.applyInk(src, InkMode.MASK, 0, false, null);

        assertEquals(0x6B009999, result.getPixel(0, 0));
        assertEquals(0xFFFFFFFF, result.getPixel(1, 0));
        assertEquals(0x35009999, result.getPixel(2, 0));
    }

    @Test
    void backgroundTransparentUsesExactMatchWithoutRecoveringNearColorAlpha() {
        Bitmap src = new Bitmap(3, 1, 32, new int[] {
            0xFFFFFFFF,
            0xFFC8C8C8,
            0xFF000000
        });

        Bitmap result = InkProcessor.applyBackgroundTransparent(src, 0xFFFFFF);

        assertEquals(0x00000000, result.getPixel(0, 0));
        assertEquals(0xFFC8C8C8, result.getPixel(1, 0));
        assertEquals(0xFF000000, result.getPixel(2, 0));
    }

    @Test
    void backgroundTransparentKeepsExactColorKeyForNon32BitBitmaps() {
        Bitmap src = new Bitmap(2, 1, 8, new int[] {
            0xFFFFFFFF,
            0xFF336699
        });

        Bitmap result = InkProcessor.applyBackgroundTransparent(src, 0xFFFFFF);

        assertEquals(0x00000000, result.getPixel(0, 0));
        assertEquals(0xFF336699, result.getPixel(1, 0));
    }

    @Test
    void backgroundTransparentKeysIndexedNearWhiteMatteSlotWithoutErasingDuplicateRgb() {
        Bitmap src = new Bitmap(3, 3, 8, new int[] {
            0xFFEFEFEF, 0xFF6794A7, 0xFFEFEFEF,
            0xFF6794A7, 0xFFEFEFEF, 0xFF6794A7,
            0xFFEFEFEF, 0xFF6794A7, 0xFFEFEFEF
        });
        src.setPaletteIndices(new byte[] {
            0, 1, 0,
            1, 5, 1,
            0, 1, 0
        });

        Bitmap result = InkProcessor.applyBackgroundTransparent(src, 0xFFFFFF);

        assertEquals(0x00000000, result.getPixel(0, 0));
        assertEquals(0xFF6794A7, result.getPixel(1, 0));
        assertEquals(0xFFEFEFEF, result.getPixel(1, 1));
        assertEquals(0x00000000, result.getPixel(2, 2));
    }

    @Test
    void backgroundTransparentKeepsOpaqueColored32BitUiPixels() {
        Bitmap src = new Bitmap(2, 1, 32, new int[] {
            0xFFFFFFFF,
            0xFF7B9498
        });

        Bitmap result = InkProcessor.applyBackgroundTransparent(src, 0xFFFFFF);

        assertEquals(0x00000000, result.getPixel(0, 0));
        assertEquals(0xFF7B9498, result.getPixel(1, 0));
    }

    @Test
    void backgroundTransparentUsesWhiteKeyColorFor32BitBitmapWithoutNativeAlpha() {
        Bitmap src = new Bitmap(3, 1, 32, new int[] {
            0xFF000000,
            0xFFFFFFFF,
            0xFFF5A000
        });

        int bg = InkProcessor.resolveBackColor(src, InkMode.BACKGROUND_TRANSPARENT, 0, false, null);
        Bitmap result = InkProcessor.applyBackgroundTransparent(src, bg);

        assertEquals(0xFFFFFF, bg);
        assertEquals(0xFF000000, result.getPixel(0, 0));
        assertEquals(0x00000000, result.getPixel(1, 0));
        assertEquals(0xFFF5A000, result.getPixel(2, 0));
    }

    @Test
    void backgroundTransparentKeysOpaqueBorderColorEvenWithNativeAlpha() {
        Bitmap src = new Bitmap(3, 2, 32, new int[] {
            0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
            0x00000000, 0xFF336699, 0x00000000
        });
        src.setNativeAlpha(true);

        Bitmap result = InkProcessor.applyInk(src, InkMode.BACKGROUND_TRANSPARENT,
                0xFFFFFF, true, null);

        assertEquals(0x00000000, result.getPixel(0, 0));
        assertEquals(0xFF336699, result.getPixel(1, 1));
    }

    @Test
    void darkenKeepsOpaqueWhitePixelsFor32BitBitmapWithoutNativeAlpha() {
        Bitmap src = new Bitmap(3, 1, 32, new int[] {
            0xFFFFFFFF,
            0xFF808080,
            0xFF000000
        });

        Bitmap result = InkProcessor.applyInk(src, InkMode.DARKEN, 0x80C040, false, null);

        assertEquals(0xFF80C040, result.getPixel(0, 0));
        assertEquals(0xFF406020, result.getPixel(1, 0));
        assertEquals(0xFF000000, result.getPixel(2, 0));
    }

    @Test
    void darkenPreservesChannelsWithFullTint() {
        Bitmap src = new Bitmap(1, 1, 32, new int[] { 0xFFE88543 });

        Bitmap result = InkProcessor.applyInk(src, InkMode.DARKEN, 0xFFCC66, false, null);

        assertEquals(0xFFE8691A, result.getPixel(0, 0));
    }

    @Test
    void darkenKeepsIndexedRectangularMediaEdgesOpaque() {
        Bitmap src = new Bitmap(4, 4, 8, new int[] {
            0xFFFFCC00, 0xFFFFCC00, 0xFFFFCC00, 0xFFFFCC00,
            0xFFFFCC00, 0xFFFFFFFF, 0xFFCCCCCC, 0xFFFFCC00,
            0xFFFFCC00, 0xFF000000, 0xFFFFFFFF, 0xFFFFCC00,
            0xFFFFCC00, 0xFFFFCC00, 0xFFFFCC00, 0xFFFFCC00
        });
        src.setPaletteIndices(new byte[] {
            (byte) 200, (byte) 200, (byte) 200, (byte) 200,
            (byte) 200, 0, 1, (byte) 200,
            (byte) 200, (byte) 255, 0, (byte) 200,
            (byte) 200, (byte) 200, (byte) 200, (byte) 200
        });
        src.setRectangularMedia(true);

        Bitmap result = InkProcessor.applyInk(src, InkMode.DARKEN, 0xFFCC66, false, null);

        assertEquals(0xFF, result.getPixel(0, 0) >>> 24);
        assertEquals(0xFF, result.getPixel(3, 3) >>> 24);
        assertEquals(0xFF, result.getPixel(1, 1) >>> 24);
    }

    @Test
    void matteRecoversAlphaFromAntialiased32BitEdges() {
        Bitmap src = new Bitmap(4, 1, 32, new int[] {
            0xFFFFFFFF,
            0x00DDDDDD,
            0xFF000000,
            0xFFFFFFFF
        });

        Bitmap result = InkProcessor.applyMatte(src, 0xFFFFFF);

        assertEquals(0x00000000, result.getPixel(0, 0));
        assertEquals(0x00000000, result.getPixel(1, 0));
        assertEquals(0xFF000000, result.getPixel(2, 0));
        assertEquals(0x00000000, result.getPixel(3, 0));
    }

    @Test
    void matteSkipsWhiteFloodFillForNativeAlpha32BitImages() {
        Bitmap src = new Bitmap(3, 1, 32, new int[] {
            0xFFFFFFFF,
            0x40000000,
            0x00FFFFFF
        });
        src.setNativeAlpha(true);

        int matte = InkProcessor.resolveMatteColor(src, InkMode.MATTE, 0, true, null);
        Bitmap result = InkProcessor.applyInk(src, InkMode.MATTE, 0, true, null);

        assertEquals(-1, matte);
        assertEquals(0xFFFFFFFF, result.getPixel(0, 0));
        assertEquals(0x40000000, result.getPixel(1, 0));
        assertEquals(0x00FFFFFF, result.getPixel(2, 0));
    }

    @Test
    void matteKeepsSolidColored32BitTile() {
        Bitmap src = new Bitmap(1, 1, 32, new int[] { 0xFFD4DDE1 });

        Bitmap result = InkProcessor.applyMatte(src, InkProcessor.resolveMatteColor(src, null, 0, false, null));

        assertEquals(0xFFD4DDE1, result.getPixel(0, 0));
    }

    @Test
    void matteKeepsSolidUniformDarkBitmapOpaque() {
        Bitmap src = new Bitmap(3, 3, 32, new int[] {
            0xFF020304, 0xFF020304, 0xFF020304,
            0xFF020304, 0xFF020304, 0xFF020304,
            0xFF020304, 0xFF020304, 0xFF020304
        });

        Bitmap result = InkProcessor.applyInk(src, InkMode.MATTE, 0, false, null);

        assertEquals(0xFF020304, result.getPixel(0, 0));
        assertEquals(0xFF020304, result.getPixel(1, 1));
        assertEquals(0xFF020304, result.getPixel(2, 2));
    }


    @Test
    void matteOnlyRemovesWhiteBoundingPixelsForMixed32BitBitmap() {
        Bitmap src = new Bitmap(3, 3, 32, new int[] {
            0xFF2A6883, 0xFF2A6883, 0xFF2A6883,
            0xFF2A6883, 0xFFFFFFFF, 0xFF2A6883,
            0xFF2A6883, 0xFF2A6883, 0xFF2A6883
        });

        int matte = InkProcessor.resolveMatteColor(src, null, 0, false, null);
        Bitmap result = InkProcessor.applyMatte(src, matte);

        assertEquals(0xFFFFFF, matte);
        assertEquals(0xFF2A6883, result.getPixel(0, 0));
        assertEquals(0xFFFFFFFF, result.getPixel(1, 1));
    }

    @Test
    void matteFallsBackToWhiteWhenMixed32BitBitmapHasNoWhiteContent() {
        Bitmap src = new Bitmap(5, 1, 32, new int[] {
            0xFF88ADBD, 0xFF88ADBD, 0xFF88ADBD, 0xFF88ADBD, 0xFF000000
        });

        int matte = InkProcessor.resolveMatteColor(src, null, 0, false, null);
        Bitmap result = InkProcessor.applyMatte(src, matte);

        assertEquals(0xFFFFFF, matte);
        assertEquals(0xFF88ADBD, result.getPixel(0, 0));
        assertEquals(0xFF000000, result.getPixel(4, 0));
    }

    @Test
    void matteIgnoresSpriteBackColorAndStillRemovesWhiteBorder() {
        Bitmap src = new Bitmap(3, 3, 32, new int[] {
            0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
            0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF,
            0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
        });

        Bitmap result = InkProcessor.applyInk(src, InkMode.MATTE, 0x6794A7, false, null);

        assertEquals(0x00000000, result.getPixel(0, 0));
        assertEquals(0xFF000000, result.getPixel(1, 1));
        assertEquals(0x00000000, result.getPixel(2, 2));
    }

    @Test
    void matteUsesWhiteKeyForPalettedBitmapEvenWhenPaletteSlotZeroIsNotWhite() {
        Bitmap src = new Bitmap(3, 3, 8, new int[] {
            0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
            0xFFFFFFFF, 0xFF00AA00, 0xFFFFFFFF,
            0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
        });
        Palette palette = new Palette(new int[] {
            0xFF7B5005,
            0xFFFFFFFF
        }, "test-matte");

        int matte = InkProcessor.resolveMatteColor(src, InkMode.MATTE, 0, false, palette);
        Bitmap result = InkProcessor.applyInk(src, InkMode.MATTE, 0, false, palette);

        assertEquals(0xFFFFFF, matte);
        assertEquals(0x00000000, result.getPixel(0, 0));
        assertEquals(0xFF00AA00, result.getPixel(1, 1));
        assertEquals(0x00000000, result.getPixel(2, 2));
    }

    @Test
    void matteKeepsNonDefaultDominantIndexedEdgeColorOpaque() {
        Bitmap src = new Bitmap(4, 4, 8, new int[] {
            0xFFFFCC00, 0xFFFFCC00, 0xFFFFCC00, 0xFFFFCC00,
            0xFFFFCC00, 0xFFFFFFFF, 0xFFCCCCCC, 0xFFFFCC00,
            0xFFFFCC00, 0xFF000000, 0xFFFFFFFF, 0xFFFFCC00,
            0xFFFFCC00, 0xFFFFCC00, 0xFFFFCC00, 0xFFFFCC00
        });
        src.setPaletteIndices(new byte[] {
            (byte) 200, (byte) 200, (byte) 200, (byte) 200,
            (byte) 200, 0, 1, (byte) 200,
            (byte) 200, (byte) 255, 0, (byte) 200,
            (byte) 200, (byte) 200, (byte) 200, (byte) 200
        });

        Bitmap result = InkProcessor.applyInk(src, InkMode.MATTE, 0, false, null);

        assertEquals(0xFFFFCC00, result.getPixel(0, 0));
        assertEquals(0xFFFFFFFF, result.getPixel(1, 1));
        assertEquals(0xFFCCCCCC, result.getPixel(2, 1));
        assertEquals(0xFF000000, result.getPixel(1, 2));
        assertEquals(0xFFFFFFFF, result.getPixel(2, 2));
        assertEquals(0xFFFFCC00, result.getPixel(3, 3));
    }

    @Test
    void matteUsesExplicitWhiteBackColorForIndexedWindowShadowBuffers() {
        Bitmap src = new Bitmap(5, 5, 8, new int[] {
            0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000,
            0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000,
            0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000,
            0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000,
            0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFF000000
        });
        src.setPaletteIndices(new byte[] {
            1, 1, 1, 1, 1,
            1, 0, 0, 0, 1,
            1, 0, 1, 0, 1,
            1, 0, 0, 0, 1,
            1, 1, 0, 1, 1
        });
        Palette palette = new Palette(new int[] {
            0xFFFFFFFF,
            0xFF000000
        }, "interface palette");
        src.setImagePalette(palette);
        src.markScriptModified();

        Bitmap result = InkProcessor.applyInk(src, InkMode.MATTE, 0xFFFFFF, false, palette);

        assertEquals(0xFF000000, result.getPixel(0, 0));
        assertEquals(0x00000000, result.getPixel(1, 1));
        assertEquals(0xFF000000, result.getPixel(2, 2));
        assertEquals(0x00000000, result.getPixel(2, 4));
    }

    @Test
    void mattePreservesWhiteBodyForOutlinedIndexedArt() {
        Bitmap src = new Bitmap(6, 5, 8, new int[] {
            0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFFFFFFFF,
            0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFFEEEEEE, 0xFFEEEEEE, 0xFF000000,
            0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFBBBBBB, 0xFFBBBBBB, 0xFF000000,
            0xFF000000, 0xFFBBBBBB, 0xFFBBBBBB, 0xFF000000, 0xFF000000, 0xFFFFFFFF,
            0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
        });
        src.setPaletteIndices(new byte[] {
            0, 0, 0, (byte) 255, (byte) 255, 0,
            0, (byte) 255, (byte) 255, 17, 17, (byte) 255,
            (byte) 255, 0, 0, 68, 68, (byte) 255,
            (byte) 255, 68, 68, (byte) 255, (byte) 255, 0,
            0, (byte) 255, (byte) 255, 0, 0, 0
        });

        Bitmap result = InkProcessor.applyInk(src, InkMode.MATTE, 0, false, null);

        assertEquals(0x00000000, result.getPixel(0, 0));
        assertEquals(0xFFFFFFFF, result.getPixel(1, 2));
        assertEquals(0xFFFFFFFF, result.getPixel(2, 2));
        assertEquals(0xFFBBBBBB, result.getPixel(3, 2));
        assertEquals(0xFF000000, result.getPixel(0, 2));
    }

    @Test
    void mattePreservesWhiteBodyAndTextForScriptBuilt32BitChatBubble() {
        Bitmap src = scriptBuilt32BitBubbleFixture();

        Bitmap result = InkProcessor.applyInkPreservingOutlinedWhiteBody(src,
                InkMode.MATTE.code(), 0, false, null, false);

        assertEquals(0x00000000, result.getPixel(0, 9));
        assertEquals(0xFFFFFFFF, result.getPixel(10, 4));
        assertEquals(0xFF000000, result.getPixel(6, 4));
        assertEquals(0xFF3A8ABF, result.getPixel(2, 2));
        assertEquals(0xFF000000, result.getPixel(9, 9));
    }

    @Test
    void defaultMatteDoesNotPreserveWhiteBodyForGenericScriptBuilt32BitUi() {
        Bitmap src = scriptBuilt32BitBubbleFixture();

        Bitmap result = InkProcessor.applyInk(src, InkMode.MATTE, 0, false, null);

        assertEquals(0x00000000, result.getPixel(10, 4));
        assertEquals(0xFF000000, result.getPixel(6, 4));
        assertEquals(0xFF3A8ABF, result.getPixel(2, 2));
    }

    private static Bitmap scriptBuilt32BitBubbleFixture() {
        Bitmap src = new Bitmap(20, 10, 32);
        src.fill(0xFFFFFFFF);

        for (int x = 4; x <= 15; x++) {
            src.setPixel(x, 0, 0xFF000000);
        }
        for (int y = 1; y <= 7; y++) {
            src.setPixel(0, y, 0xFF000000);
            src.setPixel(19, y, 0xFF000000);
        }
        for (int x = 0; x <= 7; x++) {
            src.setPixel(x, 7, 0xFF000000);
        }
        for (int x = 12; x <= 19; x++) {
            src.setPixel(x, 7, 0xFF000000);
        }
        src.setPixel(9, 8, 0xFF000000);
        src.setPixel(10, 8, 0xFF000000);
        src.setPixel(9, 9, 0xFF000000);
        src.setPixel(10, 9, 0xFF000000);

        src.setPixel(2, 2, 0xFF3A8ABF);
        for (int x = 6; x <= 9; x++) {
            src.setPixel(x, 4, 0xFF000000);
        }
        src.setPixel(6, 5, 0xFF000000);
        src.setPixel(9, 5, 0xFF000000);
        src.markScriptModified();
        return src;
    }

    @Test
    void indexedColorRemapUsesOriginalPaletteIndicesAfterMatteMasking() {
        Bitmap raw = new Bitmap(3, 1, 8, new int[] {
            0xFFFFFFFF,
            0xFF7B5005,
            0xFF000000
        });
        raw.setPaletteIndices(new byte[] {0, (byte) 128, (byte) 255});

        Bitmap masked = new Bitmap(3, 1, 8, new int[] {
            0x00000000,
            0xFF7B5005,
            0xFF000000
        });

        Bitmap remapped = InkProcessor.applyIndexedColorRemap(raw, masked, 0x000000, 0x33CC66);

        assertEquals(0x00000000, remapped.getPixel(0, 0));
        assertEquals(0xFF196633, remapped.getPixel(1, 0));
        assertEquals(0xFF000000, remapped.getPixel(2, 0));
    }

    @Test
    void addPinTreatsEdgeConnectedPaletteZeroAsBackgroundForIndexedBitmaps() {
        Bitmap src = new Bitmap(3, 3, 8, new int[] {
            0xFF000000, 0xFF000000, 0xFF000000,
            0xFF000000, 0xFF6E6E6E, 0xFF000000,
            0xFF000000, 0xFF000000, 0xFF000000
        });
        src.setPaletteIndices(new byte[] {
            0, 0, 0,
            0, (byte) 145, 0,
            0, 0, 0
        });

        Bitmap result = InkProcessor.applyInk(src, InkMode.ADD_PIN, 0, false, null);

        assertEquals(0x00000000, result.getPixel(0, 0));
        assertEquals(0xFF6E6E6E, result.getPixel(1, 1));
        assertEquals(0x00000000, result.getPixel(2, 2));
    }

    @Test
    void addPinKeepsInteriorPaletteZeroIslandsOpaque() {
        Bitmap src = new Bitmap(5, 5, 8, new int[] {
            0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000,
            0xFF000000, 0xFF202020, 0xFF202020, 0xFF202020, 0xFF000000,
            0xFF000000, 0xFF202020, 0xFF000000, 0xFF202020, 0xFF000000,
            0xFF000000, 0xFF202020, 0xFF202020, 0xFF202020, 0xFF000000,
            0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000
        });
        src.setPaletteIndices(new byte[] {
            0, 0, 0, 0, 0,
            0, 7, 7, 7, 0,
            0, 7, 0, 7, 0,
            0, 7, 7, 7, 0,
            0, 0, 0, 0, 0
        });

        Bitmap isolated = InkProcessor.applyInk(src, InkMode.ADD_PIN, 0, false, null);

        assertEquals(0x00000000, isolated.getPixel(0, 0));
        assertEquals(0xFF000000, isolated.getPixel(2, 2));
        assertEquals(0xFF202020, isolated.getPixel(1, 1));
    }

    @Test
    void addPinLeavesRgbBlackBackgroundPixelsOpaque() {
        Bitmap src = new Bitmap(2, 1, 32, new int[] {
            0xFF000000,
            0xFF6E6E6E
        });

        Bitmap result = InkProcessor.applyInk(src, InkMode.ADD_PIN, 0, false, null);

        assertEquals(0xFF000000, result.getPixel(0, 0));
        assertEquals(0xFF6E6E6E, result.getPixel(1, 0));
    }
}
