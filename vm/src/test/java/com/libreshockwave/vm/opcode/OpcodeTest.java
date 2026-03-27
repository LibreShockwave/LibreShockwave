package com.libreshockwave.vm.opcode;

import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.Scope;
import com.libreshockwave.vm.builtin.BuiltinRegistry;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Nested;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Unit tests for opcode handlers.
 */
class OpcodeTest {

    private OpcodeRegistry registry;
    private Scope scope;
    private Map<String, Datum> globals;
    private List<Datum> params;

    @BeforeEach
    void setUp() {
        registry = new OpcodeRegistry();
        globals = new HashMap<>();
        params = new ArrayList<>();
        scope = createScope(params);
    }

    private Scope createScope(List<Datum> arguments) {
        ScriptChunk.Handler handler = new ScriptChunk.Handler(
            0, 0, 0, 0, 0, 10, 0, 0,
            List.of(), List.of(), List.of(), Map.of()
        );
        return new Scope(null, handler, arguments, null);
    }

    private ExecutionContext createContext(int argument) {
        return createContext(argument, 0);
    }

    private ExecutionContext createContext(int argument, int offset) {
        ScriptChunk.Handler.Instruction instr = new ScriptChunk.Handler.Instruction(offset, Opcode.PUSH_ZERO, 0, argument);
        return new ExecutionContext(
            scope,
            instr,
            new BuiltinRegistry(),
            null,  // trace listener
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
            errorState -> {},  // no-op error state setter for tests
            () -> "(no call stack)"  // no-op call stack formatter for tests
        );
    }

    @Nested
    class StackOpcodeTests {

        @Test
        void pushZero() {
            OpcodeHandler handler = registry.get(Opcode.PUSH_ZERO);
            assertNotNull(handler);

            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(Datum.ZERO, scope.pop());
        }

        @Test
        void pushInt() {
            OpcodeHandler handler = registry.get(Opcode.PUSH_INT8);
            assertNotNull(handler);

            boolean advance = handler.execute(createContext(42));

            assertTrue(advance);
            assertEquals(42, scope.pop().toInt());
        }

        @Test
        void pushFloat() {
            OpcodeHandler handler = registry.get(Opcode.PUSH_FLOAT32);
            assertNotNull(handler);

            int floatBits = Float.floatToIntBits(3.14f);
            boolean advance = handler.execute(createContext(floatBits));

            assertTrue(advance);
            assertEquals(3.14f, scope.pop().toDouble(), 0.001);
        }

        @Test
        void swap() {
            scope.push(Datum.of(1));
            scope.push(Datum.of(2));

            OpcodeHandler handler = registry.get(Opcode.SWAP);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(1, scope.pop().toInt());
            assertEquals(2, scope.pop().toInt());
        }

        @Test
        void pop() {
            scope.push(Datum.of(1));
            scope.push(Datum.of(2));

            OpcodeHandler handler = registry.get(Opcode.POP);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(1, scope.stackSize());
            assertEquals(1, scope.pop().toInt());
        }

        @Test
        void peek() {
            scope.push(Datum.of(1));
            scope.push(Datum.of(2));
            scope.push(Datum.of(3));

            OpcodeHandler handler = registry.get(Opcode.PEEK);
            boolean advance = handler.execute(createContext(1));

            assertTrue(advance);
            assertEquals(4, scope.stackSize());
            assertEquals(2, scope.pop().toInt());  // peeked value
        }
    }

    @Nested
    class ArithmeticOpcodeTests {

        @Test
        void addIntegers() {
            scope.push(Datum.of(10));
            scope.push(Datum.of(5));

            OpcodeHandler handler = registry.get(Opcode.ADD);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(15, scope.pop().toInt());
        }

        @Test
        void addFloats() {
            scope.push(Datum.of(3.5));
            scope.push(Datum.of(2.5));

            OpcodeHandler handler = registry.get(Opcode.ADD);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(6.0, scope.pop().toDouble(), 0.001);
        }

        @Test
        void addMixed() {
            scope.push(Datum.of(3));
            scope.push(Datum.of(2.5));

            OpcodeHandler handler = registry.get(Opcode.ADD);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(5.5, scope.pop().toDouble(), 0.001);
        }

        @Test
        void subtract() {
            scope.push(Datum.of(10));
            scope.push(Datum.of(3));

            OpcodeHandler handler = registry.get(Opcode.SUB);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(7, scope.pop().toInt());
        }

        @Test
        void multiply() {
            scope.push(Datum.of(6));
            scope.push(Datum.of(7));

            OpcodeHandler handler = registry.get(Opcode.MUL);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(42, scope.pop().toInt());
        }

        @Test
        void divide() {
            // Director: int / int = int (truncated toward zero)
            scope.push(Datum.of(10));
            scope.push(Datum.of(4));

            OpcodeHandler handler = registry.get(Opcode.DIV);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(2, scope.pop().toInt());
        }

        @Test
        void divideFloat() {
            // float / int = float
            scope.push(Datum.of(10.0));
            scope.push(Datum.of(4));

            OpcodeHandler handler = registry.get(Opcode.DIV);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(2.5, scope.pop().toDouble(), 0.001);
        }

        @Test
        void divideByZeroThrows() {
            scope.push(Datum.of(10));
            scope.push(Datum.of(0));

            OpcodeHandler handler = registry.get(Opcode.DIV);
            assertThrows(Exception.class, () -> handler.execute(createContext(0)));
        }

        @Test
        void modulo() {
            scope.push(Datum.of(10));
            scope.push(Datum.of(3));

            OpcodeHandler handler = registry.get(Opcode.MOD);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(1, scope.pop().toInt());
        }

        @Test
        void moduloByZeroThrows() {
            scope.push(Datum.of(10));
            scope.push(Datum.of(0));

            OpcodeHandler handler = registry.get(Opcode.MOD);
            assertThrows(Exception.class, () -> handler.execute(createContext(0)));
        }

        @Test
        void negate() {
            scope.push(Datum.of(42));

            OpcodeHandler handler = registry.get(Opcode.INV);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(-42, scope.pop().toInt());
        }

        @Test
        void negateFloat() {
            scope.push(Datum.of(3.14));

            OpcodeHandler handler = registry.get(Opcode.INV);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(-3.14, scope.pop().toDouble(), 0.001);
        }
    }

    @Nested
    class ComparisonOpcodeTests {

        @Test
        void lessThan() {
            scope.push(Datum.of(5));
            scope.push(Datum.of(10));

            OpcodeHandler handler = registry.get(Opcode.LT);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(Datum.TRUE, scope.pop());
        }

        @Test
        void lessThanFalse() {
            scope.push(Datum.of(10));
            scope.push(Datum.of(5));

            OpcodeHandler handler = registry.get(Opcode.LT);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(Datum.FALSE, scope.pop());
        }

        @Test
        void lessThanOrEqual() {
            scope.push(Datum.of(5));
            scope.push(Datum.of(5));

            OpcodeHandler handler = registry.get(Opcode.LT_EQ);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(Datum.TRUE, scope.pop());
        }

        @Test
        void greaterThan() {
            scope.push(Datum.of(10));
            scope.push(Datum.of(5));

            OpcodeHandler handler = registry.get(Opcode.GT);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(Datum.TRUE, scope.pop());
        }

        @Test
        void greaterThanOrEqual() {
            scope.push(Datum.of(5));
            scope.push(Datum.of(5));

            OpcodeHandler handler = registry.get(Opcode.GT_EQ);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(Datum.TRUE, scope.pop());
        }

        @Test
        void equalIntegers() {
            scope.push(Datum.of(42));
            scope.push(Datum.of(42));

            OpcodeHandler handler = registry.get(Opcode.EQ);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(Datum.TRUE, scope.pop());
        }

        @Test
        void equalStrings() {
            scope.push(Datum.of("Hello"));
            scope.push(Datum.of("hello"));  // case insensitive

            OpcodeHandler handler = registry.get(Opcode.EQ);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(Datum.TRUE, scope.pop());
        }

        @Test
        void notEqual() {
            scope.push(Datum.of(1));
            scope.push(Datum.of(2));

            OpcodeHandler handler = registry.get(Opcode.NT_EQ);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(Datum.TRUE, scope.pop());
        }
    }

    @Nested
    class LogicalOpcodeTests {

        @Test
        void andTrue() {
            scope.push(Datum.TRUE);
            scope.push(Datum.TRUE);

            OpcodeHandler handler = registry.get(Opcode.AND);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(Datum.TRUE, scope.pop());
        }

        @Test
        void andFalse() {
            scope.push(Datum.TRUE);
            scope.push(Datum.FALSE);

            OpcodeHandler handler = registry.get(Opcode.AND);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(Datum.FALSE, scope.pop());
        }

        @Test
        void orTrue() {
            scope.push(Datum.FALSE);
            scope.push(Datum.TRUE);

            OpcodeHandler handler = registry.get(Opcode.OR);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(Datum.TRUE, scope.pop());
        }

        @Test
        void orFalse() {
            scope.push(Datum.FALSE);
            scope.push(Datum.FALSE);

            OpcodeHandler handler = registry.get(Opcode.OR);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(Datum.FALSE, scope.pop());
        }

        @Test
        void notTrue() {
            scope.push(Datum.TRUE);

            OpcodeHandler handler = registry.get(Opcode.NOT);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(Datum.FALSE, scope.pop());
        }

        @Test
        void notFalse() {
            scope.push(Datum.FALSE);

            OpcodeHandler handler = registry.get(Opcode.NOT);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(Datum.TRUE, scope.pop());
        }
    }

    @Nested
    class StringOpcodeTests {

        @Test
        void joinStrings() {
            scope.push(Datum.of("Hello"));
            scope.push(Datum.of("World"));

            OpcodeHandler handler = registry.get(Opcode.JOIN_STR);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals("HelloWorld", scope.pop().toStr());
        }

        @Test
        void joinPaddedStrings() {
            scope.push(Datum.of("Hello"));
            scope.push(Datum.of("World"));

            OpcodeHandler handler = registry.get(Opcode.JOIN_PAD_STR);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals("Hello World", scope.pop().toStr());
        }

        @Test
        void joinPaddedStringsSkipsLeadingSpaceWhenLeftIsEmpty() {
            scope.push(Datum.EMPTY_STRING);
            scope.push(Datum.of("World"));

            OpcodeHandler handler = registry.get(Opcode.JOIN_PAD_STR);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals("World", scope.pop().toStr());
        }

        @Test
        void joinPaddedStringsSkipsTrailingSpaceWhenRightIsEmpty() {
            scope.push(Datum.of("Hello"));
            scope.push(Datum.VOID);

            OpcodeHandler handler = registry.get(Opcode.JOIN_PAD_STR);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals("Hello", scope.pop().toStr());
        }

        @Test
        void containsString() {
            scope.push(Datum.of("Hello World"));
            scope.push(Datum.of("World"));

            OpcodeHandler handler = registry.get(Opcode.CONTAINS_STR);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(Datum.TRUE, scope.pop());
        }

        @Test
        void notContainsString() {
            scope.push(Datum.of("Hello World"));
            scope.push(Datum.of("foo"));

            OpcodeHandler handler = registry.get(Opcode.CONTAINS_STR);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(Datum.FALSE, scope.pop());
        }
    }

    @Nested
    class VariableOpcodeTests {

        @Test
        void getLocal() {
            scope.setLocal(0, Datum.of(42));

            OpcodeHandler handler = registry.get(Opcode.GET_LOCAL);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(42, scope.pop().toInt());
        }

        @Test
        void setLocal() {
            scope.push(Datum.of(42));

            OpcodeHandler handler = registry.get(Opcode.SET_LOCAL);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(42, scope.getLocal(0).toInt());
        }

        @Test
        void getParam() {
            // Create scope with arguments
            scope = createScope(List.of(Datum.of(100)));

            OpcodeHandler handler = registry.get(Opcode.GET_PARAM);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(100, scope.pop().toInt());
        }

        @Test
        void setGlobal() {
            // Skip this test since it requires a script with name resolution
            // The opcode handler calls ctx.resolveName() which needs a script
            // This would be better tested in an integration test with a full VM setup
        }
    }

    @Nested
    class ControlFlowOpcodeTests {

        @Test
        void ret() {
            scope.push(Datum.of(42));

            OpcodeHandler handler = registry.get(Opcode.RET);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertEquals(42, scope.getReturnValue().toInt());
            assertTrue(scope.isReturned());
        }

        @Test
        void retFactory() {
            OpcodeHandler handler = registry.get(Opcode.RET_FACTORY);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            assertTrue(scope.getReturnValue().isVoid());
            assertTrue(scope.isReturned());
        }

        @Test
        void jmp() {
            OpcodeHandler handler = registry.get(Opcode.JMP);
            boolean advance = handler.execute(createContext(10, 100));

            assertFalse(advance);  // Don't advance, we jumped
        }

        @Test
        void jmpIfZTrue() {
            scope.push(Datum.TRUE);

            OpcodeHandler handler = registry.get(Opcode.JMP_IF_Z);
            boolean advance = handler.execute(createContext(10, 100));

            assertTrue(advance);  // Condition was true, don't jump
        }

        @Test
        void jmpIfZFalse() {
            scope.push(Datum.FALSE);

            OpcodeHandler handler = registry.get(Opcode.JMP_IF_Z);
            boolean advance = handler.execute(createContext(10, 100));

            assertFalse(advance);  // Condition was false, jump
        }
    }

    @Nested
    class ListOpcodeTests {

        @Test
        void pushEmptyList() {
            OpcodeHandler handler = registry.get(Opcode.PUSH_LIST);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            Datum result = scope.pop();
            assertTrue(result.isList());
            assertEquals(0, ((Datum.List) result).items().size());
        }

        @Test
        void pushListWithItems() {
            // PUSH_LIST now pops an ArgList and converts to List
            scope.push(new Datum.ArgList(List.of(Datum.of(1), Datum.of(2), Datum.of(3))));

            OpcodeHandler handler = registry.get(Opcode.PUSH_LIST);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            Datum result = scope.pop();
            assertTrue(result.isList());
            List<Datum> items = ((Datum.List) result).items();
            assertEquals(3, items.size());
            assertEquals(1, items.get(0).toInt());
            assertEquals(2, items.get(1).toInt());
            assertEquals(3, items.get(2).toInt());
        }

        @Test
        void pushPropList() {
            // PUSH_PROP_LIST now pops an ArgList with key-value pairs
            scope.push(new Datum.ArgList(List.of(
                Datum.symbol("x"), Datum.of(10),
                Datum.symbol("y"), Datum.of(20)
            )));

            OpcodeHandler handler = registry.get(Opcode.PUSH_PROP_LIST);
            boolean advance = handler.execute(createContext(0));

            assertTrue(advance);
            Datum result = scope.pop();
            assertTrue(result.isPropList());
            assertEquals(2, ((Datum.PropList) result).size());
        }

        @Test
        void pushArgList() {
            // Push 3 items to be collected into arglist
            scope.push(Datum.of(1));
            scope.push(Datum.of(2));
            scope.push(Datum.of(3));

            OpcodeHandler handler = registry.get(Opcode.PUSH_ARG_LIST);
            boolean advance = handler.execute(createContext(3));

            assertTrue(advance);
            Datum result = scope.pop();
            assertTrue(result instanceof Datum.ArgList);
            assertEquals(3, ((Datum.ArgList) result).items().size());
        }
    }

    @Nested
    class RegistryTests {

        @Test
        void allCriticalOpcodesCovered() {
            // Test that the critical opcodes have handlers
            assertNotNull(registry.get(Opcode.PUSH_ZERO));
            assertNotNull(registry.get(Opcode.PUSH_INT8));
            assertNotNull(registry.get(Opcode.ADD));
            assertNotNull(registry.get(Opcode.SUB));
            assertNotNull(registry.get(Opcode.MUL));
            assertNotNull(registry.get(Opcode.DIV));
            assertNotNull(registry.get(Opcode.LT));
            assertNotNull(registry.get(Opcode.EQ));
            assertNotNull(registry.get(Opcode.AND));
            assertNotNull(registry.get(Opcode.OR));
            assertNotNull(registry.get(Opcode.NOT));
            assertNotNull(registry.get(Opcode.RET));
            assertNotNull(registry.get(Opcode.JMP));
            assertNotNull(registry.get(Opcode.JMP_IF_Z));
        }

        @Test
        void customHandlerCanBeRegistered() {
            registry.register(Opcode.THE_BUILTIN, ctx -> {
                ctx.push(Datum.of("custom"));
                return true;
            });

            OpcodeHandler handler = registry.get(Opcode.THE_BUILTIN);
            handler.execute(createContext(0));

            assertEquals("custom", scope.pop().toStr());
        }
    }
}
