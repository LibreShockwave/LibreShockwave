package com.libreshockwave.chunks;

import com.libreshockwave.id.ChunkId;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

class ScoreChunkTest {

    private static ScoreChunk.FrameInterval interval(int startFrame, int endFrame, int channelIndex) {
        return new ScoreChunk.FrameInterval(
                new ScoreChunk.FrameIntervalPrimary(startFrame, endFrame, 0, 0, channelIndex, 0, 0, 0, 0, 0, 0, 0),
                null);
    }

    private static ScoreChunk score(ScoreChunk.ScoreFrameData frameData,
                                    List<ScoreChunk.FrameInterval> intervals) {
        return new ScoreChunk(null, new ChunkId(1), new ScoreChunk.Header(0, 0, 0, 0, 0, 0),
                List.of(), frameData, intervals);
    }

    @Test
    void frameCountFallsBackToIntervalsWhenFrameBufferEmpty() {
        // Interval-based scores leave the per-frame buffer empty; the real placement is in intervals.
        ScoreChunk sc = score(ScoreChunk.ScoreFrameData.EMPTY, List.of(
                interval(1, 1382, 5),
                interval(1000, 1027, 23)));
        assertEquals(1382, sc.getFrameCount());
    }

    @Test
    void frameCountIgnoresImplausibleChannelFromParseOverrun() {
        ScoreChunk sc = score(ScoreChunk.ScoreFrameData.EMPTY, List.of(
                interval(1, 1382, 5),
                interval(0, 1701999465, 540096049))); // garbage channel + frame from an over-read
        assertEquals(1382, sc.getFrameCount());
    }

    @Test
    void frameCountUsesHeaderWhenPresent() {
        ScoreChunk.ScoreFrameData fd = new ScoreChunk.ScoreFrameData(
                new ScoreChunk.FrameDataHeader(42, 24, 6, 7),
                new byte[0], List.of(), List.of(), List.of());
        ScoreChunk sc = score(fd, List.of(interval(1, 9999, 5)));
        assertEquals(42, sc.getFrameCount()); // header count wins; intervals are not consulted
    }
}
