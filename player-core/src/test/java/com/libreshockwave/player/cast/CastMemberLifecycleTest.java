package com.libreshockwave.player.cast;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.id.ChunkId;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.jupiter.api.Assertions.*;

class CastMemberLifecycleTest {

    @Test
    void eraseClearsDynamicMemberTextAndName() {
        CastMember member = new CastMember(1, 10001, MemberType.TEXT);
        member.setProp("name", Datum.of("room_status"));
        member.setProp("text", Datum.of("hello"));

        Datum result = member.callMethod("erase", List.of());

        assertEquals(1, result.toInt());
        assertEquals("", member.getName());
        assertEquals("", member.getTextContent());
        assertEquals("empty", member.getProp("type").toKeyName());
        assertTrue(member.getProp("type").isSymbol());
    }

    @Test
    void memberTypePropertyReturnsDirectorSymbol() {
        CastMember bitmap = new CastMember(1, 10001, MemberType.BITMAP);
        Datum type = bitmap.getProp("type");

        assertTrue(type.isSymbol());
        assertEquals("bitmap", type.toKeyName());
    }

    @Test
    void textMembersReportDirectorFieldType() {
        CastMember field = new CastMember(1, 10001, MemberType.TEXT);

        Datum type = field.getProp("type");

        assertTrue(type.isSymbol());
        assertEquals("field", type.toKeyName());
    }

    @Test
    void parsedFieldValueIsCachedUntilTextChanges() {
        CastMember field = new CastMember(1, 10001, MemberType.TEXT);
        LingoVM vm = new LingoVM(null);

        field.setDynamicText("[#foo: 7]");
        Datum first = field.getParsedTextValue(vm);
        Datum second = field.getParsedTextValue(vm);

        assertSame(first, second);
        assertEquals(7, ((Datum.PropList) first).get("foo").toInt());

        field.setDynamicText("[#foo: 8]");
        Datum third = field.getParsedTextValue(vm);

        assertNotSame(first, third);
        assertEquals(8, ((Datum.PropList) third).get("foo").toInt());
    }

    @Test
    void paletteSetterRemapsDynamicBitmapMembersWithoutFileBacking() {
        Palette oldPalette = new Palette(new int[]{0xFFFFFF, 0x6C5230}, "old");
        Palette newPalette = new Palette(new int[]{0xFFFFFF, 0xC49A5A}, "new");

        Bitmap wrapper = new Bitmap(1, 1, 32);
        wrapper.setImagePalette(oldPalette);
        wrapper.setPixel(0, 0, 0xFF6C5230);

        CastMember member = new CastMember(1, 10001, MemberType.BITMAP);
        member.setBitmapDirectly(wrapper);

        CastMember.setPaletteResolver((castLib, memberNum) ->
                castLib == 9 && memberNum == 5 ? newPalette : null);
        try {
            assertTrue(member.setProp("palette", Datum.CastMemberRef.of(9, 5)));
            assertEquals(0xFFC49A5A, member.getBitmap().getPixel(0, 0));
            assertSame(newPalette, member.getBitmap().getImagePalette());

            Datum palette = member.getProp("palette");
            assertInstanceOf(Datum.CastMemberRef.class, palette);
            assertEquals(9, ((Datum.CastMemberRef) palette).castLibNum());
            assertEquals(5, ((Datum.CastMemberRef) palette).memberNum());
        } finally {
            CastMember.setPaletteResolver(null);
        }
    }

    @Test
    void blankingDynamicBitmapNameDoesNotRetireTheSlot() {
        CastMember member = new CastMember(7, 10001, MemberType.BITMAP);
        AtomicInteger retiredSlot = new AtomicInteger(-1);

        CastMember.setMemberSlotRetiredCallback((castLib, memberNum) -> {
            retiredSlot.set((castLib << 16) | memberNum);
        });
        try {
            member.setProp("name", Datum.of("bb_tempworld"));
            member.setProp("name", Datum.EMPTY_STRING);

            assertEquals("", member.getName());
            assertEquals(-1, retiredSlot.get());
        } finally {
            CastMember.setMemberSlotRetiredCallback(null);
        }
    }

    @Test
    void createDynamicMemberReusesFirstErasedRuntimeSlot() {
        CastLib castLib = new CastLib(4, null, null);

        CastMember first = castLib.createDynamicMember("bitmap");
        CastMember second = castLib.createDynamicMember("text");

        assertEquals(10000, first.getMemberNumber());
        assertEquals(10001, second.getMemberNumber());

        first.erase();

        CastMember reused = castLib.createDynamicMember("palette");

        assertSame(first, reused);
        assertEquals(10000, reused.getMemberNumber());
        assertEquals("palette", reused.getProp("type").toKeyName());
        assertTrue(reused.getProp("type").isSymbol());
    }

    @Test
    void mediaCopyTreatsTextXtraSourcesAsFields() {
        CastMemberChunk sourceChunk = createTextXtraChunk(42, "grunge_barrel.props");
        CastMember source = new CastMember(11, 42, sourceChunk, null);
        assertTrue(source.setProp("text", Datum.of("[#a: [#blend: 80]]")));

        CastMember target = new CastMember(2, 10000, MemberType.TEXT);
        CastMember.setMemberResolver((castLib, memberNum) ->
                castLib == 11 && memberNum == 42 ? source : null);
        try {
            assertTrue(target.setProp("media", Datum.CastMemberRef.of(11, 42)));
        } finally {
            CastMember.setMemberResolver(null);
        }

        assertEquals("[#a: [#blend: 80]]", target.getTextContent());
        assertEquals("field", target.getProp("type").toKeyName());
    }

    @Test
    void textXtraTargetsAcceptStringMediaAssignments() {
        CastMember target = new CastMember(11, 99, createTextXtraChunk(99, "dynamic.props"), null);

        assertTrue(target.setProp("media", Datum.of("[#foo: 1]")));
        assertEquals("[#foo: 1]", target.getTextContent());
        assertEquals("field", target.getProp("type").toKeyName());
    }

    private static CastMemberChunk createTextXtraChunk(int memberNum, String name) {
        byte[] textXtraSpecificData = new byte[]{0, 0, 0, 0, 't', 'e', 'x', 't'};
        return new CastMemberChunk(
                null,
                new ChunkId(memberNum),
                MemberType.XTRA,
                0,
                textXtraSpecificData.length,
                new byte[0],
                textXtraSpecificData,
                name,
                0,
                0,
                0);
    }
}
