package com.libreshockwave.bitmap;

/**
 * Decoder for Director bitmap data.
 * Handles RLE (PackBits) decompression and various bit depths.
 * Ported from dirplayer-rs bitmap.rs reference implementation.
 */
public class BitmapDecoder {

    /**
     * Decompress RLE-compressed bitmap data (PackBits format).
     * - If byte < 0x80: copy (byte + 1) literal bytes
     * - If byte == 0x80: no-op (PackBits standard)
     * - If byte > 0x80: repeat next byte (0x101 - byte) times
     */
    public static byte[] decompressRLE(byte[] compressed, int expectedSize) {
        if (expectedSize <= 0) {
            return new byte[0];
        }
        if (expectedSize > 100_000_000) {
            expectedSize = 100_000_000;
        }

        byte[] result = new byte[expectedSize];
        int pos = 0;
        int outPos = 0;

        while (pos < compressed.length && outPos < expectedSize) {
            int control = compressed[pos++] & 0xFF;

            if (control < 0x80) {
                // Literal run: copy (control + 1) bytes
                int count = control + 1;
                for (int i = 0; i < count && pos < compressed.length && outPos < expectedSize; i++) {
                    result[outPos++] = compressed[pos++];
                }
            } else if (control == 0x80) {
                // No-op: skip (PackBits standard)
                continue;
            } else {
                // Repeat run: repeat next byte (257 - control) times
                int count = 257 - control;
                if (pos < compressed.length) {
                    byte val = compressed[pos++];
                    for (int i = 0; i < count && outPos < expectedSize; i++) {
                        result[outPos++] = val;
                    }
                }
            }
        }

        // Return exact-sized result if we filled less than expected
        if (outPos < expectedSize) {
            byte[] trimmed = new byte[outPos];
            System.arraycopy(result, 0, trimmed, 0, outPos);
            return trimmed;
        }
        return result;
    }

    /**
     * Get alignment width in PIXELS for a given bit depth.
     * Matches dirplayer-rs get_alignment_width().
     */
    private static int getAlignmentWidth(int bitDepth) {
        return switch (bitDepth) {
            case 1 -> 16;       // 1-bit: rows aligned to 16-pixel boundary
            case 4, 32 -> 4;   // 4-bit and 32-bit: 4-pixel alignment
            case 2, 8 -> 2;    // 2-bit and 8-bit: 2-pixel alignment
            case 16 -> 1;      // 16-bit: no alignment needed
            default -> 2;
        };
    }

    /**
     * Get number of channels for a bit depth.
     */
    private static int getNumChannels(int bitDepth) {
        return switch (bitDepth) {
            case 32 -> 4;
            case 16 -> 2;
            default -> 1;
        };
    }

    /**
     * Calculate scan width in PIXELS using pixel alignment.
     * This replaces the old byte-based calculateScanWidth.
     */
    public static int calculateScanWidthPixels(int width, int bitDepth, int pitch) {
        if (pitch > 0 && bitDepth > 0) {
            // Pitch is the row byte stride. Convert to pixel width:
            return (pitch * 8) / bitDepth;
        }

        int alignmentWidth = getAlignmentWidth(bitDepth);
        if (width % alignmentWidth == 0) {
            return width;
        }
        return alignmentWidth * ((width + alignmentWidth - 1) / alignmentWidth);
    }

    /**
     * Calculate the scan width in BYTES for a bitmap row.
     * Kept for backward compatibility with alpha channel decoding.
     */
    public static int calculateScanWidth(int width, int bitDepth) {
        int bitsPerRow = width * bitDepth;
        // Align to 16-bit boundary (2 bytes)
        return (bitsPerRow + 15) / 16 * 2;
    }

    /**
     * Decode a 1-bit bitmap (monochrome).
     * Output: bit 0 → white (0xFF,0xFF,0xFF), bit 1 → black (0x00,0x00,0x00).
     * <p>
     * Per ScummVM/Director convention, 1-bit bitmaps always use System Mac palette
     * extrema regardless of the declared palette: index 0 = white, index 255 = black.
     * This ensures ink processing (matte, darken, background transparent) works
     * correctly — background (white) pixels get removed, foreground (black) pixels
     * are kept as the visible content.
     */
    public static Bitmap decode1Bit(byte[] data, int width, int height, int scanWidth, Palette palette) {
        Bitmap bitmap = new Bitmap(width, height, 1);
        byte[] indices = new byte[width * height];

        // Expand 1-bit data to 8-bit values
        int expandedLen = data.length * 8;
        byte[] expanded = new byte[expandedLen];
        int p = 0;
        for (byte datum : data) {
            int b = datum & 0xFF;
            for (int j = 1; j <= 8; j++) {
                int bit = (b & (0x1 << (8 - j))) >> (8 - j);
                expanded[p++] = (byte) (bit == 1 ? 0xFF : 0x00);
            }
        }

        // Copy from scan-width-padded data to pixel-width output.
        // Always use System Mac palette convention for 1-bit: index 0 = white, index 255 = black.
        // ScummVM skips palette dithering for 1-bit entirely ("case 1: break; // preconverted").
        Palette macPalette = Palette.SYSTEM_MAC_PALETTE;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int scanIndex = y * scanWidth + x;
                if (scanIndex >= expanded.length) break;
                int colorIndex = expanded[scanIndex] & 0xFF;
                int[] rgb = macPalette.getRGB(colorIndex);
                bitmap.setPixelRGB(x, y, rgb[0], rgb[1], rgb[2]);
                indices[y * width + x] = (byte) colorIndex;
            }
        }
        bitmap.setPaletteIndices(indices);
        return bitmap;
    }

    /**
     * Decode a 2-bit bitmap (4 colors).
     */
    public static Bitmap decode2Bit(byte[] data, int width, int height, int scanWidth, Palette palette) {
        Bitmap bitmap = new Bitmap(width, height, 2);
        byte[] indices = new byte[width * height];

        // Expand 2-bit data: each byte → 4 pixel values (0-255 range)
        byte[] expanded = new byte[data.length * 4];
        for (int i = 0; i < data.length; i++) {
            int val = data[i] & 0xFF;
            expanded[i * 4]     = (byte) Math.round(((val & 0xC0) >> 6) / 3.0f * 255.0f);
            expanded[i * 4 + 1] = (byte) Math.round(((val & 0x30) >> 4) / 3.0f * 255.0f);
            expanded[i * 4 + 2] = (byte) Math.round(((val & 0x0C) >> 2) / 3.0f * 255.0f);
            expanded[i * 4 + 3] = (byte) Math.round((val & 0x03) / 3.0f * 255.0f);
        }

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int scanIndex = y * scanWidth + x;
                if (scanIndex >= expanded.length) break;
                int colorIndex = expanded[scanIndex] & 0xFF;
                int[] rgb = palette != null ? palette.getRGB(colorIndex)
                    : new int[]{colorIndex, colorIndex, colorIndex};
                bitmap.setPixelRGB(x, y, rgb[0], rgb[1], rgb[2]);
                indices[y * width + x] = (byte) colorIndex;
            }
        }
        bitmap.setPaletteIndices(indices);
        return bitmap;
    }

    /**
     * Decode a 4-bit bitmap (16 colors).
     * Each byte contains two 4-bit palette indices.
     * Nibble values (0-15) are scaled to 0-255 for palette lookup,
     * matching Director's convention where 0=white and 15=black
     * in grayscale palettes (same approach as decode2Bit).
     */
    public static Bitmap decode4Bit(byte[] data, int width, int height, int scanWidth, Palette palette) {
        Bitmap bitmap = new Bitmap(width, height, 4);
        byte[] indices = new byte[width * height];

        // Expand 4-bit data: each byte → 2 pixel values, scaled to 0-255 palette indices.
        // Director 4-bit bitmaps use 256-color palettes; nibble 0 → index 0, nibble 15 → index 255.
        byte[] expanded = new byte[data.length * 2];
        for (int i = 0; i < data.length; i++) {
            int val = data[i] & 0xFF;
            expanded[i * 2] = (byte) Math.round(((val & 0xF0) >> 4) / 15.0f * 255.0f);
            expanded[i * 2 + 1] = (byte) Math.round((val & 0x0F) / 15.0f * 255.0f);
        }

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int scanIndex = y * scanWidth + x;
                if (scanIndex >= expanded.length) break;
                int colorIndex = expanded[scanIndex] & 0xFF;
                int[] rgb = palette != null ? palette.getRGB(colorIndex)
                    : new int[]{colorIndex, colorIndex, colorIndex};
                bitmap.setPixelRGB(x, y, rgb[0], rgb[1], rgb[2]);
                indices[y * width + x] = (byte) colorIndex;
            }
        }
        bitmap.setPaletteIndices(indices);
        return bitmap;
    }

    /**
     * Decode an 8-bit bitmap (256 colors, palette-indexed).
     */
    public static Bitmap decode8Bit(byte[] data, int width, int height, int scanWidth, Palette palette) {
        Bitmap bitmap = new Bitmap(width, height, 8);
        byte[] indices = new byte[width * height];

        if (palette == null) {
            palette = Palette.SYSTEM_MAC_PALETTE;
        }

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int scanIndex = y * scanWidth + x;
                if (scanIndex >= data.length) break;
                int colorIndex = data[scanIndex] & 0xFF;
                int[] rgb = palette.getRGB(colorIndex);
                bitmap.setPixelRGB(x, y, rgb[0], rgb[1], rgb[2]);
                indices[y * width + x] = (byte) colorIndex;
            }
        }
        bitmap.setPaletteIndices(indices);
        return bitmap;
    }

    /**
     * Decode a 16-bit bitmap (high color, RGB555).
     * When compressed, data is planar per scanline: [high bytes][low bytes] per row.
     * When uncompressed, data is sequential: high,low per pixel.
     */
    public static Bitmap decode16Bit(byte[] data, int width, int height, int scanWidth,
                                      boolean skipCompression) {
        Bitmap bitmap = new Bitmap(width, height, 16);

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int pixel16;
                if (skipCompression) {
                    // Uncompressed: sequential bytes, 2 per pixel
                    int offset = (y * scanWidth + x) * 2;
                    if (offset + 1 >= data.length) continue;
                    int high = data[offset] & 0xFF;
                    int low = data[offset + 1] & 0xFF;
                    pixel16 = (high << 8) | low;
                } else {
                    // Compressed (RLE-decoded): planar per scanline
                    // Each row: all high bytes, then all low bytes
                    int rowOffset = y * scanWidth * 2;
                    int highIdx = rowOffset + x;
                    int lowIdx = rowOffset + scanWidth + x;
                    if (highIdx >= data.length || lowIdx >= data.length) continue;
                    int high = data[highIdx] & 0xFF;
                    int low = data[lowIdx] & 0xFF;
                    pixel16 = (high << 8) | low;
                }

                // RGB555 - extract 5-bit components
                int r5 = (pixel16 >> 10) & 0x1F;
                int g5 = (pixel16 >> 5) & 0x1F;
                int b5 = pixel16 & 0x1F;

                // Convert 5-bit to 8-bit
                int r = (r5 << 3) | (r5 >> 2);
                int g = (g5 << 3) | (g5 >> 2);
                int b = (b5 << 3) | (b5 >> 2);

                bitmap.setPixelRGB(x, y, r, g, b);
            }
        }
        return bitmap;
    }

    /**
     * Decode a 32-bit bitmap (true color).
     * D4+ format: channels stored separately PER SCANLINE as [A][R][G][B].
     * D3 and earlier: interleaved ARGB per pixel.
     */
    public static Bitmap decode32Bit(byte[] data, int width, int height, int scanWidth,
                                      boolean channelsSeparated) {
        Bitmap bitmap = new Bitmap(width, height, 32);

        if (channelsSeparated) {
            // D4+ format: each scanline has channels laid out as [A...][R...][G...][B...]
            for (int y = 0; y < height; y++) {
                int lineOffset = y * scanWidth * 4;

                for (int x = 0; x < width; x++) {
                    int aIdx = lineOffset + x;
                    int rIdx = lineOffset + x + scanWidth;
                    int gIdx = lineOffset + x + scanWidth * 2;
                    int bIdx = lineOffset + x + scanWidth * 3;

                    if (bIdx >= data.length) continue;

                    int a = data[aIdx] & 0xFF;
                    int r = data[rIdx] & 0xFF;
                    int g = data[gIdx] & 0xFF;
                    int b = data[bIdx] & 0xFF;

                    bitmap.setPixelRGBA(x, y, r, g, b, a);
                }
            }
        } else {
            // Standard ARGB interleaved format (D3)
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int byteIndex = (y * scanWidth + x) * 4;
                    if (byteIndex + 3 >= data.length) continue;

                    int a = data[byteIndex] & 0xFF;
                    int r = data[byteIndex + 1] & 0xFF;
                    int g = data[byteIndex + 2] & 0xFF;
                    int b = data[byteIndex + 3] & 0xFF;

                    bitmap.setPixelRGBA(x, y, r, g, b, a);
                }
            }
        }
        return bitmap;
    }

    /**
     * Decode bitmap data with automatic bit depth detection and compression handling.
     *
     * @param data Raw bitmap data (BITD chunk)
     * @param width Bitmap width in pixels
     * @param height Bitmap height in pixels
     * @param bitDepth Bits per pixel (1, 2, 4, 8, 16, or 32)
     * @param palette Color palette for indexed formats
     * @param bigEndian Whether multi-byte values are big-endian
     * @param directorVersion Director version (our internal format: 1000=D4, 1200=D6, etc.)
     * @param pitch Row byte stride from BitmapInfo (0 if not available)
     * @return Decoded Bitmap
     */
    public static Bitmap decode(byte[] data, int width, int height, int bitDepth,
                                 Palette palette, boolean bigEndian,
                                 int directorVersion, int pitch) {
        if (width <= 0 || height <= 0 || data == null || data.length == 0) {
            return new Bitmap(Math.max(1, width), Math.max(1, height), bitDepth);
        }

        int numChannels = getNumChannels(bitDepth);
        int alignmentWidth = getAlignmentWidth(bitDepth);

        // Calculate initial scan width (in pixels)
        int scanWidth;
        if (pitch > 0 && bitDepth > 0) {
            scanWidth = (pitch * 8) / bitDepth;
        } else if (width % alignmentWidth == 0) {
            scanWidth = width;
        } else {
            scanWidth = alignmentWidth * ((width + alignmentWidth - 1) / alignmentWidth);
        }

        // Calculate expected decompressed size in bytes
        int expectedLen;
        if (bitDepth == 32 && directorVersion >= 1000) {
            expectedLen = scanWidth * height * numChannels;
        } else if (bitDepth == 1) {
            expectedLen = (scanWidth / 8) * height;
        } else if (bitDepth == 2) {
            expectedLen = (scanWidth / 4) * height;
        } else if (bitDepth == 4) {
            expectedLen = (scanWidth / 2) * height;
        } else {
            expectedLen = scanWidth * height * numChannels;
        }

        // Detect whether data is compressed or not
        boolean skipCompression = data.length >= expectedLen;

        // Check for zlib header even when sizes match — compressed data can coincidentally
        // be the same size as expected uncompressed data (common with narrow bitmaps)
        boolean isZlib = data.length >= 2 && (data[0] & 0xFF) == 0x78 &&
                ((data[1] & 0xFF) == 0x01 || (data[1] & 0xFF) == 0x5E ||
                 (data[1] & 0xFF) == 0x9C || (data[1] & 0xFF) == 0xDA);
        if (isZlib) {
            skipCompression = false;
        }

        byte[] decompressed;

        if (skipCompression) {
            // Data is uncompressed — use raw bytes (up to expectedLen)
            if (data.length > expectedLen) {
                decompressed = new byte[expectedLen];
                System.arraycopy(data, 0, decompressed, 0, expectedLen);
            } else {
                decompressed = data;
            }
        } else if (isZlib) {
            // Zlib-compressed data — decompress with java.util.zip
            try {
                java.util.zip.Inflater inflater = new java.util.zip.Inflater();
                inflater.setInput(data);
                byte[] buf = new byte[expectedLen];
                int inflated = inflater.inflate(buf);
                inflater.end();
                decompressed = new byte[inflated];
                System.arraycopy(buf, 0, decompressed, 0, inflated);
            } catch (java.util.zip.DataFormatException e) {
                // Fallback to RLE if zlib decompression fails
                decompressed = decompressRLE(data, expectedLen);
            }
        } else {
            decompressed = decompressRLE(data, expectedLen);
        }

        // Post-decompression scan_width recalculation
        if (pitch > 0) {
            // Keep pitch-based scan_width
        } else if (decompressed.length == width * height * numChannels) {
            scanWidth = width;
        } else if (bitDepth == 32 && directorVersion >= 1000) {
            scanWidth = width;
        } else if (width % alignmentWidth == 0) {
            scanWidth = width;
        } else {
            scanWidth = alignmentWidth * ((width + alignmentWidth - 1) / alignmentWidth);
        }

        return switch (bitDepth) {
            case 1 -> decode1Bit(decompressed, width, height, scanWidth, palette);
            case 2 -> decode2Bit(decompressed, width, height, scanWidth, palette);
            case 4 -> decode4Bit(decompressed, width, height, scanWidth, palette);
            case 8 -> decode8Bit(decompressed, width, height, scanWidth, palette);
            case 16 -> decode16Bit(decompressed, width, height, scanWidth, skipCompression);
            case 32 -> decode32Bit(decompressed, width, height, scanWidth,
                directorVersion >= 1000); // D4+ uses separated channels
            default -> decode8Bit(decompressed, width, height, scanWidth, palette);
        };
    }

    /**
     * Backward-compatible decode with defaults.
     */
    public static Bitmap decode(byte[] data, int width, int height, int bitDepth,
                                 Palette palette, boolean compressed, boolean bigEndian,
                                 int directorVersion) {
        return decode(data, width, height, bitDepth, palette, bigEndian, directorVersion, 0);
    }

    /**
     * Simple decode with defaults.
     */
    public static Bitmap decode(byte[] data, int width, int height, int bitDepth, Palette palette) {
        return decode(data, width, height, bitDepth, palette, true, 1200, 0);
    }
}
