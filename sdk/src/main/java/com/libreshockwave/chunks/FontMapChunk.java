package com.libreshockwave.chunks;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.format.ChunkType;
import com.libreshockwave.id.ChunkId;
import com.libreshockwave.io.BinaryReader;

import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;

/**
 * Director 4+ font map chunk (Fmap).
 *
 * The map body stores entries as nameOffset/platform/fontId records. The names
 * table starts after the map body, and each name is stored as a u16 length plus
 * MacRoman bytes.
 */
public record FontMapChunk(
        DirectorFile file,
        ChunkId id,
        List<Entry> entries
) implements Chunk {

    public record Entry(
            int fontId,
            int platform,
            String fontName
    ) {}

    @Override
    public ChunkType type() {
        return ChunkType.Fmap;
    }

    public String fontNameForId(int fontId) {
        for (Entry entry : entries) {
            if (entry.fontId() == fontId) {
                return entry.fontName();
            }
        }
        return null;
    }

    public static FontMapChunk read(DirectorFile file, BinaryReader reader, ChunkId id) {
        ByteOrder originalOrder = reader.getOrder();
        reader.setOrder(ByteOrder.BIG_ENDIAN);

        int mapLength = reader.readI32();
        reader.readI32(); // namesLength
        int bodyStart = reader.getPosition();
        int namesStart = bodyStart + mapLength + 2;

        reader.skip(8); // unknown
        int entriesUsed = reader.readI32();
        reader.skip(16); // entriesTotal + unknowns

        List<Entry> entries = new ArrayList<>();
        for (int i = 0; i < entriesUsed && reader.bytesLeft() >= 8; i++) {
            int nameOffset = reader.readI32();
            int platform = reader.readU16();
            int fontId = reader.readU16();

            int returnPos = reader.getPosition();
            String fontName = "";
            int namePos = namesStart + nameOffset;
            if (namePos >= 0 && namePos + 2 <= reader.length()) {
                reader.seek(namePos);
                int nameLength = reader.readU16();
                if (nameLength >= 0 && nameLength <= reader.bytesLeft()) {
                    fontName = reader.readStringMacRoman(nameLength);
                }
            }
            reader.seek(returnPos);

            entries.add(new Entry(fontId, platform, fontName));
        }

        reader.setOrder(originalOrder);
        return new FontMapChunk(file, id, List.copyOf(entries));
    }
}
