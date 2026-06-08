package com.libreshockwave.bitmap;

import com.libreshockwave.bitmap.Palette.InkMode;

import java.util.ArrayDeque;
import java.util.HashMap;
import java.util.Map;
import java.util.Queue;

/**
 * Drawing operations for bitmaps including ink mode blending.
 * Implements Director's copyPixels with various ink effects.
 */
public class Drawing {
    private static final int DEFAULT_RGB_MATTE = 0xFFFFFF;

    /**
     * A border-connected matte inferred from authored bitmap content.
     * Indexed Director art commonly uses palette slot 0 as the matte/background
     * entry; direct-RGB art uses the dominant edge color when it is unambiguous.
     */
    public record FloodFillMatte(Integer mattePaletteIndex, int matteColorRgb, int tolerance) {
        public FloodFillMatte(int matteColorRgb, int tolerance) {
            this(null, matteColorRgb, tolerance);
        }

        public boolean usesPaletteIndex() {
            return mattePaletteIndex != null;
        }
    }

    /**
     * Copy pixels from source to destination with ink mode blending.
     */
    public static void copyPixels(Bitmap dest, Bitmap src,
                                   int destX, int destY,
                                   int srcX, int srcY,
                                   int width, int height,
                                   InkMode ink, int blend) {
        copyPixels(dest, src, destX, destY, srcX, srcY, width, height, ink, blend, null, null);
    }

    /**
     * Copy pixels from source to destination with ink mode blending and optional mask.
     *
     * @param dest Destination bitmap
     * @param src Source bitmap
     * @param destX Destination X coordinate
     * @param destY Destination Y coordinate
     * @param srcX Source X coordinate
     * @param srcY Source Y coordinate
     * @param width Width to copy
     * @param height Height to copy
     * @param ink Ink mode for blending
     * @param blend Blend amount (0-255, used for BLEND ink)
     * @param mask Optional mask bitmap (same dimensions as source). Pixels with alpha=0 in mask are skipped.
     */
    public static void copyPixels(Bitmap dest, Bitmap src,
                                   int destX, int destY,
                                   int srcX, int srcY,
                                   int width, int height,
                                   InkMode ink, int blend,
                                   Bitmap mask) {
        copyPixels(dest, src, destX, destY, srcX, srcY, width, height, ink, blend, mask, null);
    }

    public static void copyPixels(Bitmap dest, Bitmap src,
                                   int destX, int destY,
                                   int srcX, int srcY,
                                   int width, int height,
                                   InkMode ink, int blend,
                                   Bitmap mask,
                                   Integer backgroundKeyRgb) {
        if (width <= 0 || height <= 0) return;
        if (ink == InkMode.MATTE && dest.getBitDepth() <= 8
                && copyMatteToMaskImage(dest, src, destX, destY, srcX, srcY, width, height)) {
            return;
        }
        Bitmap effectiveSrc = src;
        int effectiveSrcX = srcX;
        int effectiveSrcY = srcY;
        InkMode effectiveInk = ink;
        Integer resolvedBackgroundKeyRgb = effectiveInk == InkMode.BACKGROUND_TRANSPARENT && backgroundKeyRgb == null
                ? Integer.valueOf(0xFFFFFF)
                : backgroundKeyRgb;
        // For MATTE ink, pre-process the FULL source image with flood-fill matte.
        // Director applies matte to the entire source member, then extracts the
        // copy region. This preserves content that forms "islands" in the full
        // image but would be border-connected in a cropped sub-region (e.g.,
        // cloud bitmaps cropped during turn animations).
        if (ink == InkMode.MATTE) {
            effectiveSrc = applyMatteToRegion(src, 0, 0, src.getWidth(), src.getHeight());
            effectiveSrcX = srcX;
            effectiveSrcY = srcY;
        } else if (ink == InkMode.BACKGROUND_TRANSPARENT) {
            Bitmap backgroundTransparentSrc = applyBackgroundTransparentToRegion(
                    src, 0, 0, src.getWidth(), src.getHeight(), resolvedBackgroundKeyRgb);
            if (backgroundTransparentSrc != null) {
                effectiveSrc = backgroundTransparentSrc;
                effectiveSrcX = srcX;
                effectiveSrcY = srcY;
                // Keep BACKGROUND_TRANSPARENT active after flood-fill preprocessing.
                // Window text buffers can contain enclosed white glyph counters
                // that are not border-connected; switching to COPY preserves those
                // as opaque white pinholes instead of keying them out.
            }
        }
        boolean keyNearWhiteMatte = effectiveInk == InkMode.BACKGROUND_TRANSPARENT
                && shouldKeyNearWhiteMatte(effectiveSrc, effectiveSrcX, effectiveSrcY, width, height,
                        resolvedBackgroundKeyRgb);
        byte[] effectivePaletteIndices = effectiveSrc.getPaletteIndices();
        boolean keyDefaultIndexedMatte = effectiveInk == InkMode.BACKGROUND_TRANSPARENT
                && shouldKeyDefaultIndexedMatte(effectiveSrc, resolvedBackgroundKeyRgb, effectivePaletteIndices);
        for (int y = 0; y < height; y++) {
            int sy = effectiveSrcY + y;
            int dy = destY + y;

            if (sy < 0 || sy >= effectiveSrc.getHeight() || dy < 0 || dy >= dest.getHeight()) {
                continue;
            }

            for (int x = 0; x < width; x++) {
                int sx = effectiveSrcX + x;
                int dx = destX + x;

                if (sx < 0 || sx >= effectiveSrc.getWidth() || dx < 0 || dx >= dest.getWidth()) {
                    continue;
                }

                // Check mask at source coordinates (mask has same dimensions as source)
                if (mask != null) {
                    int mx = srcX + x;
                    int my = srcY + y;
                    if (!maskAllowsPixel(mask, mx, my)) {
                        continue;
                    }
                }

                int srcPixel = effectiveSrc.getPixel(sx, sy);
                int destPixel = dest.getPixel(dx, dy);
                int srcIndex = sy * effectiveSrc.getWidth() + sx;
                boolean skipSource = keyDefaultIndexedMatte
                        && srcIndex >= 0 && srcIndex < effectivePaletteIndices.length
                        && (effectivePaletteIndices[srcIndex] & 0xFF) == 0;
                skipSource = skipSource || (keyNearWhiteMatte && isNearWhiteMattePixel(srcPixel));

                int resultPixel = skipSource
                        ? destPixel
                        : applyInk(srcPixel, destPixel, effectiveInk, blend, resolvedBackgroundKeyRgb);
                dest.setPixelPreservePaletteIndex(dx, dy, resultPixel);
            }
        }
    }

    private static boolean shouldKeyNearWhiteMatte(Bitmap src, int srcX, int srcY,
                                                   int width, int height,
                                                   Integer backgroundKeyRgb) {
        if (src == null || src.getBitDepth() < 32 || !src.hasTransparentPixels()) {
            return false;
        }
        int keyRgb = backgroundKeyRgb != null ? (backgroundKeyRgb & 0xFFFFFF) : 0xFFFFFF;
        if (keyRgb != 0xFFFFFF || width <= 0 || height <= 0) {
            return false;
        }
        int maxX = src.getWidth() - 1;
        int maxY = src.getHeight() - 1;
        int left = Math.max(0, Math.min(maxX, srcX));
        int top = Math.max(0, Math.min(maxY, srcY));
        int right = Math.max(0, Math.min(maxX, srcX + width - 1));
        int bottom = Math.max(0, Math.min(maxY, srcY + height - 1));

        for (int x = left; x <= right; x++) {
            if (isNearWhiteMattePixel(src.getPixel(x, top))
                    || isNearWhiteMattePixel(src.getPixel(x, bottom))) {
                return true;
            }
        }
        for (int y = top + 1; y < bottom; y++) {
            if (isNearWhiteMattePixel(src.getPixel(left, y))
                    || isNearWhiteMattePixel(src.getPixel(right, y))) {
                return true;
            }
        }
        return false;
    }

    private static boolean isNearWhiteMattePixel(int pixel) {
        if (((pixel >>> 24) & 0xFF) == 0) {
            return false;
        }
        int r = (pixel >> 16) & 0xFF;
        int g = (pixel >> 8) & 0xFF;
        int b = pixel & 0xFF;
        return r >= 240 && g >= 240 && b >= 240
                && Math.abs(r - g) <= 2
                && Math.abs(g - b) <= 2;
    }

    private static boolean shouldKeyDefaultIndexedMatte(Bitmap src, Integer backgroundKeyRgb,
                                                        byte[] paletteIndices) {
        if (src == null || src.getBitDepth() > 8
                || backgroundKeyRgb == null || (backgroundKeyRgb & 0xFFFFFF) != DEFAULT_RGB_MATTE
                || !hasPaletteIndices(paletteIndices, src.getWidth(), src.getHeight())) {
            return false;
        }
        if (isUniformPaletteIndex(paletteIndices, 0)) {
            return false;
        }

        int[] pixels = src.getPixels();
        if (!edgeContainsOpaquePaletteIndex(pixels, paletteIndices, src.getWidth(), src.getHeight(), 0)) {
            return false;
        }

        int indexZeroRgb = resolvePaletteIndexRgb(pixels, paletteIndices, 0);
        return isNearWhiteGrayscale(indexZeroRgb, 232, 16)
                && hasOpaqueNonPaletteIndexContent(pixels, paletteIndices, 0);
    }

    private static boolean edgeContainsOpaquePaletteIndex(int[] pixels, byte[] paletteIndices,
                                                          int w, int h, int paletteIndex) {
        for (int index : iterateEdgeIndices(w, h)) {
            if (((pixels[index] >>> 24) & 0xFF) != 0
                    && (paletteIndices[index] & 0xFF) == paletteIndex) {
                return true;
            }
        }
        return false;
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

    private static boolean copyMatteToMaskImage(Bitmap dest, Bitmap src,
                                                int destX, int destY,
                                                int srcX, int srcY,
                                                int width, int height) {
        int w = src.getWidth();
        int h = src.getHeight();
        if (w <= 0 || h <= 0) {
            return false;
        }
        if (!isMostlyWhiteRegion(dest, destX, destY, width, height)) {
            return false;
        }

        int[] pixels = new int[w * h];
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                pixels[y * w + x] = src.getPixel(x, y);
            }
        }

        byte[] paletteIndices = src.getPaletteIndices();
        FloodFillMatte matteSpec = resolveFloodFillMatte(pixels, paletteIndices, w, h);
        if (matteSpec == null) {
            return false;
        }
        boolean[] transparent = computeFloodFillTransparency(pixels, paletteIndices, w, h, matteSpec);
        if (!isMaskSource(pixels, transparent, matteSpec)) {
            return false;
        }
        int matteLuma = maskAlphaFromPixel(0xFF000000 | (matteSpec.matteColorRgb() & 0xFFFFFF));
        boolean lightMatte = matteLuma >= 128;

        for (int y = 0; y < height; y++) {
            int sy = srcY + y;
            int dy = destY + y;
            if (sy < 0 || sy >= h || dy < 0 || dy >= dest.getHeight()) {
                continue;
            }

            for (int x = 0; x < width; x++) {
                int sx = srcX + x;
                int dx = destX + x;
                if (sx < 0 || sx >= w || dx < 0 || dx >= dest.getWidth()) {
                    continue;
                }

                int index = sy * w + sx;
                if (transparent[index]) {
                    continue;
                }

                int sourceLuma = maskAlphaFromPixel(pixels[index]);
                int maskLuma = lightMatte ? sourceLuma : 255 - sourceLuma;
                dest.setPixelPreservePaletteIndex(dx, dy, 0xFF000000 | (maskLuma << 16) | (maskLuma << 8) | maskLuma);
            }
        }

        return true;
    }

    private static boolean isMaskSource(int[] pixels, boolean[] transparent, FloodFillMatte matteSpec) {
        return isGrayscaleMaskSource(pixels, transparent)
                || isWhiteBackedMaskSource(pixels, transparent, matteSpec);
    }

    private static boolean isGrayscaleMaskSource(int[] pixels, boolean[] transparent) {
        int opaquePixels = 0;
        for (int i = 0; i < pixels.length; i++) {
            if (transparent[i] || ((pixels[i] >>> 24) & 0xFF) == 0) {
                continue;
            }
            int r = (pixels[i] >> 16) & 0xFF;
            int g = (pixels[i] >> 8) & 0xFF;
            int b = pixels[i] & 0xFF;
            if (r != g || g != b) {
                return false;
            }
            opaquePixels++;
        }
        // Text masks are sparse glyph ink on a matte background. Large filled
        // grayscale UI artwork, such as 1px window strips scaled across a panel,
        // must remain artwork rather than being converted into a luma mask.
        return opaquePixels > 0 && opaquePixels * 4 <= pixels.length * 3;
    }

    private static boolean isWhiteBackedMaskSource(int[] pixels, boolean[] transparent, FloodFillMatte matteSpec) {
        int matteLuma = maskAlphaFromPixel(0xFF000000 | (matteSpec.matteColorRgb() & 0xFFFFFF));
        if (matteLuma < 250) {
            return false;
        }

        boolean hasTransparentMatte = false;
        boolean hasOpaqueInk = false;
        for (int i = 0; i < pixels.length; i++) {
            if (((pixels[i] >>> 24) & 0xFF) == 0) {
                continue;
            }
            if (transparent[i]) {
                hasTransparentMatte = true;
            } else {
                hasOpaqueInk = true;
            }
            if (hasTransparentMatte && hasOpaqueInk) {
                return true;
            }
        }
        return false;
    }

    private static boolean isMostlyWhiteRegion(Bitmap bitmap, int x, int y, int width, int height) {
        int sampled = 0;
        int white = 0;
        int step = Math.max(1, (width * height) / 64);
        for (int i = 0; i < width * height; i += step) {
            int px = x + (i % width);
            int py = y + (i / width);
            if (px < 0 || px >= bitmap.getWidth() || py < 0 || py >= bitmap.getHeight()) {
                continue;
            }
            sampled++;
            int rgb = bitmap.getPixel(px, py) & 0xFFFFFF;
            if (rgb == 0xFFFFFF) {
                white++;
            }
        }
        return sampled > 0 && white * 4 >= sampled * 3;
    }

    public static Bitmap preprocessBackgroundTransparent(Bitmap src, Integer backgroundKeyRgb) {
        return applyBackgroundTransparentToRegion(src, 0, 0, src.getWidth(), src.getHeight(), backgroundKeyRgb);
    }

    private static Bitmap applyBackgroundTransparentToRegion(Bitmap src, int srcX, int srcY, int w, int h,
                                                            Integer backgroundKeyRgb) {
        if (w <= 0 || h <= 0 || backgroundKeyRgb == null || (backgroundKeyRgb & 0xFFFFFF) != 0xFFFFFF) {
            return null;
        }
        if (src.hasNativeMatteAlpha()) {
            return null;
        }

        Bitmap region = src.getRegion(srcX, srcY, w, h);
        int[] pixels = region.getPixels();
        byte[] paletteIndices = region.getPaletteIndices();
        FloodFillMatte matteSpec = resolveBackgroundTransparentMatte(pixels, paletteIndices, w, h);
        if (matteSpec == null) {
            return null;
        }

        boolean[] transparent = computeFloodFillTransparency(pixels, paletteIndices, w, h, matteSpec);
        boolean changed = false;
        for (int i = 0; i < pixels.length; i++) {
            if (!transparent[i]) {
                continue;
            }
            pixels[i] &= 0x00FFFFFF;
            changed = true;
        }
        if (!changed) {
            return null;
        }
        region.setNativeAlpha(true);
        return region;
    }

    /**
     * Copy entire source bitmap to destination.
     */
    public static void copyPixels(Bitmap dest, Bitmap src, int destX, int destY, InkMode ink, int blend) {
        copyPixels(dest, src, destX, destY, 0, 0, src.getWidth(), src.getHeight(), ink, blend);
    }

    /** Pack r, g, b into a fully-opaque ARGB int. */
    private static int packOpaqueRgb(int r, int g, int b) {
        return 0xFF000000 | (r << 16) | (g << 8) | b;
    }

    /**
     * Director's MASK ink derives opacity from the source pixel brightness.
     * Use luma instead of a single channel so colored masks behave consistently
     * across copyPixels and sprite-stage rendering.
     */
    public static int maskAlphaFromPixel(int pixel) {
        int r = (pixel >> 16) & 0xFF;
        int g = (pixel >> 8) & 0xFF;
        int b = pixel & 0xFF;
        return ((77 * r) + (150 * g) + (29 * b) + 128) >> 8;
    }

    /**
     * Apply ink mode to blend source and destination pixels.
     *
     * @param src Source pixel (ARGB)
     * @param dest Destination pixel (ARGB)
     * @param ink Ink mode
     * @param blend Blend factor (0-255)
     * @return Blended pixel (ARGB)
     */
    public static int applyInk(int src, int dest, InkMode ink, int blend) {
        return applyInk(src, dest, ink, blend, null);
    }

    public static int applyInk(int src, int dest, InkMode ink, int blend, Integer backgroundKeyRgb) {
        int srcA = (src >> 24) & 0xFF;
        int srcR = (src >> 16) & 0xFF;
        int srcG = (src >> 8) & 0xFF;
        int srcB = src & 0xFF;

        int destA = (dest >> 24) & 0xFF;
        int destR = (dest >> 16) & 0xFF;
        int destG = (dest >> 8) & 0xFF;
        int destB = dest & 0xFF;

        int r, g, b, a;

        switch (ink) {
            case COPY:
                // "All colors, including white, are opaque unless the image
                // contains alpha channel effects (transparency)." — Director docs
                if (srcA == 0) {
                    return dest;
                }
                if (srcA < 255) {
                    return alphaBlend(src, dest, srcA);
                }
                return src;

            case TRANSPARENT:
                // White (255,255,255) is transparent
                if (srcR == 255 && srcG == 255 && srcB == 255) {
                    return dest;
                }
                return src;

            case REVERSE:
                r = destR ^ srcR;
                g = destG ^ srcG;
                b = destB ^ srcB;
                return packOpaqueRgb(r, g, b);

            case GHOST:
                // Source appears ghosted over destination
                r = (srcR + destR) / 2;
                g = (srcG + destG) / 2;
                b = (srcB + destB) / 2;
                return packOpaqueRgb(r, g, b);

            case NOT_COPY:
                r = 255 - srcR;
                g = 255 - srcG;
                b = 255 - srcB;
                return packOpaqueRgb(r, g, b);

            case NOT_TRANSPARENT:
                // Black (0,0,0) is transparent
                if (srcR == 0 && srcG == 0 && srcB == 0) {
                    return dest;
                }
                r = 255 - srcR;
                g = 255 - srcG;
                b = 255 - srcB;
                return packOpaqueRgb(r, g, b);

            case NOT_REVERSE:
                r = destR ^ (255 - srcR);
                g = destG ^ (255 - srcG);
                b = destB ^ (255 - srcB);
                return packOpaqueRgb(r, g, b);

            case NOT_GHOST:
                r = ((255 - srcR) + destR) / 2;
                g = ((255 - srcG) + destG) / 2;
                b = ((255 - srcB) + destB) / 2;
                return packOpaqueRgb(r, g, b);

            case MATTE:
                // Use alpha channel for transparency, combined with blend.
                // copyPixels #blend controls source opacity (0=transparent, 255=opaque).
                // For #blend:70 → blend=178: black over white gives ~77 grey (matching
                // the reference info stand panel at ~85,85,85).
                if (srcA == 0) {
                    return dest;
                }
                if (blend < 255) {
                    int matteAlpha = (srcA * blend) / 255;
                    if (matteAlpha == 0) return dest;
                    return alphaBlend(src, dest, matteAlpha);
                }
                return alphaBlend(src, dest, srcA);

            case MASK:
                // Director MASK copies the source over the destination with an
                // opacity derived from the source brightness. Black mask pixels
                // contribute nothing; white pixels are fully opaque.
                a = combineAlpha(srcA, maskAlphaFromPixel(src));
                if (a == 0) {
                    return dest;
                }
                return alphaBlend(src, dest, a);

            case BLEND:
                // Director's image.copyPixels blend factor applies on top of any
                // per-pixel source alpha. Effective opacity is the product.
                if (srcA == 0 || blend <= 0) {
                    return dest;
                }
                return alphaBlend(src, dest, combineAlpha(srcA, blend));

            case ADD_PIN:
                r = Math.min(255, srcR + destR);
                g = Math.min(255, srcG + destG);
                b = Math.min(255, srcB + destB);
                return packOpaqueRgb(r, g, b);

            case ADD:
                r = (srcR + destR) & 0xFF; // Wrap around
                g = (srcG + destG) & 0xFF;
                b = (srcB + destB) & 0xFF;
                return packOpaqueRgb(r, g, b);

            case SUBTRACT_PIN:
                r = Math.max(0, destR - srcR);
                g = Math.max(0, destG - srcG);
                b = Math.max(0, destB - srcB);
                return packOpaqueRgb(r, g, b);

            case BACKGROUND_TRANSPARENT:
                // Director's Background Transparent is exact-match keying against the
                // chosen background color for this copy operation. When copyPixels
                // does not pass #bgColor, the default key remains white.
                if (srcA == 0) return dest;
                int keyRgb = backgroundKeyRgb != null ? (backgroundKeyRgb & 0xFFFFFF) : 0xFFFFFF;
                if (((srcR << 16) | (srcG << 8) | srcB) == keyRgb) {
                    return dest;
                }
                if (blend < 255 || srcA < 255) {
                    return alphaBlend(src, dest, combineAlpha(srcA, blend));
                }
                return src;

            case LIGHTEST:
                if (srcA == 0) return dest;
                r = Math.max(srcR, destR);
                g = Math.max(srcG, destG);
                b = Math.max(srcB, destB);
                return packOpaqueRgb(r, g, b);

            case SUBTRACT:
                r = (destR - srcR) & 0xFF; // Wrap around
                g = (destG - srcG) & 0xFF;
                b = (destB - srcB) & 0xFF;
                return packOpaqueRgb(r, g, b);

            case DARKEST:
                if (srcA == 0) return dest;
                r = Math.min(srcR, destR);
                g = Math.min(srcG, destG);
                b = Math.min(srcB, destB);
                return packOpaqueRgb(r, g, b);

            case LIGHTEN:
            case DARKEN:
                if (srcA == 0) return dest;
                return alphaBlend(src, dest, combineAlpha(srcA, blend));

            default:
                return src;
        }
    }

    /**
     * Alpha blend two pixels.
     */
    private static int alphaBlend(int fg, int bg, int alpha) {
        if (alpha == 0) return bg;
        if (alpha == 255) return fg;

        int fgR = (fg >> 16) & 0xFF;
        int fgG = (fg >> 8) & 0xFF;
        int fgB = fg & 0xFF;

        int bgR = (bg >> 16) & 0xFF;
        int bgG = (bg >> 8) & 0xFF;
        int bgB = bg & 0xFF;

        int invAlpha = 255 - alpha;

        int r = (fgR * alpha + bgR * invAlpha) / 255;
        int g = (fgG * alpha + bgG * invAlpha) / 255;
        int b = (fgB * alpha + bgB * invAlpha) / 255;

        return packOpaqueRgb(r, g, b);
    }

    private static int combineAlpha(int srcAlpha, int blendAlpha) {
        if (srcAlpha <= 0 || blendAlpha <= 0) {
            return 0;
        }
        if (srcAlpha >= 255) {
            return blendAlpha;
        }
        if (blendAlpha >= 255) {
            return srcAlpha;
        }
        return (srcAlpha * blendAlpha) / 255;
    }

    public static boolean maskAllowsPixel(Bitmap mask, int x, int y) {
        if (mask == null || x < 0 || x >= mask.getWidth() || y < 0 || y >= mask.getHeight()) {
            return false;
        }
        int pixel = mask.getPixel(x, y);
        if (mask.hasNativeMatteAlpha()) {
            return ((pixel >>> 24) & 0xFF) != 0;
        }
        return maskAlphaFromPixel(pixel) < 255;
    }

    /**
     * Draw a filled rectangle.
     */
    public static void fillRect(Bitmap dest, int x, int y, int width, int height, int color) {
        dest.fillRect(x, y, width, height, color);
    }

    /**
     * Draw a rectangle outline.
     */
    public static void drawRect(Bitmap dest, int x, int y, int width, int height, int color) {
        // Top
        for (int i = x; i < x + width; i++) {
            dest.setPixel(i, y, color);
        }
        // Bottom
        for (int i = x; i < x + width; i++) {
            dest.setPixel(i, y + height - 1, color);
        }
        // Left
        for (int i = y; i < y + height; i++) {
            dest.setPixel(x, i, color);
        }
        // Right
        for (int i = y; i < y + height; i++) {
            dest.setPixel(x + width - 1, i, color);
        }
    }

    /**
     * Draw a line using Bresenham's algorithm.
     */
    public static void drawLine(Bitmap dest, int x0, int y0, int x1, int y1, int color) {
        int dx = Math.abs(x1 - x0);
        int dy = Math.abs(y1 - y0);
        int sx = x0 < x1 ? 1 : -1;
        int sy = y0 < y1 ? 1 : -1;
        int err = dx - dy;

        while (true) {
            dest.setPixel(x0, y0, color);

            if (x0 == x1 && y0 == y1) break;

            int e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x0 += sx;
            }
            if (e2 < dx) {
                err += dx;
                y0 += sy;
            }
        }
    }

    /**
     * Draw a filled ellipse.
     */
    public static void fillEllipse(Bitmap dest, int cx, int cy, int rx, int ry, int color) {
        for (int y = -ry; y <= ry; y++) {
            for (int x = -rx; x <= rx; x++) {
                if ((x * x * ry * ry + y * y * rx * rx) <= (rx * rx * ry * ry)) {
                    dest.setPixel(cx + x, cy + y, color);
                }
            }
        }
    }

    /**
     * Director's image.createMatte() uses authored/native alpha when present.
     * Otherwise it falls back to flood-fill matte extraction:
     * indexed art prefers a dominant edge palette index, and RGB art prefers a
     * dominant edge color before falling back to the classic white-border matte.
     */
    public static Bitmap createMatte(Bitmap src) {
        return createMatte(src, 0);
    }

    public static Bitmap createMask(Bitmap src) {
        return createMask(src, 0);
    }

    public static Bitmap createMask(Bitmap src, int alphaThreshold) {
        int w = src.getWidth();
        int h = src.getHeight();
        if (w <= 0 || h <= 0) {
            return new Bitmap(1, 1, 32);
        }

        if (src.hasNativeMatteAlpha()) {
            return createAlphaMatte(src, alphaThreshold);
        }

        int[] pixels = new int[w * h];
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                pixels[y * w + x] = src.getPixel(x, y);
            }
        }
        byte[] paletteIndices = src.getPaletteIndices();
        FloodFillMatte matteSpec = resolveFloodFillMatte(pixels, paletteIndices, w, h);
        if (matteSpec != null) {
            boolean[] transparent = computeFloodFillTransparency(pixels, paletteIndices, w, h, matteSpec);
            if (isMaskSource(pixels, transparent, matteSpec)) {
                return createDirectMask(src, pixels, matteSpec, alphaThreshold);
            }
        }

        return createFloodFillMatte(src);
    }

    /**
     * alphaThreshold excludes pixels below the threshold.
     */
    public static Bitmap createMatte(Bitmap src, int alphaThreshold) {
        int w = src.getWidth();
        int h = src.getHeight();
        if (w <= 0 || h <= 0) {
            return new Bitmap(1, 1, 32);
        }

        if (src.hasNativeMatteAlpha()) {
            return createAlphaMatte(src, alphaThreshold);
        }

        return createFloodFillMatte(src);
    }

    private static Bitmap createAlphaMatte(Bitmap src, int alphaThreshold) {
        int w = src.getWidth();
        int h = src.getHeight();
        int threshold = Math.max(0, Math.min(255, alphaThreshold));
        int[] mask = new int[w * h];
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int alpha = (src.getPixel(x, y) >>> 24) & 0xFF;
                if (alpha < threshold) {
                    alpha = 0;
                }
                mask[y * w + x] = (alpha << 24) | 0x00FFFFFF;
            }
        }

        Bitmap matte = new Bitmap(w, h, 32, mask);
        matte.setNativeAlpha(true);
        return matte;
    }

    public static FloodFillMatte resolveFloodFillMatte(Bitmap src) {
        int w = src.getWidth();
        int h = src.getHeight();
        if (w <= 0 || h <= 0) {
            return new FloodFillMatte(0xFFFFFF, 0);
        }

        int[] pixels = new int[w * h];
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                pixels[y * w + x] = src.getPixel(x, y);
            }
        }
        byte[] paletteIndices = src.getPaletteIndices();
        return resolveFloodFillMatte(pixels, paletteIndices, w, h);
    }

    /**
     * Remove the edge-connected matte/background from the bitmap while preserving
     * the original pixel colors for the remaining content.
     */
    public static Bitmap applyFloodFillTransparency(Bitmap src) {
        return applyMatteToRegion(src, 0, 0, src.getWidth(), src.getHeight());
    }

    private static Bitmap createFloodFillMatte(Bitmap src) {
        int w = src.getWidth();
        int h = src.getHeight();
        int[] pixels = new int[w * h];
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                pixels[y * w + x] = src.getPixel(x, y);
            }
        }

        byte[] paletteIndices = src.getPaletteIndices();
        FloodFillMatte matteSpec = resolveFloodFillMatte(pixels, paletteIndices, w, h);
        boolean[] transparent = matteSpec != null
                ? computeFloodFillTransparency(pixels, paletteIndices, w, h, matteSpec)
                : new boolean[w * h];

        int[] mask = new int[w * h];
        for (int i = 0; i < pixels.length; i++) {
            if (transparent[i]) {
                mask[i] = 0x00FFFFFF;
            } else {
                int alpha = (pixels[i] >>> 24) & 0xFF;
                if (alpha == 0) {
                    alpha = 0xFF;
                }
                mask[i] = (alpha << 24) | 0x00FFFFFF;
            }
        }

        Bitmap matteBitmap = new Bitmap(w, h, 32, mask);
        matteBitmap.setNativeAlpha(true);
        return matteBitmap;
    }

    private static Bitmap createDirectMask(Bitmap src, int[] pixels,
                                           FloodFillMatte matteSpec, int alphaThreshold) {
        int w = src.getWidth();
        int h = src.getHeight();
        int threshold = Math.max(0, Math.min(255, alphaThreshold));
        int matteLuma = maskAlphaFromPixel(0xFF000000 | (matteSpec.matteColorRgb() & 0xFFFFFF));
        boolean lightMatte = matteLuma >= 128;

        int[] mask = new int[w * h];
        for (int i = 0; i < pixels.length; i++) {
            int pixel = pixels[i];
            int srcAlpha = (pixel >>> 24) & 0xFF;
            if (srcAlpha == 0) {
                mask[i] = 0xFFFFFFFF;
                continue;
            }
            int maskLuma = maskAlphaFromPixel(pixel);
            if (!lightMatte) {
                maskLuma = 255 - maskLuma;
            }
            int opacity = 255 - maskLuma;
            if (opacity < threshold) {
                maskLuma = 255;
            }
            mask[i] = 0xFF000000 | (maskLuma << 16) | (maskLuma << 8) | maskLuma;
        }

        return new Bitmap(w, h, src.getBitDepth(), mask);
    }

    /**
     * Apply matte (flood-fill from edges) to a source bitmap region.
     * Returns a new bitmap where border-connected background pixels have alpha=0.
     * Used by copyPixels with MATTE ink to properly handle source transparency.
     */
    private static Bitmap applyMatteToRegion(Bitmap src, int srcX, int srcY, int w, int h) {
        if (w <= 0 || h <= 0) {
            return new Bitmap(Math.max(w, 1), Math.max(h, 1), src.getBitDepth());
        }
        if (src.hasNativeMatteAlpha()) {
            Bitmap region = src.getRegion(srcX, srcY, w, h);
            region.setNativeAlpha(true);
            return region;
        }
        Bitmap region = src.getRegion(srcX, srcY, w, h);
        int[] pixels = region.getPixels();
        byte[] paletteIndices = region.getPaletteIndices();
        FloodFillMatte matteSpec = resolveFloodFillMatte(pixels, paletteIndices, w, h);
        if (matteSpec == null) {
            return region;
        }
        boolean[] transparent = computeFloodFillTransparency(pixels, paletteIndices, w, h, matteSpec);

        for (int i = 0; i < pixels.length; i++) {
            if (transparent[i]) {
                pixels[i] &= 0x00FFFFFF;
            }
        }

        return region;
    }

    private static FloodFillMatte resolveFloodFillMatte(int[] pixels, byte[] paletteIndices, int w, int h) {
        if (hasPaletteIndices(paletteIndices, w, h)) {
            return resolveIndexedFloodFillMatte(pixels, paletteIndices, w, h);
        }
        return resolveRgbFloodFillMatte(pixels, w, h);
    }

    private static FloodFillMatte resolveIndexedFloodFillMatte(int[] pixels, byte[] paletteIndices, int w, int h) {
        Integer matteIndex = inferDominantEdgePaletteIndex(pixels, paletteIndices, w, h);
        if (matteIndex != null) {
            int matteRgb = resolvePaletteIndexRgb(pixels, paletteIndices, matteIndex);
            if (matteIndex == 0 && isDefaultIndexedMatteRgb(matteRgb)) {
                return new FloodFillMatte(matteIndex, matteRgb, 0);
            }
        }

        if (cornerContainsPaletteIndex(paletteIndices, w, h, 0)) {
            int indexZeroRgb = resolvePaletteIndexRgb(pixels, paletteIndices, 0);
            if (isDefaultIndexedMatteRgb(indexZeroRgb)) {
                return new FloodFillMatte(0, indexZeroRgb, 0);
            }
        }
        return null;
    }

    private static boolean hasPaletteIndices(byte[] paletteIndices, int w, int h) {
        return paletteIndices != null && paletteIndices.length >= w * h;
    }

    private static boolean cornerContainsPaletteIndex(byte[] paletteIndices, int w, int h, int paletteIndex) {
        int[] cornerIndices = {
                0,
                Math.max(0, w - 1),
                Math.max(0, (h - 1) * w),
                Math.max(0, (h - 1) * w + (w - 1))
        };
        for (int index : cornerIndices) {
            if ((paletteIndices[index] & 0xFF) == paletteIndex) {
                return true;
            }
        }
        return false;
    }

    private static boolean isDefaultIndexedMatteRgb(int rgb) {
        return rgb == 0x000000 || rgb == DEFAULT_RGB_MATTE;
    }

    private static Integer inferDominantEdgePaletteIndex(int[] pixels, byte[] paletteIndices, int w, int h) {
        if (w <= 0 || h <= 0) {
            return null;
        }

        int[] counts = new int[256];
        int opaqueEdgeCount = 0;
        int dominantIndex = -1;
        int dominantCount = 0;

        int[] cornerIndices = {
                0,
                Math.max(0, w - 1),
                Math.max(0, (h - 1) * w),
                Math.max(0, (h - 1) * w + (w - 1))
        };

        for (int index : iterateEdgeIndices(w, h)) {
            if (((pixels[index] >>> 24) & 0xFF) == 0) {
                continue;
            }
            int paletteIndex = paletteIndices[index] & 0xFF;
            int count = ++counts[paletteIndex];
            opaqueEdgeCount++;
            if (count > dominantCount) {
                dominantCount = count;
                dominantIndex = paletteIndex;
            }
        }

        if (opaqueEdgeCount == 0 || dominantIndex < 0) {
            return null;
        }

        // Avoid treating uniformly filled indexed bitmaps as pure matte.
        if (isUniformPaletteIndex(paletteIndices, dominantIndex)) {
            return null;
        }

        int opaqueCornerCount = 0;
        for (int index : cornerIndices) {
            if (((pixels[index] >>> 24) & 0xFF) == 0) {
                continue;
            }
            opaqueCornerCount++;
            if ((paletteIndices[index] & 0xFF) != dominantIndex) {
                return null;
            }
        }

        if (opaqueCornerCount == 0) {
            return null;
        }

        // Require a clearly dominant authored matte on the outer edge.
        if (dominantCount * 4 < opaqueEdgeCount * 3) {
            return null;
        }

        return dominantIndex;
    }

    private static Iterable<Integer> iterateEdgeIndices(int w, int h) {
        java.util.List<Integer> indices = new java.util.ArrayList<>(Math.max(1, (w * 2) + Math.max(0, h - 2) * 2));
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

    private static boolean isUniformPaletteIndex(byte[] paletteIndices, int paletteIndex) {
        for (byte paletteEntry : paletteIndices) {
            if ((paletteEntry & 0xFF) != paletteIndex) {
                return false;
            }
        }
        return true;
    }

    private static int resolvePaletteIndexRgb(int[] pixels, byte[] paletteIndices, int paletteIndex) {
        for (int i = 0; i < pixels.length && i < paletteIndices.length; i++) {
            if ((paletteIndices[i] & 0xFF) == paletteIndex) {
                return pixels[i] & 0xFFFFFF;
            }
        }
        return 0xFFFFFF;
    }

    private static FloodFillMatte resolveRgbFloodFillMatte(int[] pixels, int w, int h) {
        Integer matteRgb = inferDominantEdgeRgb(pixels, w, h);
        if (matteRgb != null) {
            return new FloodFillMatte(matteRgb, 0);
        }
        if (!cornerContainsOpaqueRgb(pixels, w, h, DEFAULT_RGB_MATTE)) {
            return null;
        }
        return new FloodFillMatte(DEFAULT_RGB_MATTE, 0);
    }

    private static boolean cornerContainsOpaqueRgb(int[] pixels, int w, int h, int rgb) {
        int[] cornerIndices = {
                0,
                Math.max(0, w - 1),
                Math.max(0, (h - 1) * w),
                Math.max(0, (h - 1) * w + (w - 1))
        };
        for (int index : cornerIndices) {
            int pixel = pixels[index];
            if (((pixel >>> 24) & 0xFF) != 0 && (pixel & 0xFFFFFF) == rgb) {
                return true;
            }
        }
        return false;
    }

    private static FloodFillMatte resolveBackgroundTransparentMatte(int[] pixels, byte[] paletteIndices, int w, int h) {
        if (hasPaletteIndices(paletteIndices, w, h)) {
            Integer matteIndex = inferDominantEdgePaletteIndex(pixels, paletteIndices, w, h);
            if (matteIndex == null) {
                return null;
            }
            int matteRgb = resolvePaletteIndexRgb(pixels, paletteIndices, matteIndex);
            if (!isNearWhiteGrayscale(matteRgb, 232, 16)
                    || !hasOpaqueNonNearWhiteContent(pixels, paletteIndices, w, h, 232, 16)) {
                return null;
            }
            return new FloodFillMatte(matteIndex, matteRgb, 0);
        }

        if (!cornersAreNearWhite(pixels, w, h, 232, 16)) {
            return null;
        }

        int opaqueEdgeCount = 0;
        int nearWhiteEdgeCount = 0;
        for (int index : iterateEdgeIndices(w, h)) {
            if (((pixels[index] >>> 24) & 0xFF) == 0) {
                continue;
            }
            opaqueEdgeCount++;
            if (isNearWhiteGrayscale(pixels[index] & 0xFFFFFF, 232, 16)) {
                nearWhiteEdgeCount++;
            }
        }

        if (opaqueEdgeCount == 0 || nearWhiteEdgeCount * 4 < opaqueEdgeCount * 3) {
            return null;
        }
        if (!hasOpaqueNonNearWhiteContent(pixels, null, w, h, 232, 16)) {
            return null;
        }

        return new FloodFillMatte(DEFAULT_RGB_MATTE, 24);
    }

    private static Integer inferDominantEdgeRgb(int[] pixels, int w, int h) {
        if (w <= 0 || h <= 0) {
            return null;
        }

        Map<Integer, Integer> counts = new HashMap<>();
        int opaqueEdgeCount = 0;
        int dominantRgb = -1;
        int dominantCount = 0;

        int[] cornerIndices = {
                0,
                Math.max(0, w - 1),
                Math.max(0, (h - 1) * w),
                Math.max(0, (h - 1) * w + (w - 1))
        };

        for (int index : iterateEdgeIndices(w, h)) {
            if (((pixels[index] >>> 24) & 0xFF) == 0) {
                continue;
            }
            int rgb = pixels[index] & 0xFFFFFF;
            int count = counts.getOrDefault(rgb, 0) + 1;
            counts.put(rgb, count);
            opaqueEdgeCount++;
            if (count > dominantCount) {
                dominantCount = count;
                dominantRgb = rgb;
            }
        }

        if (opaqueEdgeCount == 0 || dominantRgb < 0) {
            return null;
        }

        if (isUniformRgb(pixels, dominantRgb)) {
            return null;
        }

        int opaqueCornerCount = 0;
        for (int index : cornerIndices) {
            if (((pixels[index] >>> 24) & 0xFF) == 0) {
                continue;
            }
            opaqueCornerCount++;
            if ((pixels[index] & 0xFFFFFF) != dominantRgb) {
                return null;
            }
        }

        if (opaqueCornerCount == 0) {
            return null;
        }

        if (dominantCount * 4 < opaqueEdgeCount * 3) {
            return null;
        }

        return dominantRgb;
    }

    private static boolean isUniformRgb(int[] pixels, int rgb) {
        for (int pixel : pixels) {
            if (((pixel >>> 24) & 0xFF) != 0 && (pixel & 0xFFFFFF) != rgb) {
                return false;
            }
        }
        return true;
    }

    private static boolean cornersAreNearWhite(int[] pixels, int w, int h, int minChannel, int maxDelta) {
        int[] cornerIndices = {
                0,
                Math.max(0, w - 1),
                Math.max(0, (h - 1) * w),
                Math.max(0, (h - 1) * w + (w - 1))
        };
        boolean sawOpaqueCorner = false;
        for (int index : cornerIndices) {
            if (((pixels[index] >>> 24) & 0xFF) == 0) {
                continue;
            }
            sawOpaqueCorner = true;
            if (!isNearWhiteGrayscale(pixels[index] & 0xFFFFFF, minChannel, maxDelta)) {
                return false;
            }
        }
        return sawOpaqueCorner;
    }

    private static boolean hasOpaqueNonNearWhiteContent(int[] pixels, byte[] paletteIndices, int w, int h,
                                                        int minChannel, int maxDelta) {
        int contentPixels = 0;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int index = y * w + x;
                int pixel = pixels[index];
                if (((pixel >>> 24) & 0xFF) == 0) {
                    continue;
                }
                if (!isNearWhiteGrayscale(resolvePaletteAwareRgb(pixel, paletteIndices, index), minChannel, maxDelta)) {
                    contentPixels++;
                    if (contentPixels >= 8) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    private static int resolvePaletteAwareRgb(int pixel, byte[] paletteIndices, int index) {
        if (paletteIndices != null && index >= 0 && index < paletteIndices.length) {
            int paletteIndex = paletteIndices[index] & 0xFF;
            int paletteRgb = Palette.SYSTEM_MAC_PALETTE.getColor(paletteIndex) & 0xFFFFFF;
            if (paletteRgb != 0xFFFFFF || (pixel & 0xFFFFFF) == 0xFFFFFF) {
                return paletteRgb;
            }
        }
        return pixel & 0xFFFFFF;
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

    private static boolean[] computeFloodFillTransparency(int[] pixels, byte[] paletteIndices, int w, int h,
                                                          FloodFillMatte matte) {
        boolean[] transparent = new boolean[w * h];
        Queue<Integer> queue = new ArrayDeque<>();

        for (int x = 0; x < w; x++) {
            seedMatte(pixels, paletteIndices, transparent, queue, x, 0, w, matte);
            seedMatte(pixels, paletteIndices, transparent, queue, x, h - 1, w, matte);
        }
        for (int y = 1; y < h - 1; y++) {
            seedMatte(pixels, paletteIndices, transparent, queue, 0, y, w, matte);
            seedMatte(pixels, paletteIndices, transparent, queue, w - 1, y, w, matte);
        }

        while (!queue.isEmpty()) {
            int idx = queue.poll();
            int px = idx % w;
            int py = idx / w;
            if (px > 0)     seedMatte(pixels, paletteIndices, transparent, queue, px - 1, py, w, matte);
            if (px < w - 1) seedMatte(pixels, paletteIndices, transparent, queue, px + 1, py, w, matte);
            if (py > 0)     seedMatte(pixels, paletteIndices, transparent, queue, px, py - 1, w, matte);
            if (py < h - 1) seedMatte(pixels, paletteIndices, transparent, queue, px, py + 1, w, matte);
        }

        return transparent;
    }

    private static void seedMatte(int[] pixels, byte[] paletteIndices, boolean[] transparent,
                                  Queue<Integer> queue, int x, int y, int w, FloodFillMatte matte) {
        int idx = y * w + x;
        if (!transparent[idx] && isTransparentOrMatte(pixels, paletteIndices, idx, matte)) {
            transparent[idx] = true;
            queue.add(idx);
        }
    }

    private static boolean isTransparentOrMatte(int[] pixels, byte[] paletteIndices, int index, FloodFillMatte matte) {
        int pixel = pixels[index];
        if (((pixel >>> 24) & 0xFF) == 0) {
            return true;
        }
        if (matte.usesPaletteIndex() && paletteIndices != null && index < paletteIndices.length) {
            return (paletteIndices[index] & 0xFF) == matte.mattePaletteIndex();
        }
        return matchesRgb(pixel, matte.matteColorRgb(), matte.tolerance());
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
     * Draw an ellipse outline.
     */
    public static void drawEllipse(Bitmap dest, int cx, int cy, int rx, int ry, int color) {
        int x = 0;
        int y = ry;
        int rxSq = rx * rx;
        int rySq = ry * ry;
        int p = (int)(rySq - rxSq * ry + 0.25 * rxSq);

        // Region 1
        while (rySq * x < rxSq * y) {
            dest.setPixel(cx + x, cy + y, color);
            dest.setPixel(cx - x, cy + y, color);
            dest.setPixel(cx + x, cy - y, color);
            dest.setPixel(cx - x, cy - y, color);

            if (p < 0) {
                x++;
                p += 2 * rySq * x + rySq;
            } else {
                x++;
                y--;
                p += 2 * rySq * x - 2 * rxSq * y + rySq;
            }
        }

        // Region 2
        p = (int)(rySq * (x + 0.5) * (x + 0.5) + rxSq * (y - 1) * (y - 1) - rxSq * rySq);
        while (y >= 0) {
            dest.setPixel(cx + x, cy + y, color);
            dest.setPixel(cx - x, cy + y, color);
            dest.setPixel(cx + x, cy - y, color);
            dest.setPixel(cx - x, cy - y, color);

            if (p > 0) {
                y--;
                p -= 2 * rxSq * y + rxSq;
            } else {
                y--;
                x++;
                p += 2 * rySq * x - 2 * rxSq * y + rxSq;
            }
        }
    }
}
