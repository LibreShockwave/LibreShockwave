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

    private record ResolvedPalette(Palette palette, Datum.CastMemberRef ref, String systemName) {}

    public static void register(Map<String, BiFunction<LingoVM, List<Datum>, Datum>> builtins) {
        builtins.put("image", ImageBuiltins::image);
        builtins.put("importfileinto", ImageBuiltins::importFileInto);
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

        // 4th argument: palette member reference (e.g., member("nav_ui_palette"))
        // Store the palette on the bitmap so paletteIndex() colors can be resolved correctly.
        if (args.size() >= 4) {
            Datum paletteArg = args.get(3);
            ResolvedPalette resolved = resolvePaletteFromDatum(paletteArg);
            if (resolved != null && resolved.palette() != null) {
                bmp.setImagePalette(resolved.palette());
            }
            if (resolved != null && resolved.ref() != null) {
                Datum.CastMemberRef ref = resolved.ref();
                bmp.setPaletteRefCastMember(ref.castLibNum(), ref.memberNum());
            }
            if (resolved != null && resolved.systemName() != null) {
                bmp.setPaletteRefSystemName(resolved.systemName());
            }
        }

        // Director's image() creates a white-filled image. For paletted images,
        // fill after attaching the palette so the backing indices are initialized.
        bmp.fill(0xFFFFFFFF);

        return new Datum.ImageRef(bmp);
    }

    /**
     * importFileInto(memberRef, url [, options])
     *
     * Director uses this to import downloaded external media into an existing
     * cast member. The player-side cast provider owns the actual media import
     * because platform image decoding is environment-specific.
     */
    private static Datum importFileInto(LingoVM vm, List<Datum> args) {
        if (args.size() < 2 || !(args.get(0) instanceof Datum.CastMemberRef ref)) {
            return Datum.FALSE;
        }

        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) {
            return Datum.FALSE;
        }

        String url = args.get(1).toStr();
        Datum options = args.size() >= 3 ? args.get(2) : Datum.VOID;
        boolean imported = provider.importFileIntoMember(ref.castLibNum(), ref.memberNum(), url, options);
        return imported ? Datum.TRUE : Datum.FALSE;
    }

    /**
     * Resolve a palette from a Datum (cast member reference, palette index color, etc.)
     */
    private static ResolvedPalette resolvePaletteFromDatum(Datum datum) {
        CastLibProvider provider = CastLibProvider.getProvider();

        // Handle member reference — look up the palette cast member
        if (datum instanceof Datum.CastMemberRef ref) {
            if (provider == null) return null;
            Palette palette = provider.getMemberPalette(ref.castLibNum(), ref.memberNum());
            return palette != null ? new ResolvedPalette(palette, ref, null) : null;
        }

        // Handle member looked up by name — may already be resolved to CastMemberRef
        // but could also be a generic Datum if member() returned something else
        String name = null;
        if (datum instanceof Datum.Str str) {
            name = str.value();
        } else if (datum instanceof Datum.Symbol sym) {
            name = sym.name();
        }
        if (name != null) {
            Palette builtIn = Palette.getBuiltInBySymbolName(name);
            if (builtIn != null) {
                return new ResolvedPalette(builtIn, null, Palette.normalizeBuiltInSymbolName(name));
            }
            if (provider != null) {
                Datum refDatum = provider.getMemberByName(0, name);
                if (refDatum instanceof Datum.CastMemberRef ref) {
                    Palette palette = provider.getMemberPalette(ref.castLibNum(), ref.memberNum());
                    if (palette != null) {
                        return new ResolvedPalette(palette, ref, null);
                    }
                }
                Palette palette = provider.resolvePaletteByName(name);
                if (palette != null) {
                    return new ResolvedPalette(palette, null, null);
                }
            }
        }
        return null;
    }
}
