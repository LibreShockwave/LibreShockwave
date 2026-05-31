package com.libreshockwave.cast;

import java.util.List;

/**
 * Parser for Director 7+ Text Asset Xtra XMED chunk data.
 * XMED chunks contain text content for "text" sub-type Xtra members.
 *
 * Format: The data is ASCII-encoded with tagged sections.
 * Control bytes (0x01, 0x02, 0x03) separate sections.
 * Section tags are 4-char ASCII (e.g., "0002" for text, "0008" for font).
 * Text section: after tag "0002", find a 0x00 byte, then "HEX_COUNT,MAC_ROMAN_TEXT" ending with 0x03.
 *
 * Two variants observed:
 * - Short form: tag appears early, text follows directly after null+count
 * - Long form: data starts with "FFFF", sections use 0x02 as field separator,
 *   text section identified by searching for the text content pattern
 */
public class XmedTextParser {

    /**
     * @deprecated Use {@link XmedStyledText} via {@link #parseStyled(byte[], byte[])} instead.
     */
    @Deprecated
    public record XmedText(String text, String fontName, int fontSize, int fontStyle,
                              String alignment,
                              int colorR, int colorG, int colorB) {}

    /**
     * Check if specificData indicates a "text" sub-type Xtra.
     * Director 7+ Text Asset Xtras have "text" at specificData[4..7].
     */
    public static boolean isTextXtra(byte[] specificData) {
        if (specificData == null || specificData.length < 8) return false;
        return specificData[4] == 't' && specificData[5] == 'e'
            && specificData[6] == 'x' && specificData[7] == 't';
    }

    /**
     * Parse XMED chunk data combined with CASt specificData into a fully-styled
     * XmedStyledText record. This is the primary parse entry point.
     *
     * @param xmedData     raw bytes from the XMED chunk
     * @param specificData raw bytes from the CASt specificData (may be null)
     * @return parsed XmedStyledText, or null if XMED data cannot be parsed
     */
    public static XmedStyledText parseStyled(byte[] xmedData, byte[] specificData) {
        if (xmedData == null || xmedData.length < 10) return null;

        String ascii = toAsciiString(xmedData);

        String text = extractText(xmedData, ascii);
        List<String> fontCandidates = extractFontNames(xmedData, ascii);
        String fontName = selectPrimaryFontName(fontCandidates);
        int[] fontSizeAndStyle = extractFontSizeAndStyle(xmedData, ascii,
                text != null ? text.length() : 0);
        int[] color = extractColor(xmedData, ascii);
        int primaryParagraphStyleIndex = extractPrimaryParagraphStyleIndex(xmedData, ascii);
        int paragraphStyleCount = countParagraphStyleRecords(xmedData);
        byte[] selectedParagraphStyleRecord = extractSelectedParagraphStyleRecord(xmedData, primaryParagraphStyleIndex);
        int primaryParagraphAlignmentCode = extractParagraphAlignmentCode(selectedParagraphStyleRecord);
        String alignment = alignmentFromParagraphAlignmentCode(primaryParagraphAlignmentCode);
        if (alignment == null) {
            alignment = alignmentFromParagraphStyleIndex(primaryParagraphStyleIndex);
        }

        if (fontName == null) fontName = "Geneva";
        int fontSize = fontSizeAndStyle[0];
        int fontStyle = fontSizeAndStyle[1];
        if (alignment == null) alignment = "left";

        // Extract specificData fields
        int width = 0, height = 0;
        boolean memberBold = false;
        boolean antialias = false;
        int aaThreshold = 14;

        if (specificData != null && specificData.length >= 56) {
            height = readU32BE(specificData, 48);
            width = readU32BE(specificData, 52);
        }
        if (specificData != null && specificData.length >= 40) {
            int aaDisabled = readU32BE(specificData, 12);
            if (aaDisabled == 0) {
                int thresholdEnabled = readU32BE(specificData, 24);
                if (thresholdEnabled == 0) {
                    antialias = true;
                } else {
                    aaThreshold = readU32BE(specificData, 36);
                    antialias = fontSize >= aaThreshold;
                }
            }
        }

        boolean spanBold = memberBold || (fontStyle & 1) != 0;
        boolean spanItalic = (fontStyle & 2) != 0;
        int textLen = text != null ? text.length() : 0;
        List<StyledSpan> spans = extractStyleSpans(xmedData, ascii, textLen,
                fontCandidates, fontName, fontSize, spanBold, spanItalic, color);
        String primarySpanFontName = choosePrimarySpanFontName(spans, fontName);
        int primarySpanFontSize = choosePrimarySpanFontSize(spans, fontSize);

        return new XmedStyledText(
                text,
                spans,
                fontCandidates,
                alignment,
                primaryParagraphStyleIndex,
                primaryParagraphAlignmentCode,
                paragraphStyleCount,
                true,           // wordWrap — XMED text members default to wrapping
                0,
                width, height,
                primarySpanFontName, primarySpanFontSize,
                antialias, aaThreshold,
                memberBold,
                color[0], color[1], color[2]
        );
    }

    private static List<StyledSpan> extractStyleSpans(byte[] data, String ascii, int textLen,
                                                      List<String> fontCandidates,
                                                      String fontName, int fontSize,
                                                      boolean bold, boolean italic,
                                                      int[] color) {
        List<StyleRun> runs = extractStyleRuns(data, ascii, textLen);
        List<XmedStyleRecord> styleRecords = extractStyleRecords(data, ascii);
        if (runs.isEmpty()) {
            return List.of(new StyledSpan(0, textLen, fontName, fontSize,
                    bold, italic, false,
                    color[0], color[1], color[2]));
        }

        java.util.ArrayList<StyledSpan> spans = new java.util.ArrayList<>();
        for (int i = 0; i < runs.size(); i++) {
            StyleRun run = runs.get(i);
            int start = Math.max(0, Math.min(textLen, run.offset()));
            int end = i + 1 < runs.size() ? runs.get(i + 1).offset() : textLen;
            end = Math.max(start, Math.min(textLen, end));
            if (start == end) {
                continue;
            }
            XmedStyleRecord styleRecord = styleRecordForRun(styleRecords, run.style());
            String spanFontName = resolveSpanFontName(fontCandidates, fontName, styleRecord);
            int spanFontSize = styleRecord != null && styleRecord.fontSize() > 0
                    ? styleRecord.fontSize()
                    : fontSize;
            spans.add(new StyledSpan(start, end,
                    spanFontName, spanFontSize,
                    bold, italic, (run.style() & 1) != 0,
                    color[0], color[1], color[2]));
        }

        return spans.isEmpty()
                ? List.of(new StyledSpan(0, textLen, fontName, fontSize,
                        bold, italic, false,
                        color[0], color[1], color[2]))
                : List.copyOf(spans);
    }

    private record StyleRun(int offset, int style) {}
    private record XmedStyleRecord(int fontCandidateIndex, int fontSize) {}

    /**
     * Section 0004 stores character style runs as offset/style pairs encoded
     * with Director's compact textual control-byte format:
     *   [02]<hex offset>[01]<hex style>
     */
    private static List<StyleRun> extractStyleRuns(byte[] data, String ascii, int textLen) {
        int idx = findSection(data, "0004");
        if (idx < 0) {
            return List.of();
        }
        int secStart = idx + 20;
        int secLen = parseSectionLength(ascii, idx);
        int secEnd = secLen > 0 ? Math.min(secStart + secLen, data.length) : data.length;

        java.util.ArrayList<StyleRun> runs = new java.util.ArrayList<>();
        int i = secStart;
        while (i < secEnd) {
            if (data[i] != 0x02) {
                i++;
                continue;
            }
            int offsetStart = ++i;
            while (i < secEnd && isHexDigit(data[i] & 0xFF)) {
                i++;
            }
            int offsetEnd = i;
            if (offsetStart == i || i >= secEnd || data[i] != 0x01) {
                continue;
            }
            int styleStart = ++i;
            while (i < secEnd && isHexDigit(data[i] & 0xFF)) {
                i++;
            }
            if (styleStart == i) {
                continue;
            }
            try {
                int offset = Integer.parseInt(new String(data, offsetStart, offsetEnd - offsetStart,
                        java.nio.charset.StandardCharsets.ISO_8859_1), 16);
                int style = Integer.parseInt(new String(data, styleStart, i - styleStart,
                        java.nio.charset.StandardCharsets.ISO_8859_1), 16);
                if (offset >= 0 && offset <= textLen + 2) {
                    runs.add(new StyleRun(Math.min(offset, textLen), style));
                }
            } catch (NumberFormatException e) {
                // Ignore malformed style entries.
            }
        }

        runs.sort(java.util.Comparator.comparingInt(StyleRun::offset));
        return runs;
    }

    /**
     * Parse text from XMED chunk data (legacy API without specificData).
     * Returns null if the data cannot be parsed.
     * @deprecated Use {@link #parseStyled(byte[], byte[])} instead.
     */
    @Deprecated
    public static XmedText parse(byte[] data) {
        if (data == null || data.length < 10) return null;

        String ascii = toAsciiString(data);

        String text = extractText(data, ascii);
        List<String> fontCandidates = extractFontNames(data, ascii);
        String fontName = selectPrimaryFontName(fontCandidates);
        int[] fontSizeAndStyle = extractFontSizeAndStyle(data, ascii,
                text != null ? text.length() : 0);
        int[] color = extractColor(data, ascii);
        int primaryParagraphStyleIndex = extractPrimaryParagraphStyleIndex(data, ascii);
        byte[] selectedParagraphStyleRecord = extractSelectedParagraphStyleRecord(data, primaryParagraphStyleIndex);
        int primaryParagraphAlignmentCode = extractParagraphAlignmentCode(selectedParagraphStyleRecord);
        String alignment = alignmentFromParagraphAlignmentCode(primaryParagraphAlignmentCode);
        if (alignment == null) {
            alignment = alignmentFromParagraphStyleIndex(primaryParagraphStyleIndex);
        }

        return new XmedText(text, fontName != null ? fontName : "Geneva",
                fontSizeAndStyle[0], fontSizeAndStyle[1],
                alignment != null ? alignment : "left",
                color[0], color[1], color[2]);
    }


    /**
     * Extract text content from XMED data.
     * Looks for the text section which contains "HEX_COUNT,MAC_ROMAN_TEXT".
     */
    private static String extractText(byte[] data, String ascii) {
        // Strategy 1: Look for "0002" tag as ASCII text in the data
        int tagIdx = ascii.indexOf("0002");
        if (tagIdx >= 0) {
            return extractTextAfterTag(data, tagIdx + 4);
        }

        // Strategy 2: For longer XMED data, search for the text content pattern.
        // The text is typically after a 0x00 byte, in format "HEX_COUNT,TEXT" ending with 0x03.
        // Scan for null byte followed by hex digits, comma, then readable text.
        for (int i = 0; i < data.length - 5; i++) {
            if (data[i] == 0x00) {
                int textResult = tryParseTextAt(data, i + 1);
                if (textResult >= 0) {
                    return extractCountCommaText(data, i + 1);
                }
            }
        }

        return null;
    }

    /**
     * Extract text after a tag position. Scans for null byte then "COUNT,TEXT".
     */
    private static String extractTextAfterTag(byte[] data, int startPos) {
        // Skip any length/header bytes until we find a null byte
        for (int i = startPos; i < data.length - 2; i++) {
            if (data[i] == 0x00) {
                return extractCountCommaText(data, i + 1);
            }
        }
        return null;
    }

    /**
     * Try to parse "HEX_COUNT,TEXT" starting at pos.
     * Returns the comma position if valid, -1 otherwise.
     */
    private static int tryParseTextAt(byte[] data, int pos) {
        // Look for hex digits followed by comma
        int commaIdx = -1;
        for (int i = pos; i < Math.min(pos + 10, data.length); i++) {
            int c = data[i] & 0xFF;
            if (c == ',') {
                commaIdx = i;
                break;
            }
            if (!isHexDigit(c)) return -1;
        }
        if (commaIdx < 0 || commaIdx == pos) return -1;

        // Verify there's readable text after the comma
        if (commaIdx + 1 < data.length) {
            int firstChar = data[commaIdx + 1] & 0xFF;
            if (firstChar >= 0x20 && firstChar != 0x03) {
                return commaIdx;
            }
        }
        return -1;
    }

    /**
     * Extract "HEX_COUNT,MAC_ROMAN_TEXT" from data starting at pos.
     * Text ends at 0x03 control byte or end of data.
     */
    private static String extractCountCommaText(byte[] data, int pos) {
        // Find end of this section (0x03 byte or end of data)
        int end = data.length;
        for (int i = pos; i < data.length; i++) {
            if (data[i] == 0x03) {
                end = i;
                break;
            }
        }

        if (end <= pos) return null;

        // Parse "HEX_COUNT,TEXT"
        byte[] section = new byte[end - pos];
        System.arraycopy(data, pos, section, 0, section.length);

        String raw = new String(section, java.nio.charset.StandardCharsets.ISO_8859_1);
        int commaIdx = raw.indexOf(',');
        if (commaIdx < 0) return null;

        String textPart = raw.substring(commaIdx + 1);
        if (textPart.isEmpty()) return null;

        // Decode Mac Roman characters
        byte[] macBytes = new byte[textPart.length()];
        for (int i = 0; i < textPart.length(); i++) {
            macBytes[i] = (byte) textPart.charAt(i);
        }
        return decodeMacRoman(macBytes);
    }

    /**
     * Extract font name from XMED data.
     * Section "0008" contains TWO sets of font entries:
     *   1) Mac font names (e.g., Geneva) — appear first after the section header
     *   2) Windows font names (e.g., Verdana) — appear later after metadata records
     * Each font entry: null + "40," + length_byte + font_name + null_padding (64 bytes)
     * We prefer the Windows name and fall back to the Mac name.
     */
    private static String selectPrimaryFontName(List<String> fonts) {
        if (fonts.isEmpty()) {
            return null;
        }
        String macFont = fonts.get(0);
        for (int i = 1; i < fonts.size(); i++) {
            String font = fonts.get(i);
            if (!font.equals(macFont)) {
                return font;
            }
        }
        return macFont;
    }

    private static List<String> extractFontNames(byte[] data, String ascii) {
        int tagIdx = ascii.indexOf("0008");
        if (tagIdx < 0) return List.of();

        // Collect ALL font names from section 0008 by finding "40," patterns
        // followed by a length byte and font name
        java.util.ArrayList<String> fonts = new java.util.ArrayList<>();

        for (int i = tagIdx + 20; i < data.length - 10; i++) {
            // Stop if we hit the next section (0x03 delimiter followed by "000")
            if (data[i] == 0x03 && i + 3 < data.length
                    && data[i+1] == '0' && data[i+2] == '0' && data[i+3] == '0') {
                break;
            }

            // Look for null + "40," pattern (null byte, then ASCII "40,")
            if (data[i] == 0x00 && i + 3 < data.length
                    && data[i+1] == '4' && data[i+2] == '0' && data[i+3] == ',') {
                // Length byte follows the comma
                int nameLen = data[i + 4] & 0xFF;
                if (nameLen > 0 && nameLen < 64 && i + 5 + nameLen <= data.length) {
                    String fontName = new String(data, i + 5, nameLen,
                            java.nio.charset.StandardCharsets.ISO_8859_1).trim();
                    if (!fontName.isEmpty() && !fonts.contains(fontName)) {
                        fonts.add(fontName);
                    }
                }
                // Skip past the 64-byte padded field
                i += 4 + 64 - 1;
            }
        }

        return List.copyOf(fonts);
    }

    /**
     * Extract text color from XMED data.
     * XMED text color may be stored as a tagged section (e.g., "0003" for color).
     * Returns {R, G, B} or {255, 255, 255} as default (white).
     */
    private static int[] extractColor(byte[] data, String ascii) {
        // Look for color tag "0003" which typically contains RGB color data
        int tagIdx = ascii.indexOf("0003");
        if (tagIdx >= 0) {
            // After the tag, look for color data
            for (int i = tagIdx + 4; i < data.length - 6; i++) {
                if (data[i] == 0x00) {
                    // Try to parse "HEX,R,G,B" format or direct bytes
                    String colorStr = extractCountCommaText(data, i + 1);
                    if (colorStr != null && !colorStr.isEmpty()) {
                        // Color might be encoded as comma-separated RGB
                        String[] parts = colorStr.split(",");
                        if (parts.length >= 3) {
                            try {
                                int r = Integer.parseInt(parts[0].trim());
                                int g = Integer.parseInt(parts[1].trim());
                                int b = Integer.parseInt(parts[2].trim());
                                return new int[]{r, g, b};
                            } catch (NumberFormatException e) {
                                // Not numeric
                            }
                        }
                    }
                    break;
                }
            }
        }
        // Default: white text (common for Director text on dark backgrounds)
        return new int[]{255, 255, 255};
    }

    /**
     * Extract font size and style from XMED section 0006 (per-run style data).
     *
     * Section 0006 contains character-level style runs. Each run includes a
     * font size encoded as a fixed-point hex value in the pattern:
     *   [82] [02] <hexSize>"0000" [02] "0"
     * where <hexSize> is 1-2 hex digits representing the font size in points.
     * Example: "C0000" = 12pt, "90000" = 9pt.
     *
     * The font style is extracted from the [01]-delimited early part of each run:
     *   [01]<offset> [01]<style> [81][81] [01]<lineHeight> [01]<unknown> [01]<fontStyle>
     *
     * Returns the most common font size across all runs (body text size),
     * and the font style from the first run.
     *
     * @return int[]{fontSize, fontStyle}
     */
    private static int[] extractFontSizeAndStyle(byte[] data, String ascii, int textLen) {
        // Find section 0006 — search after [03] delimiter
        int idx0006 = findSection(data, "0006");
        if (idx0006 < 0) return new int[]{9, 0};

        // Parse section header: tag(4) + length(8) + count(8)
        int secStart = idx0006 + 20;
        int secLen = parseSectionLength(ascii, idx0006);
        int secEnd = secLen > 0 ? Math.min(secStart + secLen, data.length) : data.length;

        java.util.List<Integer> styleRecordSizes = extractStyleRecordFontSizes(data, secStart, secEnd);
        int referencedSize = chooseReferencedStyleFontSize(data, ascii, textLen, styleRecordSizes);
        if (referencedSize > 0) {
            return new int[]{referencedSize, 0};
        }

        // Extract font sizes from [02]<hexSize>"0000"[02] pattern within section 0006
        // Each run has a font size encoded as fixed-point: "C0000" = 12.0pt, "90000" = 9.0pt
        java.util.Map<Integer, Integer> sizeCounts = new java.util.LinkedHashMap<>();
        int firstSize = -1;
        int fontStyle = 0;

        for (int i = secStart; i < secEnd - 6; i++) {
            if (data[i] == 0x02) {
                // Read hex digits until "0000" followed by [02]
                StringBuilder hexStr = new StringBuilder();
                int j = i + 1;
                while (j < secEnd && isHexDigit(data[j] & 0xFF)) {
                    hexStr.append((char) data[j]);
                    j++;
                }
                String hex = hexStr.toString();
                // Match pattern: 1-2 hex size digits + "0000" suffix
                if (hex.length() >= 5 && hex.endsWith("0000") && j < secEnd && data[j] == 0x02) {
                    String sizeHex = hex.substring(0, hex.length() - 4);
                    try {
                        int size = Integer.parseInt(sizeHex, 16);
                        if (size >= 6 && size <= 200) {
                            sizeCounts.put(size, sizeCounts.getOrDefault(size, 0) + 1);
                            if (firstSize < 0) firstSize = size;
                        }
                    } catch (NumberFormatException e) { /* ignore */ }
                }
            }
        }

        // Use the most common font size (body text), not the first (which may be
        // an inherited/default style record). For close ties, prefer the smaller
        // repeated size; XMED sections commonly include slightly larger defaults
        // before the concrete run style used by compact text members.
        int fontSize = 9; // default
        int maxCount = 0;
        for (var entry : sizeCounts.entrySet()) {
            if (entry.getValue() > maxCount
                    || (entry.getValue() == maxCount
                    && entry.getKey() < fontSize
                    && fontSize - entry.getKey() <= 2)) {
                maxCount = entry.getValue();
                fontSize = entry.getKey();
            }
        }
        if (sizeCounts.isEmpty() && firstSize > 0) {
            fontSize = firstSize;
        }

        return new int[]{fontSize, fontStyle};
    }

    private static List<XmedStyleRecord> extractStyleRecords(byte[] data, String ascii) {
        int idx0006 = findSection(data, "0006");
        if (idx0006 < 0) {
            return List.of();
        }

        int secStart = idx0006 + 20;
        int secLen = parseSectionLength(ascii, idx0006);
        int secEnd = secLen > 0 ? Math.min(secStart + secLen, data.length) : data.length;
        int count = parseSectionCount(ascii, idx0006);
        if (count <= 0 || secStart >= secEnd) {
            return List.of();
        }

        byte[] body = java.util.Arrays.copyOfRange(data, secStart, secEnd);
        java.util.ArrayList<XmedStyleRecord> records = new java.util.ArrayList<>(count);
        int recordStart = 0;
        for (int i = 0; i + 1 < body.length && records.size() < count - 1; i++) {
            if ((body[i] & 0xFF) == 0xC2 && (body[i + 1] & 0xFF) == 0x0A) {
                records.add(parseStyleRecord(java.util.Arrays.copyOfRange(body, recordStart, i + 2)));
                recordStart = i + 2;
            }
        }
        records.add(parseStyleRecord(java.util.Arrays.copyOfRange(body, recordStart, body.length)));
        return List.copyOf(records);
    }

    private static XmedStyleRecord parseStyleRecord(byte[] record) {
        int fontCandidateIndex = -1;
        int fontSize = -1;

        int pos = 0;
        if (record.length > 0 && record[0] == 0x02) {
            pos = skipHexField(record, 1);
            if (pos < record.length && record[pos] == 0x02) {
                pos = skipHexField(record, pos + 1);
            }
        }

        while (pos < record.length) {
            int marker = record[pos] & 0xFF;
            if (marker != 0x01 && marker != 0x02) {
                pos++;
                continue;
            }
            int fieldStart = pos + 1;
            int fieldEnd = skipHexField(record, fieldStart);
            if (fieldEnd == fieldStart) {
                pos++;
                continue;
            }
            String raw = new String(record, fieldStart, fieldEnd - fieldStart,
                    java.nio.charset.StandardCharsets.ISO_8859_1);
            try {
                int value = Integer.parseInt(raw, 16);
                if (marker == 0x01 && fontCandidateIndex < 0) {
                    fontCandidateIndex = value;
                }
                if (marker == 0x02 && fontSize < 0 && raw.length() >= 5 && raw.endsWith("0000")) {
                    int size = Integer.parseInt(raw.substring(0, raw.length() - 4), 16);
                    if (size >= 6 && size <= 200) {
                        fontSize = size;
                    }
                }
            } catch (NumberFormatException e) {
                // Ignore malformed fields.
            }
            pos = fieldEnd;
        }

        return new XmedStyleRecord(fontCandidateIndex, fontSize);
    }

    private static int skipHexField(byte[] data, int start) {
        int pos = start;
        while (pos < data.length && isHexDigit(data[pos] & 0xFF)) {
            pos++;
        }
        return pos;
    }

    private static XmedStyleRecord styleRecordForRun(List<XmedStyleRecord> styleRecords, int runStyleIndex) {
        if (runStyleIndex < 0 || runStyleIndex >= styleRecords.size()) {
            return null;
        }
        return styleRecords.get(runStyleIndex);
    }

    private static String resolveSpanFontName(List<String> fontCandidates,
                                              String fallbackFontName,
                                              XmedStyleRecord styleRecord) {
        if (styleRecord == null || styleRecord.fontCandidateIndex() < 0 || fontCandidates == null) {
            return fallbackFontName;
        }
        int idx = styleRecord.fontCandidateIndex();
        if (idx >= 0 && idx < fontCandidates.size()) {
            String candidate = fontCandidates.get(idx);
            if (candidate != null && !candidate.isBlank()) {
                return candidate;
            }
        }
        return fallbackFontName;
    }

    private static String choosePrimarySpanFontName(List<StyledSpan> spans, String fallbackFontName) {
        if (spans == null || spans.isEmpty()) {
            return fallbackFontName;
        }
        java.util.Map<String, Integer> coverage = new java.util.LinkedHashMap<>();
        for (StyledSpan span : spans) {
            String spanFontName = span.fontName() != null ? span.fontName() : fallbackFontName;
            coverage.put(spanFontName,
                    coverage.getOrDefault(spanFontName, 0) + Math.max(0, span.endOffset() - span.startOffset()));
        }
        String bestFontName = fallbackFontName;
        int bestCoverage = -1;
        for (var entry : coverage.entrySet()) {
            if (entry.getValue() > bestCoverage) {
                bestFontName = entry.getKey();
                bestCoverage = entry.getValue();
            }
        }
        return bestFontName;
    }

    private static int choosePrimarySpanFontSize(List<StyledSpan> spans, int fallbackFontSize) {
        if (spans == null || spans.isEmpty()) {
            return fallbackFontSize;
        }
        java.util.Map<Integer, Integer> coverage = new java.util.LinkedHashMap<>();
        for (StyledSpan span : spans) {
            coverage.put(span.fontSize(),
                    coverage.getOrDefault(span.fontSize(), 0) + Math.max(0, span.endOffset() - span.startOffset()));
        }
        int bestFontSize = fallbackFontSize;
        int bestCoverage = -1;
        for (var entry : coverage.entrySet()) {
            if (entry.getValue() > bestCoverage) {
                bestFontSize = entry.getKey();
                bestCoverage = entry.getValue();
            }
        }
        return bestFontSize;
    }

    private static java.util.List<Integer> extractStyleRecordFontSizes(byte[] data, int secStart, int secEnd) {
        java.util.ArrayList<Integer> sizes = new java.util.ArrayList<>();
        for (int i = secStart; i < secEnd - 6; i++) {
            if (data[i] != 0x02) {
                continue;
            }
            StringBuilder hexStr = new StringBuilder();
            int j = i + 1;
            while (j < secEnd && isHexDigit(data[j] & 0xFF)) {
                hexStr.append((char) data[j]);
                j++;
            }
            String hex = hexStr.toString();
            if (hex.length() >= 5 && hex.endsWith("0000") && j < secEnd && data[j] == 0x02) {
                try {
                    int size = Integer.parseInt(hex.substring(0, hex.length() - 4), 16);
                    if (size >= 6 && size <= 200) {
                        sizes.add(size);
                    }
                } catch (NumberFormatException e) {
                    // Ignore malformed style entries.
                }
            }
        }
        return sizes;
    }

    private static int chooseReferencedStyleFontSize(byte[] data, String ascii, int textLen,
                                                     java.util.List<Integer> styleRecordSizes) {
        if (styleRecordSizes.isEmpty() || textLen <= 0) {
            return -1;
        }

        List<StyleRun> runs = extractStyleRuns(data, ascii, textLen);
        if (runs.isEmpty()) {
            return -1;
        }

        java.util.Map<Integer, Integer> coveredCharsBySize = new java.util.LinkedHashMap<>();
        for (int i = 0; i < runs.size(); i++) {
            StyleRun run = runs.get(i);
            int styleIndex = run.style();
            if (styleIndex < 0 || styleIndex >= styleRecordSizes.size()) {
                continue;
            }
            int start = Math.max(0, Math.min(textLen, run.offset()));
            int end = i + 1 < runs.size() ? runs.get(i + 1).offset() : textLen;
            end = Math.max(start, Math.min(textLen, end));
            int coverage = end - start;
            if (coverage <= 0) {
                continue;
            }
            int size = styleRecordSizes.get(styleIndex);
            coveredCharsBySize.put(size, coveredCharsBySize.getOrDefault(size, 0) + coverage);
        }

        int bestSize = -1;
        int bestCoverage = 0;
        for (var entry : coveredCharsBySize.entrySet()) {
            if (entry.getValue() > bestCoverage) {
                bestSize = entry.getKey();
                bestCoverage = entry.getValue();
            }
        }
        return bestSize;
    }

    private static int findSection(byte[] data, String tag) {
        byte[] tagBytes = tag.getBytes(java.nio.charset.StandardCharsets.ISO_8859_1);
        for (int i = 0; i < data.length - 24; i++) {
            if (data[i] == 0x03 && i + tagBytes.length < data.length) {
                boolean match = true;
                for (int j = 0; j < tagBytes.length; j++) {
                    if (data[i + 1 + j] != tagBytes[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    return i + 1;
                }
            }
        }
        return -1;
    }

    private static int parseSectionLength(String ascii, int sectionOffset) {
        try {
            String lenHex = ascii.substring(sectionOffset + 4, Math.min(sectionOffset + 12, ascii.length()));
            return Integer.parseInt(lenHex, 16);
        } catch (Exception e) {
            return 0;
        }
    }

    private static int parseSectionCount(String ascii, int sectionOffset) {
        try {
            String countHex = ascii.substring(sectionOffset + 12, Math.min(sectionOffset + 20, ascii.length()));
            return Integer.parseInt(countHex, 16);
        } catch (Exception e) {
            return 0;
        }
    }

    /** Read a big-endian unsigned 32-bit integer from a byte array. */
    private static int readU32BE(byte[] data, int offset) {
        return ((data[offset] & 0xFF) << 24)
             | ((data[offset + 1] & 0xFF) << 16)
             | ((data[offset + 2] & 0xFF) << 8)
             |  (data[offset + 3] & 0xFF);
    }

    private static boolean isHexDigit(int c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
    }

    /**
     * Convert byte array to ASCII string, replacing non-printable bytes with '.'.
     */
    private static String toAsciiString(byte[] data) {
        char[] chars = new char[data.length];
        for (int i = 0; i < data.length; i++) {
            int b = data[i] & 0xFF;
            chars[i] = (b >= 0x20 && b < 0x7F) ? (char) b : '.';
        }
        return new String(chars);
    }

    private static String decodeMacRoman(byte[] bytes) {
        StringBuilder sb = new StringBuilder(bytes.length);
        for (byte b : bytes) {
            int c = b & 0xFF;
            if (c < 128) {
                sb.append((char) c);
            } else {
                sb.append(MAC_ROMAN[c - 128]);
            }
        }
        return sb.toString();
    }

    /**
     * Extract paragraph alignment from XMED section 0005.
     * Section 0005 contains paragraph alignment runs as [02]offset[01]value pairs.
     * The first entry's value determines the primary alignment for the text.
     *
     * XMED alignment values (different from Director's scripting convention):
     *   0 = left, 1 = center, 2 = right
     */
    private static int extractPrimaryParagraphStyleIndex(byte[] data, String ascii) {
        // Find section 0005 after a [03] delimiter
        int idx = -1;
        for (int i = 0; i < data.length - 24; i++) {
            if (data[i] == 0x03 && i + 4 < data.length
                    && data[i+1] == '0' && data[i+2] == '0' && data[i+3] == '0' && data[i+4] == '5') {
                idx = i + 1;
                break;
            }
        }
        if (idx < 0 || idx + 24 >= data.length) return 0;

        // Validate: followed by 8-char hex length + 8-char hex count
        String lenStr = ascii.substring(idx + 4, Math.min(idx + 12, ascii.length()));
        if (!lenStr.matches("[0-9A-Fa-f]+")) return 0;

        // Skip tag(4) + length(8) + count(8) = 20, then read first entry
        int bodyStart = idx + 20;
        if (bodyStart >= data.length) return 0;

        // First entry format: [02]<offset>[01]<value> or [02]<offset>[81] (high-bit value)
        // Skip the [02] delimiter if present
        int pos = bodyStart;
        if (data[pos] == 0x02) pos++;

        // Scan for the first [01]-delimited paragraph-style index. This is not the final
        // paragraph record itself; it selects a style in section 0007.
        for (int i = pos; i < Math.min(bodyStart + 30, data.length); i++) {
            if (data[i] == 0x01 && i + 1 < data.length) {
                return parseHexValue(data, i + 1);
            }
        }
        return 0;
    }

    private static int countParagraphStyleRecords(byte[] data) {
        return extractParagraphStyleRecords(data).size();
    }

    private static byte[] extractSelectedParagraphStyleRecord(byte[] data, int primaryParagraphStyleIndex) {
        List<byte[]> records = extractParagraphStyleRecords(data);
        if (primaryParagraphStyleIndex < 0 || primaryParagraphStyleIndex >= records.size()) {
            return null;
        }
        return records.get(primaryParagraphStyleIndex);
    }

    private static List<byte[]> extractParagraphStyleRecords(byte[] data) {
        int idx = findSection(data, "0007");
        if (idx < 0) {
            return List.of();
        }
        int secStart = idx + 20;
        int secLen = parseSectionLength(toAsciiString(data), idx);
        int secEnd = secLen > 0 ? Math.min(secStart + secLen, data.length) : data.length;

        java.util.ArrayList<byte[]> records = new java.util.ArrayList<>();
        int start = secStart;
        for (int i = secStart; i + 1 < secEnd; i++) {
            if ((data[i] & 0xFF) == 0xC2 && (data[i + 1] & 0xFF) == 0x12) {
                records.add(java.util.Arrays.copyOfRange(data, start, i + 2));
                start = i + 2;
            }
        }
        return List.copyOf(records);
    }

    private static int extractParagraphAlignmentCode(byte[] paragraphStyleRecord) {
        if (paragraphStyleRecord == null || paragraphStyleRecord.length == 0) {
            return -1;
        }
        for (int i = 0; i + 1 < paragraphStyleRecord.length; i++) {
            if ((paragraphStyleRecord[i] & 0xFF) == 0xC2 && (paragraphStyleRecord[i + 1] & 0xFF) == 0x0F) {
                for (int j = i + 2; j < paragraphStyleRecord.length; j++) {
                    int b = paragraphStyleRecord[j] & 0xFF;
                    if (b == 0x01 || b == 0x02) {
                        int value = parseHexValue(paragraphStyleRecord, j + 1);
                        if (value >= 0) {
                            return value;
                        }
                    }
                }
            }
        }
        return -1;
    }

    private static String alignmentFromParagraphAlignmentCode(int val) {
        return switch (val) {
            case 1 -> "center";
            case 2 -> "left";
            case 3 -> "right";
            default -> null;
        };
    }

    private static String alignmentFromParagraphStyleIndex(int val) {
        return switch (val) {
            case 1 -> "center";
            case 2 -> "right";
            default -> "left";
        };
    }

    private static int parseHexValue(byte[] data, int pos) {
        StringBuilder sb = new StringBuilder();
        while (pos < data.length) {
            int b = data[pos] & 0xFF;
            if (isHexDigit(b)) {
                sb.append((char) b);
                pos++;
            } else {
                break;
            }
        }
        if (sb.length() == 0) return 0;
        try {
            return Integer.parseInt(sb.toString(), 16);
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    private static final char[] MAC_ROMAN = {
        '\u00C4', '\u00C5', '\u00C7', '\u00C9', '\u00D1', '\u00D6', '\u00DC', '\u00E1',
        '\u00E0', '\u00E2', '\u00E4', '\u00E3', '\u00E5', '\u00E7', '\u00E9', '\u00E8',
        '\u00EA', '\u00EB', '\u00ED', '\u00EC', '\u00EE', '\u00EF', '\u00F1', '\u00F3',
        '\u00F2', '\u00F4', '\u00F6', '\u00F5', '\u00FA', '\u00F9', '\u00FB', '\u00FC',
        '\u2020', '\u00B0', '\u00A2', '\u00A3', '\u00A7', '\u2022', '\u00B6', '\u00DF',
        '\u00AE', '\u00A9', '\u2122', '\u00B4', '\u00A8', '\u2260', '\u00C6', '\u00D8',
        '\u221E', '\u00B1', '\u2264', '\u2265', '\u00A5', '\u00B5', '\u2202', '\u2211',
        '\u220F', '\u03C0', '\u222B', '\u00AA', '\u00BA', '\u03A9', '\u00E6', '\u00F8',
        '\u00BF', '\u00A1', '\u00AC', '\u221A', '\u0192', '\u2248', '\u2206', '\u00AB',
        '\u00BB', '\u2026', '\u00A0', '\u00C0', '\u00C3', '\u00D5', '\u0152', '\u0153',
        '\u2013', '\u2014', '\u201C', '\u201D', '\u2018', '\u2019', '\u00F7', '\u25CA',
        '\u00FF', '\u0178', '\u2044', '\u20AC', '\u2039', '\u203A', '\uFB01', '\uFB02',
        '\u2021', '\u00B7', '\u201A', '\u201E', '\u2030', '\u00C2', '\u00CA', '\u00C1',
        '\u00CB', '\u00C8', '\u00CD', '\u00CE', '\u00CF', '\u00CC', '\u00D3', '\u00D4',
        '\uF8FF', '\u00D2', '\u00DA', '\u00DB', '\u00D9', '\u0131', '\u02C6', '\u02DC',
        '\u00AF', '\u02D8', '\u02D9', '\u02DA', '\u00B8', '\u02DD', '\u02DB', '\u02C7',
    };
}
