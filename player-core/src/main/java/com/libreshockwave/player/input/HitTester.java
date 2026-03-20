package com.libreshockwave.player.input;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.id.InkMode;
import com.libreshockwave.player.render.pipeline.RenderSprite;
import com.libreshockwave.player.render.pipeline.StageRenderer;

import java.util.List;
import java.util.function.IntPredicate;

/**
 * Determines which sprite is under a given stage coordinate.
 * Tests from front-to-back (highest locZ/channel first).
 * Supports ink-aware hit testing: sprites with non-Copy ink modes
 * are click-through at transparent pixels (Director behavior).
 */
public final class HitTester {

    private HitTester() {}

    /**
     * Find the front-most visible sprite containing the given point.
     * Transparent pixels in sprites with non-Copy ink are click-through.
     * @return the sprite's channel number, or 0 if no sprite hit
     */
    public static int hitTest(StageRenderer renderer, int frame, int stageX, int stageY) {
        return hitTest(renderer, frame, stageX, stageY, channel -> false);
    }

    /**
     * Find the front-most visible sprite containing the given point.
     * The predicate can force specific channels to use bounding-box hits even
     * when their ink would normally make transparent pixels click-through.
     * @return the sprite's channel number, or 0 if no sprite hit
     */
    public static int hitTest(StageRenderer renderer, int frame, int stageX, int stageY,
                              IntPredicate forceBoundingBox) {
        RenderSprite sprite = findHitSprite(renderer, frame, stageX, stageY, forceBoundingBox);
        return sprite != null ? sprite.getChannel() : 0;
    }

    /**
     * Find the front-most visible sprite containing the given point and return its type.
     * @return the sprite's SpriteType, or null if no sprite hit
     */
    public static RenderSprite.SpriteType hitTestType(StageRenderer renderer, int frame, int stageX, int stageY) {
        return hitTestType(renderer, frame, stageX, stageY, channel -> false);
    }

    /**
     * Find the front-most visible sprite containing the given point and return its type.
     * The predicate can force specific channels to use bounding-box hits even
     * when their ink would normally make transparent pixels click-through.
     * @return the sprite's SpriteType, or null if no sprite hit
     */
    public static RenderSprite.SpriteType hitTestType(StageRenderer renderer, int frame, int stageX, int stageY,
                                                      IntPredicate forceBoundingBox) {
        RenderSprite sprite = findHitSprite(renderer, frame, stageX, stageY, forceBoundingBox);
        return sprite != null ? sprite.getType() : null;
    }

    /**
     * Find the front-most visible sprite at the given stage coordinate.
     * Iterates back-to-front, skipping transparent pixels for non-Copy ink sprites.
     */
    private static RenderSprite findHitSprite(StageRenderer renderer, int frame, int stageX, int stageY,
                                              IntPredicate forceBoundingBox) {
        List<RenderSprite> sprites = renderer.getLastBakedSprites();
        if (sprites == null || sprites.isEmpty()) {
            sprites = renderer.getSpritesForFrame(frame);
        }

        for (int i = sprites.size() - 1; i >= 0; i--) {
            RenderSprite sprite = sprites.get(i);
            if (!sprite.isVisible()) continue;
            if (sprite.getChannel() <= 0) continue;

            int left = sprite.getX();
            int top = sprite.getY();
            int right = left + sprite.getWidth();
            int bottom = top + sprite.getHeight();

            if (stageX >= left && stageX < right && stageY >= top && stageY < bottom) {
                if (isPixelTransparent(sprite, stageX - left, stageY - top,
                        forceBoundingBox.test(sprite.getChannel()))) {
                    continue;
                }
                return sprite;
            }
        }

        return null;
    }

    /**
     * Check if the pixel at (localX, localY) within the sprite's baked bitmap is transparent.
     * In Director, sprites with non-Copy ink modes use pixel-level hit testing -
     * transparent areas are click-through. Copy ink (0) always uses bounding-box.
     */
    private static boolean isPixelTransparent(RenderSprite sprite, int localX, int localY,
                                             boolean forceBoundingBox) {
        if (forceBoundingBox) return false;

        InkMode ink = sprite.getInkMode();
        // Copy ink: always bounding-box hit (opaque rect)
        if (ink == InkMode.COPY) return false;

        // Text and button sprites always use bounding-box hit testing.
        // Their baked bitmaps have transparent backgrounds (due to BACKGROUND_TRANSPARENT ink),
        // but clicking anywhere within the field/button bounds should register.
        RenderSprite.SpriteType type = sprite.getType();
        if (type == RenderSprite.SpriteType.TEXT || type == RenderSprite.SpriteType.BUTTON) return false;

        Bitmap baked = sprite.getBakedBitmap();
        if (baked == null) return false;

        int[] pixels = baked.getPixels();
        if (pixels == null) return false;

        // Scale local coordinates to bitmap coordinates if sprite is stretched
        int bw = baked.getWidth();
        int bh = baked.getHeight();
        int sw = sprite.getWidth();
        int sh = sprite.getHeight();
        int bx = (sw > 0 && sw != bw) ? (localX * bw / sw) : localX;
        int by = (sh > 0 && sh != bh) ? (localY * bh / sh) : localY;

        if (bx < 0 || bx >= bw || by < 0 || by >= bh) return true;

        int idx = by * bw + bx;
        if (idx < 0 || idx >= pixels.length) return true;

        int alpha = (pixels[idx] >> 24) & 0xFF;
        return alpha < 128; // Treat <50% alpha as transparent for hit testing
    }
}
