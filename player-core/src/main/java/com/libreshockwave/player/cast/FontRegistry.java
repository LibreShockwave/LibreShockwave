package com.libreshockwave.player.cast;

import com.libreshockwave.font.BitmapFont;
import com.libreshockwave.font.Pfr1Font;
import com.libreshockwave.font.Pfr1TtfConverter;
import com.libreshockwave.font.TtfBitmapRasterizer;
import com.libreshockwave.util.ValueProvider;

import java.util.concurrent.ConcurrentHashMap;

/**
 * Registry mapping font names to PFR1 bitmap font data.
 * When cast libraries load, OLE members with XMED/PFR1 chunks
 * are parsed and registered here. Text renderers check this
 * registry first before falling back to system fonts.
 */
public class FontRegistry {

    /** Font name (lowercase) -> parsed PFR1 font */
    private static final ConcurrentHashMap<String, Pfr1Font> parsedFonts = new ConcurrentHashMap<>();

    /** Font name (lowercase) -> TTF byte array (for AWT Font.createFont) */
    private static final ConcurrentHashMap<String, byte[]> ttfCache = new ConcurrentHashMap<>();

    /** Cache: "fontName:size" -> rasterized BitmapFont (for SimpleTextRenderer) */
    private static final ConcurrentHashMap<String, BitmapFont> rasterizedCache = new ConcurrentHashMap<>();

    /** Embedded TTF font variants used when a Director movie aliases a font not otherwise present. */
    private static final ConcurrentHashMap<String, TtfVariants> embeddedTtfFonts = new ConcurrentHashMap<>();

    /** First registered PFR font name — used as last-resort fallback */
    private static volatile String firstRegisteredFont;

    /** Canonical font name -> member key (lowercase) for fuzzy matching */
    private static final ConcurrentHashMap<String, String> canonicalIndex = new ConcurrentHashMap<>();

    /** Director font cast members can alias short names such as "v" to a platform font name. */
    private static final ConcurrentHashMap<String, FontAlias> aliases = new ConcurrentHashMap<>();

    /** First bundled pixel font name, used when no runtime PFR font has loaded yet. */
    private static volatile String firstEmbeddedTtfFont;

    public record FontAlias(String fontName, boolean bold) {
    }

    private record TtfVariants(
            ValueProvider<byte[]> regular,
            ValueProvider<byte[]> bold,
            ValueProvider<byte[]> italic,
            ValueProvider<byte[]> boldItalic
    ) {
        ValueProvider<byte[]> get(boolean boldRequested, boolean italicRequested) {
            ValueProvider<byte[]> requested = switch ((boldRequested ? 1 : 0) + (italicRequested ? 2 : 0)) {
                case 1 -> bold;
                case 2 -> italic;
                case 3 -> boldItalic;
                default -> regular;
            };
            return requested != null ? requested : regular;
        }

        boolean hasBold() {
            return bold != null;
        }
    }

    static {
        registerEmbeddedTtfFont("Volter",
                com.libreshockwave.fonts.volter.volter::getData,
                com.libreshockwave.fonts.volter.volter_bold::getData,
                null,
                null);
    }

    /**
     * Register a PFR1 font parsed from XMED chunk data.
     * @param memberName the cast member name (e.g. "v", "vb")
     * @param pfrData    raw PFR1 data from XMED chunk
     */
    public static void registerPfr1Font(String memberName, byte[] pfrData) {
        if (memberName == null || pfrData == null) return;

        Pfr1Font font = Pfr1Font.parse(pfrData);
        if (font == null) return;

        String key = memberName.toLowerCase();
        parsedFonts.put(key, font);

        // Track first registered font as last-resort fallback
        if (firstRegisteredFont == null) {
            firstRegisteredFont = key;
        }

        // Index canonical names for fuzzy matching
        canonicalIndex.put(canonicalFontName(memberName), key);
        if (!font.fontName.isEmpty()) {
            canonicalIndex.put(canonicalFontName(font.fontName), key);
        }

        // Convert to TTF bytes for AWT rendering
        try {
            String ttfName = font.fontName.isEmpty() ? memberName : font.fontName;
            byte[] ttfBytes = Pfr1TtfConverter.convert(font, ttfName);
            ttfCache.put(key, ttfBytes);
            if (!font.fontName.isEmpty() && !font.fontName.equalsIgnoreCase(memberName)) {
                ttfCache.put(font.fontName.toLowerCase(), ttfBytes);
            }
        } catch (Exception e) {
            // TTF conversion failed — BitmapFont fallback still available
        }

        // Also register under the internal font ID if different
        if (!font.fontName.isEmpty() && !font.fontName.equalsIgnoreCase(memberName)) {
            parsedFonts.put(font.fontName.toLowerCase(), font);
        }
    }

    /**
     * Get TTF byte array for the given font name.
     * Returns null if no PFR font is registered or TTF conversion failed.
     */
    public static byte[] getTtfBytes(String fontName) {
        if (fontName == null) return null;
        return ttfCache.get(fontName.toLowerCase());
    }

    public static void registerFontAlias(String alias, String fontName, boolean bold) {
        if (alias == null || alias.isBlank() || fontName == null || fontName.isBlank()) {
            return;
        }
        aliases.put(alias.toLowerCase(), new FontAlias(fontName, bold));
        canonicalIndex.put(canonicalFontName(alias), alias.toLowerCase());
    }

    public static FontAlias getFontAlias(String fontName) {
        if (fontName == null) {
            return null;
        }
        return aliases.get(fontName.toLowerCase());
    }

    public static void registerEmbeddedTtfFont(String fontName,
                                               ValueProvider<byte[]> regular,
                                               ValueProvider<byte[]> bold,
                                               ValueProvider<byte[]> italic,
                                               ValueProvider<byte[]> boldItalic) {
        if (fontName == null || fontName.isBlank() || regular == null) {
            return;
        }
        String key = fontName.toLowerCase();
        embeddedTtfFonts.put(key, new TtfVariants(regular, bold, italic, boldItalic));
        canonicalIndex.put(canonicalFontName(fontName), key);
        if (firstEmbeddedTtfFont == null) {
            firstEmbeddedTtfFont = fontName;
        }
    }

    public static BitmapFont getEmbeddedBitmapFont(String fontName, int fontSize,
                                                   boolean bold, boolean italic) {
        if (fontName == null) return null;
        String key = fontName.toLowerCase();
        TtfVariants embedded = embeddedTtfFonts.get(key);
        if (embedded == null) return null;

        String cacheKey = key + ":" + fontSize + ":embedded:" + (bold ? 1 : 0) + ":" + (italic ? 1 : 0);
        BitmapFont cached = rasterizedCache.get(cacheKey);
        if (cached != null) return cached;

        ValueProvider<byte[]> supplier = embedded.get(bold, italic);
        if (supplier == null) return null;

        BitmapFont rasterized = TtfBitmapRasterizer.rasterize(supplier.get(), fontSize, fontName);
        if (rasterized != null) {
            rasterizedCache.put(cacheKey, rasterized);
        }
        return rasterized;
    }

    /**
     * Get a rasterized bitmap font for the given name and size.
     * Tries PFR fonts first, then bundled Mac system fonts as fallback.
     * Uses TtfBitmapRasterizer (pure Java, TeaVM-compatible).
     */
    public static BitmapFont getBitmapFont(String fontName, int fontSize) {
        return getBitmapFont(fontName, fontSize, false, false);
    }

    public static BitmapFont getBitmapFont(String fontName, int fontSize, boolean bold, boolean italic) {
        if (fontName == null) return null;

        String key = fontName.toLowerCase();

        // Check rasterized cache
        String cacheKey = bold || italic
                ? key + ":" + fontSize + ":" + (bold ? 1 : 0) + ":" + (italic ? 1 : 0)
                : key + ":" + fontSize;
        BitmapFont cached = rasterizedCache.get(cacheKey);
        if (cached != null) return cached;

        // TTF rasterizer from PFR-converted TTF (pure Java — works on both desktop and WASM)
        byte[] ttfBytes = ttfCache.get(key);
        if (ttfBytes != null && !bold && !italic) {
            BitmapFont rasterized = TtfBitmapRasterizer.rasterize(ttfBytes, fontSize, fontName);
            if (rasterized != null) {
                rasterizedCache.put(cacheKey, rasterized);
                return rasterized;
            }
        }

        // PFR1 direct rasterization
        Pfr1Font parsed = parsedFonts.get(key);
        if (parsed != null) {
            BitmapFont rasterized = BitmapFont.fromPfr1(parsed, fontSize);
            if (rasterized != null) {
                rasterizedCache.put(cacheKey, rasterized);
                return rasterized;
            }
        }

        BitmapFont embedded = getEmbeddedBitmapFont(fontName, fontSize, bold, italic);
        if (embedded != null) {
            return embedded;
        }

        // Bundled Mac system font fallback (Geneva, Chicago, Monaco, etc.)
        BitmapFont macFont = MacFontBundle.getFont(fontName, fontSize, bold, italic);
        if (macFont != null) return macFont; // already cached by MacFontBundle

        return null;
    }

    /**
     * Direct cache lookup without triggering any font loading.
     * Used by MacFontBundle to check if a font is already cached.
     */
    static BitmapFont getBitmapFontDirect(String cacheKey) {
        return rasterizedCache.get(cacheKey);
    }

    /**
     * Get the first registered PFR font name.
     * Used as last-resort fallback when no matching font is found.
     */
    public static String getFirstRegisteredFont() {
        return firstRegisteredFont;
    }

    /**
     * Preferred pixel font for legacy Director text runs that request an
     * embedded face without a font-map id. Bundled Director pixel fonts take
     * precedence so runtime font registration order cannot change the default.
     */
    public static String getPreferredDirectorPixelFont() {
        if (firstEmbeddedTtfFont != null && !firstEmbeddedTtfFont.isBlank()) {
            return firstEmbeddedTtfFont;
        }
        return firstRegisteredFont;
    }

    /**
     * Normalize a font name to a canonical form for fuzzy matching.
     * Lowercases, replaces _ and * with space, strips trailing digit-only segments.
     * e.g. "Volter_400_0" → "volter", "Arial*Bold" → "arial bold"
     */
    static String canonicalFontName(String name) {
        if (name == null || name.isEmpty()) return "";
        String s = name.toLowerCase().replace('_', ' ').replace('*', ' ').trim();
        // Strip trailing digit-only segments (e.g. "volter 400 0" → "volter")
        s = s.replaceAll("(\\s+\\d+)+$", "");
        return s.trim();
    }

    /**
     * Multi-strategy font resolution:
     * 1. Exact match (case-insensitive) in parsedFonts
     * 2. Canonical match via canonicalIndex
     * 3. Prefix match for short names (length ≤ 3)
     * Returns the matched font key (lowercase), or null if no match.
     */
    public static String resolveFont(String fontName) {
        if (fontName == null || fontName.isEmpty()) return null;

        String key = fontName.toLowerCase();

        // 1. Exact match
        if (parsedFonts.containsKey(key)) return key;

        // 2. Canonical match
        String canonical = canonicalFontName(fontName);
        String mapped = canonicalIndex.get(canonical);
        if (mapped != null) return mapped;

        // 3. Prefix match for short names
        if (key.length() <= 3) {
            for (var entry : parsedFonts.entrySet()) {
                if (entry.getKey().startsWith(key)) return entry.getKey();
            }
        }

        return null;
    }

    /**
     * Check if a font name is registered as a PFR bitmap font.
     */
    public static boolean hasPfrFont(String fontName) {
        if (fontName == null) return false;
        return parsedFonts.containsKey(fontName.toLowerCase());
    }

    public static boolean hasEmbeddedBoldVariant(String fontName) {
        if (fontName == null) return false;
        TtfVariants embedded = embeddedTtfFonts.get(fontName.toLowerCase());
        return embedded != null && embedded.hasBold();
    }

    /**
     * Register a pre-built BitmapFont (e.g., from GDI rasterization).
     * This bypasses the TTF/PFR pipeline and stores directly in the rasterized cache.
     */
    public static void registerBitmapFont(String fontName, int fontSize, BitmapFont font) {
        if (fontName == null || font == null) return;
        String cacheKey = fontName.toLowerCase() + ":" + fontSize;
        rasterizedCache.put(cacheKey, font);
    }

    /**
     * Clear all registered fonts (used on reset/reload).
     */
    public static void clear() {
        parsedFonts.clear();
        ttfCache.clear();
        rasterizedCache.clear();
        canonicalIndex.clear();
        aliases.clear();
        firstRegisteredFont = null;
        firstEmbeddedTtfFont = null;
        registerEmbeddedTtfFont("Volter",
                com.libreshockwave.fonts.volter.volter::getData,
                com.libreshockwave.fonts.volter.volter_bold::getData,
                null,
                null);
    }
}
