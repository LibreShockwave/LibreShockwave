package com.libreshockwave.chunks;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.format.ChunkType;
import com.libreshockwave.id.*;
import com.libreshockwave.io.BinaryReader;

import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;

/**
 * Score chunk (VWSC).
 * Contains the timeline/score data with frames and sprite channels.
 *
 * Structure matches dirplayer-rs vm-rust/src/director/chunks/score.rs
 */
public record ScoreChunk(
    DirectorFile file,
    ChunkId id,
    Header header,
    List<byte[]> entries,
    ScoreFrameData frameData,
    List<FrameInterval> frameIntervals
) implements Chunk {

    @Override
    public ChunkType type() {
        return ChunkType.VWSC;
    }

    /**
     * Score chunk header (24 bytes).
     */
    public record Header(
        int totalLength,
        int unk1,
        int unk2,
        int entryCount,
        int unk3,
        int entrySizeSum
    ) {
        public static Header read(BinaryReader reader) {
            return new Header(
                reader.readI32(),
                reader.readI32(),
                reader.readI32(),
                reader.readI32(),
                reader.readI32(),
                reader.readI32()
            );
        }
    }

    /**
     * Frame data header.
     */
    public record FrameDataHeader(
        int frameCount,
        int spriteRecordSize,
        int numChannels,
        int framesVersion
    ) {}

    /**
     * Per-frame tempo channel data.
     * In D5: tempo byte at offset 21 in the 48-byte main channel area.
     * In D6+: channel 5 contains a 20-byte tempo record (tempo FPS at byte 4).
     */
    public record TempoChannelData(
        int frameIndex,
        int tempo
    ) {}

    /**
     * Per-frame palette channel data.
     * In D6+: channel 4 contains palette info (castLib at bytes 0-1, member at bytes 2-3).
     * The palette member ID determines which palette all 8-bit bitmaps use for color lookup.
     */
    public record PaletteChannelData(
        int frameIndex,
        int castLib,
        int castMember
    ) {}

    /**
     * Per-channel sprite data (24+ bytes per channel).
     */
    public record ChannelData(
        int spriteType,
        int ink,
        int trails,
        int stretch,
        int foreColor,
        int backColor,
        int castLib,
        int castMember,
        int unk1,
        int unk2,
        int posY,
        int posX,
        int height,
        int width,
        int colorFlag,
        int blendByte,
        int thicknessFlags,
        int foreColorG,
        int backColorG,
        int foreColorB,
        int backColorB
    ) {
        public static final ChannelData EMPTY = new ChannelData(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

        /** Typed accessor: returns null if castLib is 0 (empty slot). */
        public CastLibId castLibId() {
            return castLib > 0 ? new CastLibId(castLib) : null;
        }

        /** Typed accessor: returns null if castMember is 0 (empty slot). */
        public MemberId memberId() {
            return castMember > 0 ? new MemberId(castMember) : null;
        }

        public static ChannelData read(BinaryReader reader) {
            return read(reader, 28);
        }

        public static ChannelData read(BinaryReader reader, int spriteRecordSize) {
            int spriteType = reader.readU8();
            // D7: ink is bits 0-5; bit 6 = trails, bit 7 = stretch
            int inkByte = reader.readU8();
            int ink = inkByte & 0x3F;
            int trails = (inkByte >> 6) & 0x1;
            int stretch = (inkByte >> 7) & 0x1;
            int foreColor = reader.readU8();
            int backColor = reader.readU8();
            int castLib = reader.readU16();
            int castMember = reader.readU16();
            int unk1 = reader.readU16();
            int unk2 = reader.readU16();
            int posY = reader.readI16();
            int posX = reader.readI16();
            int height = reader.readU16();
            int width = reader.readU16();
            // 20 bytes read so far

            int colorFlag = 0;
            int blendByte = 0;
            int thicknessFlags = 0;
            int foreColorG = 0, backColorG = 0, foreColorB = 0, backColorB = 0;

            if (spriteRecordSize >= 24) {
                // Bytes 20-23: colorFlag, blend, and unknown bytes
                int unk3 = reader.readU8();
                colorFlag = (unk3 & 0xF0) >> 4;
                blendByte = reader.readU8(); // byte 21: blend (0-255 scale, used with BLEND ink)
                thicknessFlags = reader.readU8(); // byte 22: thickness + flip/blend flags
                reader.readU8(); // unk6
            }

            if (spriteRecordSize >= 28) {
                // Bytes 24-27: extended RGB color components
                foreColorG = reader.readU8();
                backColorG = reader.readU8();
                foreColorB = reader.readU8();
                backColorB = reader.readU8();
            }

            return new ChannelData(
                spriteType, ink, trails, stretch, foreColor, backColor,
                castLib, castMember, unk1, unk2,
                posY, posX, height, width,
                colorFlag, blendByte, thicknessFlags, foreColorG, backColorG, foreColorB, backColorB
            );
        }

        public boolean isEmpty() {
            return spriteType == 0 && ink == 0 && foreColor == 0 && backColor == 0
                && castLib == 0 && castMember == 0 && posY == 0 && posX == 0
                && height == 0 && width == 0;
        }

        /** Resolve foreColor: assemble RGB only when colorFlag bit 0 is explicitly set. */
        public int resolvedForeColor() {
            if ((colorFlag & 0x1) != 0) {
                return (foreColor << 16) | (foreColorG << 8) | foreColorB;
            }
            return foreColor;
        }

        /** Whether foreColor is an explicit RGB value (colorFlag bit 0 set). */
        public boolean isForeColorRGB() {
            return (colorFlag & 0x1) != 0;
        }

        /** Whether backColor is an explicit RGB value (colorFlag bit 1 set). */
        public boolean isBackColorRGB() {
            return (colorFlag & 0x2) != 0;
        }

        public boolean isFlipH() {
            return (thicknessFlags & 0x20) != 0;
        }

        public boolean isFlipV() {
            return (thicknessFlags & 0x40) != 0;
        }

        /** Resolve backColor: assemble RGB only when colorFlag bit 1 is explicitly set. */
        public int resolvedBackColor() {
            if ((colorFlag & 0x2) != 0) {
                return (backColor << 16) | (backColorG << 8) | backColorB;
            }
            return backColor;
        }
    }

    /**
     * Parsed frame data from Entry[0].
     */
    public record ScoreFrameData(
        FrameDataHeader header,
        byte[] decompressedData,
        List<FrameChannelEntry> frameChannelData,
        List<TempoChannelData> tempoChannelData,
        List<PaletteChannelData> paletteChannelData
    ) {
        public static final ScoreFrameData EMPTY = new ScoreFrameData(
            new FrameDataHeader(0, 24, 0, 0),
            new byte[0],
            new ArrayList<>(),
            new ArrayList<>(),
            new ArrayList<>()
        );
    }

    /**
     * Individual frame/channel data entry.
     */
    public record FrameChannelEntry(
        FrameIndex frameIndex,
        ChannelId channelIndex,
        ChannelData data
    ) {}

    /**
     * Primary interval (44 bytes) - defines frame range and channel.
     */
    public record FrameIntervalPrimary(
        int startFrame,
        int endFrame,
        int unk0,
        int unk1,
        int channelIndex,
        int unk2,
        int unk3,
        int unk4,
        int unk5,
        int unk6,
        int unk7,
        int unk8
    ) {
        /** Typed accessor for start frame (1-based). */
        public FrameId startFrameId() { return new FrameId(Math.max(1, startFrame)); }
        /** Typed accessor for end frame (1-based). */
        public FrameId endFrameId() { return new FrameId(Math.max(1, endFrame)); }
        /** Typed accessor for channel. */
        public ChannelId channelId() { return new ChannelId(channelIndex); }

        public static FrameIntervalPrimary read(BinaryReader reader) {
            return new FrameIntervalPrimary(
                reader.readI32(),
                reader.readI32(),
                reader.readI32(),
                reader.readI32(),
                reader.readI32(),
                reader.readU16(),
                reader.readI32(),
                reader.readU16(),
                reader.readI32(),
                reader.readI32(),
                reader.readI32(),
                reader.readI32()
            );
        }
    }

    /**
     * Secondary interval (8 bytes) - references behavior script.
     */
    public record FrameIntervalSecondary(
        int castLib,
        int castMember,
        int unk0
    ) {
        /** Typed accessor: returns null if castLib is 0. */
        public CastLibId castLibId() { return castLib > 0 ? new CastLibId(castLib) : null; }
        /** Typed accessor: returns null if castMember is 0. */
        public MemberId memberId() { return castMember > 0 ? new MemberId(castMember) : null; }

        public static FrameIntervalSecondary read(BinaryReader reader) {
            return new FrameIntervalSecondary(
                reader.readU16(),
                reader.readU16(),
                reader.readI32()
            );
        }
    }

    /**
     * Combined frame interval with primary and optional secondary.
     */
    public record FrameInterval(
        FrameIntervalPrimary primary,
        FrameIntervalSecondary secondary
    ) {}

    public int getFrameCount() {
        return frameData != null ? frameData.header().frameCount() : 0;
    }

    public int getChannelCount() {
        return frameData != null ? frameData.header().numChannels() : 0;
    }

    /** Raw decompressed channel data for diagnostic purposes. */
    public byte[] getRawChannelData() {
        return frameData != null ? frameData.decompressedData() : null;
    }

    public int getSpriteRecordSize() {
        return frameData != null ? frameData.header().spriteRecordSize() : 0;
    }

    /**
     * Get the tempo (FPS) set in the score's tempo channel for the given frame.
     * Returns the most recent tempo change at or before the given frame,
     * or -1 if no tempo channel data exists.
     * @param frame 0-indexed frame number
     */
    public int getFrameTempo(int frame) {
        if (frameData == null || frameData.tempoChannelData() == null) return -1;
        int result = -1;
        for (TempoChannelData td : frameData.tempoChannelData()) {
            if (td.frameIndex() <= frame) {
                result = td.tempo();
            } else {
                break;
            }
        }
        return result;
    }

    /**
     * Get the palette member info for the given frame from the score's palette channel.
     * Returns the most recent palette channel data at or before the given frame,
     * or null if no palette channel data exists.
     * @param frame 0-indexed frame number
     */
    public PaletteChannelData getFramePalette(int frame) {
        if (frameData == null || frameData.paletteChannelData() == null) return null;
        PaletteChannelData result = null;
        for (PaletteChannelData pd : frameData.paletteChannelData()) {
            if (pd.frameIndex() <= frame) {
                result = pd;
            } else {
                break;
            }
        }
        return result;
    }

    public static ScoreChunk read(DirectorFile file, BinaryReader reader, ChunkId id, int version) {
        reader.setOrder(ByteOrder.BIG_ENDIAN);

        if (reader.bytesLeft() < 24) {
            return createEmpty(file, id);
        }

        // Read header
        Header header = Header.read(reader);

        // Read offsets table
        List<Integer> offsets = new ArrayList<>();
        for (int i = 0; i <= header.entryCount(); i++) {
            if (reader.bytesLeft() < 4) break;
            offsets.add(reader.readI32());
        }

        // Validate offsets
        if (offsets.size() != header.entryCount() + 1) {
            return createEmpty(file, id);
        }

        for (int j = 0; j < offsets.size() - 1; j++) {
            if (offsets.get(j) > offsets.get(j + 1)) {
                return createEmpty(file, id);
            }
        }

        // Read all entries
        List<byte[]> entries = new ArrayList<>();
        for (int i = 0; i < header.entryCount(); i++) {
            int currentOffset = offsets.get(i);
            int nextOffset = offsets.get(i + 1);
            int length = nextOffset - currentOffset;

            if (length > 0 && reader.bytesLeft() >= length) {
                entries.add(reader.readBytes(length));
            } else {
                entries.add(new byte[0]);
            }
        }

        // Process frame data from first entry (Entry[0])
        ScoreFrameData frameData = ScoreFrameData.EMPTY;
        if (!entries.isEmpty() && entries.get(0).length > 0) {
            frameData = parseFrameData(entries.get(0));
        }

        // Process behavior attachment entries (Entry[2+])
        List<FrameInterval> frameIntervals = parseFrameIntervals(entries);

        return new ScoreChunk(file, id, header, entries, frameData, frameIntervals);
    }

    private static ScoreChunk createEmpty(DirectorFile file, ChunkId id) {
        return new ScoreChunk(
            file,
            id,
            new Header(0, 0, 0, 0, 0, 0),
            new ArrayList<>(),
            ScoreFrameData.EMPTY,
            new ArrayList<>()
        );
    }

    private static ScoreFrameData parseFrameData(byte[] data) {
        BinaryReader reader = new BinaryReader(data, ByteOrder.BIG_ENDIAN);

        if (reader.bytesLeft() < 20) {
            return ScoreFrameData.EMPTY;
        }

        // Read header
        int actualLength = reader.readI32();
        int unk1 = reader.readI32();
        int frameCount = reader.readI32();
        int framesVersion = reader.readU16();
        int spriteRecordSize = reader.readU16();
        int numChannels = reader.readU16();

        // Skip numChannelsDisplayed based on version
        if (framesVersion > 13) {
            reader.skip(2);
        } else {
            reader.skip(2);
        }

        FrameDataHeader header = new FrameDataHeader(frameCount, spriteRecordSize, numChannels, framesVersion);

        // Allocate channel data buffer with overflow protection
        int frameSize = numChannels * spriteRecordSize;
        long totalSizeLong = (long) frameCount * frameSize;
        if (totalSizeLong <= 0 || totalSizeLong > 50_000_000) {
            return ScoreFrameData.EMPTY; // Sanity limit or overflow
        }
        int totalSize = (int) totalSizeLong;
        byte[] channelData = new byte[totalSize];

        // Read delta-compressed frame data
        int frameIndex = 0;
        while (!reader.eof() && frameIndex < frameCount) {
            if (reader.bytesLeft() < 2) break;

            int length = reader.readU16();
            if (length == 0) break;

            // Carry forward previous frame data before applying deltas
            if (frameIndex > 0) {
                int prevOffset = (frameIndex - 1) * frameSize;
                int currOffset = frameIndex * frameSize;
                System.arraycopy(channelData, prevOffset, channelData, currOffset, frameSize);
            }

            int frameLength = length - 2;
            if (frameLength > 0 && reader.bytesLeft() >= frameLength) {
                byte[] frameBytes = reader.readBytes(frameLength);
                BinaryReader frameReader = new BinaryReader(frameBytes, ByteOrder.BIG_ENDIAN);

                // Parse delta entries for this frame
                while (!frameReader.eof() && frameReader.bytesLeft() >= 4) {
                    int channelSize = frameReader.readU16();
                    int channelOffset = frameReader.readU16();

                    if (channelSize > 0 && frameReader.bytesLeft() >= channelSize) {
                        byte[] channelDelta = frameReader.readBytes(channelSize);

                        int frameOffset = frameIndex * frameSize;
                        int destOffset = frameOffset + channelOffset;
                        int endOffset = destOffset + channelSize;

                        if (endOffset <= channelData.length) {
                            System.arraycopy(channelDelta, 0, channelData, destOffset, channelSize);
                        }
                    } else {
                        break;
                    }
                }
            }
            frameIndex++;
        }

        // D5 (framesVersion <= 7): first 48 bytes are packed main channels
        // (script, sounds, transition, tempo, palette), then sprite channels.
        // D6+ (framesVersion > 7): all channels uniform at spriteRecordSize each.
        // Channel 5 is the tempo channel in D6+.
        int mainChannelsSize = framesVersion <= 7 ? 48 : 0;
        boolean isD5 = mainChannelsSize > 0;

        // Parse channel data into structured entries + tempo data
        List<FrameChannelEntry> frameChannelEntries = new ArrayList<>();
        List<TempoChannelData> tempoChannelEntries = new ArrayList<>();
        List<PaletteChannelData> paletteChannelEntries = new ArrayList<>();

        for (int f = 0; f < frameCount; f++) {
            int frameStart = f * frameSize;

            if (isD5) {
                // D5: Tempo at byte 21 within the 48-byte main channel area
                if (frameStart + 22 <= channelData.length) {
                    int tempoVal = channelData[frameStart + 21] & 0xFF;
                    if (tempoVal > 0) {
                        tempoChannelEntries.add(new TempoChannelData(f, tempoVal));
                    }
                }

                // Sprite channels start at byte 48
                int numSprites = (frameSize - mainChannelsSize) / spriteRecordSize;
                for (int s = 0; s < numSprites; s++) {
                    int pos = frameStart + mainChannelsSize + s * spriteRecordSize;
                    if (pos + 24 > channelData.length) break;

                    BinaryReader channelReader = new BinaryReader(channelData, ByteOrder.BIG_ENDIAN);
                    channelReader.setPosition(pos);
                    ChannelData cd = ChannelData.read(channelReader, spriteRecordSize);
                    if (!cd.isEmpty()) {
                        int channelIndex = s + 6; // Sprite channels start at 6 in D5
                        frameChannelEntries.add(new FrameChannelEntry(new FrameIndex(f), new ChannelId(channelIndex), cd));
                    }
                }
            } else {
                // D6+: All channels uniform
                for (int c = 0; c < numChannels; c++) {
                    int pos = frameStart + c * spriteRecordSize;
                    if (pos + spriteRecordSize > channelData.length) break;

                    if (c == 4) {
                        // Tempo channel (D6+: channel 4, per ScummVM kScoreChannelTempo):
                        // tempo FPS at byte 6 within the 48-byte record
                        if (pos + 7 <= channelData.length) {
                            int tempoVal = channelData[pos + 6] & 0xFF;
                            if (tempoVal > 0 && tempoVal <= 120) {
                                tempoChannelEntries.add(new TempoChannelData(f, tempoVal));
                            }
                        }
                    } else if (c == 5) {
                        // Palette channel (D6+: channel 5, per ScummVM kScoreChannelPalette):
                        // castLib (sint16) at bytes 0-1, member (sint16) at bytes 2-3
                        if (pos + 4 <= channelData.length) {
                            int palCastLib = (short)(((channelData[pos] & 0xFF) << 8) | (channelData[pos + 1] & 0xFF));
                            int palMember = (short)(((channelData[pos + 2] & 0xFF) << 8) | (channelData[pos + 3] & 0xFF));
                            if (palMember != 0) {
                                paletteChannelEntries.add(new PaletteChannelData(f, palCastLib, palMember));
                            }
                        }
                    } else {
                        // Sprite/other channel
                        if (pos + 24 <= channelData.length) {
                            BinaryReader channelReader = new BinaryReader(channelData, ByteOrder.BIG_ENDIAN);
                            channelReader.setPosition(pos);
                            ChannelData cd = ChannelData.read(channelReader, spriteRecordSize);
                            if (!cd.isEmpty()) {
                                frameChannelEntries.add(new FrameChannelEntry(new FrameIndex(f), new ChannelId(c), cd));
                            }
                        }
                    }
                }
            }
        }

        return new ScoreFrameData(header, channelData, frameChannelEntries, tempoChannelEntries, paletteChannelEntries);
    }

    private static List<FrameInterval> parseFrameIntervals(List<byte[]> entries) {
        List<FrameInterval> results = new ArrayList<>();
        int i = 2; // Start at 2, skip entries 0 and 1

        while (i < entries.size()) {
            byte[] entryBytes = entries.get(i);

            if (entryBytes.length == 0) {
                i++;
                continue;
            }

            if (entryBytes.length >= 44 && entryBytes.length <= 48) {
                // Primary entry (44 bytes, or 48 bytes with 4 extra trailing bytes)
                BinaryReader reader = new BinaryReader(entryBytes, ByteOrder.BIG_ENDIAN);
                FrameIntervalPrimary primary = FrameIntervalPrimary.read(reader);

                // Look ahead for secondary entries
                List<FrameIntervalSecondary> secondaries = new ArrayList<>();
                int j = i + 1;

                while (j < entries.size()) {
                    int nextSize = entries.get(j).length;

                    // Check if this could be a behavior entry (8 bytes per behavior)
                    if (nextSize >= 8 && nextSize % 8 == 0) {
                        int behaviorCount = nextSize / 8;
                        BinaryReader secReader = new BinaryReader(entries.get(j), ByteOrder.BIG_ENDIAN);

                        boolean foundValidBehavior = false;
                        for (int b = 0; b < behaviorCount; b++) {
                            int castLib = secReader.readU16();
                            int castMember = secReader.readU16();
                            int unk0 = secReader.readI32();

                            if (castLib > 0 && castMember > 0) {
                                secondaries.add(new FrameIntervalSecondary(castLib, castMember, unk0));
                                foundValidBehavior = true;
                            }
                        }

                        if (foundValidBehavior) {
                            j++;
                        } else {
                            break;
                        }
                    } else {
                        break;
                    }
                }

                // Create result entries
                if (secondaries.isEmpty()) {
                    results.add(new FrameInterval(primary, null));
                } else {
                    for (FrameIntervalSecondary secondary : secondaries) {
                        results.add(new FrameInterval(primary, secondary));
                    }
                }

                i = j;
                continue;
            }

            i++;
        }

        return results;
    }
}
