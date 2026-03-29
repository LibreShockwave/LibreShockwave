package com.libreshockwave.vm.opcode.dispatch;

import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
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
    void getmemnumLazilyResolvesLoadedMemberNamesBeforePreindexMembers() {
        Datum.PropList registry = new Datum.PropList();
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        CastLibProvider.setProvider(new RegistryLookupProvider(Map.of(
                "Object Base Class", Datum.CastMemberRef.of(2, 74),
                "Core Thread Class", Datum.CastMemberRef.of(2, 75))));
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

    private static final class AliasFieldProvider implements CastLibProvider {
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
            return Datum.VOID;
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
    }

    private static final class RegistryLookupProvider implements CastLibProvider {
        private final Map<String, Datum> refsByName;

        private RegistryLookupProvider(Map<String, Datum> refsByName) {
            this.refsByName = new HashMap<>(refsByName);
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
    }
}
