package com.libreshockwave.player.input;

import com.libreshockwave.cast.BitmapInfo;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.player.cast.CastMember;
import com.libreshockwave.player.render.pipeline.RenderSprite;
import com.libreshockwave.player.render.pipeline.StageRenderer;

import java.util.ArrayList;
import java.util.List;
import java.util.function.IntPredicate;

/**
 * Determines which sprite is under a given stage coordinate.
 * Tests from front-to-back (highest locZ/channel first).
 * Director-style hit testing is bounds-based by default.
 * Per-pixel alpha hit testing is reserved for true 32-bit bitmap members with
 * native alpha, using the member's alphaThreshold.
 */
public final class HitTester {

    private HitTester() {}

    /**
     * Find the front-most visible sprite containing the given point.
     * @return the sprite's channel number, or 0 if no sprite hit
     */
    public static int hitTest(StageRenderer renderer, int frame, int stageX, int stageY) {
        return hitTest(renderer, frame, stageX, stageY, channel -> false);
    }

    /**
     * Find the front-most visible sprite containing the given point.
     * The predicate is retained for API compatibility.
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
     * The predicate is retained for API compatibility.
     * @return the sprite's SpriteType, or null if no sprite hit
     */
    public static RenderSprite.SpriteType hitTestType(StageRenderer renderer, int frame, int stageX, int stageY,
                                                      IntPredicate forceBoundingBox) {
        RenderSprite sprite = findHitSprite(renderer, frame, stageX, stageY, forceBoundingBox);
        return sprite != null ? sprite.getType() : null;
    }

    /**
     * Find ALL visible sprites containing the given point (front-to-back order).
     * Used to dispatch mouse events to every sprite at the click location,
     * not just the topmost one.
     */
    public static List<Integer> hitTestAll(StageRenderer renderer, int frame, int stageX, int stageY,
                                           IntPredicate filter) {
        List<RenderSprite> sprites = renderer.getLastBakedSprites();
        if (sprites == null || sprites.isEmpty()) {
            sprites = renderer.getSpritesForFrame(frame);
        }
        List<Integer> result = new ArrayList<>();
        for (int i = sprites.size() - 1; i >= 0; i--) {
            RenderSprite sprite = sprites.get(i);
            if (!sprite.isVisible()) continue;
            if (sprite.getChannel() <= 0) continue;

            int left = sprite.getX();
            int top = sprite.getY();
            int right = left + sprite.getWidth();
            int bottom = top + sprite.getHeight();

            if (stageX >= left && stageX < right && stageY >= top && stageY < bottom
                    && hitTestSpritePixel(sprite, stageX, stageY)) {
                result.add(sprite.getChannel());
            }
        }
        return result;
    }

    /**
     * Find the front-most visible sprite at the given stage coordinate.
     * Iterates back-to-front using sprite bounds and Director-style alpha hit
     * testing only for true 32-bit native-alpha bitmap members.
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

            if (stageX >= left && stageX < right && stageY >= top && stageY < bottom
                    && hitTestSpritePixel(sprite, stageX, stageY)) {
                return sprite;
            }
        }

        return null;
    }

    private static boolean hitTestSpritePixel(RenderSprite sprite, int stageX, int stageY) {
        if (sprite == null) {
            return false;
        }

        var baked = sprite.getBakedBitmap();
        if (baked == null) {
            return true;
        }

        int spriteWidth = sprite.getWidth() > 0 ? sprite.getWidth() : baked.getWidth();
        int spriteHeight = sprite.getHeight() > 0 ? sprite.getHeight() : baked.getHeight();
        if (spriteWidth <= 0 || spriteHeight <= 0 || baked.getWidth() <= 0 || baked.getHeight() <= 0) {
            return true;
        }

        int localX = stageX - sprite.getX();
        int localY = stageY - sprite.getY();
        if (localX < 0 || localY < 0 || localX >= spriteWidth || localY >= spriteHeight) {
            return false;
        }

        AlphaHitRule alphaHitRule = getAlphaHitRule(sprite, baked);
        if (!alphaHitRule.enabled()) {
            return true;
        }
        if (alphaHitRule.threshold() <= 0) {
            return true;
        }

        int srcX = (localX * baked.getWidth()) / spriteWidth;
        int srcY = (localY * baked.getHeight()) / spriteHeight;

        if (sprite.isFlipH() ^ sprite.hasDirectorHorizontalMirror()) {
            srcX = baked.getWidth() - 1 - srcX;
        }
        if (sprite.isFlipV()) {
            srcY = baked.getHeight() - 1 - srcY;
        }

        int alpha = (baked.getPixel(srcX, srcY) >>> 24) & 0xFF;
        return alpha >= alphaHitRule.threshold();
    }

    private static AlphaHitRule getAlphaHitRule(RenderSprite sprite, com.libreshockwave.bitmap.Bitmap baked) {
        CastMember dynamicMember = sprite.getDynamicMember();
        if (dynamicMember != null) {
            Bitmap memberBitmap = dynamicMember.getBitmap();
            if (memberBitmap == null) {
                return AlphaHitRule.DISABLED;
            }
            if (memberBitmap.getBitDepth() == 32 && memberBitmap.isNativeAlpha()) {
                return new AlphaHitRule(true, dynamicMember.getBitmapAlphaThreshold());
            }
            return AlphaHitRule.DISABLED;
        }

        if (baked == null || !baked.isNativeAlpha() || baked.getBitDepth() != 32) {
            return AlphaHitRule.DISABLED;
        }

        var castMember = sprite.getCastMember();
        if (castMember == null || !castMember.isBitmap()
                || castMember.specificData() == null || castMember.specificData().length < 10) {
            return AlphaHitRule.DISABLED;
        }

        BitmapInfo info = BitmapInfo.parse(castMember.specificData());
        if (info.bitDepth() != 32) {
            return AlphaHitRule.DISABLED;
        }

        return new AlphaHitRule(true, info.alphaThreshold());
    }

    private record AlphaHitRule(boolean enabled, int threshold) {
        private static final AlphaHitRule DISABLED = new AlphaHitRule(false, 0);
    }
}
