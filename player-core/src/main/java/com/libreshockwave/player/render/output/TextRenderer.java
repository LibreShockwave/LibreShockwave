package com.libreshockwave.player.render.output;

import com.libreshockwave.bitmap.Bitmap;

import java.util.List;
import java.util.function.ToIntFunction;

/**
 * Platform-agnostic interface for text rendering and measurement.
 * Desktop player provides an AWT implementation; WASM provides a simple stub.
 * <p>
 * Used by CastMember to render text members to bitmap images and
 * to implement charPosToLoc() measurements.
 */
public interface TextRenderer {

    /**
     * Render text content to a bitmap image.
     *
     * @return rendered bitmap, or null if rendering is not supported
     */
    Bitmap renderText(String text, int width, int height,
                      String fontName, int fontSize, String fontStyle,
                      String alignment, int textColor, int bgColor,
                      boolean wordWrap, boolean antialias,
                      int fixedLineSpace, int topSpacing);

    /**
     * Compute the pixel position of a character in text.
     * Used for Director's charPosToLoc() method.
     *
     * @return int array {x, y} in pixels, where y is the top of the text line (lineNum * lineHeight)
     */
    int[] charPosToLoc(String text, int charIndex,
                       String fontName, int fontSize, String fontStyle,
                       int fixedLineSpace, String alignment, int fieldWidth);

    /**
     * Compute the character index at a given pixel position in text.
     * Inverse of charPosToLoc(). Used for mouse click → caret placement.
     *
     * @return 0-based character index into the full text string
     */
    int locToCharPos(String text, int x, int y,
                     String fontName, int fontSize, String fontStyle,
                     int fixedLineSpace, String alignment, int fieldWidth);

    /**
     * Get the line height for text rendering.
     */
    int getLineHeight(String fontName, int fontSize, String fontStyle,
                      int fixedLineSpace);

    /**
     * Render XMED styled text to a bitmap image.
     * Dedicated path for Director 7+ Text Asset Xtra members, separate from STXT.
     * The XmedStyledText contains all properties needed for rendering.
     *
     * @param styledText the fully-parsed XMED text with per-run styling
     * @param width      bitmap width in pixels
     * @param height     bitmap height in pixels
     * @param textColor  ARGB text color (overrides span colors if non-zero)
     * @param bgColor    ARGB background color
     * @return rendered bitmap, or null if rendering is not supported
     */
    default Bitmap renderXmedText(com.libreshockwave.cast.XmedStyledText styledText,
                                  int width, int height,
                                  int textColor, int bgColor) {
        // Default: delegate to renderText() using primary font info
        if (styledText == null) return null;
        return renderText(styledText.text(), width, height,
                styledText.fontName(), styledText.fontSize(),
                styledText.fontStyleString(),
                styledText.alignment(), textColor, bgColor,
                styledText.wordWrap(), styledText.antialias(),
                styledText.fixedLineSpace(), 0);
    }

    /**
     * Split text into logical lines while preserving empty lines and treating CRLF
     * as a single line break. Director field/text members use classic Mac-style
     * returns heavily, but imported/external text may also contain CRLF/LF.
     */
    static String[] splitLines(String text) {
        if (text == null || text.isEmpty()) {
            return new String[]{""};
        }
        return text.split("\\r\\n|\\r|\\n", -1);
    }

    /**
     * Word-wrap a single line of text into multiple lines that fit within maxWidth.
     * Shared by all TextRenderer implementations.
     *
     * @param text         the line to wrap
     * @param measureWidth function that returns the pixel width of a string
     * @param maxWidth     maximum pixel width per line
     * @param out          list to append wrapped lines to
     */
    /**
     * Find which line a character index falls on in multi-line text.
     * Shared by charPosToLoc implementations.
     *
     * @param text      the full text content
     * @param charIndex 1-based character index
     * @return int array {lineNum, charsOnLine} (0-based lineNum, clamped charsOnLine)
     */
    static int[] findCharLine(String text, int charIndex) {
        if (text == null || text.isEmpty()) {
            return new int[]{0, 0};
        }

        int idx = Math.max(0, Math.min(charIndex, text.length()));
        int lineNum = 0;
        int charsOnLine = 0;

        for (int pos = 0; pos < idx; pos++) {
            char ch = text.charAt(pos);
            if (ch == '\r') {
                if ((pos + 1) < idx && (pos + 1) < text.length() && text.charAt(pos + 1) == '\n') {
                    pos++;
                }
                lineNum++;
                charsOnLine = 0;
            } else if (ch == '\n') {
                lineNum++;
                charsOnLine = 0;
            } else {
                charsOnLine++;
            }
        }

        return new int[]{lineNum, charsOnLine};
    }

    /**
     * Return the 0-based character index at the start of the requested logical line.
     * Treats CRLF as a single break while preserving the underlying string indices.
     */
    static int lineStartIndex(String text, int targetLine) {
        if (text == null || text.isEmpty() || targetLine <= 0) {
            return 0;
        }

        int lineNum = 0;
        for (int pos = 0; pos < text.length(); pos++) {
            char ch = text.charAt(pos);
            if (ch == '\r') {
                if ((pos + 1) < text.length() && text.charAt(pos + 1) == '\n') {
                    pos++;
                }
                lineNum++;
                if (lineNum == targetLine) {
                    return pos + 1;
                }
            } else if (ch == '\n') {
                lineNum++;
                if (lineNum == targetLine) {
                    return pos + 1;
                }
            }
        }

        return text.length();
    }

    static void wrapLine(String text, ToIntFunction<String> measureWidth, int maxWidth, List<String> out) {
        if (text.isEmpty()) {
            out.add("");
            return;
        }
        if (measureWidth.applyAsInt(text) <= maxWidth) {
            out.add(text);
            return;
        }
        String[] words = text.split("\\s+");
        StringBuilder current = new StringBuilder();
        for (String word : words) {
            if (current.length() == 0) {
                current.append(word);
            } else {
                String candidate = current + " " + word;
                if (measureWidth.applyAsInt(candidate) <= maxWidth) {
                    current.append(" ").append(word);
                } else {
                    out.add(current.toString());
                    current = new StringBuilder(word);
                }
            }
        }
        if (current.length() > 0) {
            out.add(current.toString());
        }
    }
}
