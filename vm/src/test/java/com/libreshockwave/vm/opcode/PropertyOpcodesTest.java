package com.libreshockwave.vm.opcode;

import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.builtin.movie.MoviePropertyProvider;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.support.NoOpCastLibProvider;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Method;
import java.util.HashMap;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;

class PropertyOpcodesTest {

    @AfterEach
    void tearDown() {
        CastLibProvider.clearProvider();
        MoviePropertyProvider.clearProvider();
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

    @Test
    void playerRefGetsMovieBackedProperties() throws Exception {
        StubMovieProvider provider = new StubMovieProvider();
        provider.movieProps.put("activewindow", Datum.STAGE);
        MoviePropertyProvider.setProvider(provider);

        assertEquals(Datum.STAGE, getPlayerProp("activeWindow"));
    }

    @Test
    void playerRefConstantsDoNotRequireMovieProvider() throws Exception {
        assertEquals(Datum.TRUE, getPlayerProp("true"));
    }

    @Test
    void playerRefSetsMovieBackedProperties() throws Exception {
        StubMovieProvider provider = new StubMovieProvider();
        MoviePropertyProvider.setProvider(provider);

        setPlayerProp("traceScript", Datum.TRUE);

        assertEquals(Datum.TRUE, provider.setProps.get("tracescript"));
    }

    private static Datum getCastMemberProp(Datum.CastMemberRef ref, String propName) throws Exception {
        Method method = PropertyOpcodes.class.getDeclaredMethod("getCastMemberProp", Datum.CastMemberRef.class, String.class);
        method.setAccessible(true);
        return (Datum) method.invoke(null, ref, propName);
    }

    private static Datum getPlayerProp(String propName) throws Exception {
        Method method = PropertyOpcodes.class.getDeclaredMethod("getPlayerProp", String.class);
        method.setAccessible(true);
        return (Datum) method.invoke(null, propName);
    }

    private static void setPlayerProp(String propName, Datum value) throws Exception {
        Method method = PropertyOpcodes.class.getDeclaredMethod("setPlayerProp", String.class, Datum.class);
        method.setAccessible(true);
        method.invoke(null, propName, value);
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

    private static final class StubMovieProvider implements MoviePropertyProvider {
        private final Map<String, Datum> movieProps = new HashMap<>();
        private final Map<String, Datum> setProps = new HashMap<>();

        @Override
        public Datum getMovieProp(String propName) {
            return movieProps.getOrDefault(propName.toLowerCase(), Datum.VOID);
        }

        @Override
        public boolean setMovieProp(String propName, Datum value) {
            setProps.put(propName.toLowerCase(), value);
            return true;
        }
    }
}
