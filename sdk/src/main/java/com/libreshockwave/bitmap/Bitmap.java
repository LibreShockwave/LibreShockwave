package com.libreshockwave.bitmap;

import java.awt.image.BufferedImage;

/**
 * Represents a decoded bitmap with RGBA pixel data.
 * This is the final decoded form ready for rendering.
 */
public class Bitmap {

    private final int width;
    private final int height;
    private final int[] pixels; // ARGB format (0xAARRGGBB)
    private final int bitDepth;
    private byte[] paletteIndices;
    private boolean scriptModified; // Set when Lingo modifies this bitmap via image API
    private boolean nativeAlpha; // True for Director-decoded 32-bit bitmaps with real alpha
    private Palette imagePalette; // Palette for 8-bit images created via image(w,h,8,paletteMember)
    private int paletteRefCastLib = -1;
    private int paletteRefMemberNum = -1;
    private String paletteRefSystemName;
    private boolean hasAnchorPoint;
    private int anchorX;
    private int anchorY;

    public Bitmap(int width, int height, int bitDepth) {
        this.width = width;
        this.height = height;
        this.bitDepth = bitDepth;
        this.pixels = new int[width * height];
    }

    public Bitmap(int width, int height, int bitDepth, int[] pixels) {
        this.width = width;
        this.height = height;
        this.bitDepth = bitDepth;
        this.pixels = pixels;
    }

    public int getWidth() {
        return width;
    }

    public int getHeight() {
        return height;
    }

    public int getBitDepth() {
        return bitDepth;
    }

    /**
     * Returns true if this bitmap was modified by Lingo image API (fill,
     * copyPixels, etc.).
     */
    public boolean isScriptModified() {
        return scriptModified;
    }

    /**
     * Returns true if this bitmap contains any fully transparent pixels (alpha=0).
     */
    public boolean hasTransparentPixels() {
        if (pixels == null || bitDepth < 32)
            return false;
        for (int pixel : pixels) {
            if ((pixel >>> 24) == 0)
                return true;
        }
        return false;
    }

    /** Returns true when this bitmap carries authored/native 32-bit alpha. */
    public boolean hasNativeMatteAlpha() {
        return bitDepth == 32 && nativeAlpha;
    }

    /** Mark this bitmap as modified by Lingo script operations. */
    public void markScriptModified() {
        this.scriptModified = true;
    }

    public boolean isNativeAlpha() {
        return nativeAlpha;
    }

    public void setNativeAlpha(boolean nativeAlpha) {
        this.nativeAlpha = nativeAlpha;
    }

    /**
     * Set the palette for this bitmap (for 8-bit images created with a palette
     * member).
     */
    public void setImagePalette(Palette palette) {
        this.imagePalette = palette;
    }

    /** Get the palette for this bitmap, or null if none. */
    public Palette getImagePalette() {
        return imagePalette;
    }

    public void setPaletteIndices(byte[] paletteIndices) {
        if (paletteIndices == null) {
            this.paletteIndices = null;
            return;
        }
        this.paletteIndices = java.util.Arrays.copyOf(paletteIndices, paletteIndices.length);
    }

    public byte[] getPaletteIndices() {
        return paletteIndices != null ? java.util.Arrays.copyOf(paletteIndices, paletteIndices.length) : null;
    }

    private void clearPaletteIndices() {
        this.paletteIndices = null;
    }

    /**
     * Director can recolor an existing image by assigning image.paletteRef
     * after pixels have already been copied into it. Our runtime stores decoded
     * ARGB pixels, so emulate that by remapping exact old-palette colors to the
     * corresponding entries in the new palette.
     *
     * @return number of pixels changed
     */
    public int remapImagePalette(Palette newPalette) {
        Palette oldPalette = this.imagePalette;
        this.imagePalette = newPalette;

        if (oldPalette == null || newPalette == null || oldPalette == newPalette) {
            return 0;
        }

        if (paletteIndices != null && paletteIndices.length == pixels.length) {
            int changed = 0;
            int max = newPalette.size();
            for (int i = 0; i < pixels.length; i++) {
                int alpha = (pixels[i] >>> 24) & 0xFF;
                if (alpha == 0) {
                    continue;
                }
                int index = paletteIndices[i] & 0xFF;
                if (index >= max) {
                    continue;
                }
                int newRgb = newPalette.getColor(index) & 0xFFFFFF;
                if ((pixels[i] & 0xFFFFFF) != newRgb) {
                    pixels[i] = (alpha << 24) | newRgb;
                    changed++;
                }
            }
            return changed;
        }

        int changed = 0;
        int max = Math.min(oldPalette.size(), newPalette.size());
        for (int i = 0; i < pixels.length; i++) {
            int pixel = pixels[i];
            int alpha = (pixel >>> 24) & 0xFF;
            if (alpha == 0) {
                continue;
            }
            int rgb = pixel & 0xFFFFFF;
            for (int idx = 0; idx < max; idx++) {
                if ((oldPalette.getColor(idx) & 0xFFFFFF) == rgb) {
                    int newRgb = newPalette.getColor(idx) & 0xFFFFFF;
                    if (newRgb != rgb) {
                        pixels[i] = (alpha << 24) | newRgb;
                        changed++;
                    }
                    break;
                }
            }
        }
        return changed;
    }

    public void setPaletteRefCastMember(int castLibNumber, int memberNumber) {
        this.paletteRefCastLib = castLibNumber;
        this.paletteRefMemberNum = memberNumber;
        this.paletteRefSystemName = null;
    }

    public int getPaletteRefCastLib() {
        return paletteRefCastLib;
    }

    public int getPaletteRefMemberNum() {
        return paletteRefMemberNum;
    }

    public void setPaletteRefSystemName(String systemName) {
        this.paletteRefSystemName = systemName;
        this.paletteRefCastLib = -1;
        this.paletteRefMemberNum = -1;
    }

    public String getPaletteRefSystemName() {
        return paletteRefSystemName;
    }

    public void clearPaletteRefMetadata() {
        this.paletteRefCastLib = -1;
        this.paletteRefMemberNum = -1;
        this.paletteRefSystemName = null;
    }

    public void setAnchorPoint(int x, int y) {
        this.hasAnchorPoint = true;
        this.anchorX = x;
        this.anchorY = y;
    }

    public boolean hasAnchorPoint() {
        return hasAnchorPoint;
    }

    public int getAnchorX() {
        return anchorX;
    }

    public int getAnchorY() {
        return anchorY;
    }

    public void clearAnchorPoint() {
        this.hasAnchorPoint = false;
        this.anchorX = 0;
        this.anchorY = 0;
    }

    public void copyPaletteMetadataFrom(Bitmap other) {
        if (other == null) {
            this.imagePalette = null;
            this.paletteIndices = null;
            clearPaletteRefMetadata();
            return;
        }
        this.imagePalette = other.imagePalette;
        this.nativeAlpha = other.nativeAlpha;
        this.paletteIndices = other.paletteIndices != null
                ? java.util.Arrays.copyOf(other.paletteIndices, other.paletteIndices.length)
                : null;
        this.paletteRefCastLib = other.paletteRefCastLib;
        this.paletteRefMemberNum = other.paletteRefMemberNum;
        this.paletteRefSystemName = other.paletteRefSystemName;
        this.hasAnchorPoint = other.hasAnchorPoint;
        this.anchorX = other.anchorX;
        this.anchorY = other.anchorY;
    }

    /**
     * Resolve a palette index color through this bitmap's palette.
     * Falls back to the given default palette if no image palette is set.
     */
    public int resolvePaletteIndex(int index, Palette fallback) {
        Palette pal = imagePalette != null ? imagePalette : fallback;
        if (pal != null) {
            return pal.getColor(index & 0xFF);
        }
        // Ultimate fallback: grayscale ramp
        int gray = 255 - (index & 0xFF);
        return (gray << 16) | (gray << 8) | gray;
    }

    public int[] getPixels() {
        return pixels;
    }

    /**
     * Get pixel at (x, y) in ARGB format.
     */
    public int getPixel(int x, int y) {
        if (x < 0 || x >= width || y < 0 || y >= height) {
            return 0;
        }
        return pixels[y * width + x];
    }

    /**
     * Set pixel at (x, y) in ARGB format.
     */
    public void setPixel(int x, int y, int argb) {
        if (x >= 0 && x < width && y >= 0 && y < height) {
            clearPaletteIndices();
            pixels[y * width + x] = argb;
        }
    }

    /**
     * Set pixel from RGB components (alpha = 255).
     */
    public void setPixelRGB(int x, int y, int r, int g, int b) {
        setPixel(x, y, 0xFF000000 | (r << 16) | (g << 8) | b);
    }

    /**
     * Set pixel from RGBA components.
     */
    public void setPixelRGBA(int x, int y, int r, int g, int b, int a) {
        setPixel(x, y, (a << 24) | (r << 16) | (g << 8) | b);
    }

    /**
     * Fill the entire bitmap with a single color.
     */
    public void fill(int argb) {
        clearPaletteIndices();
        java.util.Arrays.fill(pixels, argb);
    }

    /**
     * Fill a rectangular region.
     */
    public void fillRect(int x, int y, int w, int h, int argb) {
        clearPaletteIndices();
        int x2 = Math.min(x + w, width);
        int y2 = Math.min(y + h, height);
        x = Math.max(0, x);
        y = Math.max(0, y);

        for (int py = y; py < y2; py++) {
            for (int px = x; px < x2; px++) {
                pixels[py * width + px] = argb;
            }
        }
    }

    /**
     * Convert to a Java BufferedImage for export/display.
     */
    public BufferedImage toBufferedImage() {
        BufferedImage image = new BufferedImage(width, height, BufferedImage.TYPE_INT_ARGB);
        image.setRGB(0, 0, width, height, pixels, 0, width);
        return image;
    }

    /**
     * Create a copy of this bitmap.
     */
    public Bitmap copy() {
        int[] pixelsCopy = new int[pixels.length];
        System.arraycopy(pixels, 0, pixelsCopy, 0, pixels.length);
        Bitmap copy = new Bitmap(width, height, bitDepth, pixelsCopy);
        copy.copyPaletteMetadataFrom(this);
        return copy;
    }

    /**
     * Extract a sub-region as a new bitmap.
     */
    public Bitmap getRegion(int x, int y, int w, int h) {
        Bitmap result = new Bitmap(w, h, bitDepth);
        result.copyPaletteMetadataFrom(this);
        byte[] regionIndices = paletteIndices != null ? new byte[w * h] : null;
        for (int dy = 0; dy < h; dy++) {
            int srcY = y + dy;
            if (srcY < 0 || srcY >= height)
                continue;
            for (int dx = 0; dx < w; dx++) {
                int srcX = x + dx;
                if (srcX < 0 || srcX >= width)
                    continue;
                result.pixels[dy * w + dx] = pixels[srcY * width + srcX];
                if (regionIndices != null) {
                    regionIndices[dy * w + dx] = paletteIndices[srcY * width + srcX];
                }
            }
        }
        if (regionIndices != null) {
            result.paletteIndices = regionIndices;
        }
        if (hasAnchorPoint) {
            result.setAnchorPoint(anchorX - x, anchorY - y);
        }
        return result;
    }

    /**
     * Returns the bounding rect of non-white pixels in this bitmap.
     * Director's image.trimWhiteSpace() equivalent.
     * Returns [0,0,0,0] if the image is entirely white.
     */
    public int[] trimWhiteSpace() {
        int minX = width, minY = height, maxX = -1, maxY = -1;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int pixel = pixels[y * width + x];
                int a = (pixel >>> 24);
                if (a == 0)
                    continue; // transparent = white
                int rgb = pixel & 0xFFFFFF;
                if (rgb == 0xFFFFFF)
                    continue; // white
                if (x < minX)
                    minX = x;
                if (x > maxX)
                    maxX = x;
                if (y < minY)
                    minY = y;
                if (y > maxY)
                    maxY = y;
            }
        }
        if (maxX < 0)
            return new int[] { 0, 0, 0, 0 };
        return new int[] { minX, minY, maxX + 1, maxY + 1 };
    }

    @Override
    public String toString() {
        return "Bitmap[" + width + "x" + height + ", " + bitDepth + "-bit]";
    }

    /**
     * Create a swatch image from an array of colors.
     * Each color is rendered as a small square in a grid.
     *
     * @param colors     Array of RGB colors (0xRRGGBB format)
     * @param swatchSize Size of each color swatch in pixels
     * @param columns    Number of columns in the grid (0 = auto)
     * @return A Bitmap containing the color swatches
     */
    public static Bitmap createPaletteSwatch(int[] colors, int swatchSize, int columns) {
        if (colors == null || colors.length == 0) {
            return new Bitmap(1, 1, 32);
        }

        int count = colors.length;
        int cols = columns > 0 ? columns : (int) Math.ceil(Math.sqrt(count));
        int rows = (int) Math.ceil((double) count / cols);

        int width = cols * swatchSize;
        int height = rows * swatchSize;

        Bitmap bitmap = new Bitmap(width, height, 32);
        bitmap.fill(0xFFFFFFFF); // White background

        for (int i = 0; i < count; i++) {
            int col = i % cols;
            int row = i / cols;
            int x = col * swatchSize;
            int y = row * swatchSize;

            // Convert RGB to ARGB
            int argb = 0xFF000000 | (colors[i] & 0xFFFFFF);
            bitmap.fillRect(x, y, swatchSize, swatchSize, argb);
        }

        return bitmap;
    }

    /**
     * Create a swatch image from a Palette.
     *
     * @param palette    The palette to visualize
     * @param swatchSize Size of each color swatch in pixels
     * @return A Bitmap containing the color swatches
     */
    public static Bitmap createPaletteSwatch(Palette palette, int swatchSize) {
        int[] colors = new int[palette.size()];
        for (int i = 0; i < palette.size(); i++) {
            colors[i] = palette.getColor(i);
        }
        return createPaletteSwatch(colors, swatchSize, 16); // 16 columns for 256-color palettes
    }
}
