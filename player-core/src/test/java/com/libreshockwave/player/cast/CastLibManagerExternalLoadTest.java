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
    void setCastLibFileNameTriggersCallbackForRequestedSlotOnly() throws Exception {
        List<String> requestedFiles = new ArrayList<>();
        CastLibManager manager = new CastLibManager(null, (castLibNum, fileName) -> {
            requestedFiles.add(castLibNum + ":" + fileName);
        });
        CastLib requested = new CastLib(11, null, null);
        requested.setName("empty 9");
        CastLib unrelated = new CastLib(12, null, null);
        unrelated.setName("empty 10");
        unrelated.setFileName(EXTERNAL_CAST_URL);
        installCastLib(manager, requested);
        installCastLib(manager, unrelated);

        manager.cacheExternalData(EXTERNAL_CAST_URL, new byte[]{1, 2, 3});

        manager.setCastLibProp(11, "fileName", Datum.of(EXTERNAL_CAST_URL));

        assertEquals(List.of("11:" + EXTERNAL_CAST_URL), requestedFiles);
    }

    @Test
    void getRequestedExternalCastSlotsSkipsMatchingUnrequestedSlots() throws Exception {
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

        List<Integer> slots = manager.getRequestedExternalCastSlots(EXTERNAL_CAST_URL);

        assertEquals(List.of(11), slots);
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
    void getRequestedExternalCastSlotsFindsAuthoredExternalSlotWithoutPendingRuntimeRequest() throws Exception {
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

        List<Integer> slots = manager.getRequestedExternalCastSlots(EXTERNAL_CAST_URL);

        assertEquals(List.of(2), slots);
    }

    @Test
    void getRequestedExternalCastSlotsDoesNotFindDifferentBasename() throws Exception {
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

        List<Integer> slots = manager.getRequestedExternalCastSlots(OTHER_EXTERNAL_CAST_URL);

        assertTrue(slots.isEmpty());
    }

    @Test
    void registryLookupIgnoresRuntimeRetargetedExternalCastSlots() throws Exception {
        CastLibManager manager = new CastLibManager(null, (castLibNumber, fileName) -> {});
        CastLib transientCast = new CastLib(11, null, new CastListChunk.CastListEntry(
                "empty 9",
                "https://example.invalid/dcr/external/empty.cct",
                2,
                1,
                1,
                0,
                0));
        transientCast.setFileName(EXTERNAL_CAST_URL);
        CastMember transientMember = transientCast.createDynamicMember("bitmap");
        transientMember.setName("grunge_barrel_a_0_1_1_0_0");
        installCastLib(manager, transientCast);

        assertEquals(Datum.VOID, manager.getRegistryMemberByName(0, "grunge_barrel_a_0_1_1_0_0"));
        Datum found = manager.getMemberByName(0, "grunge_barrel_a_0_1_1_0_0");
        assertTrue(found instanceof Datum.CastMemberRef);
        Datum.CastMemberRef ref = (Datum.CastMemberRef) found;
        assertEquals(11, ref.castLibNum());
        assertEquals(transientMember.getMemberNumber(), ref.memberNum());
    }

    @Test
    void registryLookupKeepsDirectFileBoundScratchCastHidden() throws Exception {
        CastLibManager manager = new CastLibManager(null, (castLibNumber, fileName) -> {});
        CastLib transientCast = new CastLib(11, null, new CastListChunk.CastListEntry(
                "empty 9",
                "https://example.invalid/dcr/external/empty.cct",
                2,
                1,
                1,
                0,
                0));
        transientCast.setFileName(EXTERNAL_CAST_URL);
        transientCast.setName("https://example.invalid/dcr/external/hh_furni_xx_grunge_barrel.cct");
        CastMember transientMember = transientCast.createDynamicMember("bitmap");
        transientMember.setName("grunge_barrel_a_0_1_1_0_0");
        installCastLib(manager, transientCast);

        assertEquals(Datum.VOID, manager.getRegistryMemberByName(0, "grunge_barrel_a_0_1_1_0_0"));
    }

    @Test
    void registryLookupStillFindsAuthoredExternalCastMembers() throws Exception {
        CastLibManager manager = new CastLibManager(null, (castLibNumber, fileName) -> {});
        CastLib authoredCast = new CastLib(2, null, new CastListChunk.CastListEntry(
                "External Widget",
                EXTERNAL_CAST_URL,
                2,
                1,
                1,
                0,
                0));
        authoredCast.setFileName(EXTERNAL_CAST_URL);
        CastMember authoredMember = authoredCast.createDynamicMember("bitmap");
        authoredMember.setName("Core Thread Class");
        installCastLib(manager, authoredCast);

        Datum found = manager.getRegistryMemberByName(0, "Core Thread Class");
        assertTrue(found instanceof Datum.CastMemberRef);
        Datum.CastMemberRef ref = (Datum.CastMemberRef) found;
        assertEquals(2, ref.castLibNum());
        assertEquals(authoredMember.getMemberNumber(), ref.memberNum());
    }

    @Test
    void registryLookupFindsRetargetedNamedStartupCastMembersInStableNamespace() throws Exception {
        CastLibManager manager = new CastLibManager(null, (castLibNumber, fileName) -> {});
        CastLib startupCast = new CastLib(36, null, new CastListChunk.CastListEntry(
                "empty 34",
                "https://example.invalid/dcr/external/empty.cct",
                2,
                1,
                1,
                0,
                0));
        startupCast.setFileName("https://example.invalid/dcr/external/hh_interface.cct");
        startupCast.setName("hh_interface");
        CastMember startupMember = startupCast.createDynamicMember("bitmap");
        startupMember.setName("Interface Root Class");
        installCastLib(manager, startupCast);

        Datum found = manager.getRegistryMemberByName(0, "Interface Root Class");
        assertTrue(found instanceof Datum.CastMemberRef);
        Datum.CastMemberRef ref = (Datum.CastMemberRef) found;
        assertEquals(36, ref.castLibNum());
        assertEquals(startupMember.getMemberNumber(), ref.memberNum());
    }

    @Test
    void broadMemberLookupPrefersStableNamespaceOverScratchCastCollisions() throws Exception {
        CastLibManager manager = new CastLibManager(null, (castLibNumber, fileName) -> {});

        CastLib scratchCast = new CastLib(11, null, new CastListChunk.CastListEntry(
                "empty 9",
                "https://example.invalid/dcr/external/empty.cct",
                2,
                1,
                1,
                0,
                0));
        scratchCast.setFileName(EXTERNAL_CAST_URL);
        CastMember scratchMember = scratchCast.createDynamicMember("bitmap");
        scratchMember.setName("shared_preview_piece");
        installCastLib(manager, scratchCast);

        CastLib stableCast = new CastLib(36, null, new CastListChunk.CastListEntry(
                "empty 34",
                "https://example.invalid/dcr/external/empty.cct",
                2,
                1,
                1,
                0,
                0));
        stableCast.setFileName("https://example.invalid/dcr/external/hh_interface.cct");
        stableCast.setName("hh_interface");
        CastMember stableMember = stableCast.createDynamicMember("bitmap");
        stableMember.setName("shared_preview_piece");
        installCastLib(manager, stableCast);

        Datum found = manager.getMemberByName(0, "shared_preview_piece");

        assertTrue(found instanceof Datum.CastMemberRef);
        Datum.CastMemberRef ref = (Datum.CastMemberRef) found;
        assertEquals(36, ref.castLibNum());
        assertEquals(stableMember.getMemberNumber(), ref.memberNum());
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
