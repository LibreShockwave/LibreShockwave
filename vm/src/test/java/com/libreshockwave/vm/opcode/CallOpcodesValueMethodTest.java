package com.libreshockwave.vm.opcode;

import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Method;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNotSame;

class CallOpcodesValueMethodTest {
    @Test
    void rectDuplicateReturnsIndependentRectValue() throws Exception {
        Datum.Rect original = new Datum.Rect(0, 0, 92, 480);

        Datum result = callRectMethod(original, "duplicate", List.of());

        Datum.Rect duplicate = assertInstanceOf(Datum.Rect.class, result);
        assertNotSame(original, duplicate);
        assertEquals(original, duplicate);

        duplicate.setComponent(3, 230);
        assertEquals(92, original.right());
        assertEquals(230, duplicate.right());
    }

    @Test
    void pointDuplicateReturnsIndependentPointValue() throws Exception {
        Datum.Point original = new Datum.Point(17, -5);

        Datum result = callPointMethod(original, "duplicate", List.of());

        Datum.Point duplicate = assertInstanceOf(Datum.Point.class, result);
        assertNotSame(original, duplicate);
        assertEquals(original, duplicate);

        duplicate.setComponent(1, 99);
        assertEquals(17, original.x());
        assertEquals(99, duplicate.x());
    }

    @SuppressWarnings("unchecked")
    private static Datum callRectMethod(Datum.Rect rect, String methodName, List<Datum> args) throws Exception {
        Method method = CallOpcodes.class.getDeclaredMethod("handleRectMethod",
                Datum.Rect.class, String.class, List.class);
        method.setAccessible(true);
        return (Datum) method.invoke(null, rect, methodName, args);
    }

    @SuppressWarnings("unchecked")
    private static Datum callPointMethod(Datum.Point point, String methodName, List<Datum> args) throws Exception {
        Method method = CallOpcodes.class.getDeclaredMethod("handlePointMethod",
                Datum.Point.class, String.class, List.class);
        method.setAccessible(true);
        return (Datum) method.invoke(null, point, methodName, args);
    }
}
