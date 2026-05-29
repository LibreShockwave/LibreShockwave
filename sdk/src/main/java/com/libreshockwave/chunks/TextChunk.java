package com.libreshockwave.chunks;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.format.ChunkType;
import com.libreshockwave.id.ChunkId;
import com.libreshockwave.io.BinaryReader;

import java.util.ArrayList;
import java.util.List;

/**
 * Styled text chunk (STXT).
 * Contains text content with formatting information.
 */
public record TextChunk(
    DirectorFile file,
    ChunkId id,
    String text,
    List<TextRun> runs
) implements Chunk {

    @Override
    public ChunkType type() {
        return ChunkType.STXT;
    }

    public record TextRun(
        int startOffset,
        int endOffset,
        int fontId,
        int fontSize,
        int fontStyle,
        int colorR,
        int colorG,
        int colorB
    ) {}

    public static TextChunk read(DirectorFile file, BinaryReader reader, ChunkId id) {
        // STXT chunks always use big-endian format regardless of file endianness
        java.nio.ByteOrder originalOrder = reader.getOrder();
        reader.setOrder(java.nio.ByteOrder.BIG_ENDIAN);

        int headerLen = reader.readI32();
        int textLen = reader.readI32();

        reader.skip(headerLen - 8);

        String text = reader.readStringMacRoman(textLen);

        // Parse formatting runs if present
        List<TextRun> runs = new ArrayList<>();
        if (reader.bytesLeft() >= 4) {
            int runCount = reader.readI16();
            reader.skip(2);

            for (int i = 0; i < runCount && reader.bytesLeft() >= 16; i++) {
                int startOffset = reader.readI32();
                int fontId = reader.readI16();
                int fontStyle = reader.readU8();
                int fontSize;
                if (usesLegacyRunLayout(file)) {
                    reader.skip(3);
                    fontSize = reader.readI16();
                } else {
                    reader.skip(1);
                    fontSize = reader.readI16();
                    reader.skip(2); // unknown
                }
                int colorR = reader.readU8();
                int colorG = reader.readU8();
                int colorB = reader.readU8();
                reader.skip(1); // unknown

                runs.add(new TextRun(startOffset, textLen, fontId, fontSize, fontStyle,
                        colorR, colorG, colorB));
            }
        }

        // Restore original byte order
        reader.setOrder(originalOrder);

        return new TextChunk(file, id, text, runs);
    }

    private static boolean usesLegacyRunLayout(DirectorFile file) {
        return file != null && file.getConfig() != null
                && file.getConfig().directorVersion() > 0
                && file.getConfig().directorVersion() <= 1600;
    }
}
