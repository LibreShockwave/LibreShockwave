package com.libreshockwave.vm.opcode.dispatch;

import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class PropListMethodDispatcherTest {

    @Test
    void getAtStringKeyReadsSymbolEntryWhenCaseMatchesExactly() {
        Datum.PropList propList = new Datum.PropList();
        propList.add("Color", Datum.of(255), true);

        Datum result = PropListMethodDispatcher.dispatch(
                propList, "getAt", List.of(Datum.of("Color")));

        assertEquals(255, result.toInt());
    }

    @Test
    void getAtStringKeySkipsSymbolEntryWhenCaseDiffers() {
        Datum.PropList propList = new Datum.PropList();
        propList.add("room_interface", Datum.of(1), true);

        Datum result = PropListMethodDispatcher.dispatch(
                propList, "getAt", List.of(Datum.of("Room_interface")));

        assertTrue(result.isVoid());
    }

    @Test
    void getAtStringKeyAllowsCaseInsensitiveSymbolFallbackForNonRoomInterface() {
        Datum.PropList propList = new Datum.PropList();
        propList.add("color", Datum.of(255), true);

        Datum result = PropListMethodDispatcher.dispatch(
                propList, "getAt", List.of(Datum.of("Color")));

        assertEquals(255, result.toInt());
    }
}
