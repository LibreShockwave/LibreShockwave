package com.libreshockwave.player.cast;

import com.libreshockwave.bitmap.Palette;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Field;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertSame;

class CastLibManagerPaletteTest {

    @Test
    void duplicatePaletteMembersStayResolvableForWindowBuffers() throws Exception {
        CastLibManager manager = new CastLibManager(null, null);
        CastLib castLib = new CastLib(1, null, null);
        installCastLib(manager, castLib);

        CastMember sourcePaletteMember = castLib.createDynamicMember("palette");
        sourcePaletteMember.setProp("name", Datum.of("interface palette_messenger"));
        Palette sourcePalette = new Palette(new int[]{0x112233, 0x445566, 0x778899}, "Messenger UI");
        sourcePaletteMember.setPaletteData(sourcePalette);

        CastMember duplicatePaletteMember = castLib.createDynamicMember("palette");
        duplicatePaletteMember.setProp("name", Datum.of("interface palette_messengerDuplicate"));

        Datum targetRef = Datum.CastMemberRef.of(1, duplicatePaletteMember.getMemberNumber());
        manager.callMemberMethod(1, sourcePaletteMember.getMemberNumber(), "duplicate", java.util.List.of(targetRef));

        Palette resolvedByMember = manager.getMemberPalette(1, duplicatePaletteMember.getMemberNumber());
        assertNotNull(resolvedByMember);
        assertSame(sourcePalette, resolvedByMember);
        assertEquals(0x112233, resolvedByMember.getColor(0));

        Palette resolvedByName = manager.resolvePaletteByName("interface palette_messengerDuplicate");
        assertNotNull(resolvedByName);
        assertSame(sourcePalette, resolvedByName);
    }

    @Test
    void duplicatePaletteMembersAcceptEncodedSlotTargets() throws Exception {
        CastLibManager manager = new CastLibManager(null, null);
        CastLib castLib = new CastLib(3, null, null);
        installCastLib(manager, castLib);

        CastMember sourcePaletteMember = castLib.createDynamicMember("palette");
        sourcePaletteMember.setProp("name", Datum.of("purse_rclr_palette"));
        Palette sourcePalette = new Palette(new int[]{0x010203, 0xAABBCC}, "Purse");
        sourcePaletteMember.setPaletteData(sourcePalette);

        CastMember duplicatePaletteMember = castLib.createDynamicMember("palette");
        duplicatePaletteMember.setProp("name", Datum.of("purse_rclr_paletteDuplicate"));

        Datum targetSlot = Datum.of(duplicatePaletteMember.getSlotNumber());
        Datum duplicateResult = manager.callMemberMethod(
                3,
                sourcePaletteMember.getMemberNumber(),
                "duplicate",
                java.util.List.of(targetSlot));

        assertEquals(targetSlot.toInt(), duplicateResult.toInt());

        Palette resolvedByMember = manager.getMemberPalette(3, duplicatePaletteMember.getMemberNumber());
        assertNotNull(resolvedByMember);
        assertSame(sourcePalette, resolvedByMember);
        assertEquals(0x010203, resolvedByMember.getColor(0));
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
