package com.libreshockwave.player.input;

import com.libreshockwave.player.render.pipeline.RenderSprite;
import com.libreshockwave.player.render.pipeline.StageRenderer;

import java.util.ArrayList;
import java.util.List;
import java.util.function.IntPredicate;

/**
 * Determines which sprite is under a given stage coordinate.
 * Tests from front-to-back (highest locZ/channel first) using sprite bounds.
 * Director-style input targeting does not treat transparent bitmap pixels as
 * click-through.
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
     * The predicate is retained for API compatibility but stage hit testing is
     * always bounding-box based.
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
     * The predicate is retained for API compatibility but stage hit testing is
     * always bounding-box based.
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

            if (stageX >= left && stageX < right && stageY >= top && stageY < bottom) {
                result.add(sprite.getChannel());
            }
        }
        return result;
    }

    /**
     * Find the front-most visible sprite at the given stage coordinate.
     * Iterates back-to-front using sprite bounds only.
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
                return sprite;
            }
        }

        return null;
    }
}
