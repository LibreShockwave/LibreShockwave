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
}
