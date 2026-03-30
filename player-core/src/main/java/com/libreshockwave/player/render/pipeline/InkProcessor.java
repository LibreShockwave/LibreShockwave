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
            || ink == InkMode.NOT_GHOST || ink == InkMode.MATTE || ink == InkMode.ADD_PIN
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
        return applyInk(src, InkMode.fromCode(ink), backColor, useAlpha, palette, skipGraduatedAlpha);
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
        if (src == null || src.getWidth() == 0 || src.getHeight() == 0) {
            return src;
        }

        if (needsFloodFillIsolation(ink, src)) {
            src = Drawing.applyFloodFillTransparency(src);
        }

        if (ink == InkMode.MATTE) {
            // Matte ink: flood-fill from edges
            Drawing.FloodFillMatte matteSpec = resolveMatteSpec(src, ink, backColor, useAlpha, palette);
            if (matteSpec == null) {
                return src;
            }
            return applyMatte(src, matteSpec.matteColorRgb(), matteSpec.tolerance());
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
        Drawing.FloodFillMatte matteSpec = resolveMatteSpec(src, ink, backColor, useAlpha, palette);
        return matteSpec != null ? matteSpec.matteColorRgb() : -1;
    }

    private static Drawing.FloodFillMatte resolveMatteSpec(Bitmap src, InkMode ink, int backColor,
                                              boolean useAlpha, Palette palette) {
        // Native 32-bit alpha drives matte directly; no white-border extraction.
        if (src.hasNativeMatteAlpha() && useAlpha) {
            return null;
        }
        return Drawing.resolveFloodFillMatte(src);
    }

    /**
     * Resolve the background color for ink 36/7/33/35/40/41.
     * Returns -1 if the bitmap has native alpha and should skip processing.
     */
    static int resolveBackColor(Bitmap src, InkMode ink, int backColor,
                                 boolean useAlpha, Palette palette) {
        int bitDepth = src.getBitDepth();

        // Native 32-bit alpha already defines transparency when the sprite uses alpha.
        if (src.hasNativeMatteAlpha() && useAlpha) {
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

        for (int i = 0; i < srcPixels.length; i++) {
            int pixel = srcPixels[i];
            int alpha = (pixel >>> 24) & 0xFF;
            if (alpha == 0) {
                result[i] = 0x00000000; // Preserve already-transparent pixels
                continue;
            }

            int rgb = pixel & 0xFFFFFF;
            if (rgb == bgColorRGB) {
                result[i] = 0x00000000; // Fully transparent
                continue;
            }

            // Director uses exact-match keying here. Anti-aliased near-colors remain
            // opaque and can create halos unless the source uses real alpha.
            result[i] = pixel | 0xFF000000;
        }

        return new Bitmap(w, h, src.getBitDepth(), result);
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

        return new Bitmap(w, h, src.getBitDepth(), result);
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
        return new Bitmap(src.getWidth(), src.getHeight(), src.getBitDepth(), result);
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
            r = (r * tintR) / 255;
            g = (g * tintG) / 255;
            b = (b * tintB) / 255;
            result[i] = (alpha << 24) | (r << 16) | (g << 8) | b;
        }

        return new Bitmap(w, h, src.getBitDepth(), result);
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
        return new Bitmap(w, h, src.getBitDepth(), result);
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

        return new Bitmap(w, h, src.getBitDepth(), result);
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
