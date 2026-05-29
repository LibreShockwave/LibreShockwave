package com.libreshockwave.cast;

/**
 * Parsed text member properties from CASt specificData.
 * Used for Score-placed text members (STXT-based) to extract formatting info.
 */
public record TextInfo(
    int textAlign,
    int bgRed, int bgGreen, int bgBlue,
    int width, int height,
    int borderSize,
    int gutterSize,
    boolean isWordWrap
) {
    /**
     * Parse TextInfo from CASt specificData bytes.
     * Director text member specificData layout varies by version.
     */
    public static TextInfo parse(byte[] data) {
        if (data == null || data.length < 4) {
            return new TextInfo(0, 255, 255, 255, 0, 0, 0, 0, true);
        }

        int textAlign = 0;
        int bgR = 255, bgG = 255, bgB = 255;
        int width = 0, height = 0;
        int borderSize = 0;
        int gutterSize = 0;
        boolean wordWrap = true;

        if (data.length >= 48) {
            // D7+ text member specificData: alignment at offset 0, background
            // color at 2/4/6, dimensions at 38/40.
            textAlign = (short) (((data[0] & 0xFF) << 8) | (data[1] & 0xFF));
            bgR = data[2] & 0xFF;
            bgG = data[4] & 0xFF;
            bgB = data[6] & 0xFF;
            // D7+ text member: width at offset 38, height at offset 40
            width = ((data[38] & 0xFF) << 8) | (data[39] & 0xFF);
            height = ((data[40] & 0xFF) << 8) | (data[41] & 0xFF);
        } else if (data.length >= 28) {
            // Director 4/5 text members use a compact record:
            // border, gutter, shadow, textType, align, bg rgb u16s, scroll,
            // initial rect, maxHeight, shadow/flags/textHeight.
            borderSize = data[0] & 0xFF;
            gutterSize = data[1] & 0xFF;
            textAlign = (short) (((data[4] & 0xFF) << 8) | (data[5] & 0xFF));
            bgR = data[7] & 0xFF;
            bgG = data[9] & 0xFF;
            bgB = data[11] & 0xFF;
        } else {
            textAlign = (short) (((data[0] & 0xFF) << 8) | (data[1] & 0xFF));
            if (data.length >= 8) {
                bgR = data[2] & 0xFF;
                bgG = data[4] & 0xFF;
                bgB = data[6] & 0xFF;
            }
        }

        return new TextInfo(textAlign, bgR, bgG, bgB, width, height, borderSize, gutterSize, wordWrap);
    }
}
