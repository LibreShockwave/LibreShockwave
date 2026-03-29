package com.libreshockwave.vm.opcode.dispatch;

import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.id.ChunkId;
import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.Scope;
import com.libreshockwave.vm.datum.Datum;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Field;
import java.util.ArrayDeque;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ScriptInstanceMethodDispatcherTest {

    @Test
    void numericCloseThreadDefersDuringActiveHandler() throws Exception {
        LingoVM vm = new LingoVM(null);
        pushActiveScope(vm);

        assertTrue(ScriptInstanceMethodDispatcher.shouldDeferNumericCloseThread(
                vm,
                "closethread",
                List.of(Datum.of(11))));
        assertTrue(ScriptInstanceMethodDispatcher.shouldDeferNumericCloseThread(
                vm,
                "closethread",
                List.of(Datum.of(11.5))));
    }

    @Test
    void symbolicCloseThreadDoesNotDeferDuringActiveHandler() throws Exception {
        LingoVM vm = new LingoVM(null);
        pushActiveScope(vm);

        assertFalse(ScriptInstanceMethodDispatcher.shouldDeferNumericCloseThread(
                vm,
                "closethread",
                List.of(Datum.symbol("catalogue"))));
    }

    @Test
    void numericCloseThreadDoesNotDeferOutsideActiveHandler() {
        LingoVM vm = new LingoVM(null);

        assertFalse(ScriptInstanceMethodDispatcher.shouldDeferNumericCloseThread(
                vm,
                "closethread",
                List.of(Datum.of(11))));
    }

    @SuppressWarnings("unchecked")
    private static void pushActiveScope(LingoVM vm) throws Exception {
        Field callStackField = LingoVM.class.getDeclaredField("callStack");
        callStackField.setAccessible(true);
        ArrayDeque<Scope> callStack = (ArrayDeque<Scope>) callStackField.get(vm);
        ScriptChunk.Handler handler = new ScriptChunk.Handler(
                1,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                List.of(),
                List.of(),
                List.of(new ScriptChunk.Handler.Instruction(0, Opcode.RET, 0, 0)),
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
        callStack.push(new Scope(script, handler, List.of(), Datum.VOID));
    }
}
