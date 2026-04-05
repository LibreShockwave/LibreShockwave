package com.libreshockwave.vm.opcode.dispatch;

import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class PropListMethodDispatcherTest {

    @Test
    void getAtSymbolKeyMatchesSymbolEntry() {
        Datum.PropList propList = new Datum.PropList();
        propList.add("color", Datum.of(255), true);

        Datum result = PropListMethodDispatcher.dispatch(
                propList, "getAt", List.of(new Datum.Symbol("color")));

        assertEquals(255, result.toInt());
    }

    @Test
    void getAtStringKeyMatchesStringEntry() {
        Datum.PropList propList = new Datum.PropList();
        propList.add("Color", Datum.of(255), false);

        Datum result = PropListMethodDispatcher.dispatch(
                propList, "getAt", List.of(Datum.of("Color")));

        assertEquals(255, result.toInt());
    }

    @Test
    void getAtStringKeyNoFallbackWhenCaseDiffers() {
        // Cross-type fallback requires exact case: "Room_interface" != "room_interface"
        Datum.PropList propList = new Datum.PropList();
        propList.add("room_interface", Datum.of(1), true);

        Datum result = PropListMethodDispatcher.dispatch(
                propList, "getAt", List.of(Datum.of("Room_interface")));

        assertTrue(result.isVoid());
    }

    @Test
    void getAtCrossTypeFallbackOnExactCase() {
        // Cross-type fallback works when case matches exactly
        Datum.PropList propList = new Datum.PropList();
        propList.add("color", Datum.of(255), false);

        Datum result = PropListMethodDispatcher.dispatch(
                propList, "getAt", List.of(new Datum.Symbol("color")));

        assertEquals(255, result.toInt());
    }

    @Test
    void getAtSeparatesSymbolAndStringNamespaces() {
        Datum.PropList propList = new Datum.PropList();
        propList.add("key", Datum.of(1), true);   // symbol #key
        propList.add("key", Datum.of(2), false);   // string "key"

        Datum symResult = PropListMethodDispatcher.dispatch(
                propList, "getAt", List.of(new Datum.Symbol("key")));
        Datum strResult = PropListMethodDispatcher.dispatch(
                propList, "getAt", List.of(Datum.of("key")));

        assertEquals(1, symResult.toInt());
        assertEquals(2, strResult.toInt());
    }

    @Test
    void deletePropRemovesOnlyMatchingKeyType() {
        Datum.PropList propList = new Datum.PropList();
        propList.add("room_interface", Datum.of(1), true);   // symbol #room_interface
        propList.add("room_interface", Datum.of(2), false);  // string "room_interface"

        PropListMethodDispatcher.dispatch(
                propList, "deleteProp", List.of(Datum.of("room_interface")));

        assertEquals(1, propList.size());
        assertTrue(propList.entries().getFirst().isSymbolKey());
        assertEquals(1, propList.entries().getFirst().value().toInt());
    }
}
