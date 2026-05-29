package com.libreshockwave.chunks;

import com.libreshockwave.io.BinaryReader;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

class ScoreChunkChannelDataBitsTest {

    @Test
    void readPreservesTrailsAndStretchBitsFromInkByte() {
        byte[] raw = {
            1,
            (byte) 0xE0,
            0,
            0,
            0, 1,
            0, 2,
            0, 0,
            0, 0,
            0, 5,
            0, 6,
            0, 7,
            0, 8,
            0,
            0,
            0,
            0
        };

        ScoreChunk.ChannelData data = ScoreChunk.ChannelData.read(new BinaryReader(raw), 24);

        assertEquals(32, data.ink());
        assertEquals(1, data.trails());
        assertEquals(1, data.stretch());
    }

    @Test
    void readTreatsSpritePositionAsSignedInt16() {
        byte[] raw = {
            1,
            0,
            0,
            0,
            0, 1,
            0, 2,
            0, 0,
            0, 0,
            (byte) 0xFF, (byte) 0xF0,
            (byte) 0xFF, (byte) 0xFA,
            0, 7,
            0, 8,
            0,
            0,
            0,
            0
        };

        ScoreChunk.ChannelData data = ScoreChunk.ChannelData.read(new BinaryReader(raw), 24);

        assertEquals(-16, data.posY());
        assertEquals(-6, data.posX());
        assertEquals(7, data.height());
        assertEquals(8, data.width());
    }
}
