package com.libreshockwave.font;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Pure-Java TTF rasterizer for WASM environments where AWT is not available.
 * Parses TrueType font bytes and rasterizes glyphs into a BitmapFont grid.
 * Uses the same TTF data that the desktop path uses via AWT Font.createFont().
 */
public class TtfBitmapRasterizer {

    /**
     * Rasterize a TTF font into a BitmapFont at the given pixel size.
     * @param ttfBytes raw TTF file bytes (from Pfr1TtfConverter)
     * @param targetSize desired font size in pixels
     * @param fontName font name for identification
     * @return BitmapFont, or null if parsing fails
     */
    public static BitmapFont rasterize(byte[] ttfBytes, int targetSize, String fontName) {
        if (ttfBytes == null || ttfBytes.length < 12 || targetSize <= 0) return null;

        try {
            TtfData ttf = parseTtf(ttfBytes);
            if (ttf == null) return null;
            return buildBitmapFont(ttf, targetSize, fontName);
        } catch (Exception e) {
            return null;
        }
    }

    // --- TTF parsing ---

    static class TtfData {
        int unitsPerEm;
        int ascender, descender;
        int indexToLocFormat; // 0=short, 1=long
        Map<Integer, Integer> cmap = new HashMap<>(); // charCode -> glyphIndex
        int[] advanceWidths; // per glyph
        int[] lsbArray; // per glyph (left side bearing)
        int[] locaOffsets; // glyph offsets into glyf table
        byte[] glyfTable;
        int numGlyphs;
    }

    static class TtfGlyph {
        int numContours;
        int xMin, yMin, xMax, yMax;
        List<List<TtfPoint>> contours = new ArrayList<>();
    }

    static class TtfPoint {
        int x, y;
        boolean onCurve;
        TtfPoint(int x, int y, boolean onCurve) {
            this.x = x; this.y = y; this.onCurve = onCurve;
        }
    }

    private static TtfData parseTtf(byte[] data) {
        int numTables = readU16(data, 4);
        Map<String, int[]> tables = new HashMap<>(); // tag -> [offset, length]

        for (int i = 0; i < numTables; i++) {
            int base = 12 + i * 16;
            if (base + 16 > data.length) break;
            String tag = new String(data, base, 4);
            int offset = readI32(data, base + 8);
            int length = readI32(data, base + 12);
            tables.put(tag, new int[]{offset, length});
        }

        TtfData ttf = new TtfData();

        // Parse head
        int[] head = tables.get("head");
        if (head == null) return null;
        ttf.unitsPerEm = readU16(data, head[0] + 18);
        ttf.indexToLocFormat = readI16(data, head[0] + 50);

        // Parse hhea
        int[] hhea = tables.get("hhea");
        if (hhea == null) return null;
        ttf.ascender = readI16(data, hhea[0] + 4);
        ttf.descender = readI16(data, hhea[0] + 6);
        int numHMetrics = readU16(data, hhea[0] + 34);

        // Parse maxp
        int[] maxp = tables.get("maxp");
        if (maxp == null) return null;
        ttf.numGlyphs = readU16(data, maxp[0] + 4);

        // Parse hmtx
        int[] hmtx = tables.get("hmtx");
        if (hmtx == null) return null;
        ttf.advanceWidths = new int[ttf.numGlyphs];
        ttf.lsbArray = new int[ttf.numGlyphs];
        int lastAW = 0;
        for (int i = 0; i < ttf.numGlyphs; i++) {
            if (i < numHMetrics) {
                int off = hmtx[0] + i * 4;
                if (off + 4 > data.length) break;
                ttf.advanceWidths[i] = readU16(data, off);
                ttf.lsbArray[i] = readI16(data, off + 2);
                lastAW = ttf.advanceWidths[i];
            } else {
                ttf.advanceWidths[i] = lastAW;
                int off = hmtx[0] + numHMetrics * 4 + (i - numHMetrics) * 2;
                if (off + 2 <= data.length) {
                    ttf.lsbArray[i] = readI16(data, off);
                }
            }
        }

        // Parse cmap
        int[] cmap = tables.get("cmap");
        if (cmap == null) return null;
        parseCmap(data, cmap[0], ttf);

        // Parse loca
        int[] loca = tables.get("loca");
        if (loca == null) return null;
        ttf.locaOffsets = new int[ttf.numGlyphs + 1];
        for (int i = 0; i <= ttf.numGlyphs; i++) {
            if (ttf.indexToLocFormat == 0) {
                int off = loca[0] + i * 2;
                if (off + 2 <= data.length) {
                    ttf.locaOffsets[i] = readU16(data, off) * 2;
                }
            } else {
                int off = loca[0] + i * 4;
                if (off + 4 <= data.length) {
                    ttf.locaOffsets[i] = readI32(data, off);
                }
            }
        }

        // Store glyf table reference
        int[] glyf = tables.get("glyf");
        if (glyf == null) return null;
        ttf.glyfTable = new byte[glyf[1]];
        System.arraycopy(data, glyf[0], ttf.glyfTable, 0,
                Math.min(glyf[1], data.length - glyf[0]));

        return ttf;
    }

    private static void parseCmap(byte[] data, int cmapOffset, TtfData ttf) {
        int numSubtables = readU16(data, cmapOffset + 2);
        for (int i = 0; i < numSubtables; i++) {
            int base = cmapOffset + 4 + i * 8;
            if (base + 8 > data.length) break;
            int platformId = readU16(data, base);
            int encodingId = readU16(data, base + 2);
            int subtableOffset = readI32(data, base + 4);

            // Prefer platform 3 (Windows), encoding 1 (Unicode BMP)
            if (platformId == 3 && encodingId == 1) {
                parseCmapFormat4(data, cmapOffset + subtableOffset, ttf);
                return;
            }
        }
        // Fallback: try first subtable
        if (numSubtables > 0) {
            int subtableOffset = readI32(data, cmapOffset + 4 + 4);
            int format = readU16(data, cmapOffset + subtableOffset);
            if (format == 4) {
                parseCmapFormat4(data, cmapOffset + subtableOffset, ttf);
            }
        }
    }

    private static void parseCmapFormat4(byte[] data, int offset, TtfData ttf) {
        if (offset + 14 > data.length) return;
        int segCount = readU16(data, offset + 6) / 2;
        int headerSize = 14;

        int endCodesOff = offset + headerSize;
        int startCodesOff = endCodesOff + segCount * 2 + 2; // +2 for reservedPad
        int idDeltaOff = startCodesOff + segCount * 2;
        int idRangeOff = idDeltaOff + segCount * 2;

        for (int i = 0; i < segCount; i++) {
            int endCode = readU16(data, endCodesOff + i * 2);
            int startCode = readU16(data, startCodesOff + i * 2);
            int idDelta = readI16(data, idDeltaOff + i * 2);
            int idRangeOffset = readU16(data, idRangeOff + i * 2);

            if (startCode == 0xFFFF) break;

            for (int c = startCode; c <= endCode; c++) {
                int glyphIndex;
                if (idRangeOffset == 0) {
                    glyphIndex = (c + idDelta) & 0xFFFF;
                } else {
                    int rangeAddr = idRangeOff + i * 2 + idRangeOffset + (c - startCode) * 2;
                    if (rangeAddr + 2 > data.length) continue;
                    glyphIndex = readU16(data, rangeAddr);
                    if (glyphIndex != 0) {
                        glyphIndex = (glyphIndex + idDelta) & 0xFFFF;
                    }
                }
                if (glyphIndex != 0 && glyphIndex < ttf.numGlyphs) {
                    ttf.cmap.put(c, glyphIndex);
                }
            }
        }
    }

    private static TtfGlyph parseGlyph(TtfData ttf, int glyphIndex) {
        if (glyphIndex < 0 || glyphIndex >= ttf.numGlyphs) return null;
        int off = ttf.locaOffsets[glyphIndex];
        int nextOff = ttf.locaOffsets[glyphIndex + 1];
        if (off == nextOff) return null; // empty glyph
        if (off < 0 || off + 10 > ttf.glyfTable.length) return null;

        byte[] g = ttf.glyfTable;
        TtfGlyph glyph = new TtfGlyph();
        glyph.numContours = readI16(g, off);
        glyph.xMin = readI16(g, off + 2);
        glyph.yMin = readI16(g, off + 4);
        glyph.xMax = readI16(g, off + 6);
        glyph.yMax = readI16(g, off + 8);

        if (glyph.numContours < 0) return null; // composite glyphs not supported
        if (glyph.numContours == 0) return glyph;

        int pos = off + 10;

        // Read end points of contours
        int[] endPts = new int[glyph.numContours];
        for (int i = 0; i < glyph.numContours; i++) {
            if (pos + 2 > g.length) return null;
            endPts[i] = readU16(g, pos);
            pos += 2;
        }

        int numPoints = endPts[endPts.length - 1] + 1;

        // Skip instructions
        if (pos + 2 > g.length) return null;
        int instrLen = readU16(g, pos);
        pos += 2 + instrLen;

        // Read flags
        int[] flags = new int[numPoints];
        for (int i = 0; i < numPoints; i++) {
            if (pos >= g.length) return null;
            flags[i] = g[pos++] & 0xFF;
            if ((flags[i] & 0x08) != 0) { // repeat flag
                if (pos >= g.length) return null;
                int repeatCount = g[pos++] & 0xFF;
                for (int r = 0; r < repeatCount && i + 1 < numPoints; r++) {
                    flags[++i] = flags[i - 1];
                }
            }
        }

        // Read x coordinates
        int[] xCoords = new int[numPoints];
        int x = 0;
        for (int i = 0; i < numPoints; i++) {
            if ((flags[i] & 0x02) != 0) {
                if (pos >= g.length) return null;
                int dx = g[pos++] & 0xFF;
                x += (flags[i] & 0x10) != 0 ? dx : -dx;
            } else if ((flags[i] & 0x10) == 0) {
                if (pos + 2 > g.length) return null;
                x += readI16(g, pos);
                pos += 2;
            }
            xCoords[i] = x;
        }

        // Read y coordinates
        int[] yCoords = new int[numPoints];
        int y = 0;
        for (int i = 0; i < numPoints; i++) {
            if ((flags[i] & 0x04) != 0) {
                if (pos >= g.length) return null;
                int dy = g[pos++] & 0xFF;
                y += (flags[i] & 0x20) != 0 ? dy : -dy;
            } else if ((flags[i] & 0x20) == 0) {
                if (pos + 2 > g.length) return null;
                y += readI16(g, pos);
                pos += 2;
            }
            yCoords[i] = y;
        }

        // Build contours with proper on-curve/off-curve handling
        int ptIdx = 0;
        for (int c = 0; c < glyph.numContours; c++) {
            int endPt = endPts[c];
            List<TtfPoint> contour = new ArrayList<>();
            for (int i = ptIdx; i <= endPt; i++) {
                boolean onCurve = (flags[i] & 0x01) != 0;
                contour.add(new TtfPoint(xCoords[i], yCoords[i], onCurve));
            }
            if (contour.size() >= 2) {
                glyph.contours.add(contour);
            }
            ptIdx = endPt + 1;
        }

        return glyph;
    }

    // --- BitmapFont construction ---

    private static BitmapFont buildBitmapFont(TtfData ttf, int targetSize, String fontName) {
        float scale = (float) targetSize / ttf.unitsPerEm;

        // Calculate cell dimensions
        int maxAdvPx = 0;
        int maxGlyphWidthPx = 0;
        for (var entry : ttf.cmap.entrySet()) {
            int gIdx = entry.getValue();
            int advPx = Math.round(ttf.advanceWidths[gIdx] * scale);
            maxAdvPx = Math.max(maxAdvPx, advPx);
            TtfGlyph glyph = parseGlyph(ttf, gIdx);
            if (glyph != null) {
                int glyphWidthPx = Math.max(1, Math.round((glyph.xMax - glyph.xMin) * scale) + 1);
                maxGlyphWidthPx = Math.max(maxGlyphWidthPx, glyphWidthPx);
            }
        }

        int ascPx = Math.round(Math.abs(ttf.ascender) * scale);
        int descPx = Math.round(Math.abs(ttf.descender) * scale);
        int metricsLineHeight = ascPx + descPx; // matches AWT FontMetrics.getHeight()
        int cellHeight = metricsLineHeight + 1; // +1 for cell storage (avoid clipping)
        int cellWidth = Math.max(Math.max(maxAdvPx, maxGlyphWidthPx), 1);

        int bitmapWidth = cellWidth * BitmapFont.GRID_COLUMNS;
        int bitmapHeight = cellHeight * BitmapFont.GRID_ROWS;

        int[] argb = new int[bitmapWidth * bitmapHeight];
        int[] charWidths = new int[BitmapFont.NUM_CHARS];
        int[] charOffsetsX = new int[BitmapFont.NUM_CHARS];
        for (int i = 0; i < BitmapFont.NUM_CHARS; i++) charWidths[i] = cellWidth;

        Map<Integer, int[]> overflowGlyphs = new HashMap<>();
        Map<Integer, Integer> overflowWidths = new HashMap<>();
        Map<Integer, Integer> overflowOffsetsX = new HashMap<>();

        // Rasterize each mapped character
        for (var entry : ttf.cmap.entrySet()) {
            int charCode = entry.getKey();
            int glyphIndex = entry.getValue();

            int advancePx = Math.max(1, Math.round(ttf.advanceWidths[glyphIndex] * scale));
            TtfGlyph glyph = parseGlyph(ttf, glyphIndex);
            int glyphOffsetX = 0;
            int glyphDrawOffsetX = 0;
            if (glyph != null) {
                glyphDrawOffsetX = Math.round(ttf.lsbArray[glyphIndex] * scale);
                glyphOffsetX = -Math.round(glyph.xMin * scale);
            }

            if (charCode < BitmapFont.NUM_CHARS) {
                charWidths[charCode] = advancePx;
                charOffsetsX[charCode] = glyphDrawOffsetX;

                if (glyph != null && !glyph.contours.isEmpty()) {
                    int col = charCode % BitmapFont.GRID_COLUMNS;
                    int row = charCode / BitmapFont.GRID_COLUMNS;
                    int cellX = col * cellWidth;
                    int cellY = row * cellHeight;

                    rasterizeContours(glyph.contours, argb, bitmapWidth, bitmapHeight,
                            cellX + glyphOffsetX, cellY, cellWidth, cellHeight, scale, ascPx);
                }
            } else {
                overflowWidths.put(charCode, advancePx);
                overflowOffsetsX.put(charCode, glyphDrawOffsetX);

                if (glyph != null && !glyph.contours.isEmpty()) {
                    int[] cellBuf = new int[cellWidth * cellHeight];
                    rasterizeContours(glyph.contours, cellBuf, cellWidth, cellHeight,
                            glyphOffsetX, 0, cellWidth, cellHeight, scale, ascPx);
                    overflowGlyphs.put(charCode, cellBuf);
                }
            }
        }

        // Set space width if not mapped
        if (charWidths[' '] == cellWidth && ttf.cmap.containsKey((int) ' ')) {
            // already set above
        } else if (!ttf.cmap.containsKey((int) ' ')) {
            // Estimate space width as ~1/4 of unitsPerEm
            charWidths[' '] = Math.max(1, Math.round(ttf.unitsPerEm * scale / 4));
        }

        return BitmapFont.create(argb, bitmapWidth, bitmapHeight,
                cellWidth, cellHeight, charWidths, charOffsetsX, fontName, targetSize,
                ascPx, metricsLineHeight,
                overflowGlyphs, overflowWidths, overflowOffsetsX);
    }

    /**
     * Rasterize TrueType contours with proper quadratic Bezier handling.
     */
    private static void rasterizeContours(List<List<TtfPoint>> contours,
                                           int[] argb, int bufWidth, int bufHeight,
                                           int cellX, int cellY, int cellWidth, int cellHeight,
                                           float scale, int baselineY) {
        // Flatten contours to polygon edges (resolve off-curve points)
        List<List<float[]>> polygons = new ArrayList<>();
        for (List<TtfPoint> contour : contours) {
            List<float[]> points = flattenTtfContour(contour, scale, baselineY);
            if (points.size() >= 3) {
                polygons.add(points);
            }
        }

        if (polygons.isEmpty()) return;

        // Scanline rasterization with non-zero winding fill
        for (int y = 0; y < cellHeight; y++) {
            float scanY = y + 0.5f;
            List<float[]> crossings = new ArrayList<>();

            for (List<float[]> polygon : polygons) {
                int n = polygon.size();
                for (int i = 0; i < n; i++) {
                    float x0 = polygon.get(i)[0];
                    float y0 = polygon.get(i)[1];
                    float x1 = polygon.get((i + 1) % n)[0];
                    float y1 = polygon.get((i + 1) % n)[1];

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
                    float fx0 = crossings.get(i)[0];
                    float fx1 = crossings.get(i + 1)[0];
                    // Use round for pixel-center sampling (fill pixel if center is inside span)
                    int xStart = Math.max(0, Math.min(cellWidth, Math.round(fx0)));
                    int xEnd = Math.max(0, Math.min(cellWidth, Math.round(fx1)));

                    for (int bx = xStart; bx < xEnd; bx++) {
                        int px = cellX + bx;
                        int py = cellY + y;
                        if (px >= 0 && px < bufWidth && py >= 0 && py < bufHeight) {
                            int idx = py * bufWidth + px;
                            if (idx >= 0 && idx < argb.length) {
                                argb[idx] = 0xFF000000;
                            }
                        }
                    }
                }
            }
        }
    }

    /**
     * Flatten a TrueType contour (with on-curve and off-curve points) into
     * a list of pixel-space [x, y] coordinates, subdividing quadratic Beziers.
     */
    private static List<float[]> flattenTtfContour(List<TtfPoint> contour, float scale, int baselineY) {
        List<float[]> result = new ArrayList<>();
        int n = contour.size();
        if (n < 2) return result;

        // TrueType contours: implied on-curve points between consecutive off-curve points
        // First, build a complete point list with implied points
        List<TtfPoint> expanded = new ArrayList<>();
        for (int i = 0; i < n; i++) {
            TtfPoint cur = contour.get(i);
            TtfPoint next = contour.get((i + 1) % n);
            expanded.add(cur);
            if (!cur.onCurve && !next.onCurve) {
                // Insert implied on-curve point between two off-curve points
                expanded.add(new TtfPoint(
                        (cur.x + next.x) / 2,
                        (cur.y + next.y) / 2,
                        true));
            }
        }

        // Find first on-curve point to start from
        int startIdx = -1;
        for (int i = 0; i < expanded.size(); i++) {
            if (expanded.get(i).onCurve) { startIdx = i; break; }
        }
        if (startIdx < 0) return result;

        // Walk through the contour
        int sz = expanded.size();
        float startX = expanded.get(startIdx).x * scale;
        float startY = baselineY - expanded.get(startIdx).y * scale;
        result.add(new float[]{startX, startY});

        int i = (startIdx + 1) % sz;
        int count = 0;
        while (count < sz) {
            TtfPoint p = expanded.get(i);
            if (p.onCurve) {
                float px = p.x * scale;
                float py = baselineY - p.y * scale;
                result.add(new float[]{px, py});
            } else {
                // Quadratic Bezier: current off-curve point + next on-curve point
                TtfPoint next = expanded.get((i + 1) % sz);
                float cpx = p.x * scale;
                float cpy = baselineY - p.y * scale;
                float epx = next.x * scale;
                float epy = baselineY - next.y * scale;

                // Subdivide quadratic Bezier
                float[] last = result.get(result.size() - 1);
                subdivideQuadratic(result, last[0], last[1], cpx, cpy, epx, epy, 3);

                i = (i + 1) % sz; // skip the next on-curve point (already added as endpoint)
                count++;
            }
            i = (i + 1) % sz;
            count++;
            if (i == startIdx) break;
        }

        return result;
    }

    private static void subdivideQuadratic(List<float[]> result,
                                            float x0, float y0,
                                            float cpx, float cpy,
                                            float ex, float ey,
                                            int levels) {
        if (levels <= 0) {
            result.add(new float[]{ex, ey});
            return;
        }
        float mx = (x0 + 2 * cpx + ex) / 4;
        float my = (y0 + 2 * cpy + ey) / 4;
        float cp1x = (x0 + cpx) / 2;
        float cp1y = (y0 + cpy) / 2;
        float cp2x = (cpx + ex) / 2;
        float cp2y = (cpy + ey) / 2;
        subdivideQuadratic(result, x0, y0, cp1x, cp1y, mx, my, levels - 1);
        subdivideQuadratic(result, mx, my, cp2x, cp2y, ex, ey, levels - 1);
    }

    // --- Byte reading helpers ---

    private static int readU16(byte[] data, int offset) {
        if (offset + 2 > data.length) return 0;
        return ((data[offset] & 0xFF) << 8) | (data[offset + 1] & 0xFF);
    }

    private static int readI16(byte[] data, int offset) {
        int val = readU16(data, offset);
        return val >= 0x8000 ? val - 0x10000 : val;
    }

    private static int readI32(byte[] data, int offset) {
        if (offset + 4 > data.length) return 0;
        return ((data[offset] & 0xFF) << 24) |
               ((data[offset + 1] & 0xFF) << 16) |
               ((data[offset + 2] & 0xFF) << 8) |
               (data[offset + 3] & 0xFF);
    }
}
