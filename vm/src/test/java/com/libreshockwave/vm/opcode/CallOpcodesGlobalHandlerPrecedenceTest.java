package com.libreshockwave.vm.opcode;

import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.id.ChunkId;
import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.vm.HandlerRef;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.Scope;
import com.libreshockwave.vm.builtin.BuiltinRegistry;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class CallOpcodesGlobalHandlerPrecedenceTest {

    @Test
    void extCallPrefersAuthoredHandlersOverBuiltinFallbacks() {
        AtomicInteger builtinCalls = new AtomicInteger();
        AtomicInteger handlerCalls = new AtomicInteger();
        BuiltinRegistry builtins = new BuiltinRegistry();
        builtins.register("#1", (vm, args) -> {
            builtinCalls.incrementAndGet();
            return Datum.of("builtin");
        });

        ExecutionContext ctx = createContext(
                1,
                builtins,
                name -> "#1".equals(name) ? new HandlerRef(null, null) : null,
                (scriptChunk, handler, args, receiver) -> {
                    handlerCalls.incrementAndGet();
                    return Datum.of("handler");
                });
        ctx.push(new Datum.ArgList(List.of(Datum.of("arg"))));

        boolean advance = invokeExtCall(ctx);

        assertTrue(advance);
        assertEquals(1, handlerCalls.get());
        assertEquals(0, builtinCalls.get());
        assertEquals("handler", ctx.pop().toStr());
    }

    @Test
    void extCallStillFallsBackToBuiltinsWhenNoHandlerExists() {
        AtomicInteger builtinCalls = new AtomicInteger();
        BuiltinRegistry builtins = new BuiltinRegistry();
        builtins.register("#1", (vm, args) -> {
            builtinCalls.incrementAndGet();
            return Datum.of("builtin");
        });

        ExecutionContext ctx = createContext(
                1,
                builtins,
                name -> null,
                (scriptChunk, handler, args, receiver) -> Datum.of("handler"));
        ctx.push(new Datum.ArgList(List.of(Datum.of("arg"))));

        boolean advance = invokeExtCall(ctx);

        assertTrue(advance);
        assertEquals(1, builtinCalls.get());
        assertEquals("builtin", ctx.pop().toStr());
    }

    private static boolean invokeExtCall(ExecutionContext ctx) {
        try {
            Method method = CallOpcodes.class.getDeclaredMethod("extCall", ExecutionContext.class);
            method.setAccessible(true);
            return (boolean) method.invoke(null, ctx);
        } catch (NoSuchMethodException | IllegalAccessException e) {
            throw new AssertionError(e);
        } catch (InvocationTargetException e) {
            Throwable cause = e.getCause();
            if (cause instanceof RuntimeException runtimeException) {
                throw runtimeException;
            }
            if (cause instanceof Error error) {
                throw error;
            }
            throw new RuntimeException(cause);
        }
    }

    private static ExecutionContext createContext(
            int argument,
            BuiltinRegistry builtins,
            ExecutionContext.HandlerFinder handlerFinder,
            ExecutionContext.HandlerExecutor handlerExecutor) {
        ScriptChunk.Handler.Instruction instruction = new ScriptChunk.Handler.Instruction(
                0,
                Opcode.EXT_CALL,
                0,
                argument);
        ScriptChunk.Handler handler = new ScriptChunk.Handler(
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                List.of(),
                List.of(),
                List.of(instruction),
                Map.of(0, 0));
        ScriptChunk script = new ScriptChunk(
                null,
                new ChunkId(1),
                ScriptChunk.ScriptType.MOVIE_SCRIPT,
                0,
                List.of(handler),
                List.of(),
                List.of(),
                List.of(),
                new byte[0]);
        Scope scope = new Scope(script, handler, List.of(), Datum.VOID);
        return new ExecutionContext(
                scope,
                instruction,
                builtins,
                null,
                handlerExecutor,
                handlerFinder,
                new ExecutionContext.GlobalAccessor() {
                    @Override
                    public Datum getGlobal(String name) {
                        return Datum.VOID;
                    }

                    @Override
                    public void setGlobal(String name, Datum value) {
                    }
                },
                (name, args) -> builtins.invoke(name, new LingoVM(null), args),
                errorState -> {
                },
                () -> "(test)");
    }
}
