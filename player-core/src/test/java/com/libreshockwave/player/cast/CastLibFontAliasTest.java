package com.libreshockwave.player.cast;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class CastLibFontAliasTest {

    @Test
    void parsesDirectorFontAliasMember() {
        byte[] data = new byte[] {
                0x00, 0x00, 0x00, 0x04, 'f', 'o', 'n', 't',
                0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x1C, (byte) 0x80, 0x00, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x01, 0x00, 0x09, 'v', 'b',
                0x00, 'V', 'o', 'l', 't', 'e', 'r', 0x00
        };

        CastLib.FontAliasInfo alias = CastLib.parseFontAlias(data, "vb");

        assertNotNull(alias);
        assertEquals("vb", alias.alias());
        assertEquals("Volter", alias.fontName());
        assertTrue(alias.bold());
    }
}
