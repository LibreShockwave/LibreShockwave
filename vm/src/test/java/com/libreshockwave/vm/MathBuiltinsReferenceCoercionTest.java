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

    @Test
    void minAndMaxBuiltinsReduceSingleListArgument() {
        LingoVM vm = new LingoVM(null);
        Datum.List values = new Datum.List(List.of(Datum.of(-2), Datum.of(3), Datum.ZERO));

        Datum min = vm.callHandler("min", List.of(values));
        Datum max = vm.callHandler("max", List.of(values));

        assertTrue(min.isInt());
        assertEquals(-2, min.toInt());
        assertTrue(max.isInt());
        assertEquals(3, max.toInt());
    }

    @Test
    void randomBuiltinUsesVmRandomSeed() {
        LingoVM first = new LingoVM(null);
        LingoVM second = new LingoVM(null);
        first.setRandomSeed(1234);
        second.setRandomSeed(1234);

        for (int i = 0; i < 8; i++) {
            assertEquals(first.callHandler("random", List.of(Datum.of(150))).toInt(),
                    second.callHandler("random", List.of(Datum.of(150))).toInt());
        }
        assertEquals(1234, first.getRandomSeed());
    }

    @Test
    void startupRandomSequenceDefaultsToSeedZero() {
        LingoVM first = new LingoVM(null);
        LingoVM second = new LingoVM(null);

        assertEquals(0, first.getRandomSeed());
        assertEquals(0, second.getRandomSeed());
        for (int i = 0; i < 8; i++) {
            assertEquals(first.callHandler("random", List.of(Datum.of(150))).toInt(),
                    second.callHandler("random", List.of(Datum.of(150))).toInt());
        }
    }

    @Test
    void randomSeedUsesJavaCompatibleSequence() {
        LingoVM vm = new LingoVM(null);
        vm.setRandomSeed(4096);

        assertEquals(1, vm.callHandler("random", List.of(Datum.of(4))).toInt());
        assertEquals(2, vm.callHandler("random", List.of(Datum.of(2))).toInt());
        assertEquals(40, vm.callHandler("random", List.of(Datum.of(150))).toInt());
    }
}
