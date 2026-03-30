package com.libreshockwave.player;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.id.InkMode;
import com.libreshockwave.player.cast.CastMember;
import com.libreshockwave.player.input.HitTester;
import com.libreshockwave.player.render.pipeline.RenderSprite;
import com.libreshockwave.player.render.pipeline.StageRenderer;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

class HitTesterTest {

    @Test
    void copyInkAlphaBitmapStillHitsInsideBoundingBox() {
        StageRenderer renderer = new StageRenderer(null);
        renderer.setLastBakedSprites(List.of(createCopyInkAlphaSprite()));

        assertEquals(41, HitTester.hitTest(renderer, 1, 11, 11));
        assertEquals(41, HitTester.hitTest(renderer, 1, 10, 10));
    }

    @Test
    void transparentPixelsDoNotFallThroughToLowerSprite() {
        StageRenderer renderer = new StageRenderer(null);
        renderer.setLastBakedSprites(List.of(createLowerSprite(), createCopyInkAlphaSprite()));

        assertEquals(41, HitTester.hitTest(renderer, 1, 11, 11));
    }

    @Test
    void nativeAlphaBitmapsRespectAlphaThreshold() {
        StageRenderer renderer = new StageRenderer(null);
        renderer.setLastBakedSprites(List.of(createLowerSprite(), createNativeAlphaSprite(42, 128, 0x40)));

        assertEquals(40, HitTester.hitTest(renderer, 1, 11, 11));
    }

    @Test
    void alphaThresholdZeroKeepsWholeNativeAlphaRectClickable() {
        StageRenderer renderer = new StageRenderer(null);
        renderer.setLastBakedSprites(List.of(createLowerSprite(), createNativeAlphaSprite(42, 0, 0x00)));

        assertEquals(42, HitTester.hitTest(renderer, 1, 11, 11));
    }

    private static RenderSprite createCopyInkAlphaSprite() {
        Bitmap bitmap = new Bitmap(3, 3, 32);
        bitmap.fill(0xFFFF0000);
        bitmap.setPixel(1, 1, 0x00000000);

        return new RenderSprite(
                41,
                10, 10,
                3, 3,
                0,
                true,
                RenderSprite.SpriteType.BITMAP,
                null,
                null,
                0, 0,
                false, false,
                InkMode.COPY.code(), 100,
                false, false,
                bitmap,
                true);
    }

    private static RenderSprite createLowerSprite() {
        Bitmap bitmap = new Bitmap(3, 3, 32);
        bitmap.fill(0xFF00FF00);

        return new RenderSprite(
                40,
                10, 10,
                3, 3,
                0,
                true,
                RenderSprite.SpriteType.BITMAP,
                null,
                null,
                0, 0,
                false, false,
                InkMode.COPY.code(), 100,
                false, false,
                bitmap,
                true);
    }

    private static RenderSprite createNativeAlphaSprite(int channel, int alphaThreshold, int centerAlpha) {
        Bitmap bitmap = new Bitmap(3, 3, 32);
        bitmap.fill(0xFFFF0000);
        bitmap.setPixel(1, 1, ((centerAlpha & 0xFF) << 24) | 0x00FF0000);
        bitmap.setNativeAlpha(true);

        CastMember member = new CastMember(1, channel, MemberType.BITMAP);
        member.setBitmapDirectly(bitmap);
        member.setBitmapAlphaThreshold(alphaThreshold);

        return new RenderSprite(
                channel,
                10, 10,
                3, 3,
                0,
                true,
                RenderSprite.SpriteType.BITMAP,
                null,
                member,
                0, 0,
                false, false,
                InkMode.COPY.code(), 100,
                false, false,
                bitmap,
                true);
    }
}
