package com.libreshockwave.player.render.pipeline;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.bitmap.Drawing;
import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.id.InkMode;

import java.util.ArrayDeque;
import java.util.Queue;

/**
 * Processes Director ink effects on bitmaps (matte, background transparent, etc.).
 * Operates on {@link Bitmap} (int[] ARGB) — no Swing/AWT dependency.
 */
public final class InkProcessor {

    private InkProcessor() {}

    /**
     * Returns true if the given ink mode requires per-pixel transparency processing.
     */
    public static boolean shouldProcessInk(int ink) {
        return shouldProcessInk(InkMode.fromCode(ink));
    }

    /**
     * Returns true if the given ink mode requires per-pixel transparency processing.
     */
    public static boolean shouldProcessInk(InkMode ink) {
        return ink == InkMode.TRANSPARENT || ink == InkMode.REVERSE || ink == InkMode.GHOST
            || ink == InkMode.NOT_COPY || ink == InkMode.NOT_TRANSPARENT || ink == InkMode.NOT_REVERSE
            || ink == InkMode.NOT_GHOST || ink == InkMode.MATTE || ink == InkMode.MASK || ink == InkMode.ADD_PIN
            || ink == InkMode.ADD || ink == InkMode.SUBTRACT_PIN || ink == InkMode.SUBTRACT
            || ink == InkMode.BACKGROUND_TRANSPARENT || ink == InkMode.BLEND
            || ink == InkMode.LIGHTEN || ink == InkMode.DARKEN;
    }

    /**
     * Apply ink-based transparency to a bitmap.
     *
     * @param src       Source bitmap (ARGB pixels)
     * @param ink       Director ink mode (int code)
     * @param backColor Sprite backColor property
     * @param useAlpha  Whether the bitmap has native alpha channel (from BitmapInfo updateFlags bit 4)
     * @param palette   Resolved palette (may be null for non-paletted bitmaps)
     * @return A new bitmap with ink applied, or {@code src} unchanged if no processing needed
     */
    public static Bitmap applyInk(Bitmap src, int ink, int backColor,
                                   boolean useAlpha, Palette palette) {
        return applyInk(src, InkMode.fromCode(ink), backColor, useAlpha, palette, false);
    }

    public static Bitmap applyInk(Bitmap src, int ink, int backColor,
                                   boolean useAlpha, Palette palette, boolean skipGraduatedAlpha) {
        return applyInk(src, InkMode.fromCode(ink), backColor, useAlpha, palette, skipGraduatedAlpha, false);
    }

    public static Bitmap applyInkPreservingOutlinedWhiteBody(Bitmap src, int ink, int backColor,
                                   boolean useAlpha, Palette palette, boolean skipGraduatedAlpha) {
        return applyInk(src, InkMode.fromCode(ink), backColor, useAlpha, palette, skipGraduatedAlpha, true);
    }

    /**
     * Apply ink-based transparency to a bitmap.
     *
     * @param src       Source bitmap (ARGB pixels)
     * @param ink       Director ink mode
     * @param backColor Sprite backColor property
     * @param useAlpha  Whether the bitmap has native alpha channel (from BitmapInfo updateFlags bit 4)
     * @param palette   Resolved palette (may be null for non-paletted bitmaps)
     * @return A new bitmap with ink applied, or {@code src} unchanged if no processing needed
     */
    public static Bitmap applyInk(Bitmap src, InkMode ink, int backColor,
                                   boolean useAlpha, Palette palette) {
        return applyInk(src, ink, backColor, useAlpha, palette, false);
    }

    public static Bitmap applyInk(Bitmap src, InkMode ink, int backColor,
                                   boolean useAlpha, Palette palette, boolean skipGraduatedAlpha) {
        return applyInk(src, ink, backColor, useAlpha, palette, skipGraduatedAlpha, false);
    }

    private static Bitmap applyInk(Bitmap src, InkMode ink, int backColor,
                                   boolean useAlpha, Palette palette, boolean skipGraduatedAlpha,
                                   boolean preserveScriptOutlinedWhiteBody) {
        if (src == null || src.getWidth() == 0 || src.getHeight() == 0) {
            return src;
        }

        if (needsFloodFillIsolation(ink, src)) {
            src = Drawing.applyFloodFillTransparency(src);
        }

        if (ink == InkMode.MATTE) {
            // Matte ink: flood-fill from edges
            Drawing.FloodFillMatte matteSpec = resolveMatteSpec(src, ink, backColor, useAlpha, palette,
                    preserveScriptOutlinedWhiteBody);
            if (matteSpec == null) {
                return src;
            }
            Bitmap outlined = applyOutlinedWhiteBodyMatteIfNeeded(src, matteSpec, preserveScriptOutlinedWhiteBody);
            if (outlined != null) {
                return outlined;
            }
            if (matteSpec.usesPaletteIndex()) {
                return applyIndexedMatte(src, matteSpec.mattePaletteIndex());
            }
            return applyMatte(src, matteSpec.matteColorRgb(), matteSpec.tolerance());
        } else if (ink == InkMode.MASK) {
            // Mask ink derives sprite opacity from source brightness. Convert the
            // source luma into alpha so colored masks render consistently.
            return applyMask(src);
        } else if (ink == InkMode.TRANSPARENT || ink == InkMode.REVERSE
                || ink == InkMode.GHOST || ink == InkMode.NOT_COPY
                || ink == InkMode.NOT_TRANSPARENT || ink == InkMode.NOT_REVERSE) {
            // Inks 1-6: color-key transparency on white (background color).
            // Director's Transparent ink makes white/background pixels transparent.
            // For 1-bit bitmaps, palette index 0 = white = background = transparent.
            int bgColor = resolveBackColor(src, ink, backColor, useAlpha, palette);
            if (bgColor < 0) {
                return src;
            }
            return applyBackgroundTransparent(src, bgColor);
        } else if (ink == InkMode.DARKEN || ink == InkMode.LIGHTEN) {
            // DARKEN (41): matte background, multiply remaining pixels by bgColor, standard alpha composite.
            // LIGHTEN (40): matte background, MAX compositing (handled in renderer).
            // >=16-bit: color-key instead of matte (matte leaks through 1px gaps in composite images).
            Bitmap masked;
            int matteColor;
            if (src.getBitDepth() == 32 && !useAlpha) {
                // Director preserves opaque white pixels in 32-bit non-alpha buffers for
                // DARKEN/LIGHTEN. The sprite bgColor acts as the tint/filter color; it does
                // not first key out white background content.
                masked = src;
            } else if (src.getBitDepth() >= 16) {
                matteColor = resolveMatteColor(src, ink, backColor, useAlpha, palette);
                if (matteColor >= 0) {
                    masked = applyBackgroundTransparent(src, matteColor, skipGraduatedAlpha);
                } else {
                    // useAlpha=true: alpha channel handles transparency, skip matte
                    masked = src;
                }
            } else {
                matteColor = resolveMatteColor(src, ink, backColor, useAlpha, palette);
                if (matteColor >= 0) {
                    masked = applyMatte(src, matteColor);
                } else {
                    masked = src;
                }
            }

            // DARKEN: multiply opaque pixels by resolved bgColor (tint/colorize).
            // Director's Darken ink tints the sprite via multiplication with bgColor,
            // then composites with standard alpha blend — NOT per-channel MIN like Darkest (39).
            // Always resolve tint with useAlpha=false so the bgColor is never skipped.
            if (ink == InkMode.DARKEN) {
                int tintRgb = resolveBackColor(src, ink, backColor, false, palette);
                if (tintRgb >= 0 && tintRgb != 0xFFFFFF) {
                    masked = multiplyColor(masked, tintRgb);
                }
            }
            return masked;
        } else if (ink == InkMode.NOT_GHOST || ink == InkMode.ADD_PIN
                || ink == InkMode.ADD || ink == InkMode.SUBTRACT_PIN || ink == InkMode.SUBTRACT
                || ink == InkMode.BACKGROUND_TRANSPARENT || ink == InkMode.BLEND) {
            // Background transparent / not-ghost / etc: color-key
            int bgColor = resolveBackColor(src, ink, backColor, useAlpha, palette);
            if (bgColor < 0) {
                return src; // 32-bit with useAlpha — skip processing
            }
            return applyBackgroundTransparent(src, bgColor, skipGraduatedAlpha);
        }

        return src;
    }

    private static boolean needsFloodFillIsolation(InkMode ink, Bitmap src) {
        if (src.getPaletteIndices() == null) {
            return false;
        }
        return ink == InkMode.ADD_PIN || ink == InkMode.ADD;
    }

    /**
     * Resolve the matte color for ink 8 (Matte).
     * Returns -1 if the bitmap has native alpha and should skip processing.
     */
    static int resolveMatteColor(Bitmap src, InkMode ink, int backColor,
                                  boolean useAlpha, Palette palette) {
        Drawing.FloodFillMatte matteSpec = resolveMatteSpec(src, ink, backColor, useAlpha, palette, false);
        return matteSpec != null ? matteSpec.matteColorRgb() : -1;
    }

    private static Drawing.FloodFillMatte resolveMatteSpec(Bitmap src, InkMode ink, int backColor,
                                              boolean useAlpha, Palette palette,
                                              boolean preserveScriptOutlinedWhiteBody) {
        // Native 32-bit alpha drives matte directly; no white-border extraction.
        if (src.hasNativeMatteAlpha() && useAlpha) {
            return null;
        }
        if (preserveScriptOutlinedWhiteBody
                && ink == InkMode.MATTE && src.getBitDepth() == 32 && src.isScriptModified() && !src.isNativeAlpha()
                && hasOpaqueBorderColor(src, 0xFFFFFF)) {
            return new Drawing.FloodFillMatte(0xFFFFFF, 0);
        }
        if (src.getBitDepth() == 32 && !src.isScriptModified()) {
            return new Drawing.FloodFillMatte(0xFFFFFF, 0);
        }
        if ((ink == InkMode.DARKEN || ink == InkMode.LIGHTEN) && src.isRectangularMedia()) {
            return null;
        }
        Drawing.FloodFillMatte explicitIndexedMatte = resolveExplicitIndexedMatteSpec(src, backColor, palette);
        if (explicitIndexedMatte != null) {
            return explicitIndexedMatte;
        }
        return Drawing.resolveFloodFillMatte(src);
    }

    private static Drawing.FloodFillMatte resolveExplicitIndexedMatteSpec(Bitmap src, int backColor,
                                                                          Palette palette) {
        if (src == null || src.getBitDepth() > 8) {
            return null;
        }
        byte[] paletteIndices = src.getPaletteIndices();
        if (paletteIndices == null || paletteIndices.length < src.getWidth() * src.getHeight()) {
            return null;
        }

        int matteRgb = resolveBackColorIgnoringAlpha(src, backColor,
                palette != null ? palette : src.getImagePalette()) & 0xFFFFFF;
        if (matteRgb != 0xFFFFFF) {
            return null;
        }

        int matteIndex = explicitIndexedMatteIndex(src, paletteIndices, matteRgb,
                palette != null ? palette : src.getImagePalette());
        if (matteIndex < 0 || isUniformPaletteIndex(paletteIndices, matteIndex)) {
            return null;
        }
        if (!hasOpaqueNonPaletteIndexContent(src.getPixels(), paletteIndices, matteIndex)) {
            return null;
        }
        return new Drawing.FloodFillMatte(matteIndex, matteRgb, 0);
    }

    private static int explicitIndexedMatteIndex(Bitmap src, byte[] paletteIndices, int matteRgb,
                                                 Palette palette) {
        if (indexMatchesRgb(src, paletteIndices, palette, 0, matteRgb)
                && edgeContainsOpaquePaletteIndex(src.getPixels(), paletteIndices,
                        src.getWidth(), src.getHeight(), 0)) {
            return 0;
        }

        int[] counts = new int[256];
        for (int index : iterateEdgeIndices(src.getWidth(), src.getHeight())) {
            if (((src.getPixels()[index] >>> 24) & 0xFF) == 0) {
                continue;
            }
            int paletteIndex = paletteIndices[index] & 0xFF;
            if (indexMatchesRgb(src, paletteIndices, palette, paletteIndex, matteRgb)) {
                counts[paletteIndex]++;
            }
        }

        int bestIndex = -1;
        int bestCount = 0;
        for (int i = 0; i < counts.length; i++) {
            if (counts[i] > bestCount) {
                bestIndex = i;
                bestCount = counts[i];
            }
        }
        return bestIndex;
    }

    private static boolean indexMatchesRgb(Bitmap src, byte[] paletteIndices, Palette palette,
                                           int paletteIndex, int rgb) {
        int indexRgb = palette != null && paletteIndex >= 0 && paletteIndex < palette.size()
                ? palette.getColor(paletteIndex) & 0xFFFFFF
                : resolvePaletteIndexRgb(src.getPixels(), paletteIndices, paletteIndex);
        return indexRgb == (rgb & 0xFFFFFF);
    }

    private static Iterable<Integer> iterateEdgeIndices(int w, int h) {
        java.util.List<Integer> indices = new java.util.ArrayList<>(
                Math.max(1, (w * 2) + Math.max(0, h - 2) * 2));
        for (int x = 0; x < w; x++) {
            indices.add(x);
            if (h > 1) {
                indices.add((h - 1) * w + x);
            }
        }
        for (int y = 1; y < h - 1; y++) {
            indices.add(y * w);
            if (w > 1) {
                indices.add(y * w + (w - 1));
            }
        }
        return indices;
    }

    /**
     * Resolve the background color for ink 36/7/33/35/40/41.
     * Returns -1 if the bitmap has native alpha and should skip processing.
     */
    static int resolveBackColor(Bitmap src, InkMode ink, int backColor,
                                 boolean useAlpha, Palette palette) {
        int bitDepth = src.getBitDepth();

        // Native 32-bit alpha usually defines transparency when the sprite uses
        // alpha. Some Director assets still carry an opaque background-color
        // rim in BACKGROUND_TRANSPARENT ink; key that border color as well so
        // stale matte pixels do not render as white seams.
        if (src.hasNativeMatteAlpha() && useAlpha) {
            int alphaBackColor = resolveBackColorIgnoringAlpha(src, backColor, palette);
            if (ink == InkMode.BACKGROUND_TRANSPARENT && hasOpaqueBorderColor(src, alphaBackColor)) {
                return alphaBackColor;
            }
            return -1;
        }

        // Packed RGB value
        if (backColor > 255) {
            return backColor & 0xFFFFFF;
        }

        // 32-bit without alpha and non-Copy inks historically key against white.
        // Using authored-content heuristics here can erase real black outlines and
        // other UI pixels that Director preserves.
        if (bitDepth == 32 && !useAlpha && ink != InkMode.COPY) {
            return 0xFFFFFF;
        }

        // Resolve palette index through the actual palette.
        // Director backColor is a palette index — the RGB depends on which palette
        // is active. Using the bitmap's own palette ensures the resolved RGB matches
        // the decoded pixel data for correct color-key transparency.
        if (palette != null && backColor >= 0 && backColor < palette.size()) {
            return palette.getColor(backColor) & 0xFFFFFF;
        }

        // Fallback: Director grayscale ramp (0 = white, 255 = black)
        int gray = 255 - backColor;
        return (gray << 16) | (gray << 8) | gray;
    }

    private static int resolveBackColorIgnoringAlpha(Bitmap src, int backColor, Palette palette) {
        if (backColor > 255) {
            return backColor & 0xFFFFFF;
        }
        if (palette != null && backColor >= 0 && backColor < palette.size()) {
            return palette.getColor(backColor) & 0xFFFFFF;
        }
        int gray = 255 - backColor;
        return (gray << 16) | (gray << 8) | gray;
    }

    private static boolean hasOpaqueBorderColor(Bitmap src, int colorRgb) {
        int w = src.getWidth();
        int h = src.getHeight();
        for (int x = 0; x < w; x++) {
            if (isOpaqueColor(src.getPixel(x, 0), colorRgb)) return true;
            if (isOpaqueColor(src.getPixel(x, h - 1), colorRgb)) return true;
        }
        for (int y = 1; y < h - 1; y++) {
            if (isOpaqueColor(src.getPixel(0, y), colorRgb)) return true;
            if (isOpaqueColor(src.getPixel(w - 1, y), colorRgb)) return true;
        }
        return false;
    }

    private static boolean isOpaqueColor(int argb, int colorRgb) {
        return ((argb >>> 24) & 0xFF) == 0xFF && (argb & 0xFFFFFF) == colorRgb;
    }

    /**
     * Apply Background Transparent ink: pixels matching bgColorRGB become fully transparent.
     */
    static Bitmap applyBackgroundTransparent(Bitmap src, int bgColorRGB) {
        return applyBackgroundTransparent(src, bgColorRGB, false);
    }

    static Bitmap applyBackgroundTransparent(Bitmap src, int bgColorRGB, boolean skipGraduatedAlpha) {
        int w = src.getWidth();
        int h = src.getHeight();
        int[] srcPixels = src.getPixels();
        int[] result = new int[w * h];
        byte[] paletteIndices = src.getPaletteIndices();
        boolean keyDefaultIndexedMatte = shouldKeyDefaultIndexedMatte(src, bgColorRGB, paletteIndices);

        for (int i = 0; i < srcPixels.length; i++) {
            int pixel = srcPixels[i];
            int alpha = (pixel >>> 24) & 0xFF;
            if (alpha == 0) {
                result[i] = 0x00000000; // Preserve already-transparent pixels
                continue;
            }

            int rgb = pixel & 0xFFFFFF;
            if (keyDefaultIndexedMatte && i < paletteIndices.length && (paletteIndices[i] & 0xFF) == 0) {
                result[i] = 0x00000000;
                continue;
            }
            if (rgb == bgColorRGB) {
                result[i] = 0x00000000; // Fully transparent
                continue;
            }

            // Director uses exact-match keying here. Anti-aliased near-colors remain
            // opaque and can create halos unless the source uses real alpha.
            result[i] = pixel | 0xFF000000;
        }

        return newDerivedBitmap(src, result);
    }

    private static boolean shouldKeyDefaultIndexedMatte(Bitmap src, int backgroundKeyRgb, byte[] paletteIndices) {
        if (src == null || src.getBitDepth() > 8 || (backgroundKeyRgb & 0xFFFFFF) != 0xFFFFFF
                || paletteIndices == null || paletteIndices.length < src.getWidth() * src.getHeight()) {
            return false;
        }
        if (isUniformPaletteIndex(paletteIndices, 0)) {
            return false;
        }

        int[] pixels = src.getPixels();
        int w = src.getWidth();
        int h = src.getHeight();
        if (!edgeContainsOpaquePaletteIndex(pixels, paletteIndices, w, h, 0)) {
            return false;
        }

        int indexZeroRgb = resolvePaletteIndexRgb(pixels, paletteIndices, 0);
        return isNearWhiteGrayscale(indexZeroRgb, 232, 16)
                && hasOpaqueNonPaletteIndexContent(pixels, paletteIndices, 0);
    }

    private static boolean isUniformPaletteIndex(byte[] paletteIndices, int paletteIndex) {
        for (byte paletteEntry : paletteIndices) {
            if ((paletteEntry & 0xFF) != paletteIndex) {
                return false;
            }
        }
        return true;
    }

    private static boolean edgeContainsOpaquePaletteIndex(int[] pixels, byte[] paletteIndices,
                                                          int w, int h, int paletteIndex) {
        for (int x = 0; x < w; x++) {
            int top = x;
            int bottom = (h - 1) * w + x;
            if (isOpaquePaletteIndex(pixels, paletteIndices, top, paletteIndex)
                    || isOpaquePaletteIndex(pixels, paletteIndices, bottom, paletteIndex)) {
                return true;
            }
        }
        for (int y = 1; y < h - 1; y++) {
            int left = y * w;
            int right = y * w + (w - 1);
            if (isOpaquePaletteIndex(pixels, paletteIndices, left, paletteIndex)
                    || isOpaquePaletteIndex(pixels, paletteIndices, right, paletteIndex)) {
                return true;
            }
        }
        return false;
    }

    private static boolean isOpaquePaletteIndex(int[] pixels, byte[] paletteIndices, int index, int paletteIndex) {
        return index >= 0 && index < pixels.length && index < paletteIndices.length
                && ((pixels[index] >>> 24) & 0xFF) != 0
                && (paletteIndices[index] & 0xFF) == paletteIndex;
    }

    private static int resolvePaletteIndexRgb(int[] pixels, byte[] paletteIndices, int paletteIndex) {
        for (int i = 0; i < pixels.length && i < paletteIndices.length; i++) {
            if ((paletteIndices[i] & 0xFF) == paletteIndex) {
                return pixels[i] & 0xFFFFFF;
            }
        }
        return 0xFFFFFF;
    }

    private static boolean hasOpaqueNonPaletteIndexContent(int[] pixels, byte[] paletteIndices, int paletteIndex) {
        for (int i = 0; i < pixels.length && i < paletteIndices.length; i++) {
            if (((pixels[i] >>> 24) & 0xFF) != 0
                    && (paletteIndices[i] & 0xFF) != paletteIndex) {
                return true;
            }
        }
        return false;
    }

    private static boolean isNearWhiteGrayscale(int rgb, int minChannel, int maxDelta) {
        int r = (rgb >> 16) & 0xFF;
        int g = (rgb >> 8) & 0xFF;
        int b = rgb & 0xFF;
        return r >= minChannel && g >= minChannel && b >= minChannel
                && Math.abs(r - g) <= maxDelta
                && Math.abs(g - b) <= maxDelta
                && Math.abs(r - b) <= maxDelta;
    }

    static Bitmap applyMask(Bitmap src) {
        int w = src.getWidth();
        int h = src.getHeight();
        int[] srcPixels = src.getPixels();
        int[] result = new int[w * h];

        for (int i = 0; i < srcPixels.length; i++) {
            int pixel = srcPixels[i];
            int srcAlpha = (pixel >>> 24) & 0xFF;
            if (srcAlpha == 0) {
                result[i] = 0x00000000;
                continue;
            }

            int maskAlpha = Drawing.maskAlphaFromPixel(pixel);
            int combinedAlpha = (srcAlpha * maskAlpha) / 255;
            result[i] = (combinedAlpha << 24) | (pixel & 0xFFFFFF);
        }

        return newDerivedBitmap(src, result);
    }

    /**
     * Apply Matte ink (8): BFS flood-fill from image edges to remove border-connected
     * pixels matching matteColorRGB. Interior pixels of the same color are preserved.
     */
    static Bitmap applyMatte(Bitmap src, int matteColorRGB) {
        return applyMatte(src, matteColorRGB, 0);
    }

    static Bitmap applyMatte(Bitmap src, int matteColorRGB, int tolerance) {
        int w = src.getWidth();
        int h = src.getHeight();
        int[] pixels = src.getPixels();
        boolean[] transparent = new boolean[w * h];
        Queue<Integer> queue = new ArrayDeque<>();

        // Seed border pixels matching matte color
        for (int x = 0; x < w; x++) {
            seedMatte(pixels, transparent, queue, x, 0, w, matteColorRGB, tolerance);
            seedMatte(pixels, transparent, queue, x, h - 1, w, matteColorRGB, tolerance);
        }
        for (int y = 1; y < h - 1; y++) {
            seedMatte(pixels, transparent, queue, 0, y, w, matteColorRGB, tolerance);
            seedMatte(pixels, transparent, queue, w - 1, y, w, matteColorRGB, tolerance);
        }

        // Flood-fill from seeds
        while (!queue.isEmpty()) {
            int idx = queue.poll();
            int px = idx % w;
            int py = idx / w;
            if (px > 0)     seedMatte(pixels, transparent, queue, px - 1, py, w, matteColorRGB, tolerance);
            if (px < w - 1) seedMatte(pixels, transparent, queue, px + 1, py, w, matteColorRGB, tolerance);
            if (py > 0)     seedMatte(pixels, transparent, queue, px, py - 1, w, matteColorRGB, tolerance);
            if (py < h - 1) seedMatte(pixels, transparent, queue, px, py + 1, w, matteColorRGB, tolerance);
        }

        int[] result = new int[w * h];
        for (int i = 0; i < pixels.length; i++) {
            if (transparent[i]) {
                result[i] = 0x00000000;
            } else {
                result[i] = pixels[i];
            }
        }

        return newDerivedBitmap(src, result);
    }

    static Bitmap applyIndexedMatte(Bitmap src, int mattePaletteIndex) {
        byte[] paletteIndices = src.getPaletteIndices();
        if (paletteIndices == null || paletteIndices.length != src.getWidth() * src.getHeight()) {
            return src;
        }
        int w = src.getWidth();
        int h = src.getHeight();
        int[] pixels = src.getPixels();
        int[] result = java.util.Arrays.copyOf(pixels, pixels.length);
        boolean[] transparent = new boolean[pixels.length];
        Queue<Integer> queue = new ArrayDeque<>();
        int matteIndex = mattePaletteIndex & 0xFF;

        for (int x = 0; x < w; x++) {
            seedIndexedMatte(pixels, paletteIndices, transparent, queue, x, 0, w, matteIndex);
            seedIndexedMatte(pixels, paletteIndices, transparent, queue, x, h - 1, w, matteIndex);
        }
        for (int y = 1; y < h - 1; y++) {
            seedIndexedMatte(pixels, paletteIndices, transparent, queue, 0, y, w, matteIndex);
            seedIndexedMatte(pixels, paletteIndices, transparent, queue, w - 1, y, w, matteIndex);
        }

        while (!queue.isEmpty()) {
            int idx = queue.remove();
            int x = idx % w;
            int y = idx / w;
            if (x > 0) {
                seedIndexedMatte(pixels, paletteIndices, transparent, queue, x - 1, y, w, matteIndex);
            }
            if (x + 1 < w) {
                seedIndexedMatte(pixels, paletteIndices, transparent, queue, x + 1, y, w, matteIndex);
            }
            if (y > 0) {
                seedIndexedMatte(pixels, paletteIndices, transparent, queue, x, y - 1, w, matteIndex);
            }
            if (y + 1 < h) {
                seedIndexedMatte(pixels, paletteIndices, transparent, queue, x, y + 1, w, matteIndex);
            }
        }

        for (int i = 0; i < pixels.length; i++) {
            if (transparent[i] || ((pixels[i] >>> 24) & 0xFF) == 0) {
                result[i] = 0x00000000;
            }
        }
        return newDerivedBitmap(src, result);
    }

    private static void seedIndexedMatte(int[] pixels, byte[] paletteIndices, boolean[] transparent,
                                         Queue<Integer> queue, int x, int y, int w, int matteIndex) {
        int idx = y * w + x;
        if (transparent[idx] || ((pixels[idx] >>> 24) & 0xFF) == 0) {
            return;
        }
        if ((paletteIndices[idx] & 0xFF) != matteIndex) {
            return;
        }
        transparent[idx] = true;
        queue.add(idx);
    }

    private static Bitmap applyOutlinedWhiteBodyMatteIfNeeded(Bitmap src, Drawing.FloodFillMatte matteSpec,
                                                              boolean allowScriptBuilt32Bit) {
        if (src == null || matteSpec == null || matteSpec.matteColorRgb() != 0xFFFFFF) {
            return null;
        }
        int bitDepth = src.getBitDepth();
        boolean lowDepthAsset = bitDepth >= 1 && bitDepth <= 8 && !src.isScriptModified();
        boolean scriptBuilt32Bit = allowScriptBuilt32Bit
                && bitDepth == 32 && src.isScriptModified() && !src.isNativeAlpha();
        if (!lowDepthAsset && !scriptBuilt32Bit) {
            return null;
        }

        int[] pixels = src.getPixels();
        int white = 0;
        int dark = 0;
        int gray = 0;
        int colored = 0;
        for (int pixel : pixels) {
            if (((pixel >>> 24) & 0xFF) == 0) {
                continue;
            }
            int rgb = pixel & 0xFFFFFF;
            int r = (rgb >> 16) & 0xFF;
            int g = (rgb >> 8) & 0xFF;
            int b = rgb & 0xFF;
            if (Math.abs(r - g) > 4 || Math.abs(g - b) > 4) {
                if (!scriptBuilt32Bit) {
                    return null;
                }
                colored++;
                continue;
            }
            if (rgb == 0xFFFFFF) {
                white++;
            } else if (rgb <= 0x303030) {
                dark++;
            } else {
                gray++;
            }
        }

        int nonWhite = dark + gray + colored;
        if (white == 0 || nonWhite == 0 || !outlineTouchesMultipleEdges(src)) {
            return null;
        }
        if (scriptBuilt32Bit && white < nonWhite) {
            return null;
        }

        Bitmap plain = matteSpec.usesPaletteIndex()
                ? applyIndexedMatte(src, matteSpec.mattePaletteIndex())
                : applyMatte(src, matteSpec.matteColorRgb(), matteSpec.tolerance());
        int remainingWhite = 0;
        for (int pixel : plain.getPixels()) {
            if (((pixel >>> 24) & 0xFF) != 0 && (pixel & 0xFFFFFF) == 0xFFFFFF) {
                remainingWhite++;
            }
        }
        if (remainingWhite * 20 > white) {
            return null;
        }

        return applyOutlinedWhiteBodyMatte(src);
    }

    private static boolean outlineTouchesMultipleEdges(Bitmap src) {
        int w = src.getWidth();
        int h = src.getHeight();
        if (w <= 0 || h <= 0) {
            return false;
        }
        int touchedEdges = 0;
        boolean top = false;
        boolean bottom = false;
        boolean left = false;
        boolean right = false;

        for (int x = 0; x < w; x++) {
            top |= isOpaqueNonWhite(src.getPixel(x, 0));
            bottom |= isOpaqueNonWhite(src.getPixel(x, h - 1));
        }
        for (int y = 0; y < h; y++) {
            left |= isOpaqueNonWhite(src.getPixel(0, y));
            right |= isOpaqueNonWhite(src.getPixel(w - 1, y));
        }

        if (top) touchedEdges++;
        if (bottom) touchedEdges++;
        if (left) touchedEdges++;
        if (right) touchedEdges++;
        return touchedEdges >= 3 || (w == 1 && top && bottom) || (h == 1 && left && right);
    }

    private static boolean isOpaqueNonWhite(int pixel) {
        return ((pixel >>> 24) & 0xFF) != 0 && (pixel & 0xFFFFFF) != 0xFFFFFF;
    }

    private static Bitmap applyOutlinedWhiteBodyMatte(Bitmap src) {
        int w = src.getWidth();
        int h = src.getHeight();
        int[] pixels = src.getPixels();
        boolean[] barrier = new boolean[pixels.length];
        boolean[] dilated = new boolean[pixels.length];
        boolean[] outside = new boolean[pixels.length];
        Queue<Integer> queue = new ArrayDeque<>();

        for (int i = 0; i < pixels.length; i++) {
            int pixel = pixels[i];
            if (((pixel >>> 24) & 0xFF) != 0 && (pixel & 0xFFFFFF) != 0xFFFFFF) {
                barrier[i] = true;
            }
        }

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int idx = y * w + x;
                if (!barrier[idx]) {
                    continue;
                }
                for (int ny = Math.max(0, y - 1); ny <= Math.min(h - 1, y + 1); ny++) {
                    for (int nx = Math.max(0, x - 1); nx <= Math.min(w - 1, x + 1); nx++) {
                        dilated[ny * w + nx] = true;
                    }
                }
            }
        }

        for (int x = 0; x < w; x++) {
            seedOutlinedBackground(pixels, dilated, outside, queue, x, 0, w);
            seedOutlinedBackground(pixels, dilated, outside, queue, x, h - 1, w);
        }
        for (int y = 1; y < h - 1; y++) {
            seedOutlinedBackground(pixels, dilated, outside, queue, 0, y, w);
            seedOutlinedBackground(pixels, dilated, outside, queue, w - 1, y, w);
        }

        while (!queue.isEmpty()) {
            int idx = queue.remove();
            int x = idx % w;
            int y = idx / w;
            if (x > 0) seedOutlinedBackground(pixels, dilated, outside, queue, x - 1, y, w);
            if (x + 1 < w) seedOutlinedBackground(pixels, dilated, outside, queue, x + 1, y, w);
            if (y > 0) seedOutlinedBackground(pixels, dilated, outside, queue, x, y - 1, w);
            if (y + 1 < h) seedOutlinedBackground(pixels, dilated, outside, queue, x, y + 1, w);
        }

        int[] result = java.util.Arrays.copyOf(pixels, pixels.length);
        for (int i = 0; i < pixels.length; i++) {
            if (!barrier[i] && outside[i]) {
                result[i] = 0x00000000;
            }
        }
        return newDerivedBitmap(src, result);
    }

    private static void seedOutlinedBackground(int[] pixels, boolean[] dilatedBarrier, boolean[] outside,
                                               Queue<Integer> queue, int x, int y, int w) {
        int idx = y * w + x;
        if (outside[idx] || dilatedBarrier[idx] || ((pixels[idx] >>> 24) & 0xFF) == 0) {
            return;
        }
        outside[idx] = true;
        queue.add(idx);
    }

    private static void seedMatte(int[] pixels, boolean[] transparent, Queue<Integer> queue,
                                   int x, int y, int w, int matteRgb, int tolerance) {
        int idx = y * w + x;
        if (!transparent[idx] && isTransparentOrMatte(pixels[idx], matteRgb, tolerance)) {
            transparent[idx] = true;
            queue.add(idx);
        }
    }

    private static boolean isTransparentOrMatte(int pixel, int matteRgb, int tolerance) {
        return ((pixel >>> 24) & 0xFF) == 0 || matchesRgb(pixel, matteRgb, tolerance);
    }

    private static boolean matchesRgb(int pixel, int matteRgb, int tolerance) {
        int pr = (pixel >> 16) & 0xFF;
        int pg = (pixel >> 8) & 0xFF;
        int pb = pixel & 0xFF;
        int mr = (matteRgb >> 16) & 0xFF;
        int mg = (matteRgb >> 8) & 0xFF;
        int mb = matteRgb & 0xFF;
        return Math.abs(pr - mr) <= tolerance
                && Math.abs(pg - mg) <= tolerance
                && Math.abs(pb - mb) <= tolerance;
    }

    /**
     * Convert opaque white (0xFFFFFFFF) pixels to transparent white (0x00FFFFFF).
     * Used for DARKEN/LIGHTEN ink on script-modified 32-bit canvases: Director's
     * image(w,h,32) creates opaque white, but DARKEN needs to skip the white
     * background during matte while preserving non-white content for bgColor multiply.
     */
    public static Bitmap convertOpaqueWhiteToTransparent(Bitmap src) {
        int[] srcPixels = src.getPixels();
        int[] result = new int[srcPixels.length];
        boolean changed = false;
        for (int i = 0; i < srcPixels.length; i++) {
            if (srcPixels[i] == 0xFFFFFFFF) {
                result[i] = 0x00FFFFFF;
                changed = true;
            } else {
                result[i] = srcPixels[i];
            }
        }
        if (!changed) return src;
        return newDerivedBitmap(src, result);
    }

    /**
     * Multiply each opaque pixel's RGB by a tint color (normalized multiply blend).
     * Used by DARKEN ink (41) to colorize the sprite with bgColor before compositing.
     */
    static Bitmap multiplyColor(Bitmap src, int tintRgb) {
        int tintR = (tintRgb >> 16) & 0xFF;
        int tintG = (tintRgb >> 8) & 0xFF;
        int tintB = tintRgb & 0xFF;

        int w = src.getWidth();
        int h = src.getHeight();
        int[] srcPixels = src.getPixels();
        int[] result = new int[w * h];

        for (int i = 0; i < srcPixels.length; i++) {
            int alpha = (srcPixels[i] >>> 24);
            if (alpha == 0) {
                result[i] = 0;
                continue;
            }
            int r = (srcPixels[i] >> 16) & 0xFF;
            int g = (srcPixels[i] >> 8) & 0xFF;
            int b = srcPixels[i] & 0xFF;
            r = multiplyDirectorChannel(r, tintR);
            g = multiplyDirectorChannel(g, tintG);
            b = multiplyDirectorChannel(b, tintB);
            result[i] = (alpha << 24) | (r << 16) | (g << 8) | b;
        }

        return newDerivedBitmap(src, result);
    }

    private static int multiplyDirectorChannel(int src, int tint) {
        if (tint >= 255) {
            return src;
        }
        if (src >= 255) {
            return tint;
        }
        return (src * tint) >> 8;
    }

    static Bitmap applyDarkenForeColorOffset(Bitmap src, int foreColor) {
        if (src == null) {
            return null;
        }
        int foreR = (foreColor >> 16) & 0xFF;
        int foreG = (foreColor >> 8) & 0xFF;
        int foreB = foreColor & 0xFF;

        int[] srcPixels = src.getPixels();
        int[] result = new int[srcPixels.length];
        for (int i = 0; i < srcPixels.length; i++) {
            int alpha = (srcPixels[i] >>> 24) & 0xFF;
            if (alpha == 0) {
                result[i] = 0x00000000;
                continue;
            }
            int r = Math.min(255, ((srcPixels[i] >> 16) & 0xFF) + foreR);
            int g = Math.min(255, ((srcPixels[i] >> 8) & 0xFF) + foreG);
            int b = Math.min(255, (srcPixels[i] & 0xFF) + foreB);
            result[i] = (alpha << 24) | (r << 16) | (g << 8) | b;
        }
        return newDerivedBitmap(src, result);
    }

    /**
     * Replace all pixels exactly matching fromRgb with toRgb, preserving alpha.
     */
    public static Bitmap remapExactColor(Bitmap src, int fromRgb, int toRgb) {
        int w = src.getWidth();
        int h = src.getHeight();
        int[] pixels = src.getPixels();
        int[] result = new int[w * h];
        for (int i = 0; i < pixels.length; i++) {
            if ((pixels[i] & 0xFFFFFF) == fromRgb) {
                result[i] = (pixels[i] & 0xFF000000) | toRgb;
            } else {
                result[i] = pixels[i];
            }
        }
        return newDerivedBitmap(src, result);
    }

    /**
     * Apply Director's sprite-level foreColor/backColor colorization.
     * <p>
     * In Director, bitmap sprites with Copy (0) or Matte (8) ink have their pixels
     * remapped based on the sprite's foreColor and backColor properties. This is how
     * the window system creates dark backgrounds from white bitmap buffers:
     * <ul>
     *   <li>White pixels (grayscale 255) → foreColor</li>
     *   <li>Black pixels (grayscale 0) → backColor</li>
     *   <li>Gray pixels → interpolated between backColor and foreColor</li>
     * </ul>
     * This mimics Director's paletted bitmap behavior where palette index 0 (white)
     * maps to foreColor and index 255 (black) maps to backColor.
     *
     * @param src       Source bitmap (ARGB pixels)
     * @param foreColor Sprite foreColor as packed RGB (e.g., 0x000000 for black)
     * @param backColor Sprite backColor as packed RGB (e.g., 0xFFFFFF for white)
     * @return A new bitmap with colorization applied
     */
    public static Bitmap applyForeColorRemap(Bitmap src, int foreColor, int backColor) {
        if (src == null || src.getWidth() == 0 || src.getHeight() == 0) {
            return src;
        }

        // Skip if foreColor=BLACK and backColor=WHITE (identity remap for most images)
        // But NOT for paletted-style bitmaps where white→foreColor is important.
        // We always apply to ensure window system bitmaps get correct colors.

        int fr = (foreColor >> 16) & 0xFF;
        int fg = (foreColor >> 8) & 0xFF;
        int fb = foreColor & 0xFF;
        int br = (backColor >> 16) & 0xFF;
        int bg = (backColor >> 8) & 0xFF;
        int bb = backColor & 0xFF;

        int w = src.getWidth();
        int h = src.getHeight();
        int[] srcPixels = src.getPixels();
        int[] result = new int[w * h];

        for (int i = 0; i < srcPixels.length; i++) {
            int alpha = (srcPixels[i] >>> 24);
            if (alpha == 0) {
                result[i] = 0;
                continue;
            }

            int r = (srcPixels[i] >> 16) & 0xFF;
            int g = (srcPixels[i] >> 8) & 0xFF;
            int b = srcPixels[i] & 0xFF;

            // Grayscale intensity: 0=black, 255=white
            int gray = (r + g + b) / 3;

            // Director's palette remap: black (gray=0) → foreColor, white (gray=255) → backColor
            // In Director's paletted bitmap model:
            //   palette index 255 = BLACK (foreground content) → remapped to foreColor
            //   palette index 0 = WHITE (background) → remapped to backColor
            // t = gray/255: 0=black→foreColor, 1=white→backColor
            float t = gray / 255.0f;
            int nr = (int) ((1 - t) * fr + t * br + 0.5f);
            int ng = (int) ((1 - t) * fg + t * bg + 0.5f);
            int nb = (int) ((1 - t) * fb + t * bb + 0.5f);

            result[i] = (alpha << 24) | (nr << 16) | (ng << 8) | nb;
        }

        return newDerivedBitmap(src, result);
    }

    /**
     * Apply sprite-level color remapping using the source bitmap's preserved palette indices.
     * This is used after MATTE masking so indexed furni layers can keep Director's edge
     * transparency while still tinting the remaining pixels from black→foreColor / white→backColor.
     */
    static Bitmap applyIndexedColorRemap(Bitmap indexedSource, Bitmap maskedSource,
                                         int foreColor, int backColor) {
        if (indexedSource == null || maskedSource == null
                || indexedSource.getWidth() != maskedSource.getWidth()
                || indexedSource.getHeight() != maskedSource.getHeight()) {
            return maskedSource;
        }
        byte[] paletteIndices = indexedSource.getPaletteIndices();
        if (paletteIndices == null || paletteIndices.length != maskedSource.getPixels().length) {
            return maskedSource;
        }

        int fr = (foreColor >> 16) & 0xFF;
        int fg = (foreColor >> 8) & 0xFF;
        int fb = foreColor & 0xFF;
        int br = (backColor >> 16) & 0xFF;
        int bg = (backColor >> 8) & 0xFF;
        int bb = backColor & 0xFF;

        int[] maskPixels = maskedSource.getPixels();
        int[] result = new int[maskPixels.length];

        for (int i = 0; i < maskPixels.length; i++) {
            int alpha = (maskPixels[i] >>> 24) & 0xFF;
            if (alpha == 0) {
                result[i] = 0x00000000;
                continue;
            }

            int paletteIndex = paletteIndices[i] & 0xFF;
            float t = (255 - paletteIndex) / 255.0f;
            int nr = Math.round((1 - t) * fr + t * br);
            int ng = Math.round((1 - t) * fg + t * bg);
            int nb = Math.round((1 - t) * fb + t * bb);

            result[i] = (alpha << 24) | (nr << 16) | (ng << 8) | nb;
        }

        Bitmap remapped = new Bitmap(maskedSource.getWidth(), maskedSource.getHeight(),
                maskedSource.getBitDepth(), result);
        remapped.copyPaletteMetadataFrom(maskedSource);
        return remapped;
    }

    private static Bitmap newDerivedBitmap(Bitmap src, int[] pixels) {
        Bitmap derived = new Bitmap(src.getWidth(), src.getHeight(), src.getBitDepth(), pixels);
        derived.copyPaletteMetadataFrom(src);
        return derived;
    }

    /**
     * Returns true if the given ink mode supports sprite-level foreColor/backColor colorization.
     * Only Copy ink (0) supports colorization. Matte ink (8/9) is for transparency only —
     * applying colorization to Matte sprites incorrectly remaps colored bitmap content
     * (e.g., window chrome teal becomes dark gray when foreColor=BLACK).
     */
    public static boolean allowsColorize(int ink) {
        return ink == 0;
    }

    /**
     * Returns true if the given ink mode supports sprite-level foreColor/backColor colorization.
     */
    public static boolean allowsColorize(InkMode ink) {
        return ink == InkMode.COPY;
    }
}
