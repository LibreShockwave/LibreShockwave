package com.libreshockwave.chunks;

import com.libreshockwave.id.ChunkId;
import com.libreshockwave.io.BinaryReader;
import org.junit.jupiter.api.Test;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

import static org.junit.jupiter.api.Assertions.assertEquals;

class FontMapChunkTest {

    @Test
    void readsDirectorFourFontMapEntries() {
        ByteBuffer buffer = ByteBuffer.allocate(118).order(ByteOrder.BIG_ENDIAN);
        buffer.putInt(0x34); // mapLength
        buffer.putInt(0x3a); // namesLength
        buffer.position(16);
        buffer.putInt(3); // entriesUsed
        buffer.putInt(3); // entriesTotal
        putEntry(buffer, 36, 0x12, 1, 0x8001);
        putEntry(buffer, 44, 0x22, 1, 0x8002);
        putEntry(buffer, 52, 0x2e, 2, 0x8003);
        putName(buffer, 80, "Volter-Bold");
        putName(buffer, 96, "Charcoal");
        putName(buffer, 108, "Arial");

        FontMapChunk chunk = FontMapChunk.read(null, new BinaryReader(buffer.array()), new ChunkId(7));

        assertEquals(3, chunk.entries().size());
        assertEquals("Volter-Bold", chunk.fontNameForId(0x8001));
        assertEquals("Charcoal", chunk.fontNameForId(0x8002));
        assertEquals("Arial", chunk.fontNameForId(0x8003));
        assertEquals(2, chunk.entries().get(2).platform());
    }

    private static void putEntry(ByteBuffer buffer, int offset, int nameOffset, int platform, int fontId) {
        buffer.position(offset);
        buffer.putInt(nameOffset);
        buffer.putShort((short) platform);
        buffer.putShort((short) fontId);
    }

    private static void putName(ByteBuffer buffer, int offset, String name) {
        buffer.position(offset);
        buffer.putShort((short) name.length());
        for (int i = 0; i < name.length(); i++) {
            buffer.put((byte) name.charAt(i));
        }
    }
}
