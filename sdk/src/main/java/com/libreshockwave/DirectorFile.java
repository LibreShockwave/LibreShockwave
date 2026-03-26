package com.libreshockwave;

import com.libreshockwave.cast.BitmapInfo;
import com.libreshockwave.chunks.*;
import com.libreshockwave.format.AfterburnerReader;
import com.libreshockwave.format.ChunkType;
import com.libreshockwave.id.ChunkId;
import com.libreshockwave.io.BinaryReader;
import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.bitmap.BitmapDecoder;
import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.lookup.CastMemberLookup;
import com.libreshockwave.lookup.PaletteResolver;
import com.libreshockwave.lookup.ScriptLookup;

import java.io.IOException;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;

/**
 * Main entry point for reading Director/Shockwave files.
 * Supports .dir, .dxr, .dcr, and .cst files.
 */
public class DirectorFile {

    // Global parse deadline for WASM safety (0 = no deadline)
    static long parseDeadline = 0;

    /** Check if the parse deadline has expired. Call from hot loops. */
    public static boolean isParseTimedOut() {
        long d = parseDeadline;
        return d != 0 && System.currentTimeMillis() > d;
    }

    private final ByteOrder endian;
    private final boolean afterburner;
    private final int version;
    private final ChunkType movieType;
    private String basePath;  // Base path for resolving external casts

    private ConfigChunk config;
    private KeyTableChunk keyTable;
    private CastListChunk castList;
    private ScriptContextChunk scriptContext;
    private final List<ScriptContextChunk> allScriptContexts = new ArrayList<>();
    private ScriptNamesChunk scriptNames;  // Default/primary names chunk
    private final Map<ChunkId, ScriptNamesChunk> scriptNamesById = new HashMap<>();
    private ScoreChunk scoreChunk;
    private FrameLabelsChunk frameLabelsChunk;

    private final Map<ChunkId, Chunk> chunks = new HashMap<>();
    private final Map<ChunkId, ChunkInfo> chunkInfo = new HashMap<>();
    private final List<CastChunk> casts = new ArrayList<>();
    private final List<CastMemberChunk> castMembers = new ArrayList<>();
    private final List<ScriptChunk> scripts = new ArrayList<>();
    private final List<PaletteChunk> palettes = new ArrayList<>();
    private boolean capitalX = false;  // True if file uses LctX (capital X) format

    // Raw file bytes kept so evicted chunks (BITD etc.) can be re-parsed on demand
    private byte[] dataStore;
    private AfterburnerReader afterburnerReader;

    // Lazy-initialized helper classes
    private PaletteResolver paletteResolver;
    private CastMemberLookup castMemberLookup;
    private ScriptLookup scriptLookup;

    /**
     * Pluggable JPEG decoder for ediM bitmap support.
     * Must be set by the runtime environment (e.g. AWT ImageIO for desktop, JS for browser).
     * If not set, ediM bitmaps (32-bit JPEG-compressed) will be skipped.
     */
    @FunctionalInterface
    public interface JpegDecoder {
        Bitmap decode(byte[] jpegData);
    }

    private static JpegDecoder jpegDecoder;

    public static void setJpegDecoder(JpegDecoder decoder) {
        jpegDecoder = decoder;
    }

    public record ChunkInfo(
        ChunkId id,
        int fourcc,
        int offset,
        int length,
        int uncompressedLength
    ) {
        public ChunkType type() {
            return ChunkType.fromFourCC(fourcc);
        }
    }

    private DirectorFile(ByteOrder endian, boolean afterburner, int version, ChunkType movieType) {
        this.endian = endian;
        this.afterburner = afterburner;
        this.version = version;
        this.movieType = movieType;
        this.basePath = "";
    }

    // Getters

    public ByteOrder getEndian() { return endian; }
    public boolean isAfterburner() { return afterburner; }
    public int getVersion() { return version; }
    public ChunkType getMovieType() { return movieType; }
    public ConfigChunk getConfig() { return config; }
    public KeyTableChunk getKeyTable() { return keyTable; }
    public CastListChunk getCastList() { return castList; }
    public ScriptContextChunk getScriptContext() { return scriptContext; }
    public ScriptNamesChunk getScriptNames() { return scriptNames; }
    public boolean isCapitalX() { return capitalX; }
    public List<CastChunk> getCasts() { return Collections.unmodifiableList(casts); }
    public List<CastMemberChunk> getCastMembers() { return Collections.unmodifiableList(castMembers); }

    /**
     * Get a cast member by its score index (castLib, castMember).
     * Handles the minMember offset from the cast list.
     * @param castLib Cast library (0 for internal, 1+ for external)
     * @param castMemberIndex 0-based cast member index from score
     * @return The cast member, or null if not found
     */
    public CastMemberChunk getCastMemberByIndex(int castLib, int castMemberIndex) {
        return getCastMemberLookup().getByIndex(castLib, castMemberIndex);
    }

    private CastMemberLookup getCastMemberLookup() {
        if (castMemberLookup == null) {
            castMemberLookup = new CastMemberLookup(casts, castMembers, castList, config);
        }
        return castMemberLookup;
    }

    /**
     * Get a cast member by its member number (from score behavior references).
     * The member number is the slot position as seen in Director's cast window.
     * This uses the CASp chunk to map member numbers to chunk IDs.
     * @param castLib Cast library (1+)
     * @param memberNumber The member number as stored in the score
     * @return The cast member, or null if not found
     */
    public CastMemberChunk getCastMemberByNumber(int castLib, int memberNumber) {
        return getCastMemberLookup().getByNumber(castLib, memberNumber);
    }

    /**
     * Get a script by its context ID (the scriptId stored in cast members).
     * This uses the ScriptContextChunk (Lctx) to map scriptId to chunk ID.
     * @param scriptId The script ID from the cast member
     * @return The script chunk, or null if not found
     */
    public ScriptChunk getScriptByContextId(int scriptId) {
        return getScriptLookup().getByContextId(scriptId);
    }

    /**
     * Get the reliable script type for a script by looking up its associated cast member.
     * The script type stored in the cast member's specificData is the authoritative source.
     * @param script The script chunk to get the type for
     * @return The script type from the cast member, or null if not found
     */
    public ScriptChunk.ScriptType getScriptType(ScriptChunk script) {
        return getScriptLookup().getScriptType(script);
    }

    private ScriptLookup getScriptLookup() {
        if (scriptLookup == null) {
            scriptLookup = new ScriptLookup(scripts, allScriptContexts, castMembers);
        }
        return scriptLookup;
    }

    public List<ScriptChunk> getScripts() { return Collections.unmodifiableList(scripts); }
    public List<PaletteChunk> getPalettes() { return Collections.unmodifiableList(palettes); }
    public Collection<ChunkInfo> getAllChunkInfo() { return Collections.unmodifiableCollection(chunkInfo.values()); }
    public ChunkInfo getChunkInfo(ChunkId id) { return chunkInfo.get(id); }
    public String getBasePath() { return basePath; }
    public ScoreChunk getScoreChunk() { return scoreChunk; }
    public FrameLabelsChunk getFrameLabelsChunk() { return frameLabelsChunk; }
    public void setBasePath(String basePath) { this.basePath = basePath != null ? basePath : ""; }

    public Chunk getChunk(ChunkId id) {
        Chunk chunk = chunks.get(id);
        if (chunk != null) return chunk;
        // Parse chunk from raw file bytes if not yet loaded
        return reparseChunk(id);
    }

    /**
     * Release audio and other non-essential chunks to free memory.
     * In WASM, audio can't be played, so SoundChunk/MediaChunk data is wasted heap.
     * Also releases RawChunks (unimplemented chunk types).
     * Keeps BitmapChunk, TextChunk, ScriptChunk, PaletteChunk (needed for rendering/VM).
     */
    public void releaseNonEssentialChunks() {
        chunks.values().removeIf(chunk ->
            chunk instanceof com.libreshockwave.chunks.SoundChunk ||
            chunk instanceof com.libreshockwave.chunks.MediaChunk ||
            chunk instanceof com.libreshockwave.chunks.RawChunk);
    }


    /**
     * Re-parse a chunk from the raw dataStore or afterburnerReader.
     * Used when a chunk hasn't been parsed yet (lazy afterburner loading).
     * Thread-safe for concurrent BitmapCache decode access.
     */
    private Chunk reparseChunk(ChunkId id) {
        Chunk existing = chunks.get(id);
        if (existing != null) return existing;

        ChunkInfo info = chunkInfo.get(id);
        if (info == null) return null;

        try {
            BinaryReader chunkReader;
            if (afterburner && afterburnerReader != null) {
                byte[] chunkData = afterburnerReader.getChunkData(id.value());
                if (chunkData == null) return null;
                chunkReader = new BinaryReader(chunkData, endian);
            } else if (dataStore != null) {
                chunkReader = new BinaryReader(dataStore, endian)
                    .sliceReaderAt(info.offset, info.length);
            } else {
                return null;
            }

            int version = config != null ? config.directorVersion() : 0;
            Chunk chunk = parseChunkFromReader(chunkReader, info, version, capitalX);
            if (chunk != null) {
                chunks.put(id, chunk);
            }
            return chunk;
        } catch (Exception e) {
            return null;
        }
    }


    public <T extends Chunk> Optional<T> getChunk(ChunkId id, Class<T> type) {
        Chunk chunk = chunks.get(id);
        if (type.isInstance(chunk)) {
            return Optional.of(type.cast(chunk));
        }
        return Optional.empty();
    }

    /**
     * Look up XMED chunk for a cast member via KEY* table and parse into a fully-styled
     * XmedStyledText record (combining XMED data with CASt specificData).
     * Used for Director 7+ Text Asset Xtra members.
     */
    public com.libreshockwave.cast.XmedStyledText getXmedStyledTextForMember(CastMemberChunk member) {
        if (keyTable == null) return null;
        for (KeyTableChunk.KeyTableEntry entry : keyTable.getEntriesForOwner(member.id())) {
            if (entry.fourccString().equals("XMED")) {
                Chunk chunk = getChunk(entry.sectionId());
                if (chunk instanceof RawChunk rc) {
                    return com.libreshockwave.cast.XmedTextParser.parseStyled(
                            rc.data(), member.specificData());
                }
            }
        }
        return null;
    }

    /**
     * Look up XMED chunk for a cast member via KEY* table and parse text content.
     * Used for Director 7+ Text Asset Xtra members.
     * @deprecated Use {@link #getXmedStyledTextForMember(CastMemberChunk)} instead.
     */
    @Deprecated
    public com.libreshockwave.cast.XmedTextParser.XmedText getXmedTextForMember(CastMemberChunk member) {
        if (keyTable == null) return null;
        for (KeyTableChunk.KeyTableEntry entry : keyTable.getEntriesForOwner(member.id())) {
            if (entry.fourccString().equals("XMED")) {
                Chunk chunk = getChunk(entry.sectionId());
                if (chunk instanceof RawChunk rc) {
                    return com.libreshockwave.cast.XmedTextParser.parse(rc.data());
                }
            }
        }
        return null;
    }

    /**
     * Look up embedded score (VWSC/SCVW) chunk for a film loop cast member via KEY* table.
     * Film loops contain their own mini-score with sub-sprites.
     */
    public ScoreChunk getScoreForMember(CastMemberChunk member) {
        if (keyTable == null) return null;
        for (KeyTableChunk.KeyTableEntry entry : keyTable.getEntriesForOwner(member.id())) {
            String fourcc = entry.fourccString().trim();
            if (fourcc.equals("VWSC") || fourcc.equals("SCVW")) {
                Chunk chunk = getChunk(entry.sectionId());
                if (chunk instanceof ScoreChunk sc) {
                    return sc;
                }
            }
        }
        return null;
    }

    /**
     * Look up STXT (styled text) chunk for a cast member via KEY* table.
     */
    public com.libreshockwave.chunks.TextChunk getTextForMember(CastMemberChunk member) {
        if (keyTable == null) return null;
        for (KeyTableChunk.KeyTableEntry entry : keyTable.getEntriesForOwner(member.id())) {
            if (entry.fourccString().equals("STXT")) {
                Chunk chunk = getChunk(entry.sectionId());
                if (chunk instanceof com.libreshockwave.chunks.TextChunk tc) {
                    return tc;
                }
            }
        }
        return null;
    }

    // Stage properties

    public int getStageWidth() {
        return config != null ? config.stageWidth() : 0;
    }

    public int getStageHeight() {
        return config != null ? config.stageHeight() : 0;
    }

    public int getTempo() {
        return config != null ? config.tempo() : 15;
    }

    /**
     * Get the tempo set in the score's tempo channel for the given frame.
     * @param frame 0-indexed frame number
     * @return tempo in FPS, or -1 if no score tempo data exists
     */
    public int getScoreTempo(int frame) {
        return scoreChunk != null ? scoreChunk.getFrameTempo(frame) : -1;
    }

    /**
     * Get the score's palette member info for a given frame.
     * Returns null if no palette channel data exists.
     * @param frame 0-indexed frame number
     */
    public ScoreChunk.PaletteChannelData getScorePalette(int frame) {
        return scoreChunk != null ? scoreChunk.getFramePalette(frame) : null;
    }

    /**
     * Resolve a palette by ID.
     * Handles both built-in palettes (negative IDs) and custom cast member palettes (non-negative IDs).
     * @param paletteId The palette ID from BitmapInfo
     * @return The resolved Palette, or System Mac palette as fallback
     */
    public Palette resolvePalette(int paletteId) {
        return getPaletteResolver().resolve(paletteId);
    }

    /**
     * Resolve a palette by ID without fallbacks.
     * Returns null if the palette cannot be found in this file.
     * Used for cross-file palette resolution.
     */
    public Palette resolvePaletteExact(int paletteId) {
        return getPaletteResolver().resolveExact(paletteId);
    }

    /**
     * Resolve a palette by cast member number.
     * Used for paletteRef override where we have the member number of the palette cast member.
     * @param memberNumber The member number of the palette cast member
     * @return The resolved Palette, or null if not found
     */
    public Palette resolvePaletteByMemberNumber(int memberNumber) {
        // PaletteResolver uses paletteId = memberNumber - 1 convention
        return getPaletteResolver().resolve(memberNumber - 1);
    }

    private PaletteResolver getPaletteResolver() {
        if (paletteResolver == null) {
            paletteResolver = new PaletteResolver(casts, castMembers, palettes, castList,
                config, keyTable, this::getChunk);
        }
        return paletteResolver;
    }

    /**
     * Decode a bitmap cast member using an explicit palette override.
     * Used for palette swap animation where the runtime palette differs from the embedded one.
     * @param member The cast member chunk (must be a bitmap type)
     * @param paletteOverride The palette to use instead of the embedded palette reference
     * @return Optional containing the decoded bitmap, or empty if decoding fails
     */
    public Optional<Bitmap> decodeBitmap(CastMemberChunk member, Palette paletteOverride) {
        return decodeBitmapInternal(member, paletteOverride);
    }

    /**
     * Decode a bitmap cast member to a Bitmap object.
     * @param member The cast member chunk (must be a bitmap type)
     * @return Optional containing the decoded bitmap, or empty if decoding fails
     */
    public Optional<Bitmap> decodeBitmap(CastMemberChunk member) {
        return decodeBitmapInternal(member, null);
    }

    private Optional<Bitmap> decodeBitmapInternal(CastMemberChunk member, Palette paletteOverride) {
        if (!member.isBitmap() || keyTable == null) {
            return Optional.empty();
        }

        try {
            int directorVersion = config != null ? config.directorVersion() : 1200;
            BitmapInfo info = BitmapInfo.parse(member.specificData(), directorVersion);

            // Find BITD chunk via key table
            BitmapChunk bitmapChunk = null;
            byte[] ediMData = null;
            byte[] alfaData = null;

            for (KeyTableChunk.KeyTableEntry entry : keyTable.getEntriesForOwner(member.id())) {
                String fourcc = entry.fourccString();
                if (fourcc.equals("BITD")) {
                    Chunk chunk = getChunk(entry.sectionId());
                    if (chunk instanceof BitmapChunk bc) {
                        bitmapChunk = bc;
                    }
                } else if (fourcc.equals("ediM")) {
                    // ediM chunk contains JPEG-compressed RGB data for 32-bit bitmaps
                    Chunk chunk2 = getChunk(entry.sectionId());
                    if (chunk2 instanceof MediaChunk mc) {
                        ediMData = mc.audioData(); // Raw data stored in audioData field
                    } else if (chunk2 instanceof RawChunk rc) {
                        ediMData = rc.data();
                    }
                } else if (fourcc.equals("ALFA")) {
                    // ALFA chunk contains alpha channel data
                    Chunk chunk = getChunk(entry.sectionId());
                    if (chunk instanceof RawChunk rc) {
                        alfaData = rc.data();
                    }
                }
            }

            // Try ediM + ALFA decoding for 32-bit bitmaps
            if (bitmapChunk == null && ediMData != null && info.bitDepth() == 32) {
                return decodeEdiMBitmap(info, ediMData, alfaData);
            }

            if (bitmapChunk == null) {
                return Optional.empty();
            }

            // Use palette override if provided, otherwise resolve from embedded reference
            Palette palette = paletteOverride != null ? paletteOverride : resolvePalette(info.paletteId());

            // Decode bitmap with pitch info for accurate scan width
            boolean bigEndian = endian == ByteOrder.BIG_ENDIAN;

            Bitmap bitmap = BitmapDecoder.decode(
                bitmapChunk.data(),
                info.width(), info.height(), info.bitDepth(),
                palette, bigEndian, directorVersion, info.pitch()
            );
            bitmap.setNativeAlpha(info.useAlpha());
            if (palette != null) {
                bitmap.setImagePalette(palette);
            }

            return Optional.of(bitmap);
        } catch (Exception e) {
            return Optional.empty();
        }
    }

    /**
     * Decode a 32-bit bitmap from ediM (JPEG RGB) + ALFA (alpha channel) chunks.
     */
    private Optional<Bitmap> decodeEdiMBitmap(BitmapInfo info, byte[] jpegData, byte[] alfaData) {
        try {
            if (jpegDecoder == null) {
                return Optional.empty();
            }

            // Decode JPEG RGB data via pluggable decoder
            Bitmap jpegBitmap = jpegDecoder.decode(jpegData);
            if (jpegBitmap == null) {
                return Optional.empty();
            }

            int width = info.width();
            int height = info.height();

            // Create bitmap
            Bitmap bitmap = new Bitmap(width, height, 32);

            // Copy RGB from decoded JPEG
            for (int y = 0; y < height && y < jpegBitmap.getHeight(); y++) {
                for (int x = 0; x < width && x < jpegBitmap.getWidth(); x++) {
                    int rgb = jpegBitmap.getPixel(x, y);
                    int r = (rgb >> 16) & 0xFF;
                    int g = (rgb >> 8) & 0xFF;
                    int b = rgb & 0xFF;
                    bitmap.setPixelRGB(x, y, r, g, b);
                }
            }

            // Apply alpha channel if present
            if (alfaData != null && alfaData.length > 0) {
                // Decompress alpha using RLE
                int scanWidth = BitmapDecoder.calculateScanWidth(width, 8);
                int expectedSize = scanWidth * height;
                byte[] alphaChannel = BitmapDecoder.decompressRLE(alfaData, expectedSize);

                // Apply alpha to bitmap
                for (int y = 0; y < height; y++) {
                    int rowOffset = y * scanWidth;
                    for (int x = 0; x < width; x++) {
                        int byteIndex = rowOffset + x;
                        if (byteIndex < alphaChannel.length) {
                            int alpha = alphaChannel[byteIndex] & 0xFF;
                            int pixel = bitmap.getPixel(x, y);
                            bitmap.setPixel(x, y, (alpha << 24) | (pixel & 0x00FFFFFF));
                        }
                    }
                }
            } else {
                // No alpha data - set all pixels to fully opaque
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        int pixel = bitmap.getPixel(x, y);
                        bitmap.setPixel(x, y, 0xFF000000 | (pixel & 0x00FFFFFF));
                    }
                }
            }

            bitmap.setNativeAlpha(info.useAlpha() || (alfaData != null && alfaData.length > 0));

            return Optional.of(bitmap);
        } catch (Exception e) {
            return Optional.empty();
        }
    }

    /**
     * Get the number of sprite channels available.
     * Director 7+ has 1000 channels, earlier versions had fewer.
     */
    public int getChannelCount() {
        if (config == null) return 120; // default

        int dirVer = config.directorVersion();
        // Convert internal version to human-readable:
        // 1000-1099 = D4, 1100-1199 = D5, 1200-1299 = D6, 1300-1399 = D7,
        // 1400-1499 = D8, 1500-1599 = D9, 1600-1699 = D10, 1700-1799 = D11,
        // 1800-1899 = D12
        int humanVer = (dirVer / 100) * 100;
        if (humanVer >= 1300) { // Director 7+
            return 1000;
        } else if (humanVer >= 1200) { // Director 6
            return 120;
        } else if (humanVer >= 1100) { // Director 5
            return 48;
        } else {
            return 48;
        }
    }

    /**
     * Get handler name from the default script names chunk.
     * For more accurate lookup, use getHandlerName(int, ScriptContextChunk).
     */
    public String getHandlerName(int nameId) {
        if (scriptNames != null) {
            return scriptNames.getName(nameId);
        }
        return "<unknown:" + nameId + ">";
    }


    /**
     * Get a ScriptNamesChunk by its resource ID.
     */
    public ScriptNamesChunk getScriptNamesById(ChunkId id) {
        return scriptNamesById.get(id);
    }

    /**
     * Get the correct ScriptNamesChunk (Lnam) for a given ScriptChunk.
     * Each Lctx has its own lnamSectionId; this finds which Lctx owns the script
     * and returns the associated Lnam. Falls back to the default/global Lnam.
     */
    public ScriptNamesChunk getScriptNamesForScript(ScriptChunk script) {
        if (script == null) return scriptNames;
        for (ScriptContextChunk ctx : allScriptContexts) {
            for (var entry : ctx.entries()) {
                if (entry.id().equals(script.id())) {
                    ScriptNamesChunk names = scriptNamesById.get(ctx.lnamSectionId());
                    return names != null ? names : scriptNames;
                }
            }
        }
        return scriptNames;
    }

    /**
     * Get list of external cast file paths referenced by this movie.
     * Returns the raw paths as stored in the cast list.
     */
    public List<String> getExternalCastPaths() {
        List<String> paths = new ArrayList<>();
        if (castList != null) {
            for (CastListChunk.CastListEntry entry : castList.entries()) {
                if (entry.path() != null && !entry.path().isEmpty()) {
                    paths.add(entry.path());
                }
            }
        }
        return paths;
    }

    /**
     * Check if this file references external casts.
     */
    public boolean hasExternalCasts() {
        if (castList == null) return false;
        for (CastListChunk.CastListEntry entry : castList.entries()) {
            if (entry.path() != null && !entry.path().isEmpty()) {
                return true;
            }
        }
        return false;
    }

    /**
     * Check if this file has score data.
     */
    public boolean hasScore() {
        return scoreChunk != null && scoreChunk.getFrameCount() > 0;
    }

    // Script globals and properties

    /**
     * Get all unique global variable names declared across all scripts.
     * @return Set of global variable names
     */
    public Set<String> getAllGlobalNames() {
        Set<String> globals = new LinkedHashSet<>();
        if (scriptNames == null) return globals;

        for (ScriptChunk script : scripts) {
            globals.addAll(script.getGlobalNames(scriptNames));
        }
        return globals;
    }

    /**
     * Get all unique property names declared across all scripts.
     * Properties are instance variables for parent scripts/behaviors.
     * @return Set of property names
     */
    public Set<String> getAllPropertyNames() {
        Set<String> properties = new LinkedHashSet<>();
        if (scriptNames == null) return properties;

        for (ScriptChunk script : scripts) {
            properties.addAll(script.getPropertyNames(scriptNames));
        }
        return properties;
    }

    /**
     * Get globals declared by a specific script.
     * @param script The script chunk
     * @return List of global variable names, or empty list if no script names available
     */
    public List<String> getScriptGlobals(ScriptChunk script) {
        if (scriptNames == null) return List.of();
        return script.getGlobalNames(scriptNames);
    }

    /**
     * Get properties declared by a specific script.
     * @param script The script chunk
     * @return List of property names, or empty list if no script names available
     */
    public List<String> getScriptProperties(ScriptChunk script) {
        if (scriptNames == null) return List.of();
        return script.getPropertyNames(scriptNames);
    }

    /**
     * Information about globals and properties for a script.
     */
    public record ScriptInfo(
        int scriptId,
        String scriptName,
        ScriptChunk.ScriptType scriptType,
        List<String> globals,
        List<String> properties,
        List<String> handlers
    ) {}

    /**
     * Get detailed info about all scripts including their globals, properties, and handlers.
     * @return List of ScriptInfo records
     */
    public List<ScriptInfo> getScriptInfoList() {
        List<ScriptInfo> result = new ArrayList<>();
        if (scriptNames == null) return result;

        // Build a map from script chunk ID to cast member name
        Map<ChunkId, String> scriptIdToName = new HashMap<>();
        if (scriptContext != null) {
            List<ScriptContextChunk.ScriptEntry> entries = scriptContext.entries();
            for (int i = 0; i < entries.size(); i++) {
                int contextIndex = i + 1;
                ChunkId chunkId = entries.get(i).id();
                // Find the cast member with this scriptId
                for (CastMemberChunk member : castMembers) {
                    if (member.isScript() && member.scriptId() == contextIndex) {
                        scriptIdToName.put(chunkId, member.name());
                        break;
                    }
                }
            }
        }

        for (ScriptChunk script : scripts) {
            List<String> handlerNames = new ArrayList<>();
            for (ScriptChunk.Handler handler : script.handlers()) {
                handlerNames.add(scriptNames.getName(handler.nameId()));
            }

            result.add(new ScriptInfo(
                script.id().value(),
                scriptIdToName.getOrDefault(script.id(), ""),
                script.getScriptType(),
                script.getGlobalNames(scriptNames),
                script.getPropertyNames(scriptNames),
                handlerNames
            ));
        }
        return result;
    }

    // Loading

    public static DirectorFile load(Path path) throws IOException {
        byte[] data = Files.readAllBytes(path);
        DirectorFile file = load(data);
        // Set base path from file location for external cast resolution
        if (path.getParent() != null) {
            file.setBasePath(path.getParent().toString());
        }
        return file;
    }

    public static DirectorFile load(byte[] data) throws IOException {
        BinaryReader reader = new BinaryReader(data);

        // Read container header
        int containerFourCC = reader.readFourCC();
        ChunkType container = ChunkType.fromFourCC(containerFourCC);

        ByteOrder endian;
        boolean isRIFF = false;
        if (container == ChunkType.RIFX) {
            endian = ByteOrder.BIG_ENDIAN;
        } else if (container == ChunkType.XFIR) {
            endian = ByteOrder.LITTLE_ENDIAN;
        } else if (container == ChunkType.RIFF) {
            // D3 Windows RIFF format: tags are big-endian, data is little-endian
            endian = ByteOrder.LITTLE_ENDIAN;
            isRIFF = true;
        } else if (container == ChunkType.FFIR) {
            endian = ByteOrder.LITTLE_ENDIAN;
            isRIFF = true;
        } else {
            throw new IOException("Not a valid Director file: expected RIFX/XFIR/RIFF, got " +
                BinaryReader.fourCCToString(containerFourCC));
        }

        reader.setOrder(endian);
        int fileSize = reader.readI32();

        if (isRIFF) {
            // D3 RIFF format: RIFF + size + RMMP, then CFTC chunk table
            int rmmpFourCC = reader.readFourCC();
            ChunkType rmmpType = ChunkType.fromFourCC(rmmpFourCC);
            if (rmmpType != ChunkType.RMMP) {
                throw new IOException("Expected RMMP marker in RIFF file, got " +
                    BinaryReader.fourCCToString(rmmpFourCC));
            }
            return loadRIFF(reader, endian);
        }

        // Movie type codec is stored as a byte-swapped integer in little-endian files,
        // so read using the file's byte order (matches Rust: read_u32() after endian set)
        int movieFourCC = reader.readI32();
        ChunkType movieType = ChunkType.fromFourCC(movieFourCC);

        boolean afterburner = movieType.isAfterburner();

        DirectorFile file;

        if (afterburner) {
            file = loadAfterburner(reader, endian, movieType);
        } else {
            file = loadRIFX(reader, endian, movieType);
        }

        return file;
    }

    /**
     * Load a D3 Windows RIFF file. Uses CFTC chunk table instead of imap/mmap.
     * See ScummVM RIFFArchive::openStream() in archive.cpp.
     */
    private static DirectorFile loadRIFF(BinaryReader reader, ByteOrder endian) throws IOException {
        DirectorFile file = new DirectorFile(endian, false, 0, ChunkType.MV93);

        // Read CFTC chunk (chunk format table of contents)
        int cftcFourCC = reader.readFourCC();
        if (BinaryReader.fourCCToString(cftcFourCC).equals("CFTC") == false) {
            throw new IOException("Expected CFTC in RIFF file, got " + BinaryReader.fourCCToString(cftcFourCC));
        }

        int cftcSize = reader.readI32();
        int cftcStart = reader.getPosition();
        reader.skip(4); // chunk number, always 0

        int chunkIndex = 0;
        while (reader.getPosition() < cftcStart + cftcSize) {
            // RIFF entries: tag(4, big-endian), size(4, LE), id(4, LE), offset(4, LE)
            int tag = reader.readFourCC(); // tags are big-endian in RIFF
            if (tag == 0) break;

            int size = reader.readI32();
            int id = reader.readI32();
            int offset = reader.readI32();

            // Skip past the resource name at the offset
            // Resource layout: FourCC(4) + size(4) + id(4) + name_len(1) + name + data
            int savedPos = reader.getPosition();
            int resHeaderOffset = offset + 12; // skip FourCC + size + id
            if (resHeaderOffset < reader.bytesLeft() + reader.getPosition()) {
                reader.setPosition(resHeaderOffset);
                int nameLen = reader.readU8();
                int dataOffset = resHeaderOffset + 1 + nameLen;
                // Align to word boundary if needed
                if (dataOffset % 2 != 0) dataOffset++;
                int dataSize = size - 4 - 1 - nameLen; // size excludes FourCC+size, includes id
                if (dataSize < 0) dataSize = size;

                ChunkId chunkId = new ChunkId(chunkIndex);
                file.chunkInfo.put(chunkId, new ChunkInfo(chunkId, tag, dataOffset, dataSize, dataSize));
                chunkIndex++;
            }

            reader.setPosition(savedPos);
        }

        // Detect version from config chunk
        for (ChunkInfo info : file.chunkInfo.values()) {
            ChunkType type = info.type();
            if (type == ChunkType.DRCF || type == ChunkType.VWCF) {
                BinaryReader chunkReader = reader.sliceReaderAt(info.offset, info.length);
                file.config = ConfigChunk.read(file, chunkReader, info.id(), 0, endian);
                break;
            }
        }

        // Parse all chunks
        int version = file.config != null ? file.config.directorVersion() : 0;
        boolean capitalX = false;

        for (ChunkInfo info : file.chunkInfo.values()) {
            try {
                BinaryReader r = reader.sliceReaderAt(info.offset, info.length);
                Chunk chunk = file.parseChunkFromReader(r, info, version, capitalX);
                if (chunk != null) {
                    file.chunks.put(info.id(), chunk);
                    file.categorizeChunk(chunk);

                    if (chunk instanceof ScriptContextChunk) {
                        capitalX = info.fourcc() == BinaryReader.fourCC("LctX");
                        file.capitalX = capitalX;
                    }
                }
            } catch (Exception e) {
                System.err.println("Failed to parse RIFF chunk " + info.type() + ": " + e.getMessage());
            }
        }

        file.dataStore = reader.getData();
        return file;
    }

    private static DirectorFile loadRIFX(BinaryReader reader, ByteOrder endian, ChunkType movieType) throws IOException {
        DirectorFile file = new DirectorFile(endian, false, 0, movieType);

        // Read imap chunk to find mmap - FourCCs are always 4-byte ASCII (read as big-endian int)
        int imapFourCC = reader.readFourCC();
        int imapLen = reader.readI32();
        // imap content: mapVersion (4), mmapOffset (4), directorVersion (4)
        // See ScummVM archive.cpp readMemoryMap()
        int mapVersion = reader.readI32();
        int mmapOffset = reader.readI32();
        // Read directorVersion from imap if available (0 for D4, 0x4c1 for D5, 0x4c7 for D6, etc.)
        int imapDirVersion = 0;
        if (imapLen >= 12) {
            imapDirVersion = reader.readI32();
        }

        // Read mmap (memory map)
        reader.setPosition(mmapOffset);
        int mmapFourCC = reader.readFourCC();
        int mmapLen = reader.readI32();

        // Parse memory map
        int headerLen = reader.readI16();
        int entryLen = reader.readI16();
        int chunkCountMax = reader.readI32();
        int chunkCountUsed = reader.readI32();
        int junkPtr = reader.readI32();
        reader.skip(4);
        int freePtr = reader.readI32();

        // Read chunk entries - FourCCs are stored in file's byte order
        // Each entry is 20 bytes: fourcc(4) + length(4) + offset(4) + flags(2) + pad(2) + link(4)
        for (int i = 0; i < chunkCountUsed; i++) {
            if (reader.bytesLeft() < 20) {
                break;  // Not enough data for another entry
            }
            // Read fourcc using file's byte order - readI32 handles endianness
            int fourcc = reader.readI32();
            int length = reader.readI32();
            int offset = reader.readI32();
            int flags = reader.readI16();
            reader.skip(2);
            int link = reader.readI32();

            if (fourcc != 0 && offset > 0) {
                ChunkId chunkId = new ChunkId(i);
                file.chunkInfo.put(chunkId, new ChunkInfo(chunkId, fourcc, offset + 8, length, length));
            }
        }

        // Detect version from config chunk
        for (ChunkInfo info : file.chunkInfo.values()) {
            ChunkType type = info.type();
            if (type == ChunkType.DRCF || type == ChunkType.VWCF) {
                BinaryReader chunkReader = reader.sliceReaderAt(info.offset, info.length);
                file.config = ConfigChunk.read(file, chunkReader, info.id(), 0, endian);
                break;
            }
        }

        // Parse all chunks
        int version = file.config != null ? file.config.directorVersion() : 0;
        boolean capitalX = false;

        for (ChunkInfo info : file.chunkInfo.values()) {
            try {
                BinaryReader r = reader.sliceReaderAt(info.offset, info.length);
                Chunk chunk = file.parseChunkFromReader(r, info, version, capitalX);
                if (chunk != null) {
                    file.chunks.put(info.id(), chunk);
                    file.categorizeChunk(chunk);

                    if (chunk instanceof ScriptContextChunk) {
                        capitalX = info.fourcc() == BinaryReader.fourCC("LctX");
                        file.capitalX = capitalX;
                    }
                }
            } catch (Exception e) {
                // Log and continue
                System.err.println("Failed to parse chunk " + info.type() + ": " + e.getMessage());
            }
        }

        // Keep raw bytes so evicted chunks can be re-parsed on demand
        file.dataStore = reader.getData();

        return file;
    }

    private static DirectorFile loadAfterburner(BinaryReader reader, ByteOrder endian, ChunkType movieType) throws IOException {
        DirectorFile file = new DirectorFile(endian, true, 0, movieType);
        long parseStart = System.currentTimeMillis();

        // Set parse deadline: 5 seconds max for any single cast file (WASM safety)
        parseDeadline = System.currentTimeMillis() + 5000;
        try {

        // Use AfterburnerReader to parse the compressed file
        AfterburnerReader abReader = new AfterburnerReader(reader, endian);
        abReader.parse();

        int version = abReader.getDirectorVersion();
        boolean capitalX = false;

        // First pass: find and parse the config chunk to get correct version
        for (com.libreshockwave.format.ChunkInfo abInfo : abReader.getChunkInfos()) {
            String fourCCStr = abInfo.fourCC().trim();
            if (fourCCStr.equals("DRCF") || fourCCStr.equals("VWCF")) {
                try {
                    byte[] chunkData = abReader.getChunkData(abInfo.resourceId());
                    BinaryReader chunkReader = new BinaryReader(chunkData, endian);
                    file.config = ConfigChunk.read(file, chunkReader, new ChunkId(abInfo.resourceId()), 0, endian);
                    version = file.config.directorVersion();
                    break;
                } catch (Exception e) {
                    // Continue without config
                }
            }
        }

        // Second pass: parse all chunks (with 3s watchdog for WASM safety)
        for (com.libreshockwave.format.ChunkInfo abInfo : abReader.getChunkInfos()) {
            // Watchdog: abort parsing if we've been at it for more than 3 seconds.
            // Prevents WASM from hanging on problematic cast files.
            if (System.currentTimeMillis() - parseStart > 3000) {
                System.err.println("[DirectorFile] Parse watchdog triggered after 3s, aborting remaining chunks");
                break;
            }

            int fourcc = BinaryReader.fourCC(abInfo.fourCC());
            ChunkId chunkId = new ChunkId(abInfo.resourceId());
            ChunkInfo info = new ChunkInfo(
                chunkId,
                fourcc,
                abInfo.offset(),
                abInfo.compressedSize(),
                abInfo.uncompressedSize()
            );
            file.chunkInfo.put(chunkId, info);

            // Try to get and parse the chunk data
            try {
                byte[] chunkData = abReader.getChunkData(abInfo.resourceId());
                if (chunkData == null) {
                    continue; // Skip chunks with invalid data (truncated file, etc.)
                }
                BinaryReader chunkReader = new BinaryReader(chunkData, endian);

                Chunk chunk = file.parseChunkFromReader(chunkReader, info, version, capitalX);
                if (chunk != null) {
                    file.chunks.put(info.id(), chunk);
                    file.categorizeChunk(chunk);

                    // Update capitalX flag if we found a script context
                    if (chunk instanceof ScriptContextChunk) {
                        capitalX = abInfo.fourCC().equals("LctX");
                        file.capitalX = capitalX;
                    }

                    // Update version from config
                    if (chunk instanceof ConfigChunk cfg) {
                        version = cfg.directorVersion();
                    }
                }
            } catch (Exception e) {
                // Silently skip chunks that fail to parse - may be corrupted data
            }
        }

        // Keep afterburner reader so evicted chunks can be re-parsed on demand
        file.afterburnerReader = abReader;

        return file;
        } finally {
            parseDeadline = 0; // Clear deadline
        }
    }

    private Chunk parseChunkFromReader(BinaryReader reader, ChunkInfo info, int version, boolean capitalX) {
        reader.setOrder(endian);
        ChunkType type = info.type();

        return switch (type) {
            case DRCF, VWCF -> ConfigChunk.read(this, reader, info.id(), version, endian);
            case KEYp -> KeyTableChunk.read(this, reader, info.id(), version);
            case MCsL -> CastListChunk.read(this, reader, info.id(), version, endian);
            case CASp -> CastChunk.read(this, reader, info.id(), version);
            case CASt -> CastMemberChunk.read(this, reader, info.id(), version);
            case Lctx, LctX -> ScriptContextChunk.read(this, reader, info.id(), version);
            case Lnam -> ScriptNamesChunk.read(this, reader, info.id(), version);
            case Lscr -> ScriptChunk.read(this, reader, info.id(), version, capitalX);
            case VWSC, SCVW -> ScoreChunk.read(this, reader, info.id(), version);
            case VWLB -> FrameLabelsChunk.read(this, reader, info.id(), version);
            case BITD -> BitmapChunk.read(this, reader, info.id(), version);
            case CLUT -> PaletteChunk.read(this, reader, info.id(), version);
            case STXT -> TextChunk.read(this, reader, info.id());
            case snd_ -> SoundChunk.read(this, reader, info.id());
            case ediM -> MediaChunk.read(this, reader, info.id());
            default -> new RawChunk(this, info.id(), type, reader.readBytes(reader.bytesLeft()));
        };
    }

    /*
    private Chunk parseChunk(BinaryReader mainReader, ChunkInfo info, int version, boolean capitalX) {
        BinaryReader reader = mainReader.sliceReaderAt(info.offset, info.length);
        reader.setOrder(endian);

        ChunkType type = info.type();

        return switch (type) {
            case DRCF, VWCF -> ConfigChunk.read(reader, info.id, version, endian);
            case KEYp -> KeyTableChunk.read(reader, info.id, version);
            case MCsL -> CastListChunk.read(reader, info.id, version, endian);
            case CASp -> CastChunk.read(reader, info.id, version);
            case CASt -> CastMemberChunk.read(reader, info.id, version);
            case Lctx, LctX -> ScriptContextChunk.read(reader, info.id, version);
            case Lnam -> ScriptNamesChunk.read(reader, info.id, version);
            case Lscr -> ScriptChunk.read(reader, info.id, version, capitalX);
            case VWSC, SCVW -> ScoreChunk.read(reader, info.id, version);
            case VWLB -> FrameLabelsChunk.read(reader, info.id, version);
            case BITD -> BitmapChunk.read(reader, info.id, version);
            case CLUT -> PaletteChunk.read(reader, info.id, version);
            case STXT -> TextChunk.read(reader, info.id);
            case snd_ -> SoundChunk.read(reader, info.id);
            default -> new RawChunk(info.id, type, reader.readBytes(reader.bytesLeft()));
        };
    }*/

    private void categorizeChunk(Chunk chunk) {
        switch (chunk) {
            case ConfigChunk c -> this.config = c;
            case KeyTableChunk k -> this.keyTable = k;
            case CastListChunk cl -> this.castList = cl;
            case ScriptContextChunk sc -> {
                // Store all script contexts (one per cast library)
                allScriptContexts.add(sc);
                // Keep the primary context (the one with entries)
                if (this.scriptContext == null || sc.entries().size() > 0) {
                    this.scriptContext = sc;
                }
            }
            case ScriptNamesChunk sn -> {
                this.scriptNamesById.put(sn.id(), sn);
                // Also set as default if it has names (for backward compatibility)
                if (sn.names().size() > 0) {
                    this.scriptNames = sn;
                }
            }
            case CastChunk c -> this.casts.add(c);
            case CastMemberChunk cm -> this.castMembers.add(cm);
            case ScriptChunk s -> this.scripts.add(s);
            case ScoreChunk sc -> this.scoreChunk = sc;
            case FrameLabelsChunk fl -> this.frameLabelsChunk = fl;
            case PaletteChunk p -> this.palettes.add(p);
            default -> {}
        }
    }

    // Utility methods

    public void printSummary() {
        System.out.println("=== Director File Summary ===");
        System.out.println("Endian: " + (endian == ByteOrder.BIG_ENDIAN ? "Big (Mac)" : "Little (Win)"));
        System.out.println("Afterburner: " + afterburner);
        System.out.println("Movie Type: " + movieType);

        if (config != null) {
            System.out.println("\n--- Config ---");
            System.out.println("Director Version: " + config.directorVersion());
            System.out.println("Stage: " + config.stageWidth() + "x" + config.stageHeight());
            System.out.println("Tempo: " + config.tempo() + " fps");
        }

        System.out.println("\n--- Chunks ---");
        System.out.println("Total chunks: " + chunkInfo.size());
        System.out.println("Cast libraries: " + (castList != null ? castList.entries().size() : 0));
        System.out.println("Cast members: " + castMembers.size());
        System.out.println("Scripts: " + scripts.size());

        if (scoreChunk != null) {
            System.out.println("\n--- Score ---");
            System.out.println("Frames: " + scoreChunk.getFrameCount());
            System.out.println("Channels: " + scoreChunk.getChannelCount());
            System.out.println("Behavior intervals: " + scoreChunk.frameIntervals().size());
            if (frameLabelsChunk != null && !frameLabelsChunk.labels().isEmpty()) {
                System.out.println("Frame labels: " + frameLabelsChunk.labels().size());
                for (FrameLabelsChunk.FrameLabel label : frameLabelsChunk.labels()) {
                    System.out.println("  " + label.label() + " -> frame " + label.frameNum().value());
                }
            }
        }

        if (!scripts.isEmpty() && scriptNames != null) {
            System.out.println("\n--- Handlers ---");
            for (ScriptChunk script : scripts) {
                for (ScriptChunk.Handler handler : script.handlers()) {
                    String name = scriptNames.getName(handler.nameId());
                    System.out.println("  " + name + "(" + handler.argCount() + " args, " +
                        handler.localCount() + " locals, " + handler.instructions().size() + " instructions)");
                }
            }
        }
    }

    public void disassembleScript(ScriptChunk script) {
        if (scriptNames == null) {
            System.out.println("No script names available");
            return;
        }

        for (ScriptChunk.Handler handler : script.handlers()) {
            String name = scriptNames.getName(handler.nameId());

            // Print handler signature
            StringBuilder sig = new StringBuilder();
            sig.append("on ").append(name);
            if (!handler.argNameIds().isEmpty()) {
                sig.append("(");
                for (int i = 0; i < handler.argNameIds().size(); i++) {
                    if (i > 0) sig.append(", ");
                    sig.append(scriptNames.getName(handler.argNameIds().get(i)));
                }
                sig.append(")");
            }
            System.out.println("        " + sig);

            // Print locals if any
            if (!handler.localNameIds().isEmpty()) {
                StringBuilder locals = new StringBuilder("          -- locals: ");
                for (int i = 0; i < handler.localNameIds().size(); i++) {
                    if (i > 0) locals.append(", ");
                    locals.append(scriptNames.getName(handler.localNameIds().get(i)));
                }
                System.out.println(locals);
            }

            // Print bytecode with resolved names
            for (ScriptChunk.Handler.Instruction instr : handler.instructions()) {
                String line = formatInstruction(instr, handler, scriptNames);
                System.out.println("          " + line);
            }
            System.out.println("        end");
        }
    }

    private String formatInstruction(ScriptChunk.Handler.Instruction instr,
                                     ScriptChunk.Handler handler,
                                     ScriptNamesChunk names) {
        StringBuilder sb = new StringBuilder();
        sb.append(String.format("[%d] %s", instr.offset(), instr.opcode().getMnemonic()));

        Opcode op = instr.opcode();
        long arg = instr.argument();

        switch (op) {
            // Jump instructions - show target position
            case JMP, JMP_IF_Z -> {
                int target = instr.offset() + (int) arg;
                sb.append(" [").append(target).append("]");
            }
            case END_REPEAT -> {
                int target = instr.offset() - (int) arg;
                sb.append(" [").append(target).append("]");
            }

            // Name-based opcodes - resolve name from script names
            case OBJ_CALL, EXT_CALL, GET_OBJ_PROP, SET_OBJ_PROP,
                 PUSH_SYMB, GET_PROP, SET_PROP, GET_CHAINED_PROP,
                 GET_GLOBAL, SET_GLOBAL, GET_GLOBAL2, SET_GLOBAL2,
                 GET_TOP_LEVEL_PROP, NEW_OBJ -> {
                String symName = names.getName((int) arg);
                sb.append(" ").append(symName);
            }

            // Local variable access - resolve from handler's local names
            case GET_LOCAL, SET_LOCAL -> {
                if (arg >= 0 && arg < handler.localNameIds().size()) {
                    int nameId = handler.localNameIds().get((int) arg);
                    sb.append(" ").append(names.getName(nameId));
                } else {
                    sb.append(" local_").append(arg);
                }
            }

            // Parameter access - resolve from handler's arg names
            case GET_PARAM, SET_PARAM -> {
                if (arg >= 0 && arg < handler.argNameIds().size()) {
                    int nameId = handler.argNameIds().get((int) arg);
                    sb.append(" ").append(names.getName(nameId));
                } else {
                    sb.append(" param_").append(arg);
                }
            }

            // Float literal
            case PUSH_FLOAT32 -> {
                float f = Float.intBitsToFloat((int) arg);
                sb.append(" ").append(f);
            }

            // Integer literals
            case PUSH_INT8, PUSH_INT16, PUSH_INT32 -> {
                sb.append(" ").append(arg);
            }

            // Other multi-byte opcodes - just show the argument
            default -> {
                if (instr.rawOpcode() >= 0x40) {
                    sb.append(" ").append(arg);
                }
            }
        }

        return sb.toString();
    }
}
