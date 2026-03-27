package com.libreshockwave.bitmap;

import com.libreshockwave.bitmap.Palette.InkMode;

import java.util.ArrayDeque;
import java.util.Queue;

/**
 * Drawing operations for bitmaps including ink mode blending.
 * Implements Director's copyPixels with various ink effects.
 */
public class Drawing {

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
        // For MATTE ink, pre-process the FULL source image with flood-fill matte.
        // Director applies matte to the entire source member, then extracts the
        // copy region. This preserves content that forms "islands" in the full
        // image but would be border-connected in a cropped sub-region (e.g.,
        // cloud bitmaps cropped during turn animations).
        Bitmap effectiveSrc = src;
        int effectiveSrcX = srcX;
        int effectiveSrcY = srcY;
        if (ink == InkMode.MATTE) {
            effectiveSrc = applyMatteToRegion(src, 0, 0, src.getWidth(), src.getHeight());
            effectiveSrcX = srcX;
            effectiveSrcY = srcY;
        }
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
                    if (mx < 0 || mx >= mask.getWidth() || my < 0 || my >= mask.getHeight()
                            || (mask.getPixel(mx, my) >>> 24) == 0) {
                        continue;
                    }
                }

                int srcPixel = effectiveSrc.getPixel(sx, sy);
                int destPixel = dest.getPixel(dx, dy);

                int resultPixel = applyInk(srcPixel, destPixel, ink, blend, backgroundKeyRgb);
                dest.setPixel(dx, dy, resultPixel);
            }
        }
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
                // Source acts as mask, revealing destination
                a = srcR; // Use red channel as alpha
                return alphaBlend(dest, src, a);

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
     * Director's image.createMatte() prefers the image alpha layer when usable.
     */
    public static Bitmap createMatte(Bitmap src) {
        return createMatte(src, 0);
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

        if (src.isNativeAlpha() || src.hasTransparentPixels()) {
            return createAlphaMatte(src, alphaThreshold);
        }

        return createWhiteFloodFillMatte(src);
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

    private static Bitmap createWhiteFloodFillMatte(Bitmap src) {
        int w = src.getWidth();
        int h = src.getHeight();
        int[] pixels = new int[w * h];
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                pixels[y * w + x] = src.getPixel(x, y);
            }
        }

        int matteRgb = 0xFFFFFF;

        boolean[] transparent = new boolean[w * h];
        Queue<Integer> queue = new ArrayDeque<>();

        for (int x = 0; x < w; x++) {
            seedMatte(pixels, transparent, queue, x, 0, w, matteRgb);
            seedMatte(pixels, transparent, queue, x, h - 1, w, matteRgb);
        }
        for (int y = 1; y < h - 1; y++) {
            seedMatte(pixels, transparent, queue, 0, y, w, matteRgb);
            seedMatte(pixels, transparent, queue, w - 1, y, w, matteRgb);
        }

        while (!queue.isEmpty()) {
            int idx = queue.poll();
            int px = idx % w;
            int py = idx / w;
            if (px > 0)     seedMatte(pixels, transparent, queue, px - 1, py, w, matteRgb);
            if (px < w - 1) seedMatte(pixels, transparent, queue, px + 1, py, w, matteRgb);
            if (py > 0)     seedMatte(pixels, transparent, queue, px, py - 1, w, matteRgb);
            if (py < h - 1) seedMatte(pixels, transparent, queue, px, py + 1, w, matteRgb);
        }

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

        Bitmap matte = new Bitmap(w, h, 32, mask);
        matte.setNativeAlpha(true);
        return matte;
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
        int[] pixels = new int[w * h];
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int sx = srcX + x;
                int sy = srcY + y;
                if (sx >= 0 && sx < src.getWidth() && sy >= 0 && sy < src.getHeight()) {
                    pixels[y * w + x] = src.getPixel(sx, sy);
                } else {
                    pixels[y * w + x] = 0xFFFFFFFF;
                }
            }
        }

        int matteRgb = 0xFFFFFF;

        // BFS flood-fill from edges
        boolean[] transparent = new boolean[w * h];
        Queue<Integer> queue = new ArrayDeque<>();

        for (int x = 0; x < w; x++) {
            seedMatte(pixels, transparent, queue, x, 0, w, matteRgb);
            seedMatte(pixels, transparent, queue, x, h - 1, w, matteRgb);
        }
        for (int y = 1; y < h - 1; y++) {
            seedMatte(pixels, transparent, queue, 0, y, w, matteRgb);
            seedMatte(pixels, transparent, queue, w - 1, y, w, matteRgb);
        }

        while (!queue.isEmpty()) {
            int idx = queue.poll();
            int px = idx % w;
            int py = idx / w;
            if (px > 0)     seedMatte(pixels, transparent, queue, px - 1, py, w, matteRgb);
            if (px < w - 1) seedMatte(pixels, transparent, queue, px + 1, py, w, matteRgb);
            if (py > 0)     seedMatte(pixels, transparent, queue, px, py - 1, w, matteRgb);
            if (py < h - 1) seedMatte(pixels, transparent, queue, px, py + 1, w, matteRgb);
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
        if (((pixel >>> 24) & 0xFF) == 0) {
            return true;
        }
        int rgb = pixel & 0xFFFFFF;
        return rgb == matteRgb;
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
