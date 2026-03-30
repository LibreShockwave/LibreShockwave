package com.libreshockwave.font;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Rasterized bitmap font from PFR1 data.
 * Contains a grid of glyph bitmaps with per-character advance widths.
 *
 * The grid is 16 columns x 8 rows = 128 character slots (ASCII).
 * Each glyph occupies one cell of cellWidth x cellHeight pixels.
 * bitmap is ARGB int array (same format as our Bitmap class).
 */
public class BitmapFont {

    public static final int GRID_COLUMNS = 16;
    public static final int GRID_ROWS = 8;
    public static final int NUM_CHARS = 128;

    private final int[] bitmap;       // ARGB pixel data for entire grid
    private final int bitmapWidth;
    private final int bitmapHeight;
    private final int cellWidth;
    private final int cellHeight;
    private final int[] charWidths;   // per-character advance width in pixels (for grid chars 0-127)
    private final int[] charOffsetsX; // per-character draw offset relative to the pen position
    private final String fontName;
    private final int fontSize;       // target rendering size
    private final int metricsAscent;  // baseline distance from top (for text positioning)
    private final int metricsLineHeight; // line spacing (ascent + descent, matches AWT FontMetrics.getHeight())

    // Overflow storage for chars > 127 (Unicode extended)
    private final Map<Integer, int[]> overflowGlyphs; // charCode -> ARGB pixels (cellWidth x cellHeight)
    private final Map<Integer, Integer> overflowWidths; // charCode -> advance width
    private final Map<Integer, Integer> overflowOffsetsX; // charCode -> draw offset

    /**
     * Factory for constructing a BitmapFont from external rasterizers.
     */
    public static BitmapFont create(int[] bitmap, int bitmapWidth, int bitmapHeight,
                              int cellWidth, int cellHeight, int[] charWidths,
                              String fontName, int fontSize,
                              int metricsAscent, int metricsLineHeight,
                              Map<Integer, int[]> overflowGlyphs,
                              Map<Integer, Integer> overflowWidths) {
        return create(bitmap, bitmapWidth, bitmapHeight,
                cellWidth, cellHeight, charWidths, new int[NUM_CHARS],
                fontName, fontSize, metricsAscent, metricsLineHeight,
                overflowGlyphs, overflowWidths, new HashMap<>());
    }

    public static BitmapFont create(int[] bitmap, int bitmapWidth, int bitmapHeight,
                              int cellWidth, int cellHeight, int[] charWidths, int[] charOffsetsX,
                              String fontName, int fontSize,
                              int metricsAscent, int metricsLineHeight,
                              Map<Integer, int[]> overflowGlyphs,
                              Map<Integer, Integer> overflowWidths,
                              Map<Integer, Integer> overflowOffsetsX) {
        return new BitmapFont(bitmap, bitmapWidth, bitmapHeight,
                cellWidth, cellHeight, charWidths, charOffsetsX, fontName, fontSize,
                metricsAscent, metricsLineHeight,
                overflowGlyphs, overflowWidths, overflowOffsetsX);
    }

    private BitmapFont(int[] bitmap, int bitmapWidth, int bitmapHeight,
                       int cellWidth, int cellHeight, int[] charWidths, int[] charOffsetsX,
                       String fontName, int fontSize,
                       int metricsAscent, int metricsLineHeight,
                       Map<Integer, int[]> overflowGlyphs,
                       Map<Integer, Integer> overflowWidths,
                       Map<Integer, Integer> overflowOffsetsX) {
        this.bitmap = bitmap;
        this.bitmapWidth = bitmapWidth;
        this.bitmapHeight = bitmapHeight;
        this.cellWidth = cellWidth;
        this.cellHeight = cellHeight;
        this.charWidths = charWidths;
        this.charOffsetsX = charOffsetsX != null ? charOffsetsX : new int[NUM_CHARS];
        this.fontName = fontName;
        this.fontSize = fontSize;
        this.metricsAscent = metricsAscent;
        this.metricsLineHeight = metricsLineHeight;
        this.overflowGlyphs = overflowGlyphs;
        this.overflowWidths = overflowWidths;
        this.overflowOffsetsX = overflowOffsetsX != null ? overflowOffsetsX : new HashMap<>();
    }

    /** Get advance width for a character (in pixels). */
    public int getCharWidth(int charCode) {
        if (charCode >= 0 && charCode < charWidths.length) return charWidths[charCode];
        Integer ow = overflowWidths.get(charCode);
        return ow != null ? ow : cellWidth;
    }

    public int getCharOffsetX(int charCode) {
        if (charCode >= 0 && charCode < charOffsetsX.length) return charOffsetsX[charCode];
        Integer offset = overflowOffsetsX.get(charCode);
        return offset != null ? offset : 0;
    }

    /** Get total string width in pixels. */
    public int getStringWidth(String text) {
        int w = 0;
        for (int i = 0; i < text.length(); i++) {
            w += getCharWidth(text.charAt(i));
        }
        return w;
    }

    /** Get the line height (ascent + descent, for text line spacing). */
    public int getLineHeight() { return metricsLineHeight; }
    /** Get the ascent (baseline distance from top of cell). */
    public int getAscent() { return metricsAscent; }
    public String getFontName() { return fontName; }
    public int getFontSize() { return fontSize; }

    /**
     * Draw a single character at the given position into a destination ARGB buffer.
     * @param ch       character to draw
     * @param dst      destination ARGB pixel array
     * @param dstW     destination buffer width
     * @param dstH     destination buffer height
     * @param dstX     x position in destination
     * @param dstY     y position in destination
     * @param color    text color (0xAARRGGBB)
     */
    public void drawChar(char ch, int[] dst, int dstW, int dstH, int dstX, int dstY, int color) {
        int charCode = (int) ch;
        int drawX = dstX + getCharOffsetX(charCode);

        int r = (color >> 16) & 0xFF;
        int g = (color >> 8) & 0xFF;
        int b = color & 0xFF;

        if (charCode >= 0 && charCode < NUM_CHARS) {
            // Grid-based rendering for ASCII 0-127
            int col = charCode % GRID_COLUMNS;
            int row = charCode / GRID_COLUMNS;
            int cellX = col * cellWidth;
            int cellY = row * cellHeight;

            for (int cy = 0; cy < cellHeight; cy++) {
                int py = dstY + cy;
                if (py < 0 || py >= dstH) continue;
                for (int cx = 0; cx < cellWidth; cx++) {
                    int px = drawX + cx;
                    if (px < 0 || px >= dstW) continue;

                    int srcIdx = (cellY + cy) * bitmapWidth + (cellX + cx);
                    if (srcIdx < 0 || srcIdx >= bitmap.length) continue;

                    blendPixel(dst, dstW, px, py, bitmap[srcIdx], r, g, b);
                }
            }
        } else {
            // Overflow rendering for chars > 127
            int[] glyphPixels = overflowGlyphs.get(charCode);
            if (glyphPixels == null) return;

            for (int cy = 0; cy < cellHeight; cy++) {
                int py = dstY + cy;
                if (py < 0 || py >= dstH) continue;
                for (int cx = 0; cx < cellWidth; cx++) {
                    int px = drawX + cx;
                    if (px < 0 || px >= dstW) continue;

                    int srcIdx = cy * cellWidth + cx;
                    if (srcIdx < 0 || srcIdx >= glyphPixels.length) continue;

                    blendPixel(dst, dstW, px, py, glyphPixels[srcIdx], r, g, b);
                }
            }
        }
    }

    private void blendPixel(int[] dst, int dstW, int px, int py, int srcPixel, int r, int g, int b) {
        int srcA = (srcPixel >> 24) & 0xFF;
        if (srcA == 0) return;

        int dstIdx = py * dstW + px;
        if (dstIdx < 0 || dstIdx >= dst.length) return;

        if (srcA == 255) {
            dst[dstIdx] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        } else {
            int existing = dst[dstIdx];
            int ea = (existing >> 24) & 0xFF;
            int er = (existing >> 16) & 0xFF;
            int eg = (existing >> 8) & 0xFF;
            int eb = existing & 0xFF;
            int outA = srcA + (ea * (255 - srcA)) / 255;
            int outR = (r * srcA + er * (255 - srcA)) / 255;
            int outG = (g * srcA + eg * (255 - srcA)) / 255;
            int outB = (b * srcA + eb * (255 - srcA)) / 255;
            dst[dstIdx] = (outA << 24) | (outR << 16) | (outG << 8) | outB;
        }
    }

    /**
     * Rasterize a Pfr1Font into a BitmapFont grid at the given target size.
     */
    public static BitmapFont fromPfr1(Pfr1Font font, int targetHeight) {
        if (font == null || targetHeight <= 0) return null;

        Pfr1Font.FontMetrics fm = font.metrics;
        int outlineRes = fm.outlineResolution;
        if (outlineRes <= 0) outlineRes = 2048;

        float scale;
        float metricHeight = Math.abs(fm.ascender - fm.descender);
        if (metricHeight > 0) {
            scale = (float) targetHeight / metricHeight;
        } else {
            scale = (float) targetHeight / outlineRes;
        }

        // Font matrix scaling
        float matrixScaleX = font.fontMatrix[0] / 256.0f;
        float matrixScaleY = font.fontMatrix[3] / 256.0f;
        float scaleX = scale * Math.abs(matrixScaleX);
        float scaleY = scale * Math.abs(matrixScaleY);

        // Determine cell dimensions from max glyph width
        float maxSetWidth = 0;
        for (Pfr1Font.OutlineGlyph g : font.glyphs.values()) {
            maxSetWidth = Math.max(maxSetWidth, g.setWidth);
        }

        float setWidthScale = scaleX;
        float maxBboxWidth = 0;
        for (Pfr1Font.OutlineGlyph g : font.glyphs.values()) {
            float minX = Float.MAX_VALUE, maxX = -Float.MAX_VALUE;
            for (Pfr1Font.Contour c : g.contours) {
                for (float[] cmd : c.commands) {
                    minX = Math.min(minX, cmd[1]);
                    maxX = Math.max(maxX, cmd[1]);
                }
            }
            if (minX < maxX) {
                maxBboxWidth = Math.max(maxBboxWidth, maxX - minX);
            }
        }
        float maxBboxWidthPx = maxBboxWidth * scaleX;

        int cellWidth = Math.max(1, Math.max(
                (int) Math.ceil(maxSetWidth * setWidthScale),
                (int) Math.ceil(maxBboxWidthPx)));
        // Compute ascent/descent in pixels using same scale as glyphs
        float pixelScale = (float) targetHeight / outlineRes;
        int pfrAscPx = Math.round(Math.abs(fm.ascender) * pixelScale);
        int pfrDescPx = Math.round(Math.abs(fm.descender) * pixelScale);
        int pfrLineHeight = pfrAscPx + pfrDescPx;

        // Cell height needs to be large enough to hold the glyph (may need +1 for rounding)
        int cellHeight = Math.max(targetHeight, pfrLineHeight + 1);

        // Ensure descender space fits
        if (fm.descender < 0) {
            int descPx = (int) Math.ceil(Math.abs(fm.descender) * pixelScale);
            int baselineRow = (int) Math.floor(fm.ascender * pixelScale);
            cellHeight = Math.max(cellHeight, baselineRow + descPx + 1);
        }

        int bitmapWidth = cellWidth * GRID_COLUMNS;
        int bitmapHeight = cellHeight * GRID_ROWS;

        // ARGB bitmap, transparent
        int[] argb = new int[bitmapWidth * bitmapHeight];

        // Per-character widths (grid chars 0-127)
        int[] charWidths = new int[NUM_CHARS];
        for (int i = 0; i < NUM_CHARS; i++) charWidths[i] = cellWidth;

        // Overflow storage for chars > 127
        Map<Integer, int[]> overflowGlyphs = new HashMap<>();
        Map<Integer, Integer> overflowWidths = new HashMap<>();

        float fontMinX = fm.xMin;
        float fontAsc = fm.ascender;

        // Render outline glyphs
        for (Map.Entry<Integer, Pfr1Font.OutlineGlyph> entry : font.glyphs.entrySet()) {
            int charCode = entry.getKey();
            Pfr1Font.OutlineGlyph glyph = entry.getValue();

            int glyphPixelWidth = Math.round(glyph.setWidth * setWidthScale);

            if (charCode < NUM_CHARS) {
                // Grid-based rendering for ASCII 0-127
                if (glyphPixelWidth > 0) {
                    charWidths[charCode] = glyphPixelWidth;
                }

                if (glyph.contours.isEmpty()) continue;

                int col = charCode % GRID_COLUMNS;
                int row = charCode / GRID_COLUMNS;
                int cellXPos = col * cellWidth;
                int cellYPos = row * cellHeight;

                float glyphScaleX = scaleX;
                float glyphScaleY = matrixScaleY < 0 ? scaleY : -scaleY;
                float glyphOffsetX = -fontMinX * scaleX;
                float glyphOffsetY = fontAsc > 0 ? fontAsc * pixelScale : 0;

                rasterizeGlyph(glyph, argb, bitmapWidth, bitmapHeight,
                        cellXPos, cellYPos, cellWidth, cellHeight,
                        glyphScaleX, glyphScaleY, glyphOffsetX, glyphOffsetY);
            } else {
                // Overflow rendering for chars > 127
                if (glyphPixelWidth > 0) {
                    overflowWidths.put(charCode, glyphPixelWidth);
                }

                if (glyph.contours.isEmpty()) continue;

                // Rasterize into a temporary cell-sized buffer
                int[] cellBuf = new int[cellWidth * cellHeight];
                int tempW = cellWidth;
                int tempH = cellHeight;

                float glyphScaleX = scaleX;
                float glyphScaleY = matrixScaleY < 0 ? scaleY : -scaleY;
                float glyphOffsetX = -fontMinX * scaleX;
                float glyphOffsetY = fontAsc > 0 ? fontAsc * pixelScale : 0;

                rasterizeGlyph(glyph, cellBuf, tempW, tempH,
                        0, 0, cellWidth, cellHeight,
                        glyphScaleX, glyphScaleY, glyphOffsetX, glyphOffsetY);

                overflowGlyphs.put(charCode, cellBuf);
            }
        }

        // Render bitmap glyphs (only for chars without outline contours, grid only)
        for (Map.Entry<Integer, Pfr1Font.BitmapGlyph> entry : font.bitmapGlyphs.entrySet()) {
            int charCode = entry.getKey();
            Pfr1Font.BitmapGlyph bmp = entry.getValue();
            if (charCode >= NUM_CHARS) continue; // bitmap glyphs only for grid chars

            // Skip if outline glyph has contours
            Pfr1Font.OutlineGlyph outline = font.glyphs.get(charCode);
            if (outline != null && !outline.contours.isEmpty()) continue;

            int col = charCode % GRID_COLUMNS;
            int row = charCode / GRID_COLUMNS;
            int cellXPos = col * cellWidth;
            int cellYPos = row * cellHeight;

            int bmpAdv = Math.max(1, Math.round(bmp.setWidth * setWidthScale));
            charWidths[charCode] = bmpAdv;

            // Copy bitmap data to ARGB grid
            for (int gy = 0; gy < bmp.ySize; gy++) {
                for (int gx = 0; gx < bmp.xSize; gx++) {
                    int bitIndex = gy * bmp.xSize + gx;
                    int byteIdx = bitIndex / 8;
                    int bitIdx = 7 - (bitIndex % 8);
                    if (byteIdx >= bmp.imageData.length) continue;

                    boolean bit = ((bmp.imageData[byteIdx] & (1 << bitIdx)) != 0);
                    if (!font.pfrBlackPixel) bit = !bit;
                    if (!bit) continue;

                    int px = cellXPos + gx + Math.max(0, bmp.xPos);
                    int py = cellYPos + gy + Math.max(0, bmp.yPos);
                    if (px >= bitmapWidth || py >= bitmapHeight) continue;
                    if (px >= cellXPos + cellWidth || py >= cellYPos + cellHeight) continue;

                    int idx = py * bitmapWidth + px;
                    if (idx >= 0 && idx < argb.length) {
                        argb[idx] = 0xFF000000; // opaque black
                    }
                }
            }
        }

        // Fallback: copy uppercase to empty lowercase cells
        for (int lc = 'a'; lc <= 'z'; lc++) {
            int li = lc;
            int ui = lc - 32;
            if (li >= NUM_CHARS || ui >= NUM_CHARS) continue;
            if (cellHasInk(argb, bitmapWidth, (li % GRID_COLUMNS) * cellWidth,
                    (li / GRID_COLUMNS) * cellHeight, cellWidth, cellHeight)) continue;
            if (!cellHasInk(argb, bitmapWidth, (ui % GRID_COLUMNS) * cellWidth,
                    (ui / GRID_COLUMNS) * cellHeight, cellWidth, cellHeight)) continue;
            copyCell(argb, bitmapWidth, ui, li, cellWidth, cellHeight);
            charWidths[li] = charWidths[ui];
        }

        return new BitmapFont(argb, bitmapWidth, bitmapHeight,
                cellWidth, cellHeight, charWidths, new int[NUM_CHARS], font.fontName, targetHeight,
                pfrAscPx, pfrLineHeight,
                overflowGlyphs, overflowWidths, new HashMap<>());
    }

    private static boolean cellHasInk(int[] argb, int bitmapWidth,
                                       int cx, int cy, int cw, int ch) {
        for (int y = 0; y < ch; y++) {
            for (int x = 0; x < cw; x++) {
                int idx = (cy + y) * bitmapWidth + (cx + x);
                if (idx >= 0 && idx < argb.length) {
                    if (((argb[idx] >> 24) & 0xFF) > 0) return true;
                }
            }
        }
        return false;
    }

    private static void copyCell(int[] argb, int bitmapWidth,
                                  int srcIdx, int dstIdx, int cellWidth, int cellHeight) {
        int srcCol = srcIdx % GRID_COLUMNS, srcRow = srcIdx / GRID_COLUMNS;
        int dstCol = dstIdx % GRID_COLUMNS, dstRow = dstIdx / GRID_COLUMNS;
        int srcX = srcCol * cellWidth, srcY = srcRow * cellHeight;
        int dstX = dstCol * cellWidth, dstY = dstRow * cellHeight;
        for (int y = 0; y < cellHeight; y++) {
            for (int x = 0; x < cellWidth; x++) {
                int si = (srcY + y) * bitmapWidth + (srcX + x);
                int di = (dstY + y) * bitmapWidth + (dstX + x);
                if (si >= 0 && si < argb.length && di >= 0 && di < argb.length) {
                    argb[di] = argb[si];
                }
            }
        }
    }

    /**
     * Rasterize a glyph outline into the bitmap grid using scanline fill.
     */
    private static void rasterizeGlyph(Pfr1Font.OutlineGlyph glyph,
                                        int[] argb, int bitmapWidth, int bitmapHeight,
                                        int cellX, int cellY, int cellWidth, int cellHeight,
                                        float scaleX, float scaleY, float offsetX, float offsetY) {
        // Flatten contours to polygon edges
        List<List<float[]>> polygons = new java.util.ArrayList<>();
        for (Pfr1Font.Contour contour : glyph.contours) {
            List<float[]> points = new java.util.ArrayList<>();
            for (float[] cmd : contour.commands) {
                int type = (int) cmd[0];
                if (type == 0 || type == 1) {
                    // MoveTo or LineTo
                    points.add(new float[]{cmd[1], cmd[2]});
                } else if (type == 2 && cmd.length >= 7) {
                    // CurveTo - flatten to line segments
                    // cmd = [type, endX, endY, cp1X, cp1Y, cp2X, cp2Y]
                    points.add(new float[]{cmd[1], cmd[2]});
                }
            }
            if (points.size() >= 3) {
                polygons.add(points);
            }
        }

        if (polygons.isEmpty()) return;

        // Scanline rasterization with non-zero winding fill
        for (int y = 0; y < cellHeight; y++) {
            float scanY = y + 0.5f;
            List<float[]> crossings = new java.util.ArrayList<>();

            for (List<float[]> polygon : polygons) {
                int n = polygon.size();
                for (int i = 0; i < n; i++) {
                    float x0 = polygon.get(i)[0] * scaleX + offsetX;
                    float y0 = polygon.get(i)[1] * scaleY + offsetY;
                    float x1 = polygon.get((i + 1) % n)[0] * scaleX + offsetX;
                    float y1 = polygon.get((i + 1) % n)[1] * scaleY + offsetY;

                    if (Math.abs(y0 - y1) < 0.001f) continue;

                    if ((y0 <= scanY && y1 > scanY) || (y1 <= scanY && y0 > scanY)) {
                        float t = (scanY - y0) / (y1 - y0);
                        float xCross = x0 + t * (x1 - x0);
                        int dir = y0 < y1 ? 1 : -1;
                        crossings.add(new float[]{xCross, dir});
                    }
                }
            }

            crossings.sort((a, b) -> Float.compare(a[0], b[0]));

            int winding = 0;
            for (int i = 0; i < crossings.size(); i++) {
                winding += (int) crossings.get(i)[1];
                if (i + 1 < crossings.size() && winding != 0) {
                    float x0 = crossings.get(i)[0];
                    float x1 = crossings.get(i + 1)[0];
                    // Use round for pixel-center sampling (fill pixel if center is inside span)
                    int xStart = Math.max(0, Math.min(cellWidth, Math.round(x0)));
                    int xEnd = Math.max(0, Math.min(cellWidth, Math.round(x1)));

                    for (int bx = xStart; bx < xEnd; bx++) {
                        int px = cellX + bx;
                        int py = cellY + y;
                        if (px >= 0 && px < bitmapWidth && py >= 0 && py < bitmapHeight) {
                            int idx = py * bitmapWidth + px;
                            if (idx >= 0 && idx < argb.length) {
                                argb[idx] = 0xFF000000; // opaque black
                            }
                        }
                    }
                }
            }
        }
    }
}
