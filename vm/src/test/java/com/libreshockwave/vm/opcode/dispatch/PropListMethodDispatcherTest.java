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
    void getAtStringKeyFallsBackToSymbolEntry() {
        // Cross-type fallback: string key finds symbol entry when no string entry exists
        Datum.PropList propList = new Datum.PropList();
        propList.add("room_interface", Datum.of(1), true);

        Datum result = PropListMethodDispatcher.dispatch(
                propList, "getAt", List.of(Datum.of("Room_interface")));

        assertEquals(1, result.toInt());
    }

    @Test
    void getAtSymbolKeyFallsBackToStringEntry() {
        // Cross-type fallback: symbol key finds string entry when no symbol entry exists
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
}
