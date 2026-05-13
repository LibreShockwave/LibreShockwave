package com.libreshockwave.player.cast;

import com.libreshockwave.font.BitmapFont;
import com.libreshockwave.font.TtfBitmapRasterizer;
import com.libreshockwave.util.ValueProvider;

import java.util.HashMap;
import java.util.Map;

/**
 * Embedded Volter bitmap font used by Habbo Director font alias members.
 */
public final class VolterFontBundle {
    private static final Map<String, ValueProvider<byte[]>> TTF_DATA = Map.of(
            "regular", com.libreshockwave.fonts.volter.volter::getData,
            "bold", com.libreshockwave.fonts.volter.volter_bold::getData
    );

    private static final Map<String, BitmapFont> cache = new HashMap<>();

    private VolterFontBundle() {
    }

    public static BitmapFont getFont(String fontName, int fontSize, boolean bold) {
        if (!isVolter(fontName) || fontSize <= 0) {
            return null;
        }
        String variant = bold ? "bold" : "regular";
        String cacheKey = variant + ":" + fontSize;
        BitmapFont cached = cache.get(cacheKey);
        if (cached != null) {
            return cached;
        }
        ValueProvider<byte[]> supplier = TTF_DATA.get(variant);
        if (supplier == null) {
            return null;
        }
        BitmapFont font = TtfBitmapRasterizer.rasterize(supplier.get(), fontSize, "Volter");
        if (font != null) {
            cache.put(cacheKey, font);
        }
        return font;
    }

    public static boolean isVolter(String fontName) {
        return fontName != null && "volter".equalsIgnoreCase(fontName.trim());
    }
}
