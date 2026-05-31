package com.libreshockwave.cast;

import java.util.List;

/**
 * Complete styled text data for a Director 7+ Text Asset Xtra (XMED) member.
 * Captures ALL XMED text properties in one place — text content, per-run styling,
 * paragraph formatting, dimensions, and antialiasing — so the rendering path
 * is self-contained and separate from STXT text.
 */
public record XmedStyledText(
    String text,
    List<StyledSpan> styledSpans,
    List<String> fontCandidates,
    String alignment,
    int primaryParagraphStyleIndex,
    int primaryParagraphAlignmentCode,
    int paragraphStyleCount,
    boolean wordWrap,
    int fixedLineSpace,

    // Text member dimensions (from specificData @48-51=height, @52-55=width)
    int width,
    int height,

    // Primary font info (most common across spans — convenience accessors)
    String fontName,
    int fontSize,

    // Antialiasing (from specificData @12-15, @24-27, @36-39)
    boolean antialias,
    int antiAliasThreshold,

    // Member-level bold flag when known from parsed style metadata.
    boolean memberBold,

    // Text color (primary, from XMED section 0003)
    int colorR,
    int colorG,
    int colorB
) {

    /**
     * Build a font style string ("bold", "italic", "bold,italic", etc.)
     * using the member-level bold flag and the first span's style flags.
     */
    public String fontStyleString() {
        boolean bold = memberBold;
        boolean italic = false;
        boolean underline = false;
        if (!styledSpans.isEmpty()) {
            StyledSpan first = styledSpans.get(0);
            bold = bold || first.bold();
            italic = first.italic();
            underline = first.underline();
        }
        StringBuilder sb = new StringBuilder();
        if (bold) sb.append("bold");
        if (italic) { if (!sb.isEmpty()) sb.append(","); sb.append("italic"); }
        if (underline) { if (!sb.isEmpty()) sb.append(","); sb.append("underline"); }
        return sb.toString();
    }

    /**
     * Get text color as packed ARGB int (0xFFRRGGBB).
     */
    public int textColorARGB() {
        return 0xFF000000 | (colorR << 16) | (colorG << 8) | colorB;
    }
}
