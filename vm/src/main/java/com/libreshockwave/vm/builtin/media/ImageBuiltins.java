package com.libreshockwave.vm.builtin.media;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.builtin.cast.CastLibProvider;

import java.util.List;
import java.util.Map;
import java.util.function.BiFunction;

/**
 * Built-in functions for Director's image API.
 * Registers the image() constructor function.
 */
public final class ImageBuiltins {

    private ImageBuiltins() {}

    public static void register(Map<String, BiFunction<LingoVM, List<Datum>, Datum>> builtins) {
        builtins.put("image", ImageBuiltins::image);
    }

    /**
     * image(width, height, bitDepth [, paletteRef])
     * Creates a new blank image with the specified dimensions and bit depth.
     * The optional 4th argument is a palette member reference (for 8-bit paletted images).
     * The image is filled with white (0xFFFFFFFF) by default.
     */
    private static Datum image(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) {
            return Datum.VOID;
        }

        int width = args.get(0).toInt();
        int height = args.get(1).toInt();
        int bitDepth = args.size() >= 3 ? args.get(2).toInt() : 32;

        if (width <= 0 || height <= 0) {
            return Datum.VOID;
        }

        Bitmap bmp = new Bitmap(width, height, bitDepth);
        // Director's image() creates bitmaps with white fill for <=16 bit and
        // transparent white for 32-bit. The alpha=0 background is critical for
        // ink processing: DARKEN ink's matte removes opaque white pixels,
        // which would destroy wall/panel content if the canvas were opaque white.
        if (bitDepth == 32) {
            bmp.fill(0x00FFFFFF); // Transparent white (alpha=0) for 32-bit
        } else {
            bmp.fill(0xFFFFFFFF); // Opaque white for lower bit depths
        }

        // 4th argument: palette member reference (e.g., member("nav_ui_palette"))
        // Store the palette on the bitmap so paletteIndex() colors can be resolved correctly.
        if (args.size() >= 4) {
            Datum paletteArg = args.get(3);
            Palette pal = resolvePaletteFromDatum(paletteArg);
            if (pal != null) {
                bmp.setImagePalette(pal);
            }
            // Also handle #systemMac symbol
            if (paletteArg instanceof Datum.Symbol sym) {
                String name = sym.name().toLowerCase();
                if (name.equals("systemmac")) {
                    bmp.setImagePalette(Palette.SYSTEM_MAC_PALETTE);
                } else if (name.equals("systemwin") || name.equals("systemwindows")) {
                    bmp.setImagePalette(Palette.SYSTEM_WIN_PALETTE);
                }
            }
        }

        return new Datum.ImageRef(bmp);
    }

    /**
     * Resolve a palette from a Datum (cast member reference, palette index color, etc.)
     */
    private static Palette resolvePaletteFromDatum(Datum datum) {
        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) return null;

        // Handle member reference — look up the palette cast member
        if (datum instanceof Datum.CastMemberRef ref) {
            return provider.getMemberPalette(ref.castLibNum(), ref.memberNum());
        }

        // Handle member looked up by name — may already be resolved to CastMemberRef
        // but could also be a generic Datum if member() returned something else
        return null;
    }
}
