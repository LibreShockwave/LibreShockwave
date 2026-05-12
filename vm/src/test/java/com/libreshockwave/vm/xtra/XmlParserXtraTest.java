package com.libreshockwave.vm.xtra;

import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.opcode.dispatch.PropListMethodDispatcher;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

class XmlParserXtraTest {

    @Test
    void parsesDirectorStyleChildrenAndAttributes() {
        XmlParserXtra xtra = new XmlParserXtra();
        int id = xtra.createInstance(List.of());

        Datum parsed = xtra.callHandler(id, "parseString", List.of(Datum.of(
                "<?xml version=\"1.0\"?><partSets><partSet id=\"h\">" +
                        "<label>value</label>" +
                        "<part set-type=\"hd\" swim=\"0\" small=\"1\"/>" +
                        "</partSet></partSets>")));

        assertTrue(parsed.toInt() != 0);
        assertEquals(Datum.VOID, xtra.callHandler(id, "getError", List.of()));
        assertEquals(1, xtra.callHandler(id, "count", List.of(Datum.symbol("child"))).toInt());

        Datum root = xtra.callHandler(id, "getPropRef", List.of(Datum.symbol("child"), Datum.of(1)));
        Datum.PropList rootNode = assertInstanceOf(Datum.PropList.class, root);
        assertEquals("partSets", rootNode.getOrDefault("name", true, Datum.VOID).toStr());

        Datum partSet = rootNode.getOrDefault("child", true, Datum.VOID);
        Datum.List children = assertInstanceOf(Datum.List.class, partSet);
        Datum.PropList partSetNode = assertInstanceOf(Datum.PropList.class, children.items().get(0));
        assertEquals("partSet", partSetNode.getOrDefault("name", true, Datum.VOID).toStr());

        Datum.List attrNames = assertInstanceOf(Datum.List.class,
                partSetNode.getOrDefault("attributeName", true, Datum.VOID));
        Datum.List attrValues = assertInstanceOf(Datum.List.class,
                partSetNode.getOrDefault("attributeValue", true, Datum.VOID));
        assertEquals("id", attrNames.items().get(0).toStr());
        assertEquals("h", attrValues.items().get(0).toStr());

        Datum.List partSetChildren = assertInstanceOf(Datum.List.class,
                partSetNode.getOrDefault("child", true, Datum.VOID));
        Datum.PropList label = assertInstanceOf(Datum.PropList.class, partSetChildren.items().get(0));
        Datum.PropList text = assertInstanceOf(Datum.PropList.class,
                PropListMethodDispatcher.dispatch(label, "getPropRef", List.of(Datum.symbol("child"), Datum.of(1))));
        assertEquals("value", text.getOrDefault("text", true, Datum.VOID).toStr());
    }
}
