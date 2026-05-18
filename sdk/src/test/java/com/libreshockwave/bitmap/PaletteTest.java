package com.libreshockwave.bitmap;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;

class PaletteTest {

    @Test
    void builtInMetallicPaletteUsesDirectorColors() {
        Palette palette = Palette.getBuiltIn(Palette.METALLIC);

        assertSame(Palette.METALLIC_PALETTE, palette);
        assertEquals(256, palette.size());
        assertEquals(0xFFFFFF, palette.getColor(0));
        assertEquals(0x51201F, palette.getColor(64));
        assertEquals(0xD9BBA1, palette.getColor(110));
        assertEquals(0x000000, palette.getColor(255));
    }

    @Test
    void builtInRainbowPaletteIsResolvedById() {
        Palette palette = Palette.getBuiltIn(Palette.RAINBOW);

        assertSame(Palette.RAINBOW_PALETTE, palette);
        assertEquals(256, palette.size());
        assertEquals(0xFFFFFF, palette.getColor(0));
        assertEquals(0xFF0C00, palette.getColor(99));
        assertEquals(0x000000, palette.getColor(255));
    }
}
