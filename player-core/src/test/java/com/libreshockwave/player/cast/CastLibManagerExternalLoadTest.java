package com.libreshockwave.player.cast;

import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;

class CastLibManagerExternalLoadTest {

    @Test
    void setCastLibFileNameConsumesCachedDataForRequestedSlotOnly() throws Exception {
        RecordingCastLibManager manager = new RecordingCastLibManager();
        CastLib requested = new CastLib(11, null, null);
        requested.setName("empty 9");
        CastLib unrelated = new CastLib(12, null, null);
        unrelated.setName("empty 10");
        unrelated.setFileName("https://sandbox.h4bbo.net/dcr/hof_furni/hh_furni_xx_wooden_screen.cct");
        installCastLib(manager, requested);
        installCastLib(manager, unrelated);

        manager.cacheExternalData("https://sandbox.h4bbo.net/dcr/hof_furni/hh_furni_xx_wooden_screen.cct",
                new byte[]{1, 2, 3});

        manager.setCastLibProp(11, "fileName",
                Datum.of("https://sandbox.h4bbo.net/dcr/hof_furni/hh_furni_xx_wooden_screen.cct"));

        assertEquals(List.of(11), manager.loadedCastNums);
    }

    @Test
    void fulfillRequestedExternalCastDataSkipsMatchingUnrequestedSlots() throws Exception {
        RecordingCastLibManager manager = new RecordingCastLibManager();
        CastLib requested = new CastLib(11, null, null);
        requested.setName("empty 9");
        requested.setFileName("https://sandbox.h4bbo.net/dcr/hof_furni/hh_furni_xx_wooden_screen.cct");
        CastLib unrequested = new CastLib(12, null, null);
        unrequested.setName("empty 10");
        unrequested.setFileName("https://sandbox.h4bbo.net/dcr/hof_furni/hh_furni_xx_wooden_screen.cct");
        installCastLib(manager, requested);
        installCastLib(manager, unrequested);

        manager.setCastLibProp(11, "fileName",
                Datum.of("https://sandbox.h4bbo.net/dcr/hof_furni/hh_furni_xx_wooden_screen.cct"));
        manager.loadedCastNums.clear();
        manager.cacheExternalData("https://sandbox.h4bbo.net/dcr/hof_furni/hh_furni_xx_wooden_screen.cct",
                new byte[]{4, 5, 6});

        List<Integer> loaded = manager.fulfillRequestedExternalCastData(
                "https://sandbox.h4bbo.net/dcr/hof_furni/hh_furni_xx_wooden_screen.cct");

        assertEquals(List.of(11), loaded);
        assertEquals(List.of(11), manager.loadedCastNums);
    }

    private static final class RecordingCastLibManager extends CastLibManager {
        private final List<Integer> loadedCastNums = new ArrayList<>();

        RecordingCastLibManager() {
            super(null, (castLibNumber, fileName) -> {});
        }

        @Override
        public boolean setExternalCastData(int castLibNumber, byte[] data) {
            loadedCastNums.add(castLibNumber);
            return true;
        }
    }

    @SuppressWarnings("unchecked")
    private static void installCastLib(CastLibManager manager, CastLib castLib) throws Exception {
        Field initializedField = CastLibManager.class.getDeclaredField("initialized");
        initializedField.setAccessible(true);
        initializedField.setBoolean(manager, true);

        Field castLibsField = CastLibManager.class.getDeclaredField("castLibs");
        castLibsField.setAccessible(true);
        Map<Integer, CastLib> castLibs = (Map<Integer, CastLib>) castLibsField.get(manager);
        castLibs.put(castLib.getNumber(), castLib);
    }
}
