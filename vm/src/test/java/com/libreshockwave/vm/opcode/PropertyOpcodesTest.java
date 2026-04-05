package com.libreshockwave.vm.opcode;

import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.support.NoOpCastLibProvider;
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

    private static final class StubCastLibProvider extends NoOpCastLibProvider {
        private final boolean memberExists;

        private StubCastLibProvider(boolean memberExists) {
            this.memberExists = memberExists;
        }

        @Override
        public boolean memberExists(int castLibNumber, int memberNumber) {
            return memberExists && memberNumber > 0;
        }
    }
}
