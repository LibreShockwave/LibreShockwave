package com.libreshockwave.vm;

import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

import java.util.List;
import java.util.Map;

/**
 * Unit tests for Datum value types.
 */
class DatumTest {

    @Test
    void testIntegerValues() {
        Datum zero = Datum.of(0);
        Datum one = Datum.of(1);
        Datum negative = Datum.of(-42);

        assertTrue(zero.isInt());
        assertTrue(one.isInt());
        assertTrue(negative.isInt());

        assertEquals(0, zero.toInt());
        assertEquals(1, one.toInt());
        assertEquals(-42, negative.toInt());

        // Singleton optimization
        assertSame(Datum.ZERO, zero);
        assertSame(Datum.ONE, one);
    }

    @Test
    void testFloatValues() {
        Datum pi = Datum.of(3.14159);
        Datum negative = Datum.of(-2.5);

        assertTrue(pi.isFloat());
        assertTrue(negative.isFloat());

        assertEquals(3.14159, pi.toDouble(), 0.00001);
        assertEquals(-2.5, negative.toDouble(), 0.00001);

        // Int conversion truncates
        assertEquals(3, pi.toInt());
    }

    @Test
    void testStringValues() {
        Datum hello = Datum.of("Hello World");
        Datum empty = Datum.of("");

        assertTrue(hello.isString());
        assertTrue(empty.isString());

        assertEquals("Hello World", hello.toStr());
        assertEquals("", empty.toStr());

        // Empty string singleton
        assertSame(Datum.EMPTY_STRING, empty);
    }

    @Test
    void testSymbolValues() {
        Datum sym = Datum.symbol("mySymbol");

        assertTrue(sym.isSymbol());
        assertEquals("mySymbol", ((Datum.Symbol) sym).name());
        assertEquals("#mySymbol", sym.toString());
    }

    @Test
    void testListValues() {
        Datum list = Datum.list(Datum.of(1), Datum.of(2), Datum.of(3));

        assertTrue(list.isList());
        assertEquals(3, ((Datum.List) list).items().size());
        assertEquals("[1, 2, 3]", list.toString());
    }

    @Test
    void testPropListValues() {
        Datum propList = Datum.propList(Map.of("x", Datum.of(10), "y", Datum.of(20)));

        assertTrue(propList.isPropList());
        assertEquals(2, ((Datum.PropList) propList).size());
    }

    @Test
    void testPropListDuplicateKeys() {
        // Director's PropList supports duplicate keys (e.g. [#string: "user", #string: "pass"])
        Datum.PropList pl = new Datum.PropList();
        pl.add("string", Datum.of("username"));
        pl.add("string", Datum.of("password"));

        // Both entries must be preserved
        assertEquals(2, pl.size());

        // get() returns the first match
        assertEquals("username", pl.get("string").toStr());

        // Positional access returns each entry independently
        assertEquals("username", pl.getValue(0).toStr());
        assertEquals("password", pl.getValue(1).toStr());

        // Both keys are "string"
        assertEquals("string", pl.getKey(0));
        assertEquals("string", pl.getKey(1));

        // toString shows both entries
        assertEquals("[#string: \"username\", #string: \"password\"]", pl.toString());
    }

    @Test
    void testPropListDuplicateKeysViaPushPropList() {
        // Verify that the propList literal [#string: a, #string: b] preserves both entries
        Datum.PropList pl = new Datum.PropList();
        pl.add("short", Datum.of(1));
        pl.add("string", Datum.of("hello"));
        pl.add("short", Datum.of(2));  // duplicate key

        assertEquals(3, pl.size());
        // get returns first match
        assertEquals(1, pl.get("short").toInt());
        // positional access
        assertEquals(1, pl.getValue(0).toInt());
        assertEquals("hello", pl.getValue(1).toStr());
        assertEquals(2, pl.getValue(2).toInt());
    }

    @Test
    void testPropListPutUpdatesFirstMatch() {
        Datum.PropList pl = new Datum.PropList();
        pl.add("a", Datum.of(1));
        pl.add("b", Datum.of(2));
        pl.add("a", Datum.of(3));

        // put updates FIRST match only
        pl.put("a", Datum.of(99));
        assertEquals(3, pl.size());
        assertEquals(99, pl.getValue(0).toInt());  // first "a" updated
        assertEquals(3, pl.getValue(2).toInt());    // second "a" unchanged
    }

    @Test
    void testLingoEqualsCrossType() {
        // Director: #foo = "foo" is TRUE (case-insensitive)
        assertTrue(Datum.symbol("info").lingoEquals(Datum.of("info")));
        assertTrue(Datum.symbol("Info").lingoEquals(Datum.of("info")));
        assertTrue(Datum.of("INFO").lingoEquals(Datum.symbol("info")));

        // Director: VOID = 0 is TRUE
        assertTrue(Datum.VOID.lingoEquals(Datum.ZERO));
        assertTrue(Datum.ZERO.lingoEquals(Datum.VOID));

        // Director: 1 = 1.0 is TRUE
        assertTrue(Datum.of(1).lingoEquals(Datum.of(1.0)));

        // Not equal
        assertFalse(Datum.symbol("foo").lingoEquals(Datum.of("bar")));
        assertFalse(Datum.of(1).lingoEquals(Datum.of(2)));
    }

    @Test
    void testListGetOneWithSymbolStringCrossType() {
        // Simulates connection manager's exists() check:
        // pItemList contains string "info", searching for symbol #Info
        Datum.List list = (Datum.List) Datum.list(Datum.of("info"), Datum.of("mus"));
        Datum target = Datum.symbol("Info");

        // getOne should find the match using lingoEquals
        boolean found = false;
        for (int i = 0; i < list.items().size(); i++) {
            if (list.items().get(i).lingoEquals(target)) {
                found = true;
                break;
            }
        }
        assertTrue(found, "Symbol #Info should match string \"info\" in list search");
    }

    @Test
    void testTruthiness() {
        // Falsy values
        assertFalse(Datum.VOID.isTruthy());
        assertFalse(Datum.ZERO.isTruthy());
        assertFalse(Datum.of(0.0).isTruthy());
        assertFalse(Datum.EMPTY_STRING.isTruthy());

        // Truthy values
        assertTrue(Datum.ONE.isTruthy());
        assertTrue(Datum.of(42).isTruthy());
        assertTrue(Datum.of(-1).isTruthy());
        assertTrue(Datum.of(0.001).isTruthy());
        assertTrue(Datum.of("hello").isTruthy());
        assertTrue(Datum.list().isTruthy());
    }

    @Test
    void testTypeCoercion() {
        // String to number
        assertEquals(42, Datum.of("42").toInt());
        assertEquals(3.14, Datum.of("3.14").toDouble(), 0.001);

        // Invalid string to number
        assertEquals(0, Datum.of("abc").toInt());
        assertEquals(0.0, Datum.of("abc").toDouble(), 0.001);

        // Number to string
        assertEquals("42", Datum.of(42).toStr());
        assertEquals("3.14", Datum.of(3.14).toStr());
    }

    @Test
    void testPointAndRect() {
        Datum point = new Datum.Point(100, 200);
        Datum rect = new Datum.Rect(0, 0, 640, 480);

        assertEquals("point(100, 200)", point.toString());
        assertEquals("rect(0, 0, 640, 480)", rect.toString());
    }

    @Test
    void testSpriteAndCastMemberRef() {
        Datum sprite = Datum.SpriteRef.of(5);
        Datum member = Datum.CastMemberRef.of(1, 10);

        assertEquals("sprite(5)", sprite.toString());
        assertEquals("member(10, 1)", member.toString());
    }

    @Test
    void testNumericOperations() {
        Datum a = Datum.of(10);
        Datum b = Datum.of(3);

        // These would be done by the VM, but we can test the coercion
        assertEquals(10, a.toInt());
        assertEquals(3, b.toInt());
        assertEquals(10.0, a.toDouble(), 0.001);
        assertEquals(3.0, b.toDouble(), 0.001);
    }
}
