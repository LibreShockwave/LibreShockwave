package com.libreshockwave.vm.util;

import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

class LingoValueParserTest {

    @Test
    void parsesQuotedKeyPropListWithEmptyNestedPropLists() {
        Datum parsed = LingoValueParser.parseWithPartial(
                "[\"a\": [:], \"b\": [:], \"c\": [#ink: 33]]",
                new LingoVM(null));

        assertInstanceOf(Datum.PropList.class, parsed);
        Datum.PropList props = (Datum.PropList) parsed;

        assertInstanceOf(Datum.PropList.class, props.get("a"));
        assertInstanceOf(Datum.PropList.class, props.get("b"));
        assertInstanceOf(Datum.PropList.class, props.get("c"));
        assertEquals(33, ((Datum.PropList) props.get("c")).get("ink").toInt());
    }

    @Test
    void parsesBareKeyNestedPropListsUsedByDynamicAssetData() {
        String source = "[\r" +
                "states:[1,2],\r" +
                "statestrings:[ \"off\", \"on\" ],\r" +
                "layers:[ \r" +
                "a:[ [ frames:[ 0 ] ] ], \r" +
                "b:[ [ frames:[ 0 ] ], [ loop:0, delay:2, frames:[ 1, 2, 3 ] ] ]\r" +
                "]\r" +
                "]";

        Datum parsed = LingoValueParser.parseWithPartial(source, new LingoVM(null));

        assertInstanceOf(Datum.PropList.class, parsed);
        Datum.PropList props = (Datum.PropList) parsed;

        assertInstanceOf(Datum.List.class, props.get("states"));
        assertEquals(2, ((Datum.List) props.get("states")).items().size());
        assertInstanceOf(Datum.PropList.class, props.get("layers"));

        Datum.PropList layers = (Datum.PropList) props.get("layers");
        assertInstanceOf(Datum.List.class, layers.get("b"));

        Datum.List bLayers = (Datum.List) layers.get("b");
        assertEquals(2, bLayers.items().size());
        assertInstanceOf(Datum.PropList.class, bLayers.items().get(1));

        Datum.PropList animatedLayer = (Datum.PropList) bLayers.items().get(1);
        assertEquals(0, animatedLayer.get("loop").toInt());
        assertEquals(2, animatedLayer.get("delay").toInt());
        assertInstanceOf(Datum.List.class, animatedLayer.get("frames"));
    }

    @Test
    void parsesFlatQuotedStringListsUsedBySystemPropsClassVariables() {
        Datum parsed = LingoValueParser.parseWithPartial(
                "[\"Manager Template Class\",\"Variable Container Class\"]",
                new LingoVM(null));

        assertInstanceOf(Datum.List.class, parsed);
        Datum.List list = (Datum.List) parsed;
        assertEquals(2, list.items().size());
        assertEquals("Manager Template Class", list.items().get(0).toStr());
        assertEquals("Variable Container Class", list.items().get(1).toStr());
    }

    @Test
    void parsesFlatMixedLiteralListsWithoutRegexDependency() {
        Datum parsed = LingoValueParser.parseWithPartial(
                "[#core, 7, 3.5, \"Broker Manager Class\"]",
                new LingoVM(null));

        assertInstanceOf(Datum.List.class, parsed);
        Datum.List list = (Datum.List) parsed;
        assertEquals("core", list.items().get(0).toKeyName());
        assertEquals(7, list.items().get(1).toInt());
        assertEquals(3.5, list.items().get(2).toDouble(), 0.0001);
        assertEquals("Broker Manager Class", list.items().get(3).toStr());
    }
}
