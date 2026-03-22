package com.libreshockwave.vm;

import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.opcode.CallOpcodesTestHelper;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.BeforeEach;
import static org.junit.jupiter.api.Assertions.*;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Unit tests for script instances, ancestor chains, and property lookup.
 * Tests the Lingo object model used by Director for parent scripts.
 */
class ScriptInstanceTest {

    @BeforeEach
    void setUp() {
        // Clear any existing provider
        com.libreshockwave.vm.builtin.cast.CastLibProvider.setProvider(null);
    }

    // ========== Basic Script Instance Tests ==========

    @Test
    void testCreateScriptInstance() {
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());

        assertEquals(1, instance.scriptId());
        assertTrue(instance.properties().isEmpty());
    }

    @Test
    void testScriptInstanceProperties() {
        Map<String, Datum> props = new LinkedHashMap<>();
        props.put("name", Datum.of("TestInstance"));
        props.put("value", Datum.of(42));

        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, props);

        assertEquals("TestInstance", instance.properties().get("name").toStr());
        assertEquals(42, instance.properties().get("value").toInt());
    }

    @Test
    void testScriptInstanceToString() {
        Datum.ScriptInstance instance = new Datum.ScriptInstance(5, new LinkedHashMap<>());

        assertEquals("<script instance 5>", instance.toString());
    }

    @Test
    void testScriptInstancePropertiesAreMutable() {
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());

        // Properties should be mutable
        instance.properties().put("added", Datum.of("value"));
        assertEquals("value", instance.properties().get("added").toStr());

        instance.properties().put("added", Datum.of("modified"));
        assertEquals("modified", instance.properties().get("added").toStr());

        instance.properties().remove("added");
        assertNull(instance.properties().get("added"));
    }

    // ========== List Method Tests ==========

    @Test
    void testListAddAt() {
        Datum.List list = new Datum.List(new ArrayList<>(List.of(
            Datum.of(1),
            Datum.of(2),
            Datum.of(3)
        )));

        // addAt(list, 2, 99) should insert 99 at position 2 (1-indexed)
        Datum result = CallOpcodesTestHelper.callListMethod(list, "addat",
            List.of(Datum.of(2), Datum.of(99)));

        // List should now be [1, 99, 2, 3]
        assertEquals(4, list.items().size());
        assertEquals(1, list.items().get(0).toInt());
        assertEquals(99, list.items().get(1).toInt());
        assertEquals(2, list.items().get(2).toInt());
        assertEquals(3, list.items().get(3).toInt());
    }

    @Test
    void testListAddAtBeginning() {
        Datum.List list = new Datum.List(new ArrayList<>(List.of(
            Datum.of(1),
            Datum.of(2)
        )));

        CallOpcodesTestHelper.callListMethod(list, "addat",
            List.of(Datum.of(1), Datum.of(0)));

        // List should now be [0, 1, 2]
        assertEquals(3, list.items().size());
        assertEquals(0, list.items().get(0).toInt());
        assertEquals(1, list.items().get(1).toInt());
        assertEquals(2, list.items().get(2).toInt());
    }

    @Test
    void testListAddAtEnd() {
        Datum.List list = new Datum.List(new ArrayList<>(List.of(
            Datum.of(1),
            Datum.of(2)
        )));

        CallOpcodesTestHelper.callListMethod(list, "addat",
            List.of(Datum.of(3), Datum.of(99)));

        // List should now be [1, 2, 99]
        assertEquals(3, list.items().size());
        assertEquals(1, list.items().get(0).toInt());
        assertEquals(2, list.items().get(1).toInt());
        assertEquals(99, list.items().get(2).toInt());
    }

    @Test
    void testListAddAtBeyondEnd() {
        Datum.List list = new Datum.List(new ArrayList<>(List.of(
            Datum.of(1)
        )));

        // addAt with position beyond list size should add at end
        CallOpcodesTestHelper.callListMethod(list, "addat",
            List.of(Datum.of(100), Datum.of(99)));

        assertEquals(2, list.items().size());
        assertEquals(1, list.items().get(0).toInt());
        assertEquals(99, list.items().get(1).toInt());
    }

    @Test
    void testListGetAt() {
        Datum.List list = new Datum.List(new ArrayList<>(List.of(
            Datum.of(10),
            Datum.of(20),
            Datum.of(30)
        )));

        // Lingo is 1-indexed
        assertEquals(10, CallOpcodesTestHelper.callListMethod(list, "getat",
            List.of(Datum.of(1))).toInt());
        assertEquals(20, CallOpcodesTestHelper.callListMethod(list, "getat",
            List.of(Datum.of(2))).toInt());
        assertEquals(30, CallOpcodesTestHelper.callListMethod(list, "getat",
            List.of(Datum.of(3))).toInt());
        assertTrue(CallOpcodesTestHelper.callListMethod(list, "getat",
            List.of(Datum.of(4))).isVoid());
    }

    @Test
    void testListSetAt() {
        Datum.List list = new Datum.List(new ArrayList<>(List.of(
            Datum.of(10),
            Datum.of(20),
            Datum.of(30)
        )));

        CallOpcodesTestHelper.callListMethod(list, "setat",
            List.of(Datum.of(2), Datum.of(99)));

        assertEquals(10, list.items().get(0).toInt());
        assertEquals(99, list.items().get(1).toInt());
        assertEquals(30, list.items().get(2).toInt());
    }

    @Test
    void testListDeleteAt() {
        Datum.List list = new Datum.List(new ArrayList<>(List.of(
            Datum.of(10),
            Datum.of(20),
            Datum.of(30)
        )));

        CallOpcodesTestHelper.callListMethod(list, "deleteat", List.of(Datum.of(2)));

        assertEquals(2, list.items().size());
        assertEquals(10, list.items().get(0).toInt());
        assertEquals(30, list.items().get(1).toInt());
    }

    @Test
    void testListGetOne() {
        Datum.List list = new Datum.List(new ArrayList<>(List.of(
            Datum.of("a"),
            Datum.of("b"),
            Datum.of("c")
        )));

        // Find "b" at position 2
        Datum pos = CallOpcodesTestHelper.callListMethod(list, "getone",
            List.of(Datum.of("b")));
        assertEquals(2, pos.toInt());

        // Not found returns 0
        Datum notFound = CallOpcodesTestHelper.callListMethod(list, "getone",
            List.of(Datum.of("z")));
        assertEquals(0, notFound.toInt());
    }

    @Test
    void testListAppend() {
        Datum.List list = new Datum.List(new ArrayList<>(List.of(
            Datum.of(1)
        )));

        CallOpcodesTestHelper.callListMethod(list, "append", List.of(Datum.of(2)));

        assertEquals(2, list.items().size());
        assertEquals(1, list.items().get(0).toInt());
        assertEquals(2, list.items().get(1).toInt());
    }

    @Test
    void testListCount() {
        Datum.List list = new Datum.List(new ArrayList<>(List.of(
            Datum.of(1),
            Datum.of(2),
            Datum.of(3)
        )));

        Datum count = CallOpcodesTestHelper.callListMethod(list, "count", List.of());
        assertEquals(3, count.toInt());
    }

    @Test
    void testListDuplicate() {
        Datum.List original = new Datum.List(new ArrayList<>(List.of(
            Datum.of(1),
            Datum.of(2)
        )));

        Datum result = CallOpcodesTestHelper.callListMethod(original, "duplicate", List.of());

        assertTrue(result instanceof Datum.List);
        Datum.List copy = (Datum.List) result;

        // Same values
        assertEquals(2, copy.items().size());
        assertEquals(1, copy.items().get(0).toInt());
        assertEquals(2, copy.items().get(1).toInt());

        // But different list object
        assertNotSame(original.items(), copy.items());
    }

    @Test
    void testListSort() {
        Datum.List list = new Datum.List(new ArrayList<>(List.of(
            Datum.of(3),
            Datum.of(1),
            Datum.of(2)
        )));

        CallOpcodesTestHelper.callListMethod(list, "sort", List.of());

        assertEquals(1, list.items().get(0).toInt());
        assertEquals(2, list.items().get(1).toInt());
        assertEquals(3, list.items().get(2).toInt());
    }

    @Test
    void testListSetAtPadsWithVoid() {
        // Like dirplayer-rs: setAt pads with VOID if index > current length
        Datum.List list = new Datum.List(new ArrayList<>(List.of(
            Datum.of(1)
        )));

        // Set at position 4 (1-indexed) in a list with only 1 element
        CallOpcodesTestHelper.callListMethod(list, "setat",
            List.of(Datum.of(4), Datum.of(99)));

        // Should now be [1, VOID, VOID, 99]
        assertEquals(4, list.items().size());
        assertEquals(1, list.items().get(0).toInt());
        assertTrue(list.items().get(1).isVoid());
        assertTrue(list.items().get(2).isVoid());
        assertEquals(99, list.items().get(3).toInt());
    }

    @Test
    void testListGetLast() {
        Datum.List list = new Datum.List(new ArrayList<>(List.of(
            Datum.of(10),
            Datum.of(20),
            Datum.of(30)
        )));

        Datum last = CallOpcodesTestHelper.callListMethod(list, "getlast", List.of());
        assertEquals(30, last.toInt());
    }

    @Test
    void testListGetLastEmpty() {
        Datum.List list = new Datum.List(new ArrayList<>());

        Datum last = CallOpcodesTestHelper.callListMethod(list, "getlast", List.of());
        assertTrue(last.isVoid());
    }

    @Test
    void testListDeleteOne() {
        Datum.List list = new Datum.List(new ArrayList<>(List.of(
            Datum.of("a"),
            Datum.of("b"),
            Datum.of("c"),
            Datum.of("b")  // Duplicate
        )));

        // deleteOne only removes the FIRST match
        CallOpcodesTestHelper.callListMethod(list, "deleteone", List.of(Datum.of("b")));

        assertEquals(3, list.items().size());
        assertEquals("a", list.items().get(0).toStr());
        assertEquals("c", list.items().get(1).toStr());
        assertEquals("b", list.items().get(2).toStr()); // Second "b" remains
    }

    @Test
    void testListFindPos() {
        // findPos is identical to getOne
        Datum.List list = new Datum.List(new ArrayList<>(List.of(
            Datum.of("a"),
            Datum.of("b"),
            Datum.of("c")
        )));

        Datum pos = CallOpcodesTestHelper.callListMethod(list, "findpos",
            List.of(Datum.of("b")));
        assertEquals(2, pos.toInt());

        Datum notFound = CallOpcodesTestHelper.callListMethod(list, "findpos",
            List.of(Datum.of("z")));
        assertEquals(0, notFound.toInt());
    }

    @Test
    void testListJoin() {
        Datum.List list = new Datum.List(new ArrayList<>(List.of(
            Datum.of("hello"),
            Datum.of("world")
        )));

        Datum result = CallOpcodesTestHelper.callListMethod(list, "join",
            List.of(Datum.of(" ")));
        assertEquals("hello world", result.toStr());
    }

    @Test
    void testListJoinWithComma() {
        Datum.List list = new Datum.List(new ArrayList<>(List.of(
            Datum.of(1),
            Datum.of(2),
            Datum.of(3)
        )));

        Datum result = CallOpcodesTestHelper.callListMethod(list, "join",
            List.of(Datum.of(",")));
        assertEquals("1,2,3", result.toStr());
    }

    @Test
    void testListJoinNoSeparator() {
        Datum.List list = new Datum.List(new ArrayList<>(List.of(
            Datum.of("a"),
            Datum.of("b"),
            Datum.of("c")
        )));

        Datum result = CallOpcodesTestHelper.callListMethod(list, "join", List.of());
        assertEquals("abc", result.toStr());
    }

    // ========== PropList Method Tests ==========

    @Test
    void testPropListSetaProp() {
        Datum.PropList propList = new Datum.PropList();

        CallOpcodesTestHelper.callPropListMethod(propList, "setaprop",
            List.of(Datum.symbol("key"), Datum.of("value")));

        assertEquals("value", propList.get("key").toStr());
    }

    @Test
    void testPropListGetaProp() {
        Datum.PropList propList = new Datum.PropList();
        propList.put("key", true, Datum.of("value"));

        Datum result = CallOpcodesTestHelper.callPropListMethod(propList, "getaprop",
            List.of(Datum.symbol("key")));

        assertEquals("value", result.toStr());
    }

    @Test
    void testPropListCount() {
        Datum.PropList propList = new Datum.PropList();
        propList.put("a", true, Datum.of(1));
        propList.put("b", true, Datum.of(2));

        Datum count = CallOpcodesTestHelper.callPropListMethod(propList, "count", List.of());
        assertEquals(2, count.toInt());
    }

    @Test
    void testPropListFindPos() {
        Datum.PropList propList = new Datum.PropList();
        propList.put("first", true, Datum.of(1));
        propList.put("second", true, Datum.of(2));
        propList.put("third", true, Datum.of(3));

        Datum pos1 = CallOpcodesTestHelper.callPropListMethod(propList, "findpos",
            List.of(Datum.symbol("first")));
        assertEquals(1, pos1.toInt());

        Datum pos2 = CallOpcodesTestHelper.callPropListMethod(propList, "findpos",
            List.of(Datum.symbol("second")));
        assertEquals(2, pos2.toInt());

        Datum pos3 = CallOpcodesTestHelper.callPropListMethod(propList, "findpos",
            List.of(Datum.symbol("third")));
        assertEquals(3, pos3.toInt());

        Datum notFound = CallOpcodesTestHelper.callPropListMethod(propList, "findpos",
            List.of(Datum.symbol("nonexistent")));
        assertEquals(0, notFound.toInt());
    }

    @Test
    void testPropListGetAt() {
        Datum.PropList propList = new Datum.PropList();
        propList.put("a", true, Datum.of(10));
        propList.put("b", true, Datum.of(20));

        // Get by symbol key
        Datum byKey = CallOpcodesTestHelper.callPropListMethod(propList, "getat",
            List.of(Datum.symbol("a")));
        assertEquals(10, byKey.toInt());

        // Get by index (1-based)
        Datum byIndex = CallOpcodesTestHelper.callPropListMethod(propList, "getat",
            List.of(Datum.of(2)));
        assertEquals(20, byIndex.toInt());
    }

    @Test
    void testPropListDuplicate() {
        Datum.PropList original = new Datum.PropList();
        original.put("x", true, Datum.of(1));

        Datum result = CallOpcodesTestHelper.callPropListMethod(original, "duplicate", List.of());

        assertTrue(result instanceof Datum.PropList);
        Datum.PropList copy = (Datum.PropList) result;

        assertEquals(1, copy.get("x").toInt());
        assertNotSame(original.entries(), copy.entries());
    }

    // ========== Script Instance Method Tests ==========

    @Test
    void testScriptInstanceSetAt() {
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());

        CallOpcodesTestHelper.callScriptInstanceMethod(null, instance, "setat",
            List.of(Datum.symbol("name"), Datum.of("Test")));

        assertEquals("Test", instance.properties().get("name").toStr());
    }

    @Test
    void testScriptInstanceSetaProp() {
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());

        CallOpcodesTestHelper.callScriptInstanceMethod(null, instance, "setaprop",
            List.of(Datum.symbol("value"), Datum.of(42)));

        assertEquals(42, instance.properties().get("value").toInt());
    }

    @Test
    void testScriptInstanceGetAt() {
        Map<String, Datum> props = new LinkedHashMap<>();
        props.put("name", Datum.of("TestInstance"));
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, props);

        Datum result = CallOpcodesTestHelper.callScriptInstanceMethod(null, instance, "getat",
            List.of(Datum.symbol("name")));

        assertEquals("TestInstance", result.toStr());
    }

    @Test
    void testScriptInstanceCount() {
        Map<String, Datum> props = new LinkedHashMap<>();
        props.put("a", Datum.of(1));
        props.put("b", Datum.of(2));
        props.put("c", Datum.of(3));
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, props);

        Datum result = CallOpcodesTestHelper.callScriptInstanceMethod(null, instance, "count",
            List.of());

        assertEquals(3, result.toInt());
    }

    @Test
    void testScriptInstanceIlk() {
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());

        Datum result = CallOpcodesTestHelper.callScriptInstanceMethod(null, instance, "ilk",
            List.of());

        assertTrue(result instanceof Datum.Symbol);
        assertEquals("instance", ((Datum.Symbol) result).name());
    }

    @Test
    void testScriptInstanceAddProp() {
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, new LinkedHashMap<>());

        CallOpcodesTestHelper.callScriptInstanceMethod(null, instance, "addprop",
            List.of(Datum.symbol("newProp"), Datum.of("newValue")));

        assertEquals("newValue", instance.properties().get("newProp").toStr());
    }

    @Test
    void testScriptInstanceDeleteProp() {
        Map<String, Datum> props = new LinkedHashMap<>();
        props.put("keep", Datum.of(1));
        props.put("delete", Datum.of(2));
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, props);

        CallOpcodesTestHelper.callScriptInstanceMethod(null, instance, "deleteprop",
            List.of(Datum.symbol("delete")));

        assertTrue(instance.properties().containsKey("keep"));
        assertFalse(instance.properties().containsKey("delete"));
    }

    // ========== Ancestor Chain Tests ==========

    @Test
    void testAncestorPropertyDirect() {
        // Create an ancestor instance
        Map<String, Datum> ancestorProps = new LinkedHashMap<>();
        ancestorProps.put("baseValue", Datum.of(100));
        Datum.ScriptInstance ancestor = new Datum.ScriptInstance(2, ancestorProps);

        // Create main instance with ancestor
        Map<String, Datum> props = new LinkedHashMap<>();
        props.put("ancestor", ancestor);
        props.put("myValue", Datum.of(50));
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, props);

        // Direct property lookup
        assertEquals(50, instance.properties().get("myValue").toInt());
        // Ancestor is accessible
        assertTrue(instance.properties().get("ancestor") instanceof Datum.ScriptInstance);
    }

    @Test
    void testGetPropertyFromAncestorChain() {
        // Create a chain: instance -> ancestor1 -> ancestor2
        Map<String, Datum> ancestor2Props = new LinkedHashMap<>();
        ancestor2Props.put("deepValue", Datum.of("deep"));
        Datum.ScriptInstance ancestor2 = new Datum.ScriptInstance(3, ancestor2Props);

        Map<String, Datum> ancestor1Props = new LinkedHashMap<>();
        ancestor1Props.put("middleValue", Datum.of("middle"));
        ancestor1Props.put("ancestor", ancestor2);
        Datum.ScriptInstance ancestor1 = new Datum.ScriptInstance(2, ancestor1Props);

        Map<String, Datum> props = new LinkedHashMap<>();
        props.put("topValue", Datum.of("top"));
        props.put("ancestor", ancestor1);
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, props);

        // getAt should find topValue on instance
        Datum topResult = CallOpcodesTestHelper.callScriptInstanceMethod(null, instance, "getat",
            List.of(Datum.symbol("topValue")));
        assertEquals("top", topResult.toStr());

        // getAt should find middleValue on ancestor1
        Datum middleResult = CallOpcodesTestHelper.callScriptInstanceMethod(null, instance, "getat",
            List.of(Datum.symbol("middleValue")));
        assertEquals("middle", middleResult.toStr());

        // getAt should find deepValue on ancestor2
        Datum deepResult = CallOpcodesTestHelper.callScriptInstanceMethod(null, instance, "getat",
            List.of(Datum.symbol("deepValue")));
        assertEquals("deep", deepResult.toStr());
    }

    @Test
    void testAncestorPropertyShadowing() {
        // Create an ancestor with a property
        Map<String, Datum> ancestorProps = new LinkedHashMap<>();
        ancestorProps.put("value", Datum.of("ancestor"));
        Datum.ScriptInstance ancestor = new Datum.ScriptInstance(2, ancestorProps);

        // Create main instance with same property name (should shadow ancestor)
        Map<String, Datum> props = new LinkedHashMap<>();
        props.put("ancestor", ancestor);
        props.put("value", Datum.of("instance"));
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, props);

        Datum result = CallOpcodesTestHelper.callScriptInstanceMethod(null, instance, "getat",
            List.of(Datum.symbol("value")));

        // Should get instance's value, not ancestor's
        assertEquals("instance", result.toStr());
    }

    @Test
    void testGetPropertyNotFound() {
        Map<String, Datum> props = new LinkedHashMap<>();
        props.put("existing", Datum.of(1));
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, props);

        Datum result = CallOpcodesTestHelper.callScriptInstanceMethod(null, instance, "getat",
            List.of(Datum.symbol("nonexistent")));

        assertTrue(result.isVoid());
    }

    @Test
    void testDeepAncestorChain() {
        // Create a 5-level deep ancestor chain
        Datum.ScriptInstance current = null;

        for (int i = 5; i >= 1; i--) {
            Map<String, Datum> chainProps = new LinkedHashMap<>();
            chainProps.put("level", Datum.of(i));
            chainProps.put("name" + i, Datum.of("Level " + i));
            if (current != null) {
                chainProps.put("ancestor", current);
            }
            current = new Datum.ScriptInstance(i, chainProps);
        }

        // current is now level 1, with ancestor chain to level 5
        Datum.ScriptInstance instance = current;

        // Should find name1 on instance
        Datum name1 = CallOpcodesTestHelper.callScriptInstanceMethod(null, instance, "getat",
            List.of(Datum.symbol("name1")));
        assertEquals("Level 1", name1.toStr());

        // Should find name3 on ancestor chain
        Datum name3 = CallOpcodesTestHelper.callScriptInstanceMethod(null, instance, "getat",
            List.of(Datum.symbol("name3")));
        assertEquals("Level 3", name3.toStr());

        // Should find name5 at the end of chain
        Datum name5 = CallOpcodesTestHelper.callScriptInstanceMethod(null, instance, "getat",
            List.of(Datum.symbol("name5")));
        assertEquals("Level 5", name5.toStr());
    }

    @Test
    void testAncestorChainDoesNotLoop() {
        // Create an instance that points to itself as ancestor (should not infinite loop)
        Map<String, Datum> props = new LinkedHashMap<>();
        props.put("value", Datum.of(42));
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, props);

        // This would be a bug in real code, but our implementation should handle it
        instance.properties().put("ancestor", instance);

        // Should find value and not infinite loop (due to safety limit)
        Datum result = CallOpcodesTestHelper.callScriptInstanceMethod(null, instance, "getat",
            List.of(Datum.symbol("value")));
        assertEquals(42, result.toInt());
    }

    // ========== Object Manager Pattern Tests ==========

    @Test
    void testSetaPropUpdatesPObjectList() {
        // Create pObjectList (simulates Object Manager's property list)
        Datum.PropList pObjectList = new Datum.PropList();

        // Create instance with pObjectList
        Map<String, Datum> props = new LinkedHashMap<>();
        props.put("pObjectList", pObjectList);
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, props);

        // Create a sub-instance to store
        Datum.ScriptInstance subInstance = new Datum.ScriptInstance(2, new LinkedHashMap<>());

        // setaProp(instance, #myManager, subInstance)
        CallOpcodesTestHelper.callScriptInstanceMethod(null, instance, "setaprop",
            List.of(Datum.symbol("myManager"), subInstance));

        assertEquals(subInstance, instance.properties().get("myManager"));

        // Should also be set on pObjectList
        assertEquals(subInstance, pObjectList.get("myManager"));
    }

    @Test
    void testSetaPropWithoutPObjectList() {
        // Create instance without pObjectList
        Map<String, Datum> props = new LinkedHashMap<>();
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, props);

        // setaProp should still work, just only sets on instance
        CallOpcodesTestHelper.callScriptInstanceMethod(null, instance, "setaprop",
            List.of(Datum.symbol("value"), Datum.of(42)));

        assertEquals(42, instance.properties().get("value").toInt());
    }

    // ========== ScriptRef Tests ==========

    @Test
    void testScriptRefInInstance() {
        // Create instance with __scriptRef__
        Map<String, Datum> props = new LinkedHashMap<>();
        props.put("__scriptRef__", Datum.ScriptRef.of(1, 10));
        Datum.ScriptInstance instance = new Datum.ScriptInstance(1, props);

        Datum scriptRef = instance.properties().get("__scriptRef__");
        assertTrue(scriptRef instanceof Datum.ScriptRef);

        Datum.ScriptRef ref = (Datum.ScriptRef) scriptRef;
        assertEquals(1, ref.castLibNum());
        assertEquals(10, ref.memberNum());
    }

    @Test
    void testScriptRefToString() {
        Datum ref = Datum.ScriptRef.of(2, 15);
        assertEquals("<script 15, 2>", ref.toString());
    }

    @Test
    void testScriptRefEquality() {
        Datum ref1 = Datum.ScriptRef.of(1, 10);
        Datum ref2 = Datum.ScriptRef.of(1, 10);
        Datum ref3 = Datum.ScriptRef.of(2, 10);

        assertEquals(ref1, ref2);
        assertNotEquals(ref1, ref3);
    }
}
