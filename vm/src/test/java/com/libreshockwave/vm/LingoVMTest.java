package com.libreshockwave.vm;

import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.builtin.flow.UpdateProvider;
import com.libreshockwave.vm.builtin.movie.MoviePropertyProvider;
import com.libreshockwave.vm.HandlerRef;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.support.NoOpCastLibProvider;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

import java.util.concurrent.atomic.AtomicInteger;
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

    @Test
    void testPrefsAreCaseInsensitiveAndMissingReturnsVoid() {
        LingoVM vm = new LingoVM(null);

        assertTrue(vm.getPref("Blocktime").isVoid());

        Datum stored = vm.setPref("blocktime", Datum.of("123"));
        assertEquals("123", stored.toStr());

        Datum fetched = vm.getPref("Blocktime");
        assertTrue(fetched.isString());
        assertEquals("123", fetched.toStr());
    }

    @Test
    void testBuiltinPrefsUseVmPreferenceStorage() {
        LingoVM vm = new LingoVM(null);

        assertTrue(vm.callHandler("getPref", List.of(Datum.of("Blocktime"))).isVoid());

        Datum stored = vm.callHandler("setPref", List.of(Datum.of("blocktime"), Datum.of("123")));
        assertEquals("123", stored.toStr());

        Datum fetched = vm.callHandler("getPref", List.of(Datum.of("Blocktime")));
        assertTrue(fetched.isString());
        assertEquals("123", fetched.toStr());
    }

    @Test
    void testDeferredTasksFlushOnlyAtExplicitBoundary() {
        LingoVM vm = new LingoVM(null);
        AtomicInteger calls = new AtomicInteger();

        vm.deferTask(calls::incrementAndGet);

        assertEquals(0, calls.get());

        vm.flushDeferredTasks();

        assertEquals(1, calls.get());
    }

    @Test
    void testGotoNetPageBuiltinDelegatesToMovieProvider() {
        LingoVM vm = new LingoVM(null);
        RecordingMovieProvider provider = new RecordingMovieProvider();
        MoviePropertyProvider.setProvider(provider);
        try {
            Datum result = vm.callHandler("gotoNetPage",
                    List.of(Datum.of("https://example.com"), Datum.of("_new")));
            assertTrue(result.isTruthy());
            assertEquals("https://example.com", provider.lastGotoUrl);
            assertEquals("_new", provider.lastGotoTarget);
        } finally {
            MoviePropertyProvider.clearProvider();
        }
    }

    @Test
    void testCallHandlerPrefersGlobalScriptHandlerOverBuiltin() {
        OverridingHandlerVm vm = new OverridingHandlerVm();

        Datum result = vm.callHandler("getmemnum", List.of(Datum.of("much_sofa1b_a_0_2_0")));

        assertEquals(1, vm.executeCount);
        assertEquals("script:getmemnum", result.toStr());
    }

    @Test
    void testReceiveUpdateAndRemoveUpdateAreNotBuiltins() {
        LingoVM vm = new LingoVM(null);
        RecordingUpdateProvider provider = new RecordingUpdateProvider();
        UpdateProvider.setProvider(provider);
        try {
            vm.callHandler("receiveUpdate", List.of(Datum.of("obj-id")));
            vm.callHandler("removeUpdate", List.of(Datum.of("obj-id")));

            assertEquals(0, provider.receiveCalls);
            assertEquals(0, provider.removeCalls);
        } finally {
            UpdateProvider.clearProvider();
        }
    }

    @Test
    void testFormatTraceArgumentShowsVoidAndSymbolsExplicitly() {
        assertEquals("<VOID>", LingoVM.formatTraceArgument(Datum.VOID));
        assertEquals("#mouseUp", LingoVM.formatTraceArgument(Datum.symbol("mouseUp")));
        assertEquals("\"login_a\"", LingoVM.formatTraceArgument(Datum.of("login_a")));
    }

    @Test
    void testMemberBuiltinResolvesCastLibNameArgument() {
        LingoVM vm = new LingoVM(null);
        RecordingCastProvider provider = new RecordingCastProvider();
        CastLibProvider.setProvider(provider);
        try {
            Datum result = vm.callHandler("member", List.of(Datum.of("present_gen5_small"), Datum.of("bin")));
            assertEquals(11, provider.lastMemberByNameCastLibNumber);
            assertEquals("present_gen5_small", provider.lastMemberByName);
            assertTrue(result instanceof Datum.CastMemberRef);
        } finally {
            CastLibProvider.clearProvider();
        }
    }

    @Test
    void testMemberBuiltinResolvesCastLibSymbolArgument() {
        LingoVM vm = new LingoVM(null);
        RecordingCastProvider provider = new RecordingCastProvider();
        CastLibProvider.setProvider(provider);
        try {
            Datum result = vm.callHandler("member", List.of(Datum.of("present_gen5_small"), Datum.symbol("bin")));
            assertEquals(11, provider.lastMemberByNameCastLibNumber);
            assertEquals("present_gen5_small", provider.lastMemberByName);
            assertTrue(result instanceof Datum.CastMemberRef);
        } finally {
            CastLibProvider.clearProvider();
        }
    }

    @Test
    void testMemberBuiltinReturnsInvalidRefWhenNamedMemberIsMissingFromSpecificCast() {
        LingoVM vm = new LingoVM(null);
        RecordingCastProvider provider = new RecordingCastProvider();
        provider.memberByNameResult = Datum.VOID;
        CastLibProvider.setProvider(provider);
        try {
            Datum result = vm.callHandler("member", List.of(Datum.of("grunge_barrel.props"), Datum.of("bin")));
            assertEquals(11, provider.lastMemberByNameCastLibNumber);
            assertEquals("grunge_barrel.props", provider.lastMemberByName);
            assertEquals(11, provider.lastGetMemberCastLibNumber);
            assertEquals(0, provider.lastGetMemberNumber);
            assertInstanceOf(Datum.CastMemberRef.class, result);
            Datum.CastMemberRef ref = (Datum.CastMemberRef) result;
            assertEquals(11, ref.castLibNum());
            assertEquals(0, ref.memberNum());
        } finally {
            CastLibProvider.clearProvider();
        }
    }

    @Test
    void testFieldBuiltinResolvesCastLibNameArgument() {
        LingoVM vm = new LingoVM(null);
        RecordingCastProvider provider = new RecordingCastProvider();
        provider.fieldValue = "ok";
        CastLibProvider.setProvider(provider);
        try {
            Datum result = vm.callHandler("field", List.of(Datum.of("memberalias.index"), Datum.of("bin")));
            assertEquals(11, provider.lastFieldCastId);
            assertEquals("memberalias.index", provider.lastFieldMemberName);
            assertEquals("ok", result.toStr());
        } finally {
            CastLibProvider.clearProvider();
        }
    }

    @Test
    void testFieldBuiltinResolvesCastLibSymbolArgument() {
        LingoVM vm = new LingoVM(null);
        RecordingCastProvider provider = new RecordingCastProvider();
        provider.fieldValue = "ok";
        CastLibProvider.setProvider(provider);
        try {
            Datum result = vm.callHandler("field", List.of(Datum.of("memberalias.index"), Datum.symbol("bin")));
            assertEquals(11, provider.lastFieldCastId);
            assertEquals("memberalias.index", provider.lastFieldMemberName);
            assertEquals("ok", result.toStr());
        } finally {
            CastLibProvider.clearProvider();
        }
    }

    @Test
    void testValueBuiltinUsesParsedFieldDatum() {
        LingoVM vm = new LingoVM(null);
        RecordingCastProvider provider = new RecordingCastProvider();
        provider.fieldValue = "[#foo: 7]";
        provider.fieldParsedValue = Datum.propList(java.util.Map.of("foo", Datum.of(7)));
        CastLibProvider.setProvider(provider);
        try {
            Datum field = vm.callHandler("field", List.of(Datum.of("grunge_barrel.props"), Datum.of("bin")));
            assertInstanceOf(Datum.FieldText.class, field);

            Datum parsed = vm.callHandler("value", List.of(field));
            assertInstanceOf(Datum.PropList.class, parsed);
            assertEquals(1, provider.fieldParsedCalls);
            assertEquals(7, ((Datum.PropList) parsed).get("foo").toInt());
        } finally {
            CastLibProvider.clearProvider();
        }
    }

    @Test
    void testValueBuiltinFallsBackToGenericFieldParsingWhenProviderCacheMisses() {
        LingoVM vm = new LingoVM(null);
        RecordingCastProvider provider = new RecordingCastProvider();
        provider.fieldValue = "[#foo: 9]";
        provider.fieldParsedValue = Datum.VOID;
        CastLibProvider.setProvider(provider);
        try {
            Datum field = vm.callHandler("field", List.of(Datum.of("grunge_barrel.props"), Datum.of("bin")));

            Datum parsed = vm.callHandler("value", List.of(field));
            assertInstanceOf(Datum.PropList.class, parsed);
            assertEquals(1, provider.fieldParsedCalls);
            assertEquals(9, ((Datum.PropList) parsed).get("foo").toInt());
        } finally {
            CastLibProvider.clearProvider();
        }
    }

    @Test
    void testValueBuiltinPreservesBareMultiWordScriptClassNames() {
        LingoVM vm = new LingoVM(null);
        RecordingCastProvider provider = new RecordingCastProvider();
        CastLibProvider.setProvider(provider);
        try {
            Datum result = vm.callHandler("value", List.of(Datum.of("Broker Manager Class")));

            assertTrue(result.isString());
            assertEquals("Broker Manager Class", result.toStr());
            assertEquals(0, provider.lastMemberByNameCastLibNumber);
            assertEquals("Broker Manager Class", provider.lastMemberByName);
        } finally {
            CastLibProvider.clearProvider();
        }
    }

    @Test
    void testValueBuiltinStillReturnsVoidForUnknownMultiWordStrings() {
        LingoVM vm = new LingoVM(null);
        RecordingCastProvider provider = new RecordingCastProvider();
        provider.memberByNameResult = Datum.VOID;
        CastLibProvider.setProvider(provider);
        try {
            Datum result = vm.callHandler("value", List.of(Datum.of("Broker Manager Class")));

            assertTrue(result.isVoid());
            assertEquals(0, provider.lastMemberByNameCastLibNumber);
            assertEquals("Broker Manager Class", provider.lastMemberByName);
        } finally {
            CastLibProvider.clearProvider();
        }
    }

    @Test
    void testFieldDatumBehavesLikeAString() {
        LingoVM vm = new LingoVM(null);
        RecordingCastProvider provider = new RecordingCastProvider();
        provider.fieldValue = "ok";
        CastLibProvider.setProvider(provider);
        try {
            Datum field = vm.callHandler("field", List.of(Datum.of("memberalias.index"), Datum.of("bin")));

            assertInstanceOf(Datum.FieldText.class, field);
            assertTrue(vm.callHandler("stringp", List.of(field)).isTruthy());
            assertFalse(vm.callHandler("objectp", List.of(field)).isTruthy());
            assertEquals(2, vm.callHandler("length", List.of(field)).toInt());
        } finally {
            CastLibProvider.clearProvider();
        }
    }

    private static final class RecordingMovieProvider implements MoviePropertyProvider {
        private String lastPropName;
        private Datum lastValue = Datum.VOID;
        private String lastGotoUrl;
        private String lastGotoTarget;

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

        @Override
        public void gotoNetPage(String url, String target) {
            lastGotoUrl = url;
            lastGotoTarget = target;
        }
    }

    private static final class RecordingCastProvider extends NoOpCastLibProvider {
        private int lastMemberByNameCastLibNumber = -1;
        private String lastMemberByName;
        private int lastGetMemberCastLibNumber = -1;
        private int lastGetMemberNumber = -1;
        private int lastFieldCastId = -1;
        private String lastFieldMemberName;
        private String fieldValue = "";
        private Datum fieldParsedValue = Datum.VOID;
        private int fieldParsedCalls = 0;
        private Datum memberByNameResult = Datum.CastMemberRef.of(11, 7);

        @Override
        public int getCastLibByName(String name) {
            if ("bin".equalsIgnoreCase(name)) {
                return 11;
            }
            return -1;
        }

        @Override
        public Datum getMember(int castLibNumber, int memberNumber) {
            lastGetMemberCastLibNumber = castLibNumber;
            lastGetMemberNumber = memberNumber;
            return Datum.CastMemberRef.of(castLibNumber, memberNumber);
        }

        @Override
        public Datum getMemberByName(int castLibNumber, String memberName) {
            lastMemberByNameCastLibNumber = castLibNumber;
            lastMemberByName = memberName;
            return memberByNameResult;
        }

        @Override
        public int getCastLibCount() {
            return 11;
        }

        @Override
        public String getFieldValue(Object memberNameOrNum, int castId) {
            lastFieldCastId = castId;
            lastFieldMemberName = String.valueOf(memberNameOrNum);
            return fieldValue;
        }

        @Override
        public Datum getFieldDatum(Object memberNameOrNum, int castId) {
            lastFieldCastId = castId;
            lastFieldMemberName = String.valueOf(memberNameOrNum);
            return new Datum.FieldText(fieldValue, castId > 0 ? castId : 11, 7);
        }

        @Override
        public Datum getFieldParsedValue(int castLibNumber, int memberNumber, LingoVM vm) {
            fieldParsedCalls++;
            return fieldParsedValue;
        }
    }

    private static final class RecordingUpdateProvider implements UpdateProvider {
        private int receiveCalls;
        private int removeCalls;

        @Override
        public void receiveUpdate(Datum target) {
            receiveCalls++;
        }

        @Override
        public void removeUpdate(Datum target) {
            removeCalls++;
        }
    }

    private static final class OverridingHandlerVm extends LingoVM {
        private int executeCount;

        private OverridingHandlerVm() {
            super(null);
        }

        @Override
        public HandlerRef findHandler(String handlerName) {
            if ("getmemnum".equalsIgnoreCase(handlerName)) {
                return new HandlerRef(null, null);
            }
            return null;
        }

        @Override
        public Datum executeHandler(com.libreshockwave.chunks.ScriptChunk script,
                                    com.libreshockwave.chunks.ScriptChunk.Handler handler,
                                    List<Datum> args, Datum receiver) {
            executeCount++;
            return Datum.of("script:getmemnum");
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
    void testBuiltinGetaPropSeparatesSymbolAndStringNamespaces() {
        LingoVM vm = new LingoVM(null);
        Datum.PropList propList = new Datum.PropList();
        propList.add("room_interface", Datum.of(1), true);
        propList.add("room_interface", Datum.of(2), false);

        Datum symbolResult = vm.callHandler("getaProp", List.of(propList, Datum.symbol("room_interface")));
        Datum stringResult = vm.callHandler("getaProp", List.of(propList, Datum.of("room_interface")));

        assertEquals(1, symbolResult.toInt());
        assertEquals(2, stringResult.toInt());
    }

    @Test
    void testBuiltinDeletePropRemovesOnlyMatchingKeyType() {
        LingoVM vm = new LingoVM(null);
        Datum.PropList propList = new Datum.PropList();
        propList.add("room_interface", Datum.of(1), true);
        propList.add("room_interface", Datum.of(2), false);

        vm.callHandler("deleteProp", List.of(propList, Datum.of("room_interface")));

        assertEquals(1, propList.size());
        assertTrue(propList.entries().getFirst().isSymbolKey());
        assertEquals(1, propList.entries().getFirst().value().toInt());
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
