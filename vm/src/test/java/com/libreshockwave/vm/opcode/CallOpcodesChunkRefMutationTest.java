package com.libreshockwave.vm.opcode;

import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.id.ChunkId;
import com.libreshockwave.id.VarType;
import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.Scope;
import com.libreshockwave.vm.builtin.BuiltinRegistry;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;

class CallOpcodesChunkRefMutationTest {

    @Test
    void deletingCharRangeThroughChunkRefMutatesParameter() {
        ExecutionContext ctx = createContext(List.of(Datum.of("\\TCODE/client/")));
        Datum.VarRef varRef = new Datum.VarRef(VarType.PARAM, 0);

        Datum.ChunkRef chunkRef = invokeGetPropRef(
                ctx,
                varRef,
                List.of(Datum.symbol("char"), Datum.of(1), Datum.of(6)));

        invokeDelete(ctx, chunkRef);

        assertEquals("/client/", ctx.getParam(0).toStr());
    }

    private static Datum.ChunkRef invokeGetPropRef(ExecutionContext ctx, Datum.VarRef varRef, List<Datum> args) {
        try {
            Method method = CallOpcodes.class.getDeclaredMethod(
                    "handleVarRefMethod",
                    ExecutionContext.class,
                    Datum.VarRef.class,
                    String.class,
                    List.class);
            method.setAccessible(true);
            return (Datum.ChunkRef) method.invoke(null, ctx, varRef, "getPropRef", args);
        } catch (NoSuchMethodException | IllegalAccessException e) {
            throw new AssertionError(e);
        } catch (InvocationTargetException e) {
            throw unwrap(e);
        }
    }

    private static void invokeDelete(ExecutionContext ctx, Datum.ChunkRef chunkRef) {
        try {
            Method method = CallOpcodes.class.getDeclaredMethod(
                    "handleChunkRefMethod",
                    ExecutionContext.class,
                    Datum.ChunkRef.class,
                    String.class,
                    List.class);
            method.setAccessible(true);
            method.invoke(null, ctx, chunkRef, "delete", List.of());
        } catch (NoSuchMethodException | IllegalAccessException e) {
            throw new AssertionError(e);
        } catch (InvocationTargetException e) {
            throw unwrap(e);
        }
    }

    private static RuntimeException unwrap(InvocationTargetException e) {
        Throwable cause = e.getCause();
        if (cause instanceof RuntimeException runtimeException) {
            return runtimeException;
        }
        if (cause instanceof Error error) {
            throw error;
        }
        return new RuntimeException(cause);
    }

    private static ExecutionContext createContext(List<Datum> args) {
        ScriptChunk.Handler.Instruction instruction = new ScriptChunk.Handler.Instruction(
                0,
                Opcode.OBJ_CALL,
                0,
                0);
        ScriptChunk.Handler handler = new ScriptChunk.Handler(
                0,
                0,
                0,
                0,
                args.size(),
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
                ScriptChunk.ScriptType.PARENT,
                0,
                List.of(handler),
                List.of(),
                List.of(),
                List.of(),
                new byte[0]);
        Scope scope = new Scope(script, handler, args, Datum.VOID);
        BuiltinRegistry builtins = new BuiltinRegistry();
        return new ExecutionContext(
                scope,
                instruction,
                builtins,
                null,
                (scriptChunk, targetHandler, invokeArgs, receiver) -> Datum.VOID,
                name -> null,
                new ExecutionContext.GlobalAccessor() {
                    @Override
                    public Datum getGlobal(String name) {
                        return Datum.VOID;
                    }

                    @Override
                    public void setGlobal(String name, Datum value) {
                    }
                },
                (name, invokeArgs) -> builtins.invoke(name, new LingoVM(null), invokeArgs),
                errorState -> {
                },
                () -> "(test)");
    }
}
