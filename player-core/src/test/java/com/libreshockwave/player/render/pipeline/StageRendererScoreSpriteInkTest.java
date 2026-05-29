package com.libreshockwave.player.render.pipeline;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.chunks.ScoreChunk;
import com.libreshockwave.format.ChunkType;
import com.libreshockwave.id.InkMode;
import com.libreshockwave.player.render.SpriteRegistry;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.nio.ByteOrder;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class StageRendererScoreSpriteInkTest {

    @Test
    void scoreSpriteUsesRuntimeInkOverride() throws Exception {
        StageRenderer renderer = new StageRenderer(newEmptyDirectorFile());
        SpriteRegistry registry = renderer.getSpriteRegistry();
        ScoreChunk.ChannelData data = channelData(InkMode.COPY.code(), 0, 0, 0);

        registry.getOrCreate(7, data).setInk(InkMode.BLEND.code());

        RenderSprite sprite = invokeCreateRenderSprite(renderer, 7, data);
        assertNotNull(sprite);
        assertEquals(InkMode.BLEND.code(), sprite.getInk());
    }

    @Test
    void scoreUpdatesRefreshNonOverriddenInkAndBlend() {
        SpriteRegistry registry = new SpriteRegistry();
        ScoreChunk.ChannelData start = channelData(InkMode.COPY.code(), 0, 0, 0);
        ScoreChunk.ChannelData updated = channelData(InkMode.BLEND.code(), 0, 0, 255);

        registry.getOrCreate(3, start);
        registry.updateFromScore(3, updated);

        assertEquals(InkMode.BLEND.code(), registry.get(3).getInk());
        assertEquals(0, registry.get(3).getBlend());
    }

    @Test
    void scoreUpdatesPreserveExplicitInkAndBlendOverrides() {
        SpriteRegistry registry = new SpriteRegistry();
        ScoreChunk.ChannelData start = channelData(InkMode.COPY.code(), 0, 0, 0);
        ScoreChunk.ChannelData updated = channelData(InkMode.BLEND.code(), 0, 0, 255);

        var state = registry.getOrCreate(4, start);
        state.setInk(InkMode.DARKEN.code());
        state.setBlend(77);

        registry.updateFromScore(4, updated);

        assertEquals(InkMode.DARKEN.code(), registry.get(4).getInk());
        assertEquals(77, registry.get(4).getBlend());
    }

    @Test
    void scoreSpriteResolvesPaletteColorNumbers() throws Exception {
        StageRenderer renderer = new StageRenderer(newEmptyDirectorFile());
        ScoreChunk.ChannelData data = new ScoreChunk.ChannelData(
            1,
            InkMode.COPY.code(),
            0,
            0,
            255,
            0,
            0,
            0,
            0,
            0,
            10,
            10,
            10,
            10,
            0,
            0,
            0,
            0,
            0,
            0,
            0
        );

        RenderSprite sprite = invokeCreateRenderSprite(renderer, 5, data);

        assertNotNull(sprite);
        assertEquals(0x000000, sprite.getForeColor());
    }

    @Test
    void scoreSpriteKeepsExplicitRgbColors() throws Exception {
        StageRenderer renderer = new StageRenderer(newEmptyDirectorFile());
        ScoreChunk.ChannelData data = new ScoreChunk.ChannelData(
            1,
            InkMode.COPY.code(),
            0,
            0,
            0x12,
            0,
            0,
            0,
            0,
            0,
            10,
            10,
            10,
            10,
            1,
            0,
            0,
            0x34,
            0,
            0x56,
            0
        );

        RenderSprite sprite = invokeCreateRenderSprite(renderer, 6, data);

        assertNotNull(sprite);
        assertEquals(0x123456, sprite.getForeColor());
    }

    @Test
    void scoreBehaviorChannelsMarkRenderedSpriteAsBehaviorBacked() throws Exception {
        StageRenderer renderer = new StageRenderer(newEmptyDirectorFile());
        ScoreChunk.ChannelData data = channelData(InkMode.MATTE.code(), 0, 0, 0);

        RenderSprite before = invokeCreateRenderSprite(renderer, 12, data);
        assertNotNull(before);
        assertFalse(before.hasBehaviors());

        renderer.getSpriteRegistry().markScoreBehaviorChannel(12);
        RenderSprite after = invokeCreateRenderSprite(renderer, 12, data);

        assertNotNull(after);
        assertTrue(after.hasBehaviors());
    }

    @Test
    void oldScoreRgbExpansionMatchesNativeFiveBitChannels() {
        assertEquals(0x84CEEF, StageRenderer.expandScoreRgb555(0x84CCE8));
    }

    private static RenderSprite invokeCreateRenderSprite(StageRenderer renderer, int channel,
                                                         ScoreChunk.ChannelData data) throws Exception {
        Method method = StageRenderer.class.getDeclaredMethod(
            "createRenderSprite", int.class, ScoreChunk.ChannelData.class);
        method.setAccessible(true);
        return (RenderSprite) method.invoke(renderer, channel, data);
    }

    private static DirectorFile newEmptyDirectorFile() throws Exception {
        Constructor<DirectorFile> ctor = DirectorFile.class.getDeclaredConstructor(
            ByteOrder.class, boolean.class, int.class, ChunkType.class);
        ctor.setAccessible(true);
        return ctor.newInstance(ByteOrder.BIG_ENDIAN, false, 0, ChunkType.RIFX);
    }

    private static ScoreChunk.ChannelData channelData(int ink, int trails, int stretch, int blendByte) {
        return new ScoreChunk.ChannelData(
            1,
            ink,
            trails,
            stretch,
            0,
            0,
            0,
            0,
            0,
            0,
            10,
            10,
            10,
            10,
            0,
            blendByte,
            0,
            0,
            0,
            0,
            0
        );
    }
}
