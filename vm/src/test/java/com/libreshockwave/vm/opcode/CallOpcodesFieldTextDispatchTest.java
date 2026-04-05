package com.libreshockwave.vm.opcode;

import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.id.ChunkId;
import com.libreshockwave.id.VarType;
import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.Scope;
import com.libreshockwave.vm.builtin.BuiltinRegistry;
import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.support.NoOpCastLibProvider;
import org.junit.jupiter.api.Test;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;

class CallOpcodesFieldTextDispatchTest {

    @Test
    void dispatchMethodTreatsFieldTextAsStringReceiver() {
        ExecutionContext ctx = createContext(0, 0);

        Datum result = invokeDispatchMethod(
                ctx,
                new Datum.FieldText("alpha beta", 11, 7),
                "count",
                List.of(Datum.symbol("word")));

        assertEquals(2, result.toInt());
    }

    @Test
    void handleVarRefMethodTreatsFieldTextLocalsAsStrings() {
        ExecutionContext ctx = createContext(0, 1);
        ctx.setLocal(0, new Datum.FieldText("ok", 11, 7));

        Datum result = invokeHandleVarRefMethod(
                ctx,
                new Datum.VarRef(VarType.LOCAL, 0),
                "length",
                List.of());

        assertEquals(2, result.toInt());
    }

    @Test
    void stringOpcodesReturnsFieldDatumForFieldContextVars() {
        RecordingCastProvider provider = new RecordingCastProvider();
        CastLibProvider.setProvider(provider);
        try {
            ExecutionContext ctx = createContext(0, 0);

            Datum result = invokeGetContextVar(ctx, 0x6, Datum.of("memberalias.index"), Datum.of(11));

            Datum.FieldText fieldText = assertInstanceOf(Datum.FieldText.class, result);
            assertEquals("ok", fieldText.value());
            assertEquals(11, provider.lastFieldCastId);
            assertEquals("memberalias.index", provider.lastFieldMemberName);
        } finally {
            CastLibProvider.clearProvider();
        }
    }

    @Test
    void charChunkHelpersIgnoreInvertedRanges() {
        assertEquals("", invokeGetStringChunk("abc", "char", 1, -1));
        assertEquals("abc", invokeDeleteChunkRange("abc", "char", 1, -1));
        assertEquals("", invokeGetStringChunk("abc", "char", 3, 1));
        assertEquals("abc", invokeDeleteChunkRange("abc", "char", 3, 1));
    }

    private static Datum invokeDispatchMethod(ExecutionContext ctx, Datum target, String methodName, List<Datum> args) {
        try {
            Method method = CallOpcodes.class.getDeclaredMethod(
                    "dispatchMethod",
                    ExecutionContext.class,
                    Datum.class,
                    String.class,
                    List.class);
            method.setAccessible(true);
            return (Datum) method.invoke(null, ctx, target, methodName, args);
        } catch (NoSuchMethodException | IllegalAccessException e) {
            throw new AssertionError(e);
        } catch (InvocationTargetException e) {
            throw unwrap(e);
        }
    }

    private static Datum invokeHandleVarRefMethod(ExecutionContext ctx, Datum.VarRef varRef,
                                                  String methodName, List<Datum> args) {
        try {
            Method method = CallOpcodes.class.getDeclaredMethod(
                    "handleVarRefMethod",
                    ExecutionContext.class,
                    Datum.VarRef.class,
                    String.class,
                    List.class);
            method.setAccessible(true);
            return (Datum) method.invoke(null, ctx, varRef, methodName, args);
        } catch (NoSuchMethodException | IllegalAccessException e) {
            throw new AssertionError(e);
        } catch (InvocationTargetException e) {
            throw unwrap(e);
        }
    }

    private static Datum invokeGetContextVar(ExecutionContext ctx, int varType, Datum idDatum, Datum castIdDatum) {
        try {
            Method method = StringOpcodes.class.getDeclaredMethod(
                    "getContextVar",
                    ExecutionContext.class,
                    int.class,
                    Datum.class,
                    Datum.class);
            method.setAccessible(true);
            return (Datum) method.invoke(null, ctx, varType, idDatum, castIdDatum);
        } catch (NoSuchMethodException | IllegalAccessException e) {
            throw new AssertionError(e);
        } catch (InvocationTargetException e) {
            throw unwrap(e);
        }
    }

    private static String invokeGetStringChunk(String str, String chunkType, int start, int end) {
        try {
            Method method = CallOpcodes.class.getDeclaredMethod(
                    "getStringChunk",
                    String.class,
                    String.class,
                    int.class,
                    int.class);
            method.setAccessible(true);
            return (String) method.invoke(null, str, chunkType, start, end);
        } catch (NoSuchMethodException | IllegalAccessException e) {
            throw new AssertionError(e);
        } catch (InvocationTargetException e) {
            throw unwrap(e);
        }
    }

    private static String invokeDeleteChunkRange(String str, String chunkType, int start, int end) {
        try {
            Method method = CallOpcodes.class.getDeclaredMethod(
                    "deleteChunkRange",
                    String.class,
                    String.class,
                    int.class,
                    int.class);
            method.setAccessible(true);
            return (String) method.invoke(null, str, chunkType, start, end);
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

    private static ExecutionContext createContext(int argument, int localCount) {
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
                localCount,
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
        BuiltinRegistry builtins = new BuiltinRegistry();
        return new ExecutionContext(
                scope,
                instruction,
                builtins,
                null,
                (scriptChunk, targetHandler, args, receiver) -> Datum.VOID,
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
                (name, args) -> builtins.invoke(name, new LingoVM(null), args),
                errorState -> {
                },
                () -> "(test)");
    }

    private static final class RecordingCastProvider extends NoOpCastLibProvider {
        private int lastFieldCastId = -1;
        private String lastFieldMemberName;

        @Override
        public String getFieldValue(Object memberNameOrNum, int castId) {
            return "ok";
        }

        @Override
        public Datum getFieldDatum(Object memberNameOrNum, int castId) {
            lastFieldCastId = castId;
            lastFieldMemberName = String.valueOf(memberNameOrNum);
            return new Datum.FieldText("ok", castId, 7);
        }
    }
}
