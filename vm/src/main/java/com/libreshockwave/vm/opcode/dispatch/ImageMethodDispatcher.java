package com.libreshockwave.vm.opcode.dispatch;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.bitmap.Drawing;
import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.datum.Datum;

import java.util.List;

/**
 * Handles method calls on ImageRef objects.
 * Implements Director's image API: fill, draw, copyPixels, duplicate, etc.
 */
public final class ImageMethodDispatcher {

    private ImageMethodDispatcher() {}

    public static Datum dispatch(Datum.ImageRef imageRef, String methodName, List<Datum> args) {
        String method = methodName.toLowerCase();
        Bitmap bmp = imageRef.bitmap();

        return switch (method) {
            case "fill" -> { bmp.markScriptModified(); yield fill(bmp, args); }
            case "draw" -> { bmp.markScriptModified(); yield draw(bmp, args); }
            case "copypixels" -> {
                bmp.markScriptModified();
                yield copyPixels(bmp, args);
            }
            case "duplicate" -> new Datum.ImageRef(bmp.copy());
            case "crop" -> crop(bmp, args);
            case "setpixel" -> {
                // image.setPixel(x, y, color)
                if (args.size() >= 3) {
                    int px = args.get(0).toInt();
                    int py = args.get(1).toInt();
                    int color = Datum.datumToArgb(args.get(2));
                    if (px >= 0 && px < bmp.getWidth() && py >= 0 && py < bmp.getHeight()) {
                        bmp.setPixel(px, py, color);
                        bmp.markScriptModified();
                    }
                }
                yield Datum.VOID;
            }
            case "getpixel" -> {
                // image.getPixel(x, y) → returns color
                if (args.size() >= 2) {
                    int px = args.get(0).toInt();
                    int py = args.get(1).toInt();
                    if (px >= 0 && px < bmp.getWidth() && py >= 0 && py < bmp.getHeight()) {
                        int pixel = bmp.getPixel(px, py);
                        int r = (pixel >> 16) & 0xFF;
                        int g = (pixel >> 8) & 0xFF;
                        int b = pixel & 0xFF;
                        yield new Datum.Color(r, g, b);
                    }
                }
                yield Datum.VOID;
            }
            case "trimwhitespace" -> {
                // Director's trimWhiteSpace() returns a CROPPED image (not a rect).
                // All Habbo usages treat the return value as an image.
                int[] bounds = bmp.trimWhiteSpace();
                if (bounds[2] <= bounds[0] || bounds[3] <= bounds[1]) {
                    // Entirely white - return 1x1 white image
                    Bitmap empty = new Bitmap(1, 1, bmp.getBitDepth());
                    empty.fill(0xFFFFFFFF);
                    yield new Datum.ImageRef(empty);
                }
                yield new Datum.ImageRef(bmp.getRegion(bounds[0], bounds[1],
                        bounds[2] - bounds[0], bounds[3] - bounds[1]));
            }
            case "creatematte" -> {
                int alphaThreshold = 0;
                if (!args.isEmpty() && !args.get(0).isVoid()) {
                    alphaThreshold = args.get(0).toInt();
                }
                yield new Datum.ImageRef(Drawing.createMatte(bmp, alphaThreshold));
            }
            case "getat" -> {
                // getAt(index) on image - some scripts use this
                // NOTE: Uses if-else instead of nested switch to avoid TeaVM WASM issue
                // with nested switch expressions using yield.
                if (args.isEmpty()) yield Datum.VOID;
                int index = args.get(0).toInt();
                if (index == 1) {
                    yield Datum.of(bmp.getWidth());
                } else if (index == 2) {
                    yield Datum.of(bmp.getHeight());
                } else {
                    yield Datum.VOID;
                }
            }
            default -> Datum.VOID;
        };
    }

    /**
     * Get a property from an ImageRef.
     */
    public static void setProperty(Datum.ImageRef imageRef, String propName, Datum value) {
        Bitmap bmp = imageRef.bitmap();
        switch (propName.toLowerCase()) {
            case "paletteref" -> {
                if (value instanceof Datum.CastMemberRef ref) {
                    CastLibProvider provider = CastLibProvider.getProvider();
                    if (provider != null) {
                        Palette pal = provider.getMemberPalette(ref.castLibNum(), ref.memberNum());
                        if (pal != null) {
                            bmp.remapImagePalette(pal);
                            bmp.setPaletteRefCastMember(ref.castLibNum(), ref.memberNum());
                        }
                    }
                } else if (value instanceof Datum.Symbol sym) {
                    String name = sym.name().toLowerCase();
                    if ("systemmac".equals(name)) {
                        bmp.remapImagePalette(Palette.SYSTEM_MAC_PALETTE);
                        bmp.setPaletteRefSystemName("systemMac");
                    } else if ("systemwin".equals(name) || "systemwindows".equals(name)) {
                        bmp.remapImagePalette(Palette.SYSTEM_WIN_PALETTE);
                        bmp.setPaletteRefSystemName("systemWin");
                    }
                }
            }
            default -> System.err.println("[LingoVM] Unhandled ImageRef set: " + propName);
        }
    }

    public static Datum getProperty(Datum.ImageRef imageRef, String propName) {
        Bitmap bmp = imageRef.bitmap();
        return switch (propName.toLowerCase()) {
            case "rect" -> new Datum.Rect(0, 0, bmp.getWidth(), bmp.getHeight());
            case "width" -> Datum.of(bmp.getWidth());
            case "height" -> Datum.of(bmp.getHeight());
            case "depth" -> Datum.of(bmp.getBitDepth());
            case "ilk" -> Datum.symbol("image");
            case "image" -> imageRef; // Self-reference for .image on an image
            case "paletteref" -> {
                if (bmp.getPaletteRefCastLib() >= 1 && bmp.getPaletteRefMemberNum() >= 1) {
                    yield Datum.CastMemberRef.of(bmp.getPaletteRefCastLib(), bmp.getPaletteRefMemberNum());
                }
                if (bmp.getPaletteRefSystemName() != null) {
                    yield Datum.symbol(bmp.getPaletteRefSystemName());
                }
                yield Datum.VOID;
            }
            default -> Datum.VOID;
        };
    }

    /**
     * image.fill(rect, color) - Fill a rectangular region with a color.
     * In Director: image.fill(destRect, color)
     * Also supports: image.fill(left, top, right, bottom, color)
     */
    private static Datum fill(Bitmap bmp, List<Datum> args) {
        if (args.size() < 2) return Datum.VOID;

        Datum firstArg = args.get(0);

        int left, top, right, bottom;
        Datum colorDatum;

        if (firstArg instanceof Datum.Rect rect) {
            // fill(rect, color)
            left = rect.left();
            top = rect.top();
            right = rect.right();
            bottom = rect.bottom();
            colorDatum = args.get(1);
        } else if (args.size() >= 5) {
            // fill(left, top, right, bottom, color)
            left = args.get(0).toInt();
            top = args.get(1).toInt();
            right = args.get(2).toInt();
            bottom = args.get(3).toInt();
            colorDatum = args.get(4);
        } else {
            return Datum.VOID;
        }

        // Director's image() creates white-filled images by default.
        // When fill() receives VOID as color, default to white — this matches
        // Director's convention where VOID means "use default background."
        // Habbo's clearImage() passes pProps[#bgColor] which may be VOID
        // when the layout definition omits bgColor.
        // Use bitmap-aware color resolution so paletteIndex() colors resolve
        // through the target bitmap's custom palette (e.g., nav_ui_palette).
        int colorArgb = colorDatum.isVoid() ? 0xFFFFFFFF : Datum.datumToArgb(colorDatum, bmp);

        int w = right - left;
        int h = bottom - top;
        if (w > 0 && h > 0) {
            bmp.fillRect(left, top, w, h, colorArgb);
        }

        return Datum.VOID;
    }

    /**
     * image.draw(rect, propList) - Draw a shape outline.
     * In Director: image.draw(destRect, [#color: color, #shapeType: #rect])
     * Also supports: image.draw(left, top, right, bottom, propList)
     */
    private static Datum draw(Bitmap bmp, List<Datum> args) {
        if (args.size() < 2) return Datum.VOID;

        Datum firstArg = args.get(0);

        int left, top, right, bottom;
        Datum propsArg;

        if (firstArg instanceof Datum.Rect rect) {
            // draw(rect, propList)
            left = rect.left();
            top = rect.top();
            right = rect.right();
            bottom = rect.bottom();
            propsArg = args.get(1);
        } else if (args.size() >= 5) {
            // draw(left, top, right, bottom, propList)
            left = args.get(0).toInt();
            top = args.get(1).toInt();
            right = args.get(2).toInt();
            bottom = args.get(3).toInt();
            propsArg = args.get(4);
        } else {
            return Datum.VOID;
        }

        // Extract color from propList
        int colorArgb = 0xFF000000; // default black
        String shapeType = "rect";

        if (propsArg instanceof Datum.PropList pl) {
            Datum colorDatum = getPropIgnoreCase(pl, "color", "Color");
            if (!colorDatum.isVoid()) {
                colorArgb = Datum.datumToArgb(colorDatum);
            }
            Datum shapeDatum = getPropIgnoreCase(pl, "shapeType", "shapetype");
            if (shapeDatum instanceof Datum.Symbol s) {
                shapeType = s.name().toLowerCase();
            }
        } else {
            // Second arg is a color directly
            colorArgb = Datum.datumToArgb(propsArg);
        }

        int w = right - left;
        int h = bottom - top;
        if (w <= 0 || h <= 0) return Datum.VOID;

        switch (shapeType) {
            case "rect" -> Drawing.drawRect(bmp, left, top, w, h, colorArgb);
            case "oval", "ellipse" -> {
                int cx = left + w / 2;
                int cy = top + h / 2;
                Drawing.drawEllipse(bmp, cx, cy, w / 2, h / 2, colorArgb);
            }
            case "line" -> Drawing.drawLine(bmp, left, top, right, bottom, colorArgb);
            default -> Drawing.drawRect(bmp, left, top, w, h, colorArgb);
        }

        return Datum.VOID;
    }

    /**
     * image.copyPixels(sourceImage, destRect, srcRect [, propList])
     * Copies pixels from source to this image with optional ink and blend.
     */
    private static Datum copyPixels(Bitmap dest, List<Datum> args) {
        if (args.size() < 3) {
            return Datum.VOID;
        }

        Datum srcDatum = args.get(0);
        if (!(srcDatum instanceof Datum.ImageRef srcRef)) {
            return Datum.VOID;
        }
        Bitmap src = srcRef.bitmap();
        if (src == null) {
            return Datum.VOID;
        }

        Datum destRectDatum = args.get(1);
        Datum srcRectDatum = args.get(2);

        // Handle quad destRect: list of 4 points for perspective/flip transforms
        if (destRectDatum instanceof Datum.List quadList && quadList.items().size() == 4
                && srcRectDatum instanceof Datum.Rect srcRect) {
            return copyPixelsQuad(dest, src, quadList, srcRect, args);
        }

        if (!(destRectDatum instanceof Datum.Rect destRect)) {
            return Datum.VOID;
        }
        if (!(srcRectDatum instanceof Datum.Rect srcRect)) {
            return Datum.VOID;
        }

        // Optional propList with ink, blend, color, bgColor, maskImage
        Palette.InkMode ink = Palette.InkMode.COPY;
        int blend = 255;
        int colorRemap = -1;   // #color param: remap BLACK (foreground) pixels to this color
        int bgColorRemap = -1; // #bgColor param: remap WHITE (background) pixels to this color
        Bitmap mask = null;    // #maskImage param: matte mask for transparency

        if (args.size() >= 4 && args.get(3) instanceof Datum.PropList pl) {
            // Check for #ink property
            Datum inkDatum = getPropIgnoreCase(pl, "ink", "Ink");
            if (inkDatum instanceof Datum.Int inkInt) {
                ink = inkFromInt(inkInt.value());
            }
            // Check for #blend property
            Datum blendDatum = getPropIgnoreCase(pl, "blend", "Blend");
            if (!blendDatum.isVoid()) {
                blend = (int) (blendDatum.toDouble() * 255.0 / 100.0);
            }
            // Check for #color property (foreground color remap)
            // Resolve PaletteIndexColor through source bitmap's palette first, then
            // destination's. The source typically has the content-specific palette
            // (e.g., wall pattern palette) while the destination is a generic canvas.
            Datum colorDatum = getPropIgnoreCase(pl, "color", "Color");
            if (!colorDatum.isVoid()) {
                Bitmap resolveTarget = (colorDatum instanceof Datum.PaletteIndexColor && src.getImagePalette() != null) ? src : dest;
                colorRemap = Datum.datumToArgb(colorDatum, resolveTarget) & 0xFFFFFF;
            }
            // Check for #bgColor property (background color remap)
            Datum bgColorDatum = getPropIgnoreCase(pl, "bgColor", "bgcolor", "BgColor");
            if (!bgColorDatum.isVoid()) {
                Bitmap resolveTarget = (bgColorDatum instanceof Datum.PaletteIndexColor && src.getImagePalette() != null) ? src : dest;
                bgColorRemap = Datum.datumToArgb(bgColorDatum, resolveTarget) & 0xFFFFFF;
            }
            // Check for #maskImage property (matte mask for transparency)
            Datum maskDatum = getPropIgnoreCase(pl, "maskImage", "maskimage", "MaskImage");
            if (maskDatum instanceof Datum.ImageRef maskRef) {
                mask = maskRef.bitmap();
            }
        }

        // Director ignores #maskImage when the source image already has native alpha in use.
        if (src.getBitDepth() == 32 && src.isNativeAlpha()) {
            mask = null;
        }

        int srcW = srcRect.right() - srcRect.left();
        int srcH = srcRect.bottom() - srcRect.top();
        int destW = destRect.right() - destRect.left();
        int destH = destRect.bottom() - destRect.top();
        if (dest.getImagePalette() == null && src.getImagePalette() != null) {
            dest.copyPaletteMetadataFrom(src);
        }
        Integer backgroundKeyRgb = ink == Palette.InkMode.BACKGROUND_TRANSPARENT
                ? Integer.valueOf(bgColorRemap >= 0 ? bgColorRemap : 0xFFFFFF)
                : null;
        // Apply #color/#bgColor remapping only for grayscale source bitmaps.
        // Director's copyPixels remap is designed for default black/white text bitmaps
        // (e.g., title text rendered as black-on-white, remapped to white-on-teal).
        // Already-colored bitmaps (e.g., text rendered with explicit txtColor/txtBgColor)
        // must NOT be remapped — doing so destroys their carefully set pixel colors.
        Bitmap effectiveSrc = src;
        int effectiveSrcX = srcRect.left();
        int effectiveSrcY = srcRect.top();
        boolean remapToAlphaMask = false;
        boolean grayscaleColorized = false;
        if ((colorRemap >= 0 || bgColorRemap >= 0) && ink != Palette.InkMode.BACKGROUND_TRANSPARENT) {
            // Sample source pixels to check if they're grayscale (safe to remap)
            boolean isGrayscale = true;
            int sampleStep = Math.max(1, (srcW * srcH) / 64);
            for (int i = 0; i < srcW * srcH && isGrayscale; i += sampleStep) {
                int sx = srcRect.left() + (i % srcW);
                int sy = srcRect.top() + (i / srcW);
                int p = src.getPixel(sx, sy);
                if ((p >>> 24) == 0) continue; // skip transparent
                int r = (p >> 16) & 0xFF;
                int g = (p >> 8) & 0xFF;
                int b = p & 0xFF;
                if (Math.abs(r - g) > 2 || Math.abs(g - b) > 2) {
                    isGrayscale = false;
                }
            }

            if (isGrayscale) {
                int fgR = colorRemap >= 0 ? (colorRemap >> 16) & 0xFF : 0;
                int fgG = colorRemap >= 0 ? (colorRemap >> 8) & 0xFF : 0;
                int fgB = colorRemap >= 0 ? colorRemap & 0xFF : 0;
                int bgR = bgColorRemap >= 0 ? (bgColorRemap >> 16) & 0xFF : 255;
                int bgG = bgColorRemap >= 0 ? (bgColorRemap >> 8) & 0xFF : 255;
                int bgB = bgColorRemap >= 0 ? bgColorRemap & 0xFF : 255;
                boolean transparentBackground = colorRemap >= 0 && bgColorRemap < 0;

                effectiveSrc = new Bitmap(srcW, srcH, src.getBitDepth());
                for (int y = 0; y < srcH; y++) {
                    for (int x = 0; x < srcW; x++) {
                        int pixel = src.getPixel(srcRect.left() + x, srcRect.top() + y);
                        int alpha = (pixel >>> 24);
                        int r = (pixel >> 16) & 0xFF;
                        int g = (pixel >> 8) & 0xFF;
                        int b = pixel & 0xFF;
                        int gray = (r + g + b) / 3;
                        if (transparentBackground) {
                            int maskAlpha = (255 - gray) * alpha / 255;
                            int outR = colorRemap >= 0 ? fgR : 0;
                            int outG = colorRemap >= 0 ? fgG : 0;
                            int outB = colorRemap >= 0 ? fgB : 0;
                            effectiveSrc.setPixel(x, y, (maskAlpha << 24) | (outR << 16) | (outG << 8) | outB);
                        } else {
                            float t = gray / 255.0f;
                            int nr = (int) ((1 - t) * fgR + t * bgR + 0.5f);
                            int ng = (int) ((1 - t) * fgG + t * bgG + 0.5f);
                            int nb = (int) ((1 - t) * fgB + t * bgB + 0.5f);
                            effectiveSrc.setPixel(x, y, (alpha << 24) | (nr << 16) | (ng << 8) | nb);
                        }
                    }
                }
                effectiveSrcX = 0;
                effectiveSrcY = 0;
                remapToAlphaMask = transparentBackground;
                grayscaleColorized = true;
            }
        }

        Palette.InkMode effectiveInk = ink;
        if (remapToAlphaMask) {
            effectiveInk = Palette.InkMode.COPY;
        }
        if (effectiveInk == Palette.InkMode.DARKEN) {
            if (!grayscaleColorized) {
                effectiveSrc = multiplyBitmapColor(effectiveSrc, bgColorRemap >= 0 ? bgColorRemap : 0xFFFFFF);
                effectiveSrcX = 0;
                effectiveSrcY = 0;
            }
        }
        // Director's copyPixels applies a global blend factor to the copied pixels.
        // With the default COPY ink, blend<100 behaves like a blend operation over
        // the current destination instead of a straight overwrite.
        if (blend < 255 && effectiveInk == Palette.InkMode.COPY) {
            effectiveInk = Palette.InkMode.BLEND;
        }

        if (srcW == destW && srcH == destH) {
            // No scaling needed - direct copy
            Drawing.copyPixels(dest, effectiveSrc,
                    destRect.left(), destRect.top(),
                    effectiveSrcX, effectiveSrcY,
                    srcW, srcH, effectiveInk, blend, mask, backgroundKeyRgb);
        } else {
            // Scaling needed - create scaled intermediate, applying mask at source coordinates
            Bitmap scaled = new Bitmap(destW, destH, effectiveSrc.getBitDepth());
            scaled.copyPaletteMetadataFrom(effectiveSrc);
            for (int y = 0; y < destH; y++) {
                int sy = effectiveSrcY + (y * srcH / destH);
                for (int x = 0; x < destW; x++) {
                    int sx = effectiveSrcX + (x * srcW / destW);
                    // Check mask at original source coordinates during scaling
                    if (mask != null) {
                        int origSx = srcRect.left() + (x * srcW / destW);
                        int origSy = srcRect.top() + (y * srcH / destH);
                        if (origSx < 0 || origSx >= mask.getWidth()
                                || origSy < 0 || origSy >= mask.getHeight()
                                || (mask.getPixel(origSx, origSy) >>> 24) == 0) {
                            continue; // Leave as transparent (default 0)
                        }
                    }
                    scaled.setPixel(x, y, effectiveSrc.getPixel(sx, sy));
                }
            }
            // Mask already applied during scaling, so pass null to Drawing
            Drawing.copyPixels(dest, scaled,
                    destRect.left(), destRect.top(),
                    0, 0, destW, destH, effectiveInk, blend, null, backgroundKeyRgb);
        }

        return Datum.VOID;
    }

    /**
     * copyPixels with quad destination (list of 4 points).
     * Director uses this for image-space transforms such as flipH/flipV and
     * 90-degree rotations (used heavily by the Habbo window/dropdown system).
     */
    private static Datum copyPixelsQuad(Bitmap dest, Bitmap src, Datum.List quad,
                                         Datum.Rect srcRect, List<Datum> args) {
        // Extract the 4 corner points
        var items = quad.items();
        if (items.size() != 4) return Datum.VOID;

        // Director quad order: [topLeft, topRight, bottomRight, bottomLeft]
        // (confirmed by Scripting Reference: "upper left, upper right, lower right, and lower left")
        int[] px = new int[4], py = new int[4];
        for (int i = 0; i < 4; i++) {
            if (items.get(i) instanceof Datum.Point p) {
                px[i] = p.x();
                py[i] = p.y();
            } else {
                return Datum.VOID;
            }
        }

        int srcW = srcRect.right() - srcRect.left();
        int srcH = srcRect.bottom() - srcRect.top();
        if (srcW <= 0 || srcH <= 0) return Datum.VOID;

        // Determine bounding box of the quad
        int minX = Math.min(Math.min(px[0], px[1]), Math.min(px[2], px[3]));
        int minY = Math.min(Math.min(py[0], py[1]), Math.min(py[2], py[3]));
        int maxX = Math.max(Math.max(px[0], px[1]), Math.max(px[2], px[3]));
        int maxY = Math.max(Math.max(py[0], py[1]), Math.max(py[2], py[3]));
        int destW = maxX - minX;
        int destH = maxY - minY;
        if (destW <= 0 || destH <= 0) return Datum.VOID;

        // Parse optional ink/blend from propList (4th argument)
        Palette.InkMode ink = Palette.InkMode.COPY;
        int blend = 255;
        if (args.size() > 3 && args.get(3) instanceof Datum.PropList pl) {
            Datum inkDatum = getPropIgnoreCase(pl, "ink", "Ink");
            if (inkDatum instanceof Datum.Int inkInt) {
                ink = inkFromInt(inkInt.value());
            }
            Datum blendDatum = getPropIgnoreCase(pl, "blend", "Blend");
            if (!blendDatum.isVoid()) {
                blend = (int) (blendDatum.toDouble() * 255.0 / 100.0);
            }
        }

        // Map Director's quad orientation back into source-space coordinates.
        // This covers identity, flips, and 90-degree rotations.
        Bitmap transformed = new Bitmap(destW, destH, src.getBitDepth());
        transformed.copyPaletteMetadataFrom(src);
        boolean axisAligned =
                (px[0] == minX || px[0] == maxX) && (py[0] == minY || py[0] == maxY)
                        && (px[1] == minX || px[1] == maxX) && (py[1] == minY || py[1] == maxY)
                        && (px[2] == minX || px[2] == maxX) && (py[2] == minY || py[2] == maxY)
                        && (px[3] == minX || px[3] == maxX) && (py[3] == minY || py[3] == maxY);

        if (axisAligned) {
            double c0x = px[0] == minX ? 0.0 : 1.0;
            double c0y = py[0] == minY ? 0.0 : 1.0;
            double axisXX = (px[1] == minX ? 0.0 : 1.0) - c0x;
            double axisXY = (py[1] == minY ? 0.0 : 1.0) - c0y;
            double axisYX = (px[3] == minX ? 0.0 : 1.0) - c0x;
            double axisYY = (py[3] == minY ? 0.0 : 1.0) - c0y;

            for (int y = 0; y < destH; y++) {
                double dv = ((double) y + 0.5) / destH;
                for (int x = 0; x < destW; x++) {
                    double du = ((double) x + 0.5) / destW;
                    double relX = du - c0x;
                    double relY = dv - c0y;

                    double srcU = relX * axisXX + relY * axisXY;
                    double srcV = relX * axisYX + relY * axisYY;

                    int srcX = srcRect.left() + clamp((int) Math.floor(srcU * srcW), 0, srcW - 1);
                    int srcY = srcRect.top() + clamp((int) Math.floor(srcV * srcH), 0, srcH - 1);
                    if (srcX >= 0 && srcX < src.getWidth() && srcY >= 0 && srcY < src.getHeight()) {
                        transformed.setPixel(x, y, src.getPixel(srcX, srcY));
                    }
                }
            }
        } else {
            // Fallback to the previous behaviour for quads that are not simple
            // axis-aligned rectangle transforms.
            boolean flipH = px[0] > px[1];
            boolean flipV = py[0] > py[3];
            for (int y = 0; y < destH; y++) {
                for (int x = 0; x < destW; x++) {
                    int srcX = flipH ? (srcW - 1 - (x * srcW / destW)) : (x * srcW / destW);
                    int srcY = flipV ? (srcH - 1 - (y * srcH / destH)) : (y * srcH / destH);
                    srcX += srcRect.left();
                    srcY += srcRect.top();
                    if (srcX >= 0 && srcX < src.getWidth() && srcY >= 0 && srcY < src.getHeight()) {
                        transformed.setPixel(x, y, src.getPixel(srcX, srcY));
                    }
                }
            }
        }
        Drawing.copyPixels(dest, transformed, minX, minY, 0, 0, destW, destH, ink, blend);

        return Datum.VOID;
    }

    private static int clamp(int value, int min, int max) {
        return Math.max(min, Math.min(max, value));
    }

    private static Bitmap multiplyBitmapColor(Bitmap src, int tintRgb) {
        if (tintRgb == 0xFFFFFF) {
            return src;
        }

        int tintR = (tintRgb >> 16) & 0xFF;
        int tintG = (tintRgb >> 8) & 0xFF;
        int tintB = tintRgb & 0xFF;

        Bitmap tinted = new Bitmap(src.getWidth(), src.getHeight(), src.getBitDepth());
        tinted.copyPaletteMetadataFrom(src);

        for (int y = 0; y < src.getHeight(); y++) {
            for (int x = 0; x < src.getWidth(); x++) {
                int pixel = src.getPixel(x, y);
                int alpha = (pixel >>> 24) & 0xFF;
                if (alpha == 0) {
                    continue;
                }
                int r = ((pixel >> 16) & 0xFF) * tintR / 255;
                int g = ((pixel >> 8) & 0xFF) * tintG / 255;
                int b = (pixel & 0xFF) * tintB / 255;
                tinted.setPixel(x, y, (alpha << 24) | (r << 16) | (g << 8) | b);
            }
        }
        return tinted;
    }

    /**
     * image.crop(rect) - Crop image to rectangle, return new image.
     */
    private static Datum crop(Bitmap bmp, List<Datum> args) {
        if (args.isEmpty()) return Datum.VOID;
        if (!(args.get(0) instanceof Datum.Rect rect)) return Datum.VOID;

        int w = rect.right() - rect.left();
        int h = rect.bottom() - rect.top();
        if (w <= 0 || h <= 0) return Datum.VOID;

        Bitmap cropped = bmp.getRegion(rect.left(), rect.top(), w, h);
        return new Datum.ImageRef(cropped);
    }

    /**
     * Look up a property by name in a PropList, trying the given key first,
     * then common casing variants (lowercase, capitalized). Returns Datum.VOID if not found.
     */
    private static Datum getPropIgnoreCase(Datum.PropList pl, String... keys) {
        for (String key : keys) {
            Datum val = pl.get(key);
            if (val != null) return val;
        }
        return Datum.VOID;
    }

    /**
     * Convert Director ink number to InkMode enum.
     */
    private static Palette.InkMode inkFromInt(int inkNum) {
        return switch (inkNum) {
            case 0 -> Palette.InkMode.COPY;
            case 1 -> Palette.InkMode.TRANSPARENT;
            case 2 -> Palette.InkMode.REVERSE;
            case 3 -> Palette.InkMode.GHOST;
            case 4 -> Palette.InkMode.NOT_COPY;
            case 5 -> Palette.InkMode.NOT_TRANSPARENT;
            case 6 -> Palette.InkMode.NOT_REVERSE;
            case 7 -> Palette.InkMode.NOT_GHOST;
            case 8 -> Palette.InkMode.MATTE;
            case 9 -> Palette.InkMode.MASK;
            case 32 -> Palette.InkMode.BLEND;
            case 33 -> Palette.InkMode.ADD_PIN;
            case 34 -> Palette.InkMode.ADD;
            case 35 -> Palette.InkMode.SUBTRACT_PIN;
            case 36 -> Palette.InkMode.BACKGROUND_TRANSPARENT;
            case 37 -> Palette.InkMode.LIGHTEST;
            case 38 -> Palette.InkMode.SUBTRACT;
            case 39 -> Palette.InkMode.DARKEST;
            case 40 -> Palette.InkMode.LIGHTEN;
            case 41 -> Palette.InkMode.DARKEN;
            default -> Palette.InkMode.COPY;
        };
    }
}
