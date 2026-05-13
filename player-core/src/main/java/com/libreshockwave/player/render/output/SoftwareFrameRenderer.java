package com.libreshockwave.player.render.output;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.id.InkMode;
import com.libreshockwave.player.render.pipeline.FrameSnapshot;
import com.libreshockwave.player.render.pipeline.RenderSprite;

/**
 * Pure-Java software renderer that composites a FrameSnapshot into an ARGB int[] buffer.
 * No AWT dependency — works in WASM via TeaVM and anywhere else.
 */
public final class SoftwareFrameRenderer {

    private SoftwareFrameRenderer() {}

    /**
     * Render a FrameSnapshot to a Bitmap using pure int[] compositing.
     */
    public static Bitmap renderFrame(FrameSnapshot snapshot, int stageWidth, int stageHeight) {
        int pixelCount = stageWidth * stageHeight;
        int[] argb = new int[pixelCount];

        // Match AwtFrameRenderer behavior: stageImage replaces background, not composited on top
        Bitmap stageImage = snapshot.stageImage();
        if (stageImage != null) {
            // Copy stageImage pixels directly (same as AWT drawImage with no bg fill)
            int[] srcPixels = stageImage.getPixels();
            int srcW = stageImage.getWidth();
            int srcH = stageImage.getHeight();
            if (srcPixels != null) {
                for (int y = 0; y < Math.min(srcH, stageHeight); y++) {
                    for (int x = 0; x < Math.min(srcW, stageWidth); x++) {
                        argb[y * stageWidth + x] = srcPixels[y * srcW + x];
                    }
                }
            }
        } else {
            // Fill with background color (opaque)
            int bg = snapshot.backgroundColor() | 0xFF000000;
            for (int i = 0; i < pixelCount; i++) {
                argb[i] = bg;
            }
        }

        // 3. Composite each visible sprite in order
        for (RenderSprite sprite : snapshot.sprites()) {
            if (!sprite.isVisible()) continue;

            Bitmap baked = sprite.getBakedBitmap();
            if (baked == null) continue;
            if (baked.getWidth() <= 0 || baked.getHeight() <= 0) continue;
            if (baked.getPixels() == null || baked.getPixels().length == 0) continue;

            int sx = sprite.getX();
            int sy = sprite.getY();
            int sw = sprite.getWidth() > 0 ? sprite.getWidth() : baked.getWidth();
            int sh = sprite.getHeight() > 0 ? sprite.getHeight() : baked.getHeight();
            int blend = sprite.getBlend();
            InkMode ink = sprite.getInkMode();
            boolean flipH = sprite.isFlipH() ^ sprite.hasDirectorHorizontalMirror();
            boolean flipV = sprite.isFlipV();

            if (sw == baked.getWidth() && sh == baked.getHeight()) {
                blitBitmap(argb, stageWidth, stageHeight,
                        baked.getPixels(), baked.getWidth(), baked.getHeight(),
                        sx, sy, blend, ink, flipH, flipV);
            } else {
                blitBitmapScaled(argb, stageWidth, stageHeight,
                        baked.getPixels(), baked.getWidth(), baked.getHeight(),
                        sx, sy, sw, sh, blend, ink, flipH, flipV);
            }
        }

        // Stage border disabled: Director Shockwave player in browser does not
        // draw a border around the stage — only the standalone projector does.
        // drawStageBorder(argb, stageWidth, stageHeight, 0xFF000000);

        return new Bitmap(stageWidth, stageHeight, 32, argb);
    }

    private static void drawStageBorder(int[] argb, int w, int h, int color) {
        for (int x = 0; x < w; x++) {
            argb[x] = color;
            argb[(h - 1) * w + x] = color;
        }
        for (int y = 0; y < h; y++) {
            argb[y * w] = color;
            argb[y * w + w - 1] = color;
        }
    }

    // ========================================================================
    // Alpha blitting (unscaled)
    // ========================================================================

    static void blitBitmap(int[] argb, int stageWidth, int stageHeight,
                           int[] srcPixels, int srcW, int srcH,
                           int dstX, int dstY, int blend, InkMode ink,
                           boolean flipH, boolean flipV) {
        if (srcPixels == null || srcW <= 0 || srcH <= 0) return;
        if (srcPixels.length < srcW * srcH) return;

        int sx0 = Math.max(0, -dstX);
        int sy0 = Math.max(0, -dstY);
        int sx1 = Math.min(srcW, stageWidth - dstX);
        int sy1 = Math.min(srcH, stageHeight - dstY);
        if (sx0 >= sx1 || sy0 >= sy1) return;

        int argbLen = argb.length;
        boolean useSpecialInk = isSpecialCompositingInk(ink);

        for (int sy = sy0; sy < sy1; sy++) {
            int fetchY = flipV ? (srcH - 1 - sy) : sy;
            for (int sx = sx0; sx < sx1; sx++) {
                int fetchX = flipH ? (srcW - 1 - sx) : sx;
                int srcIdx = fetchY * srcW + fetchX;
                int src = srcPixels[srcIdx];
                int srcA = (src >> 24) & 0xFF;
                if (srcA == 0) continue;

                if (blend < 100) {
                    srcA = (srcA * blend) / 100;
                    if (srcA == 0) continue;
                }

                int dstIdx = (dstY + sy) * stageWidth + (dstX + sx);
                if (dstIdx < 0 || dstIdx >= argbLen) continue;

                if (useSpecialInk) {
                    compositeSpecialInk(argb, dstIdx, src, srcA, ink);
                } else if (srcA >= 255) {
                    argb[dstIdx] = src | 0xFF000000;
                } else {
                    alphaComposite(argb, dstIdx, src, srcA);
                }
            }
        }
    }

    // ========================================================================
    // Alpha blitting (scaled — nearest neighbor)
    // ========================================================================

    static void blitBitmapScaled(int[] argb, int stageWidth, int stageHeight,
                                 int[] srcPixels, int srcW, int srcH,
                                 int dstX, int dstY, int dstW, int dstH, int blend, InkMode ink,
                                 boolean flipH, boolean flipV) {
        if (srcPixels == null || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;
        if (srcPixels.length < srcW * srcH) return;

        int dx0 = Math.max(0, dstX);
        int dy0 = Math.max(0, dstY);
        int dx1 = Math.min(stageWidth, dstX + dstW);
        int dy1 = Math.min(stageHeight, dstY + dstH);
        if (dx0 >= dx1 || dy0 >= dy1) return;

        int srcLen = srcPixels.length;
        int argbLen = argb.length;
        boolean useSpecialInk = isSpecialCompositingInk(ink);

        for (int dy = dy0; dy < dy1; dy++) {
            int srcY = ((dy - dstY) * srcH) / dstH;
            if (flipV) srcY = srcH - 1 - srcY;
            if (srcY < 0 || srcY >= srcH) continue;

            for (int dx = dx0; dx < dx1; dx++) {
                int srcX = ((dx - dstX) * srcW) / dstW;
                if (flipH) srcX = srcW - 1 - srcX;
                if (srcX < 0 || srcX >= srcW) continue;

                int srcIdx = srcY * srcW + srcX;
                if (srcIdx < 0 || srcIdx >= srcLen) continue;

                int src = srcPixels[srcIdx];
                int srcA = (src >> 24) & 0xFF;
                if (srcA == 0) continue;

                if (blend < 100) {
                    srcA = (srcA * blend) / 100;
                    if (srcA == 0) continue;
                }

                int dstIdx = dy * stageWidth + dx;
                if (dstIdx < 0 || dstIdx >= argbLen) continue;

                if (useSpecialInk) {
                    compositeSpecialInk(argb, dstIdx, src, srcA, ink);
                } else if (srcA >= 255) {
                    argb[dstIdx] = src | 0xFF000000;
                } else {
                    alphaComposite(argb, dstIdx, src, srcA);
                }
            }
        }
    }

    // ========================================================================
    // Alpha composite helper
    // ========================================================================

    /**
     * Returns true for ink modes that need special compositing (not standard alpha blend).
     */
    private static boolean isSpecialCompositingInk(InkMode ink) {
        return ink == InkMode.ADD_PIN || ink == InkMode.ADD
            || ink == InkMode.SUBTRACT_PIN || ink == InkMode.SUBTRACT
            || ink == InkMode.LIGHTEST || ink == InkMode.DARKEST
            || ink == InkMode.LIGHTEN
            || ink == InkMode.REVERSE || ink == InkMode.GHOST
            || ink == InkMode.NOT_COPY || ink == InkMode.NOT_TRANSPARENT
            || ink == InkMode.NOT_REVERSE || ink == InkMode.NOT_GHOST;
    }

    /**
     * Composite a source pixel onto the destination using a special ink mode.
     * The srcA parameter controls how much of the effect is applied (blend).
     */
    private static void compositeSpecialInk(int[] argb, int dstIdx, int src, int srcA, InkMode ink) {
        if (dstIdx < 0 || dstIdx >= argb.length) return;
        int dst = argb[dstIdx];

        int srcR = (src >> 16) & 0xFF;
        int srcG = (src >> 8) & 0xFF;
        int srcB = src & 0xFF;
        int dstR = (dst >> 16) & 0xFF;
        int dstG = (dst >> 8) & 0xFF;
        int dstB = dst & 0xFF;

        int outR, outG, outB;

        switch (ink) {
            case ADD_PIN, ADD -> {
                outR = Math.min(255, dstR + srcR);
                outG = Math.min(255, dstG + srcG);
                outB = Math.min(255, dstB + srcB);
            }
            case SUBTRACT_PIN, SUBTRACT -> {
                outR = Math.max(0, dstR - srcR);
                outG = Math.max(0, dstG - srcG);
                outB = Math.max(0, dstB - srcB);
            }
            case DARKEST -> {
                outR = Math.min(dstR, srcR);
                outG = Math.min(dstG, srcG);
                outB = Math.min(dstB, srcB);
            }
            case LIGHTEN, LIGHTEST -> {
                outR = Math.max(dstR, srcR);
                outG = Math.max(dstG, srcG);
                outB = Math.max(dstB, srcB);
            }
            case REVERSE -> {
                // XOR: src ^ dst
                outR = srcR ^ dstR;
                outG = srcG ^ dstG;
                outB = srcB ^ dstB;
            }
            case GHOST -> {
                // AND(~src, dst)
                outR = (~srcR & 0xFF) & dstR;
                outG = (~srcG & 0xFF) & dstG;
                outB = (~srcB & 0xFF) & dstB;
            }
            case NOT_COPY -> {
                // Invert source
                outR = ~srcR & 0xFF;
                outG = ~srcG & 0xFF;
                outB = ~srcB & 0xFF;
            }
            case NOT_TRANSPARENT -> {
                // AND(src, dst)
                outR = srcR & dstR;
                outG = srcG & dstG;
                outB = srcB & dstB;
            }
            case NOT_REVERSE -> {
                // XOR(~src, dst)
                outR = (~srcR & 0xFF) ^ dstR;
                outG = (~srcG & 0xFF) ^ dstG;
                outB = (~srcB & 0xFF) ^ dstB;
            }
            case NOT_GHOST -> {
                // OR(~src, dst)
                outR = (~srcR & 0xFF) | dstR;
                outG = (~srcG & 0xFF) | dstG;
                outB = (~srcB & 0xFF) | dstB;
            }
            default -> {
                alphaComposite(argb, dstIdx, src, srcA);
                return;
            }
        }

        // Apply blend (srcA) as interpolation between dst and result
        if (srcA < 255) {
            int invA = 255 - srcA;
            outR = (outR * srcA + dstR * invA) / 255;
            outG = (outG * srcA + dstG * invA) / 255;
            outB = (outB * srcA + dstB * invA) / 255;
        }

        argb[dstIdx] = 0xFF000000 | (outR << 16) | (outG << 8) | outB;
    }

    private static void alphaComposite(int[] argb, int dstIdx, int src, int srcA) {
        if (dstIdx < 0 || dstIdx >= argb.length) return;
        int dst = argb[dstIdx];
        int dstA = (dst >> 24) & 0xFF;
        int invA = 255 - srcA;

        int outA = srcA + (dstA * invA / 255);
        if (outA == 0) {
            argb[dstIdx] = 0;
            return;
        }

        int srcR = (src >> 16) & 0xFF;
        int srcG = (src >> 8) & 0xFF;
        int srcB = src & 0xFF;
        int dstR = (dst >> 16) & 0xFF;
        int dstG = (dst >> 8) & 0xFF;
        int dstB = dst & 0xFF;

        // Use rounding (+ outA/2) instead of truncation for correct alpha blending
        int half = outA / 2;
        int outR = (srcR * srcA + dstR * dstA * invA / 255 + half) / outA;
        int outG = (srcG * srcA + dstG * dstA * invA / 255 + half) / outA;
        int outB = (srcB * srcA + dstB * dstA * invA / 255 + half) / outA;

        argb[dstIdx] = (outA << 24) | (outR << 16) | (outG << 8) | outB;
    }
}
