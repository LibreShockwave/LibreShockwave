package com.libreshockwave.vm.opcode;

import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Method;

import static org.junit.jupiter.api.Assertions.assertEquals;

class PropertyOpcodesTest {

    @AfterEach
    void tearDown() {
        CastLibProvider.clearProvider();
    }

    @Test
    void invalidCastMemberRefReportsZeroNumber() throws Exception {
        CastLibProvider.setProvider(new StubCastLibProvider(false));

        Datum.CastMemberRef ref = (Datum.CastMemberRef) Datum.CastMemberRef.of(11, 7);

        assertEquals(0, getCastMemberProp(ref, "number").toInt());
        assertEquals(0, getCastMemberProp(ref, "memberNum").toInt());
        assertEquals(11, getCastMemberProp(ref, "castLibNum").toInt());
    }

    @Test
    void validCastMemberRefKeepsEncodedNumber() throws Exception {
        CastLibProvider.setProvider(new StubCastLibProvider(true));

        Datum.CastMemberRef ref = (Datum.CastMemberRef) Datum.CastMemberRef.of(11, 7);

        assertEquals((11 << 16) | 7, getCastMemberProp(ref, "number").toInt());
        assertEquals(7, getCastMemberProp(ref, "memberNum").toInt());
        assertEquals(11, getCastMemberProp(ref, "castLibNum").toInt());
    }

    @Test
    void emptyMemberRefReportsZeroNumber() throws Exception {
        CastLibProvider.setProvider(new StubCastLibProvider(true));

        Datum.CastMemberRef ref = (Datum.CastMemberRef) Datum.CastMemberRef.of(11, 0);

        assertEquals(0, getCastMemberProp(ref, "number").toInt());
        assertEquals(0, getCastMemberProp(ref, "memberNum").toInt());
        assertEquals(11, getCastMemberProp(ref, "castLibNum").toInt());
    }

    private static Datum getCastMemberProp(Datum.CastMemberRef ref, String propName) throws Exception {
        Method method = PropertyOpcodes.class.getDeclaredMethod("getCastMemberProp", Datum.CastMemberRef.class, String.class);
        method.setAccessible(true);
        return (Datum) method.invoke(null, ref, propName);
    }

    private static final class StubCastLibProvider implements CastLibProvider {
        private final boolean memberExists;

        private StubCastLibProvider(boolean memberExists) {
            this.memberExists = memberExists;
        }

        @Override
        public int getCastLibByNumber(int castLibNumber) {
            return castLibNumber;
        }

        @Override
        public int getCastLibByName(String name) {
            return -1;
        }

        @Override
        public Datum getCastLibProp(int castLibNumber, String propName) {
            return Datum.VOID;
        }

        @Override
        public boolean setCastLibProp(int castLibNumber, String propName, Datum value) {
            return false;
        }

        @Override
        public Datum getMember(int castLibNumber, int memberNumber) {
            return Datum.CastMemberRef.of(castLibNumber, memberNumber);
        }

        @Override
        public Datum getMemberByName(int castLibNumber, String memberName) {
            return Datum.VOID;
        }

        @Override
        public int getCastLibCount() {
            return 0;
        }

        @Override
        public Datum getMemberProp(int castLibNumber, int memberNumber, String propName) {
            return Datum.VOID;
        }

        @Override
        public boolean setMemberProp(int castLibNumber, int memberNumber, String propName, Datum value) {
            return false;
        }

        @Override
        public boolean memberExists(int castLibNumber, int memberNumber) {
            return memberExists && memberNumber > 0;
        }
    }
}
