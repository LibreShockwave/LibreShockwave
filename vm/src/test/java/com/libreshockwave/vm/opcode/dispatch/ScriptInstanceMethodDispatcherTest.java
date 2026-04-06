package com.libreshockwave.vm.opcode.dispatch;

import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.id.ChunkId;
import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.Scope;
import com.libreshockwave.vm.builtin.BuiltinRegistry;
import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.opcode.ExecutionContext;
import com.libreshockwave.vm.support.NoOpCastLibProvider;
import org.junit.jupiter.api.Test;

import java.lang.reflect.Field;
import java.util.ArrayDeque;
import java.util.Deque;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
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
    void numericCloseThreadDoesNotDeferWhileFlushingDeferredTasks() throws Exception {
        LingoVM vm = new LingoVM(null);
        pushActiveScope(vm);
        setBooleanField(vm, "flushingDeferredTasks", true);

        assertFalse(ScriptInstanceMethodDispatcher.shouldDeferNumericCloseThread(
                vm,
                "closethread",
                List.of(Datum.of(11))));
    }

    @Test
    void numericCloseThreadDoesNotDeferOutsideActiveHandler() {
        LingoVM vm = new LingoVM(null);

        assertFalse(ScriptInstanceMethodDispatcher.shouldDeferNumericCloseThread(
                vm,
                "closethread",
                List.of(Datum.of(11))));
    }

    @Test
    void numericCloseThreadDispatchQueuesTickBoundaryTask() throws Exception {
        LingoVM vm = new LingoVM(null);
        pushActiveScope(vm);
        setCurrentVm(vm);

        try {
            Datum result = ScriptInstanceMethodDispatcher.dispatch(
                    null,
                    new Datum.ScriptInstance(77, new LinkedHashMap<>()),
                    "closeThread",
                    List.of(Datum.of(11)));

            assertTrue(result.isTruthy());
            assertEquals(0, getDequeSize(vm, "deferredScriptInstanceCalls"));
            assertEquals(1, getDequeSize(vm, "deferredTasks"));
        } finally {
            clearCurrentVm();
        }
    }

    @Test
    void explicitScriptHandlerRunsBeforeMemberRegistryFallback() {
        ScriptChunk.Handler handler = createTestHandler();
        ScriptChunk script = createTestScript(handler);
        Scope scope = new Scope(script, handler, List.of(), Datum.VOID);
        ExecutionContext ctx = new ExecutionContext(
                scope,
                handler.instructions().getFirst(),
                new BuiltinRegistry(),
                null,
                (ignoredScript, ignoredHandler, args, receiver) -> Datum.of("script:getmemnum"),
                ignoredName -> null,
                new ExecutionContext.GlobalAccessor() {
                    @Override
                    public Datum getGlobal(String name) {
                        return Datum.VOID;
                    }

                    @Override
                    public void setGlobal(String name, Datum value) {}
                },
                (name, args) -> Datum.VOID,
                ignored -> {},
                () -> "");

        Datum.PropList registry = new Datum.PropList();
        Datum.ScriptInstance instance = new Datum.ScriptInstance(77, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        CastLibProvider.setProvider(new ScriptHandlerProvider(script, handler, false));
        try {
            Datum result = ScriptInstanceMethodDispatcher.dispatch(
                    ctx,
                    instance,
                    "getmemnum",
                    List.of(Datum.of("Object Base Class")));
            assertEquals("script:getmemnum", result.toStr());
            assertEquals(null, registry.get("Object Base Class"));
        } finally {
            CastLibProvider.clearProvider();
        }
    }

    @Test
    void stableRegistryPrefillSeedsRegistryBeforeExplicitScriptHandler() {
        ScriptChunk.Handler handler = createTestHandler();
        ScriptChunk script = createTestScript(handler);
        Scope scope = new Scope(script, handler, List.of(), Datum.VOID);
        ExecutionContext ctx = new ExecutionContext(
                scope,
                handler.instructions().getFirst(),
                new BuiltinRegistry(),
                null,
                (ignoredScript, ignoredHandler, args, receiver) -> Datum.of("script:getmemnum"),
                ignoredName -> null,
                new ExecutionContext.GlobalAccessor() {
                    @Override
                    public Datum getGlobal(String name) {
                        return Datum.VOID;
                    }

                    @Override
                    public void setGlobal(String name, Datum value) {}
                },
                (name, args) -> Datum.VOID,
                ignored -> {},
                () -> "");

        Datum.PropList registry = new Datum.PropList();
        Datum.ScriptInstance instance = new Datum.ScriptInstance(77, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        CastLibProvider.setProvider(new ScriptHandlerProvider(script, handler, true));
        try {
            Datum result = ScriptInstanceMethodDispatcher.dispatch(
                    ctx,
                    instance,
                    "getmemnum",
                    List.of(Datum.of("Object Base Class")));
            assertEquals("script:getmemnum", result.toStr());
            assertEquals((2 << 16) | 74, registry.get("Object Base Class").toInt());
        } finally {
            CastLibProvider.clearProvider();
        }
    }

    @Test
    void scriptBootstrapPrefillSeedsRegistryBeforeExplicitScriptHandler() {
        ScriptChunk.Handler handler = createTestHandler();
        ScriptChunk script = createTestScript(handler);
        Scope scope = new Scope(script, handler, List.of(), Datum.VOID);
        ExecutionContext ctx = new ExecutionContext(
                scope,
                handler.instructions().getFirst(),
                new BuiltinRegistry(),
                null,
                (ignoredScript, ignoredHandler, args, receiver) -> Datum.of("script:getmemnum"),
                ignoredName -> null,
                new ExecutionContext.GlobalAccessor() {
                    @Override
                    public Datum getGlobal(String name) {
                        return Datum.VOID;
                    }

                    @Override
                    public void setGlobal(String name, Datum value) {}
                },
                (name, args) -> Datum.VOID,
                ignored -> {},
                () -> "");

        Datum.PropList registry = new Datum.PropList();
        Datum.ScriptInstance instance = new Datum.ScriptInstance(77, new LinkedHashMap<>());
        instance.properties().put("pAllMemNumList", registry);

        CastLibProvider.setProvider(new ScriptHandlerProvider(script, handler, false, true));
        try {
            Datum result = ScriptInstanceMethodDispatcher.dispatch(
                    ctx,
                    instance,
                    "getmemnum",
                    List.of(Datum.of("Object Base Class")));
            assertEquals("script:getmemnum", result.toStr());
            assertEquals((2 << 16) | 74, registry.get("Object Base Class").toInt());
        } finally {
            CastLibProvider.clearProvider();
        }
    }

    @Test
    void setAtWritesRegularScriptInstanceProperty() {
        Datum.ScriptInstance instance = new Datum.ScriptInstance(77, new LinkedHashMap<>());

        Datum result = ScriptInstanceMethodDispatcher.dispatch(
                null,
                instance,
                "setAt",
                List.of(Datum.symbol("pFacadeId"), Datum.symbol("snowwar_loungesystem")));

        assertTrue(result.isVoid());
        assertTrue(instance.properties().get("pFacadeId") instanceof Datum.Symbol);
        assertEquals("snowwar_loungesystem", ((Datum.Symbol) instance.properties().get("pFacadeId")).name());
    }

    @Test
    void setAtUpdatesAncestorOwnedProperty() {
        LinkedHashMap<String, Datum> ancestorProps = new LinkedHashMap<>();
        ancestorProps.put("pFacadeId", Datum.symbol("old"));
        Datum.ScriptInstance ancestor = new Datum.ScriptInstance(78, ancestorProps);

        LinkedHashMap<String, Datum> childProps = new LinkedHashMap<>();
        childProps.put("ancestor", ancestor);
        Datum.ScriptInstance instance = new Datum.ScriptInstance(77, childProps);

        ScriptInstanceMethodDispatcher.dispatch(
                null,
                instance,
                "setAt",
                List.of(Datum.symbol("pFacadeId"), Datum.symbol("snowwar_loungesystem")));

        assertTrue(ancestor.properties().get("pFacadeId") instanceof Datum.Symbol);
        assertEquals("snowwar_loungesystem", ((Datum.Symbol) ancestor.properties().get("pFacadeId")).name());
        assertFalse(instance.properties().containsKey("pFacadeId"));
    }

    @SuppressWarnings("unchecked")
    private static void pushActiveScope(LingoVM vm) throws Exception {
        Field callStackField = LingoVM.class.getDeclaredField("callStack");
        callStackField.setAccessible(true);
        ArrayDeque<Scope> callStack = (ArrayDeque<Scope>) callStackField.get(vm);
        ScriptChunk.Handler handler = createTestHandler();
        ScriptChunk script = createTestScript(handler);
        callStack.push(new Scope(script, handler, List.of(), Datum.VOID));
    }

    private static ScriptChunk.Handler createTestHandler() {
        return new ScriptChunk.Handler(
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
    }

    private static ScriptChunk createTestScript(ScriptChunk.Handler handler) {
        return new ScriptChunk(
                null,
                new ChunkId(1),
                ScriptChunk.ScriptType.PARENT,
                0,
                List.of(handler),
                List.of(),
                List.of(),
                List.of(),
                new byte[0]);
    }

    @SuppressWarnings("unchecked")
    private static int getDequeSize(LingoVM vm, String fieldName) throws Exception {
        Field field = LingoVM.class.getDeclaredField(fieldName);
        field.setAccessible(true);
        return ((Deque<Object>) field.get(vm)).size();
    }

    private static void setBooleanField(LingoVM vm, String fieldName, boolean value) throws Exception {
        Field field = LingoVM.class.getDeclaredField(fieldName);
        field.setAccessible(true);
        field.setBoolean(vm, value);
    }

    @SuppressWarnings("unchecked")
    private static void setCurrentVm(LingoVM vm) throws Exception {
        Field field = LingoVM.class.getDeclaredField("CURRENT_VM");
        field.setAccessible(true);
        ThreadLocal<LingoVM> threadLocal = (ThreadLocal<LingoVM>) field.get(null);
        threadLocal.set(vm);
    }

    @SuppressWarnings("unchecked")
    private static void clearCurrentVm() throws Exception {
        Field field = LingoVM.class.getDeclaredField("CURRENT_VM");
        field.setAccessible(true);
        ThreadLocal<LingoVM> threadLocal = (ThreadLocal<LingoVM>) field.get(null);
        threadLocal.remove();
    }

    private static final class ScriptHandlerProvider extends NoOpCastLibProvider {
        private final ScriptChunk script;
        private final ScriptChunk.Handler handler;
        private final boolean exposeStableRegistryMembers;
        private final boolean exposeBootstrapScriptMembers;

        private ScriptHandlerProvider(
                ScriptChunk script,
                ScriptChunk.Handler handler,
                boolean exposeStableRegistryMembers) {
            this(script, handler, exposeStableRegistryMembers, false);
        }

        private ScriptHandlerProvider(
                ScriptChunk script,
                ScriptChunk.Handler handler,
                boolean exposeStableRegistryMembers,
                boolean exposeBootstrapScriptMembers) {
            this.script = script;
            this.handler = handler;
            this.exposeStableRegistryMembers = exposeStableRegistryMembers;
            this.exposeBootstrapScriptMembers = exposeBootstrapScriptMembers;
        }

        @Override
        public Datum getMember(int castLibNumber, int memberNumber) {
            return Datum.VOID;
        }

        @Override
        public Datum getMemberByName(int castLibNumber, String memberName) {
            return Datum.CastMemberRef.of(2, 74);
        }

        @Override
        public Datum getRegistryMemberByName(int castLibNumber, String memberName) {
            if (!exposeStableRegistryMembers) {
                return Datum.VOID;
            }
            return Datum.CastMemberRef.of(2, 74);
        }

        @Override
        public Datum getMemberProp(int castLibNumber, int memberNumber, String propName) {
            if ("type".equalsIgnoreCase(propName) && exposeBootstrapScriptMembers) {
                return Datum.symbol("script");
            }
            return Datum.VOID;
        }

        @Override
        public HandlerLocation findHandlerInScript(int scriptId, String handlerName) {
            if (scriptId == 77 && "getmemnum".equalsIgnoreCase(handlerName)) {
                return new HandlerLocation(1, script, handler, null);
            }
            return null;
        }
    }
}
