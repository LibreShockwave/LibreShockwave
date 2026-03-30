package com.libreshockwave.vm;

import com.libreshockwave.vm.builtin.movie.MoviePropertyProvider;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

import java.util.List;

/**
 * Unit tests for the Lingo VM.
 */
class LingoVMTest {

    @Test
    void testBuiltinSetCursorAliasesCursor() {
        LingoVM vm = new LingoVM(null);
        RecordingMovieProvider provider = new RecordingMovieProvider();
        MoviePropertyProvider.setProvider(provider);
        try {
            Datum result = vm.callHandler("setcursor", List.of(Datum.symbol("timer")));
            assertTrue(result.isVoid());
            assertEquals("cursor", provider.lastPropName);
            assertEquals("timer", provider.lastValue.toStr());
        } finally {
            MoviePropertyProvider.clearProvider();
        }
    }

    @Test
    void testGlobalVariables() {
        // Create a VM with a null file (we test globals without a file)
        LingoVM vm = new LingoVM(null);

        // Initially empty
        assertTrue(vm.getGlobals().isEmpty());

        // Set and get
        vm.setGlobal("score", Datum.of(100));
        assertEquals(100, vm.getGlobal("score").toInt());

        // Unknown globals return VOID
        assertTrue(vm.getGlobal("unknown").isVoid());

        // Clear
        vm.clearGlobals();
        assertTrue(vm.getGlobals().isEmpty());
    }

    private static final class RecordingMovieProvider implements MoviePropertyProvider {
        private String lastPropName;
        private Datum lastValue = Datum.VOID;

        @Override
        public Datum getMovieProp(String propName) {
            return Datum.VOID;
        }

        @Override
        public boolean setMovieProp(String propName, Datum value) {
            lastPropName = propName;
            lastValue = value;
            return true;
        }
    }

    @Test
    void testBuiltinAbs() {
        LingoVM vm = new LingoVM(null);

        // Test via callHandler (will use builtin)
        Datum result = vm.callHandler("abs", List.of(Datum.of(-42)));
        assertEquals(42, result.toInt());

        result = vm.callHandler("abs", List.of(Datum.of(42)));
        assertEquals(42, result.toInt());

        result = vm.callHandler("abs", List.of(Datum.of(-3.14)));
        assertEquals(3.14, result.toDouble(), 0.001);
    }

    @Test
    void testBuiltinSqrt() {
        LingoVM vm = new LingoVM(null);

        Datum result = vm.callHandler("sqrt", List.of(Datum.of(16)));
        assertEquals(4.0, result.toDouble(), 0.001);

        result = vm.callHandler("sqrt", List.of(Datum.of(2)));
        assertEquals(1.414, result.toDouble(), 0.01);
    }

    @Test
    void testBuiltinRandom() {
        LingoVM vm = new LingoVM(null);

        // Random should return 1 to max
        for (int i = 0; i < 100; i++) {
            Datum result = vm.callHandler("random", List.of(Datum.of(10)));
            int value = result.toInt();
            assertTrue(value >= 1 && value <= 10, "Random value " + value + " out of range 1-10");
        }
    }

    @Test
    void testBuiltinString() {
        LingoVM vm = new LingoVM(null);

        Datum result = vm.callHandler("string", List.of(Datum.of(42)));
        assertEquals("42", result.toStr());

        result = vm.callHandler("string", List.of(Datum.of(3.14)));
        assertEquals("3.14", result.toStr());
    }

    @Test
    void testBuiltinInteger() {
        LingoVM vm = new LingoVM(null);

        Datum result = vm.callHandler("integer", List.of(Datum.of(3.7)));
        assertEquals(4, result.toInt());

        result = vm.callHandler("integer", List.of(Datum.of("42")));
        assertEquals(42, result.toInt());
    }

    @Test
    void testBuiltinFloat() {
        LingoVM vm = new LingoVM(null);

        Datum result = vm.callHandler("float", List.of(Datum.of(42)));
        assertEquals(42.0, result.toDouble(), 0.001);
    }

    @Test
    void testBuiltinFloatReturnsOriginalStringForNonNumeric() {
        // float() should return the original string unchanged if it's not a valid number
        // This matches dirplayer-rs behavior and is important for scripts that use
        // floatp(float(x)) to check if x is numeric
        LingoVM vm = new LingoVM(null);

        // Non-numeric string should be returned unchanged
        Datum result = vm.callHandler("float", List.of(Datum.of("hello")));
        assertTrue(result.isString(), "float(\"hello\") should return a string");
        assertEquals("hello", result.toStr());

        // String with class name (like in Variable Container Class props)
        result = vm.callHandler("float", List.of(Datum.of("CastLoad Manager Class")));
        assertTrue(result.isString(), "float(\"CastLoad Manager Class\") should return a string");
        assertEquals("CastLoad Manager Class", result.toStr());

        // Numeric string should be converted
        result = vm.callHandler("float", List.of(Datum.of("3.14")));
        assertTrue(result.isFloat(), "float(\"3.14\") should return a float");
        assertEquals(3.14, result.toDouble(), 0.001);
    }

    @Test
    void testBuiltinIntegerReturnsVoidForNonNumericString() {
        // integer() returns VOID for non-numeric strings (Director behavior).
        // ScummVM: strtol fails, res left as default (VOID).
        LingoVM vm = new LingoVM(null);

        // Non-numeric string should return VOID
        Datum result = vm.callHandler("integer", List.of(Datum.of("hello")));
        assertTrue(result.isVoid(), "integer(\"hello\") should return VOID");

        // Numeric string should be converted
        result = vm.callHandler("integer", List.of(Datum.of("42")));
        assertTrue(result.isInt(), "integer(\"42\") should return an int");
        assertEquals(42, result.toInt());

        // Float string: rounds to nearest, same as integer(3.7)
        result = vm.callHandler("integer", List.of(Datum.of("3.7")));
        assertTrue(result.isInt(), "integer(\"3.7\") should return an int");
        assertEquals(4, result.toInt());
    }

    @Test
    void testBuiltinLength() {
        LingoVM vm = new LingoVM(null);

        // String length
        Datum result = vm.callHandler("length", List.of(Datum.of("hello")));
        assertEquals(5, result.toInt());

        // List length
        result = vm.callHandler("length", List.of(Datum.list(Datum.of(1), Datum.of(2), Datum.of(3))));
        assertEquals(3, result.toInt());
    }

    @Test
    void testBuiltinChars() {
        LingoVM vm = new LingoVM(null);

        // chars("hello", 2, 4) should return "ell"
        Datum result = vm.callHandler("chars", List.of(Datum.of("hello"), Datum.of(2), Datum.of(4)));
        assertEquals("ell", result.toStr());
    }

    @Test
    void testBuiltinCharToNum() {
        LingoVM vm = new LingoVM(null);

        Datum result = vm.callHandler("charToNum", List.of(Datum.of("A")));
        assertEquals(65, result.toInt());
    }

    @Test
    void testBuiltinNumToChar() {
        LingoVM vm = new LingoVM(null);

        Datum result = vm.callHandler("numToChar", List.of(Datum.of(65)));
        assertEquals("A", result.toStr());
    }

    @Test
    void testBuiltinPoint() {
        LingoVM vm = new LingoVM(null);

        Datum result = vm.callHandler("point", List.of(Datum.of(100), Datum.of(200)));
        assertTrue(result instanceof Datum.Point);
        assertEquals(100, ((Datum.Point) result).x());
        assertEquals(200, ((Datum.Point) result).y());
    }

    @Test
    void testBuiltinRect() {
        LingoVM vm = new LingoVM(null);

        Datum result = vm.callHandler("rect", List.of(
            Datum.of(0), Datum.of(0), Datum.of(640), Datum.of(480)));
        assertTrue(result instanceof Datum.Rect);
        assertEquals(0, ((Datum.Rect) result).left());
        assertEquals(0, ((Datum.Rect) result).top());
        assertEquals(640, ((Datum.Rect) result).right());
        assertEquals(480, ((Datum.Rect) result).bottom());
    }

    @Test
    void testBuiltinColor() {
        LingoVM vm = new LingoVM(null);

        Datum result = vm.callHandler("color", List.of(
            Datum.of(255), Datum.of(128), Datum.of(0)));
        assertTrue(result instanceof Datum.Color);
        assertEquals(255, ((Datum.Color) result).r());
        assertEquals(128, ((Datum.Color) result).g());
        assertEquals(0, ((Datum.Color) result).b());
    }

    @Test
    void testBuiltinCount() {
        LingoVM vm = new LingoVM(null);

        Datum list = Datum.list(Datum.of(1), Datum.of(2), Datum.of(3));
        Datum result = vm.callHandler("count", List.of(list));
        assertEquals(3, result.toInt());
    }

    @Test
    void testBuiltinGetAt() {
        LingoVM vm = new LingoVM(null);

        Datum list = Datum.list(Datum.of(10), Datum.of(20), Datum.of(30));

        // Lingo is 1-indexed
        Datum result = vm.callHandler("getAt", List.of(list, Datum.of(1)));
        assertEquals(10, result.toInt());

        result = vm.callHandler("getAt", List.of(list, Datum.of(2)));
        assertEquals(20, result.toInt());

        result = vm.callHandler("getAt", List.of(list, Datum.of(3)));
        assertEquals(30, result.toInt());

        // Out of bounds
        result = vm.callHandler("getAt", List.of(list, Datum.of(4)));
        assertTrue(result.isVoid());
    }

    @Test
    void testTrigFunctions() {
        LingoVM vm = new LingoVM(null);

        // sin(90) should be ~1
        Datum result = vm.callHandler("sin", List.of(Datum.of(90)));
        assertEquals(1.0, result.toDouble(), 0.001);

        // cos(0) should be 1
        result = vm.callHandler("cos", List.of(Datum.of(0)));
        assertEquals(1.0, result.toDouble(), 0.001);

        // cos(90) should be ~0
        result = vm.callHandler("cos", List.of(Datum.of(90)));
        assertEquals(0.0, result.toDouble(), 0.001);
    }

    @Test
    void testUnknownHandlerReturnsVoid() {
        LingoVM vm = new LingoVM(null);

        Datum result = vm.callHandler("nonExistentHandler", List.of());
        assertTrue(result.isVoid());
    }

    @Test
    void testCallStackDepth() {
        LingoVM vm = new LingoVM(null);

        assertEquals(0, vm.getCallStackDepth());
        assertNull(vm.getCurrentScope());
    }
}
