package com.libreshockwave.vm.builtin.sprite;

import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.*;

class SpriteEventBrokerSupportTest {

    @Test
    void registerProcedureCreatesSyntheticBrokerForBareSpriteChannel() {
        RecordingSpriteProvider provider = new RecordingSpriteProvider();
        SpritePropertyProvider.setProvider(provider);
        try {
            Datum result = SpriteEventBrokerSupport.dispatchSpriteMethod(
                    42,
                    "registerProcedure",
                    List.of(Datum.symbol("eventProcRoom"), Datum.symbol("room_interface"), Datum.symbol("mouseDown")));

            assertTrue(result.isTruthy());

            List<Datum> scriptInstances = provider.getScriptInstanceList(42);
            assertNotNull(scriptInstances);
            assertEquals(1, scriptInstances.size());
            assertInstanceOf(Datum.ScriptInstance.class, scriptInstances.get(0));

            Datum.ScriptInstance broker = (Datum.ScriptInstance) scriptInstances.get(0);
            assertEquals(42, broker.properties().get("spritenum").toInt());
            assertTrue(broker.properties().get(SpriteEventBrokerSupport.SYNTHETIC_BROKER_FLAG).isTruthy());

            Datum procListDatum = broker.properties().get("pProcList");
            assertInstanceOf(Datum.PropList.class, procListDatum);
            Datum.PropList procList = (Datum.PropList) procListDatum;

            Datum mouseDownEntry = procList.get("mouseDown");
            assertInstanceOf(Datum.List.class, mouseDownEntry);
            Datum.List entry = (Datum.List) mouseDownEntry;
            assertEquals("eventProcRoom", entry.items().get(0).toKeyName());
            assertEquals("room_interface", entry.items().get(1).toKeyName());
        } finally {
            SpritePropertyProvider.clearProvider();
        }
    }

    @Test
    void registerProcedureVoidMethodAndEventExpandsToPerEventCallbacks() {
        RecordingSpriteProvider provider = new RecordingSpriteProvider();
        SpritePropertyProvider.setProvider(provider);
        try {
            Datum clientId = Datum.symbol("login_a");
            Datum result = SpriteEventBrokerSupport.dispatchSpriteMethod(
                    7,
                    "registerProcedure",
                    List.of(Datum.VOID, clientId, Datum.VOID));

            assertTrue(result.isTruthy());

            List<Datum> scriptInstances = provider.getScriptInstanceList(7);
            assertNotNull(scriptInstances);
            assertEquals(1, scriptInstances.size());
            assertInstanceOf(Datum.ScriptInstance.class, scriptInstances.get(0));

            Datum.ScriptInstance broker = (Datum.ScriptInstance) scriptInstances.get(0);
            Datum procListDatum = broker.properties().get("pProcList");
            assertInstanceOf(Datum.PropList.class, procListDatum);
            Datum.PropList procList = (Datum.PropList) procListDatum;

            assertProcEntry(procList, "mouseEnter", "mouseEnter", clientId);
            assertProcEntry(procList, "mouseLeave", "mouseLeave", clientId);
            assertProcEntry(procList, "mouseWithin", "mouseWithin", clientId);
            assertProcEntry(procList, "mouseDown", "mouseDown", clientId);
            assertProcEntry(procList, "mouseUp", "mouseUp", clientId);
            assertProcEntry(procList, "mouseUpOutSide", "mouseUpOutSide", clientId);
            assertProcEntry(procList, "keyDown", "keyDown", clientId);
            assertProcEntry(procList, "keyUp", "keyUp", clientId);
        } finally {
            SpritePropertyProvider.clearProvider();
        }
    }

    @Test
    void setMemberDispatchUsesSpriteMemberMethodPath() {
        RecordingSpriteProvider provider = new RecordingSpriteProvider();
        SpritePropertyProvider.setProvider(provider);
        try {
            Datum memberRef = Datum.CastMemberRef.of(3, 41);

            Datum result = SpriteEventBrokerSupport.dispatchSpriteMethod(
                    19,
                    "setMember",
                    List.of(memberRef));

            assertTrue(result.isTruthy());
            assertEquals(19, provider.lastSetMemberSpriteNum);
            assertSame(memberRef, provider.lastSetMemberValue);
        } finally {
            SpritePropertyProvider.clearProvider();
        }
    }

    private static void assertProcEntry(Datum.PropList procList, String eventKey, String methodName, Datum clientId) {
        Datum entryDatum = procList.get(eventKey);
        assertInstanceOf(Datum.List.class, entryDatum);
        Datum.List entry = (Datum.List) entryDatum;
        assertEquals(2, entry.items().size());
        assertInstanceOf(Datum.Symbol.class, entry.items().get(0));
        assertEquals(methodName, entry.items().get(0).toKeyName());
        assertEquals(clientId.toKeyName(), entry.items().get(1).toKeyName());
    }

    private static final class RecordingSpriteProvider implements SpritePropertyProvider {
        private final Map<Integer, Map<String, Datum>> spriteProps = new HashMap<>();
        private int lastSetMemberSpriteNum = -1;
        private Datum lastSetMemberValue = Datum.VOID;

        @Override
        public Datum getSpriteProp(int spriteNum, String propName) {
            return spriteProps.getOrDefault(spriteNum, Map.of()).getOrDefault(propName.toLowerCase(), Datum.VOID);
        }

        @Override
        public boolean setSpriteProp(int spriteNum, String propName, Datum value) {
            spriteProps.computeIfAbsent(spriteNum, ignored -> new HashMap<>())
                    .put(propName.toLowerCase(), value);
            return true;
        }

        @Override
        public boolean setSpriteMember(int spriteNum, Datum value) {
            lastSetMemberSpriteNum = spriteNum;
            lastSetMemberValue = value;
            return true;
        }

        @Override
        public List<Datum> getScriptInstanceList(int spriteNum) {
            Datum datum = getSpriteProp(spriteNum, "scriptinstancelist");
            if (datum instanceof Datum.List list) {
                return list.items();
            }
            return null;
        }
    }
}
