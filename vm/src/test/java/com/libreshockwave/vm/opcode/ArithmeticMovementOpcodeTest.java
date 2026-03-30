package com.libreshockwave.vm.opcode;

import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.vm.Scope;
import com.libreshockwave.vm.builtin.BuiltinRegistry;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ArithmeticMovementOpcodeTest {

    private OpcodeRegistry registry;
    private Scope scope;
    private Map<String, Datum> globals;

    @BeforeEach
    void setUp() {
        registry = new OpcodeRegistry();
        globals = new HashMap<>();
        scope = createScope(List.of());
    }

    private Scope createScope(List<Datum> arguments) {
        ScriptChunk.Handler handler = new ScriptChunk.Handler(
                0, 0, 0, 0, 0, 10, 0, 0,
                List.of(), List.of(), List.of(), Map.of()
        );
        return new Scope(null, handler, arguments, null);
    }

    private ExecutionContext createContext() {
        ScriptChunk.Handler.Instruction instr = new ScriptChunk.Handler.Instruction(0, Opcode.PUSH_ZERO, 0, 0);
        return new ExecutionContext(
                scope,
                instr,
                new BuiltinRegistry(),
                null,
                (script, handler, args, receiver) -> Datum.VOID,
                name -> null,
                new ExecutionContext.GlobalAccessor() {
                    @Override
                    public Datum getGlobal(String name) {
                        return globals.getOrDefault(name, Datum.VOID);
                    }

                    @Override
                    public void setGlobal(String name, Datum value) {
                        globals.put(name, value);
                    }
                },
                (name, args) -> Datum.VOID,
                errorState -> {},
                () -> "(no call stack)"
        );
    }

    @Test
    void multiplyListByFloatScalesEachElement() {
        scope.push(new Datum.List(List.of(Datum.of(10), Datum.of(20), Datum.of(30))));
        scope.push(Datum.of(0.5));

        OpcodeHandler handler = registry.get(Opcode.MUL);
        boolean advance = handler.execute(createContext());

        assertTrue(advance);
        Datum result = scope.pop();
        assertInstanceOf(Datum.List.class, result);
        var items = ((Datum.List) result).items();
        assertEquals(5.0, items.get(0).toDouble(), 0.001);
        assertEquals(10.0, items.get(1).toDouble(), 0.001);
        assertEquals(15.0, items.get(2).toDouble(), 0.001);
    }

    @Test
    void multiplyScalarByListScalesEachElement() {
        scope.push(Datum.of(0.5));
        scope.push(new Datum.List(List.of(Datum.of(10), Datum.of(20), Datum.of(30))));

        OpcodeHandler handler = registry.get(Opcode.MUL);
        boolean advance = handler.execute(createContext());

        assertTrue(advance);
        Datum result = scope.pop();
        assertInstanceOf(Datum.List.class, result);
        var items = ((Datum.List) result).items();
        assertEquals(5.0, items.get(0).toDouble(), 0.001);
        assertEquals(10.0, items.get(1).toDouble(), 0.001);
        assertEquals(15.0, items.get(2).toDouble(), 0.001);
    }

    @Test
    void divideListByFloatScalesEachElement() {
        scope.push(new Datum.List(List.of(Datum.of(10), Datum.of(20), Datum.of(30))));
        scope.push(Datum.of(2.0));

        OpcodeHandler handler = registry.get(Opcode.DIV);
        boolean advance = handler.execute(createContext());

        assertTrue(advance);
        Datum result = scope.pop();
        assertInstanceOf(Datum.List.class, result);
        var items = ((Datum.List) result).items();
        assertEquals(5.0, items.get(0).toDouble(), 0.001);
        assertEquals(10.0, items.get(1).toDouble(), 0.001);
        assertEquals(15.0, items.get(2).toDouble(), 0.001);
    }

    @Test
    void multiplyPointByFloatDoesNotTruncateFactorToZero() {
        scope.push(new Datum.Point(10, 20));
        scope.push(Datum.of(0.5));

        OpcodeHandler handler = registry.get(Opcode.MUL);
        boolean advance = handler.execute(createContext());

        assertTrue(advance);
        Datum result = scope.pop();
        assertInstanceOf(Datum.Point.class, result);
        Datum.Point point = (Datum.Point) result;
        assertEquals(5, point.x());
        assertEquals(10, point.y());
    }

    @Test
    void roomMovementInterpolationExpressionProducesInterpolatedList() {
        OpcodeHandler sub = registry.get(Opcode.SUB);
        OpcodeHandler mul = registry.get(Opcode.MUL);
        OpcodeHandler add = registry.get(Opcode.ADD);

        Datum.List start = new Datum.List(List.of(Datum.of(100), Datum.of(200), Datum.of(3000)));
        Datum.List dest = new Datum.List(List.of(Datum.of(120), Datum.of(240), Datum.of(3200)));

        scope.push(dest);
        scope.push(start);
        assertTrue(sub.execute(createContext()));

        scope.push(Datum.of(0.5));
        assertTrue(mul.execute(createContext()));

        scope.push(start);
        assertTrue(add.execute(createContext()));

        Datum result = scope.pop();
        assertInstanceOf(Datum.List.class, result);
        var items = ((Datum.List) result).items();
        assertEquals(110.0, items.get(0).toDouble(), 0.001);
        assertEquals(220.0, items.get(1).toDouble(), 0.001);
        assertEquals(3100.0, items.get(2).toDouble(), 0.001);
    }
}
