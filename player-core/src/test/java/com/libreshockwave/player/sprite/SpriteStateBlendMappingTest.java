package com.libreshockwave.player.sprite;

import com.libreshockwave.chunks.ScoreChunk;
import com.libreshockwave.id.InkMode;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

class SpriteStateBlendMappingTest {

    @Test
    void constructorConvertsScoreBlendByteUsingDirectorInvertedScale() {
        SpriteState sprite = new SpriteState(1, channelData(InkMode.BLEND.code(), 255));
        assertEquals(0, sprite.getBlend());

        sprite = new SpriteState(1, channelData(InkMode.BLEND.code(), 128));
        assertEquals(Math.round((255 - 128) * 100f / 255f), sprite.getBlend());

        sprite = new SpriteState(1, channelData(InkMode.BACKGROUND_TRANSPARENT.code(), 204));
        assertEquals(Math.round((255 - 204) * 100f / 255f), sprite.getBlend());
    }

    @Test
    void applyScoreDefaultsUsesSameInvertedBlendScale() {
        SpriteState sprite = new SpriteState(1);
        sprite.applyScoreDefaults(channelData(InkMode.BLEND.code(), 255));
        assertEquals(0, sprite.getBlend());

        SpriteState partial = new SpriteState(1);
        partial.applyScoreDefaults(channelData(InkMode.MATTE.code(), 64));
        assertEquals(Math.round((255 - 64) * 100f / 255f), partial.getBlend());
    }

    private static ScoreChunk.ChannelData channelData(int ink, int blendByte) {
        return new ScoreChunk.ChannelData(
            1,
            ink,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
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
