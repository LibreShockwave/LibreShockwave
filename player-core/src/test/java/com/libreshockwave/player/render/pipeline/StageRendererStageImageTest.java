package com.libreshockwave.player.render.pipeline;

import com.libreshockwave.bitmap.Bitmap;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

class StageRendererStageImageTest {

    @Test
    void pristineStageImageDoesNotOverrideUpdatedBackground() {
        StageRenderer renderer = new StageRenderer(null);

        Bitmap stageImage = renderer.getStageImage();
        assertEquals(0xFFFFFFFF, stageImage.getPixel(0, 0));

        renderer.setBackgroundColor(0x000000);

        assertEquals(0xFF000000, stageImage.getPixel(0, 0));
        assertNull(renderer.getRenderableStageImage());

        FrameSnapshot snapshot = new FrameSnapshot(
                1,
                1,
                1,
                renderer.getBackgroundColor(),
                List.of(),
                "",
                renderer.getRenderableStageImage(),
                0,
                RenderPipelineTrace.EMPTY
        );

        assertEquals(0xFF000000, snapshot.renderFrame().getPixel(0, 0));
    }

    @Test
    void scriptModifiedStageImageStillRenders() {
        StageRenderer renderer = new StageRenderer(null);

        Bitmap stageImage = renderer.getStageImage();
        stageImage.setPixel(0, 0, 0xFFFFFFFF);
        stageImage.markScriptModified();
        renderer.setBackgroundColor(0x000000);

        assertSame(stageImage, renderer.getRenderableStageImage());

        FrameSnapshot snapshot = new FrameSnapshot(
                1,
                1,
                1,
                renderer.getBackgroundColor(),
                List.of(),
                "",
                renderer.getRenderableStageImage(),
                0,
                RenderPipelineTrace.EMPTY
        );

        assertEquals(0xFFFFFFFF, snapshot.renderFrame().getPixel(0, 0));
    }

    @Test
    void resetClearsStageImage() {
        StageRenderer renderer = new StageRenderer(null);
        renderer.getStageImage().markScriptModified();

        renderer.reset();

        assertFalse(renderer.hasStageImage());
        assertNull(renderer.getRenderableStageImage());
    }
}
