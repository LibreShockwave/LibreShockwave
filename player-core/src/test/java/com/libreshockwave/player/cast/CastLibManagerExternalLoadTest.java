package com.libreshockwave.player.cast;

import com.libreshockwave.chunks.CastListChunk;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class CastLibManagerExternalLoadTest {

    private static final String EXTERNAL_CAST_URL =
            "https://example.invalid/dcr/external/widget.cct";
    private static final String OTHER_EXTERNAL_CAST_URL =
            "https://example.invalid/dcr/external/other-widget.cct";

    @Test
    void setCastLibFileNameConsumesCachedDataForRequestedSlotOnly() throws Exception {
        RecordingCastLibManager manager = new RecordingCastLibManager();
        CastLib requested = new CastLib(11, null, null);
        requested.setName("empty 9");
        CastLib unrelated = new CastLib(12, null, null);
        unrelated.setName("empty 10");
        unrelated.setFileName(EXTERNAL_CAST_URL);
        installCastLib(manager, requested);
        installCastLib(manager, unrelated);

        manager.cacheExternalData(EXTERNAL_CAST_URL, new byte[]{1, 2, 3});

        manager.setCastLibProp(11, "fileName", Datum.of(EXTERNAL_CAST_URL));

        assertEquals(List.of(11), manager.loadedCastNums);
    }

    @Test
    void fulfillRequestedExternalCastDataSkipsMatchingUnrequestedSlots() throws Exception {
        RecordingCastLibManager manager = new RecordingCastLibManager();
        CastLib requested = new CastLib(11, null, null);
        requested.setName("empty 9");
        requested.setFileName(EXTERNAL_CAST_URL);
        CastLib unrequested = new CastLib(12, null, null);
        unrequested.setName("empty 10");
        unrequested.setFileName(EXTERNAL_CAST_URL);
        installCastLib(manager, requested);
        installCastLib(manager, unrequested);

        manager.setCastLibProp(11, "fileName", Datum.of(EXTERNAL_CAST_URL));
        manager.loadedCastNums.clear();
        manager.cacheExternalData(EXTERNAL_CAST_URL, new byte[]{4, 5, 6});

        List<Integer> loaded = manager.fulfillRequestedExternalCastData(EXTERNAL_CAST_URL);

        assertEquals(List.of(11), loaded);
        assertEquals(List.of(11), manager.loadedCastNums);
    }

    @Test
    void cacheExternalDataMarksMatchingExternalCastsAsFetched() throws Exception {
        CastLibManager manager = new CastLibManager(null, (castLibNumber, fileName) -> {});
        CastLib castLib = new CastLib(2, null, null);
        castLib.setFileName(EXTERNAL_CAST_URL);
        installCastLib(manager, castLib);

        manager.cacheExternalData(EXTERNAL_CAST_URL, new byte[]{7, 8, 9});

        assertTrue(castLib.isFetched());
    }

    @Test
    void fulfillRequestedExternalCastDataLoadsAuthoredExternalSlotWithoutPendingRuntimeRequest() throws Exception {
        RecordingCastLibManager manager = new RecordingCastLibManager();
        CastLib authored = new CastLib(2, null, new CastListChunk.CastListEntry(
                "External Widget",
                EXTERNAL_CAST_URL,
                2,
                1,
                1,
                0,
                0));
        installCastLib(manager, authored);

        manager.cacheExternalData(EXTERNAL_CAST_URL, new byte[]{7, 8, 9});

        List<Integer> loaded = manager.fulfillRequestedExternalCastData(EXTERNAL_CAST_URL);

        assertEquals(List.of(2), loaded);
        assertEquals(List.of(2), manager.loadedCastNums);
    }

    @Test
    void fulfillRequestedExternalCastDataDoesNotLoadDifferentBasename() throws Exception {
        RecordingCastLibManager manager = new RecordingCastLibManager();
        CastLib authored = new CastLib(2, null, new CastListChunk.CastListEntry(
                "External Widget",
                EXTERNAL_CAST_URL,
                2,
                1,
                1,
                0,
                0));
        installCastLib(manager, authored);

        manager.cacheExternalData(OTHER_EXTERNAL_CAST_URL, new byte[]{7, 8, 9});

        List<Integer> loaded = manager.fulfillRequestedExternalCastData(OTHER_EXTERNAL_CAST_URL);

        assertTrue(loaded.isEmpty());
        assertTrue(manager.loadedCastNums.isEmpty());
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
