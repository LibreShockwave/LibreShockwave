package com.libreshockwave.player.cast;

import com.libreshockwave.cast.MemberType;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Field;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

class CastLibManagerFieldAccessTest {

    @Test
    void getFieldDatumReturnsMemberBackedFieldTextForNamedLookup() throws Exception {
        CastLibManager manager = new CastLibManager(null, (castLibNumber, fileName) -> {});
        CastLib castLib = new CastLib(3, null, null);
        installCastLib(manager, castLib);

        CastMember field = castLib.createDynamicMember("text");
        field.setName("memberalias.index");
        field.setDynamicText("alpha");

        Datum result = manager.getFieldDatum("memberalias.index", 3);

        Datum.FieldText fieldText = assertInstanceOf(Datum.FieldText.class, result);
        assertEquals("alpha", fieldText.toStr());
        assertEquals(3, fieldText.castLibNum());
        assertEquals(field.getMemberNumber(), fieldText.memberNum());
    }

    @Test
    void getFieldParsedValueUsesFieldMemberCache() throws Exception {
        CastLibManager manager = new CastLibManager(null, (castLibNumber, fileName) -> {});
        CastLib castLib = new CastLib(5, null, null);
        installCastLib(manager, castLib);

        CastMember field = castLib.createDynamicMember("text");
        field.setName("widget.props");
        field.setDynamicText("[#foo: 7]");

        LingoVM vm = new LingoVM(null);
        Datum first = manager.getFieldParsedValue(5, field.getMemberNumber(), vm);
        Datum second = manager.getFieldParsedValue(5, field.getMemberNumber(), vm);

        assertInstanceOf(Datum.PropList.class, first);
        assertSame(first, second);
        assertEquals(7, ((Datum.PropList) first).get("foo").toInt());
    }

    @Test
    void setFieldValueUpdatesResolvedFieldMemberByEncodedSlot() throws Exception {
        CastLibManager manager = new CastLibManager(null, (castLibNumber, fileName) -> {});
        CastLib castLib = new CastLib(7, null, null);
        installCastLib(manager, castLib);

        CastMember field = castLib.createDynamicMember("text");
        assertEquals(MemberType.TEXT, field.getMemberType());
        field.setName("room_status");
        field.setDynamicText("before");

        manager.setFieldValue(field.getSlotNumber(), 0, "after");

        assertEquals("after", manager.getFieldValue(field.getSlotNumber(), 0));
        Datum result = manager.getFieldDatum(field.getSlotNumber(), 0);
        assertTrue(result instanceof Datum.FieldText);
        assertEquals("after", result.toStr());
    }

    @Test
    void createNamedMemberReturnsEncodedSlotAndPreservesLookupName() throws Exception {
        CastLibManager manager = new CastLibManager(null, (castLibNumber, fileName) -> {});
        CastLib castLib = new CastLib(1, null, null);
        installCastLib(manager, castLib);

        Datum created = manager.createMember("common.button.text", "text");

        assertTrue(created.isInt());
        int encoded = created.toInt();
        assertEquals(1, encoded >> 16);
        CastMember member = castLib.getMember(encoded & 0xFFFF);
        assertEquals("common.button.text", member.getName());
        assertEquals(MemberType.TEXT, member.getMemberType());
        Datum found = manager.getMemberByName(0, "common.button.text");
        Datum.CastMemberRef ref = assertInstanceOf(Datum.CastMemberRef.class, found);
        assertEquals(1, ref.castLibNum());
        assertEquals(member.getMemberNumber(), ref.memberNum());
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
