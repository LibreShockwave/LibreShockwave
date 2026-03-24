package com.libreshockwave.player.render.pipeline;

import com.libreshockwave.bitmap.Bitmap;
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

        if (ink == InkMode.MATTE) {
            // Matte ink: flood-fill from edges
            int matteColor = resolveMatteColor(src, ink, backColor, useAlpha, palette);
            if (matteColor < 0) {
                return src; // 32-bit with useAlpha — skip processing
            }
            // Director: sprite bgColor replaces matte color (typically white) before
            // transparency processing. A white bitmap with bgColor=someColor appears
            // as a solid colored rectangle — all white becomes bgColor, matte finds
            // nothing to remove. When bgColor is default white, this is a no-op.
            if (backColor > 255) {
                int bgRgb = backColor & 0xFFFFFF;
                if (bgRgb != matteColor) {
                    src = remapExactColor(src, matteColor, bgRgb);
                }
            }
            return applyMatte(src, matteColor);
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
            if (src.getBitDepth() >= 16) {
                int matteColor = resolveMatteColor(src, ink, backColor, useAlpha, palette);
                if (matteColor < 0) return src;
                masked = applyBackgroundTransparent(src, matteColor, skipGraduatedAlpha);
            } else {
                int matteColor = resolveMatteColor(src, ink, backColor, useAlpha, palette);
                if (matteColor < 0) return src;
                masked = applyMatte(src, matteColor);
            }

            // DARKEN: multiply opaque pixels by resolved bgColor (tint/colorize).
            // Director's Darken ink tints the sprite via multiplication with bgColor,
            // then composites with standard alpha blend — NOT per-channel MIN like Darkest (39).
            if (ink == InkMode.DARKEN) {
                int tintRgb = resolveBackColor(src, ink, backColor, useAlpha, palette);
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

    /**
     * Resolve the matte color for ink 8 (Matte).
     * Returns -1 if the bitmap has native alpha and should skip processing.
     */
    static int resolveMatteColor(Bitmap src, InkMode ink, int backColor,
                                  boolean useAlpha, Palette palette) {
        int bitDepth = src.getBitDepth();

        // 32-bit with native alpha: skip matte entirely
        if (bitDepth == 32 && useAlpha) {
            return -1;
        }

        // Paletted (bitDepth <= 8): use palette index 0
        if (bitDepth <= 8 && palette != null) {
            return palette.getColor(0) & 0xFFFFFF;
        }

        // Director-authored 32-bit MATTE assets fall into two buckets:
        // text/icons on a matte-colored backing, and solid-color swatches used
        // to build panels. Mixed-color assets should matte out their edge color
        // (typically the top-left background), while solid swatches must not be
        // erased entirely just because their top-left pixel matches the fill.
        if (bitDepth == 32) {
            return resolve32BitMatteColor(src);
        }

        // 16-bit or other: white
        return 0xFFFFFF;
    }

    private static int resolve32BitMatteColor(Bitmap src) {
        int[] pixels = src.getPixels();
        int firstOpaque = -1;
        boolean hasWhite = false;
        for (int pixel : pixels) {
            if (((pixel >>> 24) & 0xFF) == 0) {
                continue;
            }
            int rgb = pixel & 0xFFFFFF;
            if (rgb == 0xFFFFFF) {
                hasWhite = true;
            }
            if (firstOpaque < 0) {
                firstOpaque = rgb;
            }
        }
        int topLeft = src.getPixel(0, 0) & 0xFFFFFF;
        if (hasWhite && topLeft != 0xFFFFFF) {
            return topLeft;
        }
        return 0xFFFFFF;
    }

    /**
     * Resolve the background color for ink 36/7/33/35/40/41.
     * Returns -1 if the bitmap has native alpha and should skip processing.
     */
    static int resolveBackColor(Bitmap src, InkMode ink, int backColor,
                                 boolean useAlpha, Palette palette) {
        int bitDepth = src.getBitDepth();

        // 32-bit with native alpha: skip processing
        if (bitDepth == 32 && useAlpha) {
            return -1;
        }

        // Packed RGB value
        if (backColor > 255) {
            return backColor & 0xFFFFFF;
        }

        // 32-bit without alpha and ink is not Copy: force white
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
     * <p>
     * For 32-bit bitmaps, uses graduated alpha for pixels near the background color.
     * This converts AWT's RGB-domain anti-aliasing into proper alpha-domain anti-aliasing,
     * avoiding white halo artifacts around anti-aliased text glyphs.
     */
    static Bitmap applyBackgroundTransparent(Bitmap src, int bgColorRGB) {
        return applyBackgroundTransparent(src, bgColorRGB, false);
    }

    static Bitmap applyBackgroundTransparent(Bitmap src, int bgColorRGB, boolean skipGraduatedAlpha) {
        int w = src.getWidth();
        int h = src.getHeight();
        int[] srcPixels = src.getPixels();
        int[] result = new int[w * h];
        int bgR = (bgColorRGB >> 16) & 0xFF;
        int bgG = (bgColorRGB >> 8) & 0xFF;
        int bgB = bgColorRGB & 0xFF;

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

            // 32-bit text buffers often arrive as fully opaque grayscale RGB that was
            // anti-aliased against the background color. Recover proper alpha by
            // unblending those near-gray pixels from that background so faint halos
            // don't stay as opaque pixels behind glyphs.
            //
            // Already-colored UI pixels are typically authored as real opaque colors,
            // not grayscale anti-aliased blends. Preserving them as opaque avoids
            // washing solid row strips and panel parts into translucent bars.
            if (src.getBitDepth() == 32) {
                if (!skipGraduatedAlpha) {
                    int r = (pixel >> 16) & 0xFF;
                    int g = (pixel >> 8) & 0xFF;
                    int b = pixel & 0xFF;
                    if (isApproximatelyGray(r, g, b)) {
                        int recoveredAlpha = Math.max(Math.abs(r - bgR),
                                Math.max(Math.abs(g - bgG), Math.abs(b - bgB)));

                        if (recoveredAlpha > 0 && recoveredAlpha < 255) {
                            int fgR = unblendChannel(r, bgR, recoveredAlpha);
                            int fgG = unblendChannel(g, bgG, recoveredAlpha);
                            int fgB = unblendChannel(b, bgB, recoveredAlpha);
                            result[i] = (recoveredAlpha << 24) | (fgR << 16) | (fgG << 8) | fgB;
                            continue;
                        }
                    }
                }

                result[i] = 0xFF000000 | rgb;
            } else {
                // Director uses palette-index matching for non-32-bit background
                // transparent ink, which reduces to exact color-key transparency here.
                result[i] = pixel | 0xFF000000;
            }
        }

        return new Bitmap(w, h, src.getBitDepth(), result);
    }

    private static int unblendChannel(int observed, int background, int alpha) {
        int value = (observed * 255 - background * (255 - alpha)) / alpha;
        if (value < 0) return 0;
        if (value > 255) return 255;
        return value;
    }

    private static boolean isApproximatelyGray(int r, int g, int b) {
        return Math.abs(r - g) <= 2 && Math.abs(g - b) <= 2 && Math.abs(r - b) <= 2;
    }

    /**
     * Apply Matte ink (8): BFS flood-fill from image edges to remove border-connected
     * pixels matching matteColorRGB. Interior pixels of the same color are preserved.
     */
    static Bitmap applyMatte(Bitmap src, int matteColorRGB) {
        int w = src.getWidth();
        int h = src.getHeight();
        int[] pixels = src.getPixels();
        boolean[] transparent = new boolean[w * h];
        Queue<Integer> queue = new ArrayDeque<>();

        // Seed border pixels matching matte color
        for (int x = 0; x < w; x++) {
            seedMatte(pixels, transparent, queue, x, 0, w, matteColorRGB);
            seedMatte(pixels, transparent, queue, x, h - 1, w, matteColorRGB);
        }
        for (int y = 1; y < h - 1; y++) {
            seedMatte(pixels, transparent, queue, 0, y, w, matteColorRGB);
            seedMatte(pixels, transparent, queue, w - 1, y, w, matteColorRGB);
        }

        // Flood-fill from seeds
        while (!queue.isEmpty()) {
            int idx = queue.poll();
            int px = idx % w;
            int py = idx / w;
            if (px > 0)     seedMatte(pixels, transparent, queue, px - 1, py, w, matteColorRGB);
            if (px < w - 1) seedMatte(pixels, transparent, queue, px + 1, py, w, matteColorRGB);
            if (py > 0)     seedMatte(pixels, transparent, queue, px, py - 1, w, matteColorRGB);
            if (py < h - 1) seedMatte(pixels, transparent, queue, px, py + 1, w, matteColorRGB);
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
                                   int x, int y, int w, int matteRgb) {
        int idx = y * w + x;
        if (!transparent[idx] && isTransparentOrMatte(pixels[idx], matteRgb)) {
            transparent[idx] = true;
            queue.add(idx);
        }
    }

    private static boolean isTransparentOrMatte(int pixel, int matteRgb) {
        return ((pixel >>> 24) & 0xFF) == 0 || (pixel & 0xFFFFFF) == matteRgb;
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
