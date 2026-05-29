package com.libreshockwave.chunks;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.format.ChunkType;
import com.libreshockwave.id.ChunkId;

/**
 * Base interface for all Director file chunks.
 */
public sealed interface Chunk permits
        ConfigChunk,
        CastListChunk,
        CastChunk,
        CastMemberChunk,
        KeyTableChunk,
        ScriptContextChunk,
        ScriptNamesChunk,
        ScriptChunk,
        ScoreChunk,
        FrameLabelsChunk,
        BitmapChunk,
        PaletteChunk,
        TextChunk,
        FontMapChunk,
        SoundChunk,
        MediaChunk,
        RawChunk {

    DirectorFile file();

    ChunkType type();

    ChunkId id();
}
