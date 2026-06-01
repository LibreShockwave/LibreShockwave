package com.libreshockwave.player.render.output;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.player.render.pipeline.FrameSnapshot;
import com.libreshockwave.player.render.pipeline.RenderPipelineTrace;
import com.libreshockwave.player.render.pipeline.RenderSprite;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

class SoftwareFrameRendererTransformTest {

    @Test
    void spriteBlendUsesPercentCompositingOverOpaqueStage() {
        assertBlackThirtyPercentBlend(0x668085, 0xFF475A5D);
    }

    @Test
    void spriteBlendUsesDirectorPercentRoundingForHalfFractions() {
        assertBlackThirtyPercentBlend(0x005500, 0xFF003B00);
    }

    @Test
    void spriteBlendUsesDirectorFixedPointOpacityForNavigatorShadowColors() {
        assertBlackThirtyPercentBlend(0x53686C, 0xFF3A494B);
        assertBlackThirtyPercentBlend(0x517900, 0xFF385500);
    }

    private static void assertBlackThirtyPercentBlend(int backgroundColor, int expectedColor) {
        Bitmap src = new Bitmap(1, 1, 32, new int[]{
                0xFF000000
        });
        RenderSprite sprite = new RenderSprite(
                1,
                0, 0,
                1, 1,
                0,
                true,
                RenderSprite.SpriteType.BITMAP,
                null,
                null,
                0, 0xFFFFFF,
                false, false,
                0, 30,
                false, false,
                0.0, 0.0,
                src,
                false
        );

        Bitmap rendered = new FrameSnapshot(
                1, 1, 1, backgroundColor,
                List.of(sprite),
                "",
                null,
                0,
                RenderPipelineTrace.EMPTY
        ).renderFrame();

        assertEquals(expectedColor, rendered.getPixel(0, 0));
    }

    @Test
    void directorMirrorTransformFlipsSpriteHorizontally() {
        Bitmap src = new Bitmap(2, 1, 32, new int[]{
                0xFFFF0000,
                0xFF0000FF
        });
        RenderSprite sprite = new RenderSprite(
                1,
                0, 0,
                2, 1,
                0,
                true,
                RenderSprite.SpriteType.BITMAP,
                null,
                null,
                0, 0xFFFFFF,
                false, false,
                0, 100,
                false, false,
                180.0, 180.0,
                src,
                false
        );

        Bitmap rendered = new FrameSnapshot(
                1, 2, 1, 0,
                List.of(sprite),
                "",
                null,
                0,
                RenderPipelineTrace.EMPTY
        ).renderFrame();

        assertEquals(0xFF0000FF, rendered.getPixel(0, 0));
        assertEquals(0xFFFF0000, rendered.getPixel(1, 0));
    }

    @Test
    void directorMirrorAndFlipHCancelOut() {
        Bitmap src = new Bitmap(2, 1, 32, new int[]{
                0xFFFF0000,
                0xFF0000FF
        });
        RenderSprite sprite = new RenderSprite(
                1,
                0, 0,
                2, 1,
                0,
                true,
                RenderSprite.SpriteType.BITMAP,
                null,
                null,
                0, 0xFFFFFF,
                false, false,
                0, 100,
                true, false,
                180.0, 180.0,
                src,
                false
        );

        Bitmap rendered = new FrameSnapshot(
                1, 2, 1, 0,
                List.of(sprite),
                "",
                null,
                0,
                RenderPipelineTrace.EMPTY
        ).renderFrame();

        assertEquals(0xFFFF0000, rendered.getPixel(0, 0));
        assertEquals(0xFF0000FF, rendered.getPixel(1, 0));
    }

    @Test
    void scaledTransparentSpriteMatchesAwtRenderer() {
        Bitmap src = new Bitmap(2, 2, 32, new int[]{
                0x00FFFFFF, 0xFF000000,
                0xFFCCCCCC, 0x00FFFFFF
        });
        RenderSprite sprite = new RenderSprite(
                1,
                0, 0,
                5, 3,
                0,
                true,
                RenderSprite.SpriteType.BITMAP,
                null,
                null,
                8, 0xFFFFFF,
                false, false,
                0, 100,
                false, false,
                0.0, 0.0,
                src,
                false
        );

        FrameSnapshot snapshot = new FrameSnapshot(
                1, 5, 3, 0,
                List.of(sprite),
                "",
                null,
                0,
                RenderPipelineTrace.EMPTY
        );

        Bitmap software = snapshot.renderFrame();
        Bitmap awt = AwtFrameRenderer.renderFrame(snapshot, 5, 3);

        for (int y = 0; y < 3; y++) {
            for (int x = 0; x < 5; x++) {
                assertEquals(awt.getPixel(x, y), software.getPixel(x, y),
                        "pixel mismatch at " + x + "," + y);
            }
        }
    }
}
