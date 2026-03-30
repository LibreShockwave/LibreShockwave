package com.libreshockwave.vm;

import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class MathBuiltinsReferenceCoercionTest {

    @Test
    void integerBuiltinCoercesSpriteRefsToTheirChannelNumber() {
        LingoVM vm = new LingoVM(null);

        Datum result = vm.callHandler("integer", List.of(Datum.SpriteRef.of(42)));

        assertTrue(result.isInt());
        assertEquals(42, result.toInt());
    }

    @Test
    void integerBuiltinCoercesCastLibRefsToTheirLibraryNumber() {
        LingoVM vm = new LingoVM(null);

        Datum result = vm.callHandler("integer", List.of(Datum.CastLibRef.of(9)));

        assertTrue(result.isInt());
        assertEquals(9, result.toInt());
    }

    @Test
    void integerBuiltinCoercesColorRefsToPackedRgb() {
        LingoVM vm = new LingoVM(null);

        Datum result = vm.callHandler("integer", List.of(new Datum.Color(0x12, 0x34, 0x56)));

        assertTrue(result.isInt());
        assertEquals(0x123456, result.toInt());
    }

    @Test
    void integerBuiltinParsesDirectorStarHexStrings() {
        LingoVM vm = new LingoVM(null);

        Datum white = vm.callHandler("integer", List.of(Datum.of("*ffffff")));
        Datum accent = vm.callHandler("integer", List.of(Datum.of(" *00cc66 ")));

        assertTrue(white.isInt());
        assertEquals(0xFFFFFF, white.toInt());
        assertTrue(accent.isInt());
        assertEquals(0x00CC66, accent.toInt());
    }

    @Test
    void floatBuiltinCoercesReferenceLikeDatumsViaNumericValue() {
        LingoVM vm = new LingoVM(null);

        Datum sprite = vm.callHandler("float", List.of(Datum.SpriteRef.of(42)));
        Datum castLib = vm.callHandler("float", List.of(Datum.CastLibRef.of(9)));
        Datum color = vm.callHandler("float", List.of(new Datum.Color(0x12, 0x34, 0x56)));

        assertTrue(sprite.isFloat());
        assertEquals(42.0, sprite.toDouble(), 0.001);
        assertTrue(castLib.isFloat());
        assertEquals(9.0, castLib.toDouble(), 0.001);
        assertTrue(color.isFloat());
        assertEquals(0x123456, color.toDouble(), 0.001);
    }
}
