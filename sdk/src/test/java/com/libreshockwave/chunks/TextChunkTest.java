package com.libreshockwave.chunks;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.format.ChunkType;
import com.libreshockwave.id.ChunkId;
import com.libreshockwave.io.BinaryReader;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.nio.ByteOrder;

import static org.junit.jupiter.api.Assertions.assertEquals;

class TextChunkTest {

    @Test
    void directorFiveStxtRunsReadFontSizeAfterLegacyRunPadding() throws Exception {
        byte[] raw = new byte[] {
                0x00, 0x00, 0x00, 0x0c,
                0x00, 0x00, 0x00, 0x18,
                0x00, 0x00, 0x00, 0x16,
                'C', 'o', 'p', 'y', 'r', 'i', 'g', 'h',
                't', ' ', 'H', 'a', 'b', 'b', 'o', ' ',
                'L', 't', 'd', ' ', '2', '0', '0', '1',
                0x00, 0x01, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x00,
                (byte) 0x80,
                0x12, 0x00, 0x00,
                0x00, 0x09,
                (byte) 0xff, (byte) 0xff, (byte) 0xff,
                (byte) 0xff
        };

        TextChunk chunk = TextChunk.read(directorFile(1600), new BinaryReader(raw), new ChunkId(1));

        assertEquals("Copyright Habbo Ltd 2001", chunk.text());
        assertEquals(1, chunk.runs().size());
        TextChunk.TextRun run = chunk.runs().get(0);
        assertEquals(9, run.fontSize());
        assertEquals(128, run.fontStyle());
        assertEquals(255, run.colorR());
        assertEquals(255, run.colorG());
        assertEquals(255, run.colorB());
    }

    @Test
    void modernStxtRunsKeepPostStyleSingleBytePadding() throws Exception {
        byte[] raw = new byte[] {
                0x00, 0x00, 0x00, 0x0c,
                0x00, 0x00, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x00,
                'X',
                0x00, 0x01, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x00,
                0x00,
                0x00,
                0x00, 0x0c,
                0x00, 0x00,
                0x11, 0x22, 0x33,
                0x44
        };

        TextChunk chunk = TextChunk.read(directorFile(1950), new BinaryReader(raw), new ChunkId(1));

        TextChunk.TextRun run = chunk.runs().get(0);
        assertEquals(12, run.fontSize());
        assertEquals(0x11, run.colorR());
        assertEquals(0x22, run.colorG());
        assertEquals(0x33, run.colorB());
    }

    private static DirectorFile directorFile(int directorVersion) throws Exception {
        Constructor<DirectorFile> ctor = DirectorFile.class.getDeclaredConstructor(
                ByteOrder.class, boolean.class, int.class, ChunkType.class);
        ctor.setAccessible(true);
        DirectorFile file = ctor.newInstance(ByteOrder.BIG_ENDIAN, false, directorVersion, ChunkType.MV93);
        ConfigChunk config = new ConfigChunk(file, new ChunkId(0), directorVersion,
                0, 0, 540, 720, 0, 0, 15, 0, 0, 0,
                0, 0, 0, 0, 0, 0, (short) 0);
        Field configField = DirectorFile.class.getDeclaredField("config");
        configField.setAccessible(true);
        configField.set(file, config);
        return file;
    }
}
