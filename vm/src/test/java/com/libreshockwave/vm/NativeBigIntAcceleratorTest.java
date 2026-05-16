package com.libreshockwave.vm;

import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.*;

class NativeBigIntAcceleratorTest {
    @Test
    void acceleratesPowModForHugeIntShapedObjects() {
        Datum base = hugeInt(7);
        Datum exponent = hugeInt(560);
        Datum modulus = hugeInt(561);

        Datum result = NativeBigIntAccelerator.tryPowMod(base, List.of(exponent, modulus));

        assertInstanceOf(Datum.ScriptInstance.class, result);
        assertEquals(1, toInt((Datum.ScriptInstance) result));
    }

    @Test
    void ignoresObjectsWithoutHugeIntShape() {
        Datum.ScriptInstance receiver = new Datum.ScriptInstance(1, Map.of("pBase", Datum.of(10000)));

        assertNull(NativeBigIntAccelerator.tryPowMod(receiver, List.of(hugeInt(2), hugeInt(5))));
    }

    private static Datum.ScriptInstance hugeInt(int value) {
        Map<String, Datum> props = new LinkedHashMap<>();
        props.put(Datum.PROP_SCRIPT_REF, Datum.VOID);
        props.put("pData_test", toData(value));
        props.put("pNegative", value < 0 ? Datum.ONE : Datum.ZERO);
        props.put("pBase", Datum.of(10000));
        props.put("pDigits", Datum.of(4));
        props.put("pScript", Datum.VOID);
        return new Datum.ScriptInstance(value, props);
    }

    private static Datum.List toData(int value) {
        int remaining = Math.abs(value);
        java.util.ArrayList<Datum> items = new java.util.ArrayList<>();
        while (remaining != 0) {
            items.add(Datum.of(remaining % 10000));
            remaining /= 10000;
        }
        return new Datum.List(items);
    }

    private static int toInt(Datum.ScriptInstance instance) {
        Datum.List data = (Datum.List) instance.properties().get("pData_test");
        int value = 0;
        for (int i = data.items().size() - 1; i >= 0; i--) {
            value = value * 10000 + data.items().get(i).toInt();
        }
        return instance.properties().get("pNegative").toInt() != 0 ? -value : value;
    }
}
