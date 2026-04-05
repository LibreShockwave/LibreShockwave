package com.libreshockwave.vm;

import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.support.NoOpCastLibProvider;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ValueBuiltinFieldParsingTest {

    @Test
    void valueBuiltinUsesParsedFieldDatum() {
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
    void valueBuiltinFallsBackToGenericFieldParsingWhenProviderCacheMisses() {
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
    void valueBuiltinPreservesBareMultiWordScriptClassNames() {
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
    void valueBuiltinStillReturnsVoidForUnknownMultiWordStrings() {
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
    void fieldDatumBehavesLikeAString() {
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

    private static final class RecordingCastProvider extends NoOpCastLibProvider {
        private int lastMemberByNameCastLibNumber = -1;
        private String lastMemberByName;
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
}
