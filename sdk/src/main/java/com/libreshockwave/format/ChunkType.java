package com.libreshockwave.format;

import com.libreshockwave.io.BinaryReader;

import java.util.HashMap;
import java.util.Map;

/**
 * Director file chunk types identified by FourCC codes.
 */
public enum ChunkType {
    // Container chunks
    RIFX("RIFX", "RIFX Container"),
    XFIR("XFIR", "RIFX Container (little-endian)"),
    RIFF("RIFF", "RIFF Container (D3 Windows)"),
    FFIR("FFIR", "RIFF Container (D3 Windows, little-endian)"),

    // Movie types
    MV93("MV93", "Director Movie"),
    MC95("MC95", "Director Cast"),
    FGDM("FGDM", "Shockwave Movie (Afterburner)"),
    FGDC("FGDC", "Shockwave Cast (Afterburner)"),
    RMMP("RMMP", "RIFF Resource Map Marker (D3)"),

    // Memory/resource management
    IMAP("imap", "Initial Map"),
    MMAP("mmap", "Memory Map"),
    JUNK("junk", "Junk/Padding"),
    FREE("free", "Free Space"),

    // Configuration
    DRCF("DRCF", "Director Config (D6+)"),
    VWCF("VWCF", "Director Config (D5)"),

    // Cast management
    MCsL("MCsL", "Cast List"),
    CASp("CAS*", "Cast Member Array"),  // Note: * in FourCC
    CASt("CASt", "Cast Member Definition"),
    KEYp("KEY*", "Key Table"),          // Note: * in FourCC
    Cinf("Cinf", "Cast Info"),
    VWCR("VWCR", "Cast Members (D3)"),
    VWCI("VWCI", "Cast Info (D3)"),

    // Scripts
    LctX("LctX", "Script Context (capital X)"),
    Lctx("Lctx", "Script Context"),
    Lnam("Lnam", "Script Names"),
    Lscr("Lscr", "Script Bytecode"),

    // Score/timeline
    VWSC("VWSC", "Score Data"),
    SCVW("SCVW", "Score Data (alternate)"),
    VWLB("VWLB", "Frame Labels"),
    Sord("Sord", "Score Ordering"),

    // Media
    BITD("BITD", "Bitmap Data"),
    ALFA("ALFA", "Alpha Channel Data"),
    CLUT("CLUT", "Color Lookup Table (Palette)"),
    STXT("STXT", "Styled Text"),
    Fmap("Fmap", "Font Map"),
    snd_("snd ", "Sound Data"),         // Note: space in FourCC
    ediM("ediM", "Media Resource"),
    XMED("XMED", "Extended Media"),

    // Effects and thumbnails
    FXmp("FXmp", "Effect Map"),
    Thum("Thum", "Thumbnail"),

    // Unknown
    UNKNOWN("????", "Unknown Chunk");

    private static final Map<Integer, ChunkType> BY_FOURCC = new HashMap<>();
    private static final Map<String, ChunkType> BY_STRING = new HashMap<>();

    static {
        for (ChunkType type : values()) {
            if (type != UNKNOWN) {
                BY_FOURCC.put(type.fourcc, type);
                BY_STRING.put(type.fourccString, type);
            }
        }
        // Handle special cases with asterisks
        BY_FOURCC.put(BinaryReader.fourCC("CAS*"), CASp);
        BY_FOURCC.put(BinaryReader.fourCC("KEY*"), KEYp);
        BY_STRING.put("CAS*", CASp);
        BY_STRING.put("KEY*", KEYp);
    }

    private final int fourcc;
    private final String fourccString;
    private final String description;

    ChunkType(String fourccStr, String description) {
        this.fourccString = fourccStr;
        this.fourcc = BinaryReader.fourCC(fourccStr);
        this.description = description;
    }

    public int getFourCC() {
        return fourcc;
    }

    public String getFourCCString() {
        return fourccString;
    }

    public String getDescription() {
        return description;
    }

    public boolean isContainer() {
        return this == RIFX || this == XFIR || this == RIFF || this == FFIR;
    }

    public boolean isRIFF() {
        return this == RIFF || this == FFIR;
    }

    public boolean isMovieType() {
        return this == MV93 || this == MC95 || this == FGDM || this == FGDC;
    }

    public boolean isAfterburner() {
        return this == FGDM || this == FGDC;
    }

    public boolean isConfig() {
        return this == DRCF || this == VWCF;
    }

    public boolean isScript() {
        return this == LctX || this == Lctx || this == Lnam || this == Lscr;
    }

    public boolean isScore() {
        return this == VWSC || this == SCVW || this == VWLB || this == Sord;
    }

    public boolean isMedia() {
        return this == BITD || this == CLUT || this == STXT || this == Fmap || this == snd_ || this == ediM || this == XMED;
    }

    public static ChunkType fromFourCC(int fourcc) {
        return BY_FOURCC.getOrDefault(fourcc, UNKNOWN);
    }

    public static ChunkType fromString(String fourcc) {
        return BY_STRING.getOrDefault(fourcc, UNKNOWN);
    }

    @Override
    public String toString() {
        return fourccString + " (" + description + ")";
    }
}
