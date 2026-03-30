package com.libreshockwave.vm.opcode.dispatch;

import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class MemberRegistryMethodDispatcherTest {

    @Test
    void readAliasIndexesFromFieldImportsAliasesIntoMemberRegistry() {
        Datum.PropList registry = new Datum.PropList();
        registry.putTyped("hcc_stool_a_0_1_1_0_0", false, Datum.of(0x0B0004));
        registry.putTyped("hcc_stool_small", false, Datum.of(0x0B0003));
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        CastLibProvider.setProvider(new AliasFieldProvider(
                "hcc_stool_a_0_1_1_2_0=hcc_stool_a_0_1_1_0_0\r\n"
                        + "hcc_stool_mirror=hcc_stool_a_0_1_1_0_0*\r\n"
                        + "broken=missing\r\n"));
        try {
            MemberRegistryMethodDispatcher.DispatchResult result =
                    MemberRegistryMethodDispatcher.dispatch(instance, "readAliasIndexesFromField",
                            List.of(Datum.of("memberalias.index"), Datum.of(11)));

            assertTrue(result.handled());
            assertEquals(2, result.value().toInt());
            assertEquals(0x0B0004, registry.get("hcc_stool_a_0_1_1_2_0").toInt());
            assertEquals(-0x0B0004, registry.get("hcc_stool_mirror").toInt());
        } finally {
            CastLibProvider.clearProvider();
            MemberRegistryMethodDispatcher.clearRememberedAliases();
        }
    }

    @Test
    void reapplyPersistentAliasesRestoresAliasesAfterTemporaryCastReset() {
        Datum.PropList registry = new Datum.PropList();
        registry.putTyped("grunge_barrel_a_0_1_1_0_0", false, Datum.of(0x0C0007));
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        CastLibProvider.setProvider(new AliasFieldProvider(
                "grunge_barrel_a_0_1_1_2_0=grunge_barrel_a_0_1_1_0_0\r\n"
                        + "grunge_barrel_mirror=grunge_barrel_a_0_1_1_0_0*\r\n"));
        try {
            MemberRegistryMethodDispatcher.dispatch(instance, "readAliasIndexesFromField",
                    List.of(Datum.of("memberalias.index"), Datum.of(11)));

            registry.remove("grunge_barrel_a_0_1_1_2_0");
            registry.remove("grunge_barrel_mirror");

            int restored = MemberRegistryMethodDispatcher.reapplyPersistentAliases(11);
            assertEquals(2, restored);
            assertEquals(0x0C0007, registry.get("grunge_barrel_a_0_1_1_2_0").toInt());
            assertEquals(-0x0C0007, registry.get("grunge_barrel_mirror").toInt());
        } finally {
            CastLibProvider.clearProvider();
            MemberRegistryMethodDispatcher.clearRememberedAliases();
        }
    }

    @Test
    void getmemnumLazilyRestoresRememberedAliasesAfterRegistryDeletion() {
        Datum.PropList registry = new Datum.PropList();
        registry.putTyped("sofachair_silo_a_0_1_1_0_0", false, Datum.of(0x0C0007));
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        CastLibProvider.setProvider(new AliasFieldProvider(
                "sofachair_silo_a_0_1_1_2_0=sofachair_silo_a_0_1_1_0_0*\r\n"));
        try {
            MemberRegistryMethodDispatcher.dispatch(instance, "readAliasIndexesFromField",
                    List.of(Datum.of("memberalias.index"), Datum.of(11)));

            registry.remove("sofachair_silo_a_0_1_1_2_0");

            MemberRegistryMethodDispatcher.DispatchResult memNumResult =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "getmemnum",
                            List.of(Datum.of("sofachair_silo_a_0_1_1_2_0")));
            assertTrue(memNumResult.handled());
            assertEquals(-0x0C0007, memNumResult.value().toInt());
            assertEquals(-0x0C0007, registry.get("sofachair_silo_a_0_1_1_2_0").toInt());

            MemberRegistryMethodDispatcher.DispatchResult existsResult =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "memberExists",
                            List.of(Datum.of("sofachair_silo_a_0_1_1_2_0")));
            assertTrue(existsResult.handled());
            assertEquals(1, existsResult.value().toInt());

            MemberRegistryMethodDispatcher.DispatchResult memberResult =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "getmember",
                            List.of(Datum.of("sofachair_silo_a_0_1_1_2_0")));
            assertTrue(memberResult.handled());
            Datum.CastMemberRef ref = assertInstanceOf(Datum.CastMemberRef.class, memberResult.value());
            assertEquals(12, ref.castLibNum());
            assertEquals(7, ref.memberNum());
        } finally {
            CastLibProvider.clearProvider();
            MemberRegistryMethodDispatcher.clearRememberedAliases();
        }
    }

    @Test
    void readAliasIndexesFromFieldBootstrapsStableTargetsThatWereNotPreindexedYet() {
        Datum.PropList registry = new Datum.PropList();
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        CastLibProvider.setProvider(new AliasFieldRegistryProvider(
                "hcc_stool_alias=hcc_stool_a_0_1_1_0_0\r\n",
                Map.of("hcc_stool_a_0_1_1_0_0", Datum.CastMemberRef.of(11, 4))));
        try {
            MemberRegistryMethodDispatcher.DispatchResult result =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "readAliasIndexesFromField",
                            List.of(Datum.of("memberalias.index"), Datum.of(11)));

            assertTrue(result.handled());
            assertEquals(1, result.value().toInt());
            assertEquals((11 << 16) | 4, registry.get("hcc_stool_a_0_1_1_0_0").toInt());
            assertEquals((11 << 16) | 4, registry.get("hcc_stool_alias").toInt());
        } finally {
            CastLibProvider.clearProvider();
            MemberRegistryMethodDispatcher.clearRememberedAliases();
        }
    }

    @Test
    void reapplyAllPersistentAliasesRestoresAliasesWhenTargetsAppearLater() {
        Datum.PropList registry = new Datum.PropList();
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        CastLibProvider.setProvider(new AliasFieldProvider(
                "grunge_barrel_alias=grunge_barrel_a_0_1_1_0_0\r\n"));
        try {
            MemberRegistryMethodDispatcher.dispatch(
                    instance,
                    "readAliasIndexesFromField",
                    List.of(Datum.of("memberalias.index"), Datum.of(11)));

            CastLibProvider.setProvider(new RegistryLookupProvider(
                    Map.of(),
                    Map.of("grunge_barrel_a_0_1_1_0_0", Datum.CastMemberRef.of(11, 7))));

            int restored = MemberRegistryMethodDispatcher.reapplyAllPersistentAliases();
            assertEquals(1, restored);
            assertEquals((11 << 16) | 7, registry.get("grunge_barrel_a_0_1_1_0_0").toInt());
            assertEquals((11 << 16) | 7, registry.get("grunge_barrel_alias").toInt());
        } finally {
            CastLibProvider.clearProvider();
            MemberRegistryMethodDispatcher.clearRememberedAliases();
        }
    }

    @Test
    void readAliasIndexesFromFieldPublishesTransientRoomAliasesWithoutLeakingRawTargets() {
        Datum.PropList registry = new Datum.PropList();
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        int transientSlot = (11 << 16) | 7;
        CastLibProvider.setProvider(new TransientAliasSourceProvider(
                12,
                11,
                "room_bar.window=room_bar_window_a_0_0_0_0\r\n",
                Map.of("room_bar_window_a_0_0_0_0", Datum.CastMemberRef.of(11, 7)),
                Set.of()));
        try {
            MemberRegistryMethodDispatcher.DispatchResult imported =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "readAliasIndexesFromField",
                            List.of(Datum.of("memberalias.index"), Datum.of(11)));

            assertTrue(imported.handled());
            assertEquals(1, imported.value().toInt());
            assertEquals(transientSlot, registry.get("room_bar.window").toInt());
            assertNull(registry.get("room_bar_window_a_0_0_0_0"));

            MemberRegistryMethodDispatcher.DispatchResult exists =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "memberExists",
                            List.of(Datum.of("room_bar.window")));

            assertTrue(exists.handled());
            assertEquals(1, exists.value().toInt());
            assertEquals(transientSlot, registry.get("room_bar.window").toInt());
            assertNull(registry.get("room_bar_window_a_0_0_0_0"));
        } finally {
            CastLibProvider.clearProvider();
            MemberRegistryMethodDispatcher.clearRememberedAliases();
        }
    }

    @Test
    void getmemnumLazilyDiscoversTransientMemberAliasIndexesFromLoadedCast() {
        Datum.PropList registry = new Datum.PropList();
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        int transientSlot = (11 << 16) | 7;
        CastLibProvider.setProvider(new TransientAliasSourceProvider(
                12,
                11,
                "room_bar.window=room_bar_window_a_0_0_0_0\r\n",
                Map.of("room_bar_window_a_0_0_0_0", Datum.CastMemberRef.of(11, 7)),
                Set.of()));
        try {
            MemberRegistryMethodDispatcher.DispatchResult result =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "getmemnum",
                            List.of(Datum.of("room_bar.window")));

            assertTrue(result.handled());
            assertEquals(transientSlot, result.value().toInt());
            assertEquals(transientSlot, registry.get("room_bar.window").toInt());
            assertNull(registry.get("room_bar_window_a_0_0_0_0"));
        } finally {
            CastLibProvider.clearProvider();
            MemberRegistryMethodDispatcher.clearRememberedAliases();
        }
    }

    @Test
    void getmemnumLazilyResolvesLoadedMemberNamesBeforePreindexMembers() {
        Datum.PropList registry = new Datum.PropList();
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        CastLibProvider.setProvider(new RegistryLookupProvider(
                Map.of(
                        "Object Base Class", Datum.CastMemberRef.of(2, 74),
                        "Core Thread Class", Datum.CastMemberRef.of(2, 75)),
                Map.of(),
                Map.of(
                        "Object Base Class", "script",
                        "Core Thread Class", "script")));
        try {
            MemberRegistryMethodDispatcher.DispatchResult result =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "getmemnum",
                            List.of(Datum.of("Object Base Class")));

            assertTrue(result.handled());
            assertEquals((2 << 16) | 74, result.value().toInt());
            assertEquals((2 << 16) | 74, registry.get("Object Base Class").toInt());

            MemberRegistryMethodDispatcher.DispatchResult existsResult =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "exists",
                            List.of(Datum.of("Core Thread Class")));
            assertTrue(existsResult.handled());
            assertEquals(1, existsResult.value().toInt());

            MemberRegistryMethodDispatcher.DispatchResult memberResult =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "getmember",
                            List.of(Datum.of("Core Thread Class")));
            assertTrue(memberResult.handled());
            Datum.CastMemberRef ref = assertInstanceOf(Datum.CastMemberRef.class, memberResult.value());
            assertEquals(2, ref.castLibNum());
            assertEquals(75, ref.memberNum());
        } finally {
            CastLibProvider.clearProvider();
            MemberRegistryMethodDispatcher.clearRememberedAliases();
        }
    }

    @Test
    void getmemnumBootstrapsFieldDefinitionsFromBroadLookupWhileRegistryIsEmpty() {
        Datum.PropList registry = new Datum.PropList();
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        CastLibProvider.setProvider(new RegistryLookupProvider(
                Map.of("entry.visual", Datum.CastMemberRef.of(34, 176)),
                Map.of(),
                Map.of("entry.visual", "field")));
        try {
            MemberRegistryMethodDispatcher.DispatchResult result =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "getmemnum",
                            List.of(Datum.of("entry.visual")));

            assertTrue(result.handled());
            assertEquals((34 << 16) | 176, result.value().toInt());
            assertEquals((34 << 16) | 176, registry.get("entry.visual").toInt());
        } finally {
            CastLibProvider.clearProvider();
            MemberRegistryMethodDispatcher.clearRememberedAliases();
        }
    }

    @Test
    void getmemnumBootstrapsFieldDefinitionsEvenAfterRegistryHasExistingEntries() {
        Datum.PropList registry = new Datum.PropList();
        registry.putTyped("Object Base Class", false, Datum.of((2 << 16) | 74));
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        CastLibProvider.setProvider(new RegistryLookupProvider(
                Map.of("entry.visual", Datum.CastMemberRef.of(34, 176)),
                Map.of(),
                Map.of("entry.visual", "field")));
        try {
            MemberRegistryMethodDispatcher.DispatchResult result =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "getmemnum",
                            List.of(Datum.of("entry.visual")));

            assertTrue(result.handled());
            assertEquals((34 << 16) | 176, result.value().toInt());
            assertEquals((34 << 16) | 176, registry.get("entry.visual").toInt());
        } finally {
            CastLibProvider.clearProvider();
            MemberRegistryMethodDispatcher.clearRememberedAliases();
        }
    }

    @Test
    void getmemnumDoesNotBootstrapNonScriptMembersFromBroadLookup() {
        Datum.PropList registry = new Datum.PropList();
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        CastLibProvider.setProvider(new RegistryLookupProvider(
                Map.of("grunge_barrel_a_0_1_1_0_0", Datum.CastMemberRef.of(11, 7)),
                Map.of(),
                Map.of("grunge_barrel_a_0_1_1_0_0", "bitmap")));
        try {
            MemberRegistryMethodDispatcher.DispatchResult result =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "getmemnum",
                            List.of(Datum.of("grunge_barrel_a_0_1_1_0_0")));

            assertTrue(result.handled());
            assertEquals(0, result.value().toInt());
            assertNull(registry.get("grunge_barrel_a_0_1_1_0_0"));
        } finally {
            CastLibProvider.clearProvider();
            MemberRegistryMethodDispatcher.clearRememberedAliases();
        }
    }

    @Test
    void prefillSeedsStableVisibleMembersWithoutHandling() {
        Datum.PropList registry = new Datum.PropList();
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        CastLibProvider.setProvider(new RegistryLookupProvider(Map.of(
                "Object Base Class", Datum.CastMemberRef.of(2, 74))));
        try {
            MemberRegistryMethodDispatcher.DispatchResult resolved =
                    MemberRegistryMethodDispatcher.prefill(
                            instance,
                            "getmemnum",
                            List.of(Datum.of("Object Base Class")));
            assertEquals(MemberRegistryMethodDispatcher.NOT_HANDLED, resolved);
            assertEquals((2 << 16) | 74, registry.get("Object Base Class").toInt());

            MemberRegistryMethodDispatcher.DispatchResult unresolved =
                    MemberRegistryMethodDispatcher.prefill(
                            instance,
                            "getmemnum",
                            List.of(Datum.of("Missing Class")));
            assertEquals(MemberRegistryMethodDispatcher.NOT_HANDLED, unresolved);
        } finally {
            CastLibProvider.clearProvider();
            MemberRegistryMethodDispatcher.clearRememberedAliases();
        }
    }

    @Test
    void getmemnumIgnoresTransientRuntimeRetargetedCastMembers() {
        Datum.PropList registry = new Datum.PropList();
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        CastLibProvider.setProvider(new RegistryLookupProvider(
                Map.of("grunge_barrel_a_0_1_1_0_0", Datum.CastMemberRef.of(11, 7)),
                Map.of()));
        try {
            MemberRegistryMethodDispatcher.DispatchResult result =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "getmemnum",
                            List.of(Datum.of("grunge_barrel_a_0_1_1_0_0")));

            assertTrue(result.handled());
            assertEquals(0, result.value().toInt());
            assertEquals(null, registry.get("grunge_barrel_a_0_1_1_0_0"));
        } finally {
            CastLibProvider.clearProvider();
            MemberRegistryMethodDispatcher.clearRememberedAliases();
        }
    }

    @Test
    void getmemnumKeepsExplicitlyIndexedStableRetargetedSlotsWhileSourceCastIsLive() {
        Datum.PropList registry = new Datum.PropList();
        registry.putTyped("grunge_barrel_a_0_1_1_0_0", false, Datum.of((11 << 16) | 7));
        registry.putTyped("grunge_barrel_a_0_1_1_2_0", false, Datum.of(-((11 << 16) | 7)));
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        CastLibProvider.setProvider(new VisibilityAwareProvider(
                Set.of((11 << 16) | 7),
                Set.of(),
                Set.of((11 << 16) | 7)));
        try {
            MemberRegistryMethodDispatcher.DispatchResult result =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "getmemnum",
                            List.of(Datum.of("grunge_barrel_a_0_1_1_0_0")));

            assertTrue(result.handled());
            assertEquals((11 << 16) | 7, result.value().toInt());
            assertEquals((11 << 16) | 7, registry.get("grunge_barrel_a_0_1_1_0_0").toInt());

            MemberRegistryMethodDispatcher.DispatchResult aliasResult =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "memberExists",
                            List.of(Datum.of("grunge_barrel_a_0_1_1_2_0")));

            assertTrue(aliasResult.handled());
            assertEquals(1, aliasResult.value().toInt());
            assertEquals(-((11 << 16) | 7), registry.get("grunge_barrel_a_0_1_1_2_0").toInt());
        } finally {
            CastLibProvider.clearProvider();
            MemberRegistryMethodDispatcher.clearRememberedAliases();
        }
    }

    @Test
    void getmemnumHidesExplicitlyIndexedScratchSlotsWhileSourceCastIsLive() {
        Datum.PropList registry = new Datum.PropList();
        registry.putTyped("grunge_barrel_a_0_1_1_0_0", false, Datum.of((11 << 16) | 7));
        registry.putTyped("grunge_barrel_a_0_1_1_2_0", false, Datum.of(-((11 << 16) | 7)));
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        CastLibProvider.setProvider(new VisibilityAwareProvider(
                Set.of(),
                Set.of((11 << 16) | 7),
                Set.of((11 << 16) | 7)));
        try {
            MemberRegistryMethodDispatcher.DispatchResult result =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "getmemnum",
                            List.of(Datum.of("grunge_barrel_a_0_1_1_0_0")));

            assertTrue(result.handled());
            assertEquals(0, result.value().toInt());
            assertNull(registry.get("grunge_barrel_a_0_1_1_0_0"));

            MemberRegistryMethodDispatcher.DispatchResult aliasResult =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "memberExists",
                            List.of(Datum.of("grunge_barrel_a_0_1_1_2_0")));

            assertTrue(aliasResult.handled());
            assertEquals(0, aliasResult.value().toInt());
            assertNull(registry.get("grunge_barrel_a_0_1_1_2_0"));
        } finally {
            CastLibProvider.clearProvider();
            MemberRegistryMethodDispatcher.clearRememberedAliases();
        }
    }

    @Test
    void getmemnumRemovesIndexedSlotsOnceTheirTransientCastHasReset() {
        Datum.PropList registry = new Datum.PropList();
        registry.putTyped("grunge_barrel_a_0_1_1_0_0", false, Datum.of((11 << 16) | 7));
        registry.putTyped("grunge_barrel_a_0_1_1_2_0", false, Datum.of(-((11 << 16) | 7)));
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        CastLibProvider.setProvider(new VisibilityAwareProvider(Set.of(), Set.of(), Set.of()));
        try {
            MemberRegistryMethodDispatcher.DispatchResult result =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "getmemnum",
                            List.of(Datum.of("grunge_barrel_a_0_1_1_0_0")));

            assertTrue(result.handled());
            assertEquals(0, result.value().toInt());
            assertNull(registry.get("grunge_barrel_a_0_1_1_0_0"));

            MemberRegistryMethodDispatcher.DispatchResult aliasResult =
                    MemberRegistryMethodDispatcher.dispatch(
                            instance,
                            "memberExists",
                            List.of(Datum.of("grunge_barrel_a_0_1_1_2_0")));

            assertTrue(aliasResult.handled());
            assertEquals(0, aliasResult.value().toInt());
            assertNull(registry.get("grunge_barrel_a_0_1_1_2_0"));
        } finally {
            CastLibProvider.clearProvider();
            MemberRegistryMethodDispatcher.clearRememberedAliases();
        }
    }

    private static class AliasFieldProvider implements CastLibProvider {
        private final String fieldText;

        private AliasFieldProvider(String fieldText) {
            this.fieldText = fieldText;
        }

        @Override
        public int getCastLibByNumber(int castLibNumber) {
            return castLibNumber;
        }

        @Override
        public int getCastLibByName(String name) {
            return 0;
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
        public Datum getFieldDatum(Object memberNameOrNum, int castId) {
            return new Datum.FieldText(fieldText, castId, 2);
        }

        @Override
        public boolean memberExists(int castLibNumber, int memberNumber) {
            return castLibNumber > 0 && memberNumber > 0;
        }
    }

    private static final class AliasFieldRegistryProvider extends AliasFieldProvider {
        private final Map<String, Datum> registryRefsByName;

        private AliasFieldRegistryProvider(String fieldText, Map<String, Datum> registryRefsByName) {
            super(fieldText);
            this.registryRefsByName = new HashMap<>(registryRefsByName);
        }

        @Override
        public Datum getRegistryMemberByName(int castLibNumber, String memberName) {
            return registryRefsByName.getOrDefault(memberName, Datum.VOID);
        }
    }

    private static final class TransientAliasSourceProvider extends AliasFieldProvider {
        private final int castLibCount;
        private final int aliasCastLibNumber;
        private final Map<String, Datum> refsByName;
        private final Set<Integer> registryVisibleSlots;
        private final Set<Integer> liveSlots;

        private TransientAliasSourceProvider(
                int castLibCount,
                int aliasCastLibNumber,
                String fieldText,
                Map<String, Datum> refsByName,
                Set<Integer> registryVisibleSlots) {
            super(fieldText);
            this.castLibCount = castLibCount;
            this.aliasCastLibNumber = aliasCastLibNumber;
            this.refsByName = new HashMap<>(refsByName);
            this.registryVisibleSlots = registryVisibleSlots;
            this.liveSlots = new java.util.HashSet<>();
            this.liveSlots.add((aliasCastLibNumber << 16) | 2);
            for (Datum ref : refsByName.values()) {
                if (ref instanceof Datum.CastMemberRef cmr) {
                    this.liveSlots.add((cmr.castLibNum() << 16) | cmr.memberNum());
                }
            }
        }

        @Override
        public Datum getMemberByName(int castLibNumber, String memberName) {
            if (castLibNumber != aliasCastLibNumber) {
                return Datum.VOID;
            }
            if ("memberalias.index".equalsIgnoreCase(memberName)) {
                return Datum.CastMemberRef.of(aliasCastLibNumber, 2);
            }
            return refsByName.getOrDefault(memberName, Datum.VOID);
        }

        @Override
        public Datum getFieldDatum(Object memberNameOrNum, int castId) {
            if (castId == aliasCastLibNumber
                    && memberNameOrNum instanceof String memberName
                    && "memberalias.index".equalsIgnoreCase(memberName)) {
                return super.getFieldDatum(memberNameOrNum, castId);
            }
            return Datum.EMPTY_STRING;
        }

        @Override
        public Datum getRegistryMemberByName(int castLibNumber, String memberName) {
            Datum ref = refsByName.get(memberName);
            if (ref instanceof Datum.CastMemberRef cmr
                    && registryVisibleSlots.contains((cmr.castLibNum() << 16) | cmr.memberNum())) {
                return ref;
            }
            return Datum.VOID;
        }

        @Override
        public int getCastLibCount() {
            return castLibCount;
        }

        @Override
        public boolean isRegistryVisibleMember(int castLibNumber, int memberNumber) {
            return registryVisibleSlots.contains((castLibNumber << 16) | memberNumber);
        }

        @Override
        public boolean memberExists(int castLibNumber, int memberNumber) {
            return liveSlots.contains((castLibNumber << 16) | memberNumber);
        }
    }

    private static final class RegistryLookupProvider implements CastLibProvider {
        private final Map<String, Datum> refsByName;
        private final Map<String, Datum> registryRefsByName;
        private final Map<Integer, Datum> typeBySlot;

        private RegistryLookupProvider(Map<String, Datum> refsByName) {
            this(refsByName, refsByName, Map.of());
        }

        private RegistryLookupProvider(Map<String, Datum> refsByName, Map<String, Datum> registryRefsByName) {
            this(refsByName, registryRefsByName, Map.of());
        }

        private RegistryLookupProvider(
                Map<String, Datum> refsByName,
                Map<String, Datum> registryRefsByName,
                Map<String, String> typesByName) {
            this.refsByName = new HashMap<>(refsByName);
            this.registryRefsByName = new HashMap<>(registryRefsByName);
            this.typeBySlot = new HashMap<>();
            for (var entry : typesByName.entrySet()) {
                Datum ref = this.refsByName.get(entry.getKey());
                if (!(ref instanceof Datum.CastMemberRef cmr)) {
                    ref = this.registryRefsByName.get(entry.getKey());
                }
                if (ref instanceof Datum.CastMemberRef cmr) {
                    int slotValue = (cmr.castLibNum() << 16) | cmr.memberNum();
                    this.typeBySlot.put(slotValue, Datum.symbol(entry.getValue()));
                }
            }
        }

        @Override
        public int getCastLibByNumber(int castLibNumber) {
            return castLibNumber;
        }

        @Override
        public int getCastLibByName(String name) {
            return 0;
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
            return refsByName.getOrDefault(memberName, Datum.VOID);
        }

        @Override
        public Datum getRegistryMemberByName(int castLibNumber, String memberName) {
            return registryRefsByName.getOrDefault(memberName, Datum.VOID);
        }

        @Override
        public int getCastLibCount() {
            return 0;
        }

        @Override
        public Datum getMemberProp(int castLibNumber, int memberNumber, String propName) {
            if ("type".equalsIgnoreCase(propName)) {
                return typeBySlot.getOrDefault((castLibNumber << 16) | memberNumber, Datum.VOID);
            }
            return Datum.VOID;
        }

        @Override
        public boolean setMemberProp(int castLibNumber, int memberNumber, String propName, Datum value) {
            return false;
        }

        @Override
        public boolean isRegistryVisibleMember(int castLibNumber, int memberNumber) {
            return memberExists(castLibNumber, memberNumber);
        }

        @Override
        public boolean memberExists(int castLibNumber, int memberNumber) {
            for (Datum ref : refsByName.values()) {
                if (ref instanceof Datum.CastMemberRef cmr
                        && cmr.castLibNum() == castLibNumber
                        && cmr.memberNum() == memberNumber) {
                    return true;
                }
            }
            for (Datum ref : registryRefsByName.values()) {
                if (ref instanceof Datum.CastMemberRef cmr
                        && cmr.castLibNum() == castLibNumber
                        && cmr.memberNum() == memberNumber) {
                    return true;
                }
            }
            return false;
        }
    }

    private static final class VisibilityAwareProvider implements CastLibProvider {
        private final Set<Integer> visibleSlots;
        private final Set<Integer> hiddenSlots;
        private final Set<Integer> liveSlots;

        private VisibilityAwareProvider(Set<Integer> visibleSlots, Set<Integer> hiddenSlots, Set<Integer> liveSlots) {
            this.visibleSlots = visibleSlots;
            this.hiddenSlots = hiddenSlots;
            this.liveSlots = liveSlots;
        }

        @Override
        public int getCastLibByNumber(int castLibNumber) {
            return castLibNumber;
        }

        @Override
        public int getCastLibByName(String name) {
            return 0;
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
        public boolean isRegistryVisibleMember(int castLibNumber, int memberNumber) {
            int slotValue = (castLibNumber << 16) | memberNumber;
            if (!liveSlots.contains(slotValue)) {
                return false;
            }
            if (hiddenSlots.contains(slotValue)) {
                return false;
            }
            return visibleSlots.isEmpty() || visibleSlots.contains(slotValue);
        }

        @Override
        public boolean memberExists(int castLibNumber, int memberNumber) {
            int slotValue = (castLibNumber << 16) | memberNumber;
            return liveSlots.contains(slotValue);
        }
    }
}
