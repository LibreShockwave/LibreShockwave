package com.libreshockwave.vm.opcode;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.chunks.ScriptNamesChunk;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.HandlerRef;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.datum.LingoException;
import com.libreshockwave.vm.Scope;
import com.libreshockwave.vm.TraceListener;
import com.libreshockwave.vm.builtin.BuiltinRegistry;

import java.util.ArrayList;
import java.util.List;

/**
 * Execution context passed to opcode handlers.
 * Provides access to the current scope and VM utilities needed during instruction execution.
 */
public final class ExecutionContext {

    private final Scope scope;
    private final LingoVM vm;
    private ScriptChunk.Handler.Instruction instruction;
    private int argument;
    private final BuiltinRegistry builtins;
    private final TraceListener traceListener;

    // Callbacks to LingoVM
    private final HandlerExecutor handlerExecutor;
    private final HandlerFinder handlerFinder;
    private final GlobalAccessor globalAccessor;
    private final BuiltinInvoker builtinInvoker;
    private final ErrorStateSetter errorStateSetter;
    private final CallStackFormatter callStackFormatter;
    private final int variableMultiplier;
    private final ScriptNamesChunk scriptNames;
    private int cachedJumpOffset = Integer.MIN_VALUE;
    private int cachedJumpIndex = -1;

    public ExecutionContext(
            Scope scope,
            ScriptChunk.Handler.Instruction instruction,
            BuiltinRegistry builtins,
            TraceListener traceListener,
            HandlerExecutor handlerExecutor,
            HandlerFinder handlerFinder,
            GlobalAccessor globalAccessor,
            BuiltinInvoker builtinInvoker,
            ErrorStateSetter errorStateSetter,
            CallStackFormatter callStackFormatter) {
        this(scope, null, instruction, builtins, traceListener, handlerExecutor, handlerFinder,
                globalAccessor, builtinInvoker, errorStateSetter, callStackFormatter);
    }

    public ExecutionContext(
            Scope scope,
            LingoVM vm,
            ScriptChunk.Handler.Instruction instruction,
            BuiltinRegistry builtins,
            TraceListener traceListener,
            HandlerExecutor handlerExecutor,
            HandlerFinder handlerFinder,
            GlobalAccessor globalAccessor,
            BuiltinInvoker builtinInvoker,
            ErrorStateSetter errorStateSetter,
            CallStackFormatter callStackFormatter) {
        this.scope = scope;
        this.vm = vm;
        this.instruction = instruction;
        this.argument = instruction.argument();
        this.builtins = builtins;
        this.traceListener = traceListener;
        this.handlerExecutor = handlerExecutor;
        this.handlerFinder = handlerFinder;
        this.globalAccessor = globalAccessor;
        this.builtinInvoker = builtinInvoker;
        this.errorStateSetter = errorStateSetter;
        this.callStackFormatter = callStackFormatter;
        this.variableMultiplier = computeVariableMultiplier(scope.getScript());
        this.scriptNames = resolveScriptNames(scope.getScript());
    }

    /**
     * Update the instruction for this context (reuse across instructions in a handler).
     * Avoids creating a new ExecutionContext + closures per instruction.
     */
    public void setInstruction(ScriptChunk.Handler.Instruction instr) {
        this.instruction = instr;
        this.argument = instr.argument();
    }

    /**
     * Set the VM error state. When true, no more handlers will execute.
     */
    public void setErrorState(boolean errorState) {
        if (errorStateSetter != null) {
            errorStateSetter.setErrorState(errorState);
        }
    }

    @FunctionalInterface
    public interface ErrorStateSetter {
        void setErrorState(boolean errorState);
    }

    // Scope access

    public Scope getScope() {
        return scope;
    }

    public int getArgument() {
        return argument;
    }

    public ScriptChunk.Handler.Instruction getInstruction() {
        return instruction;
    }

    /**
     * Get the variable multiplier for the current script's Director version.
     * capitalX → 1, dirVersion >= 500 → 8, else → 6.
     */
    public int getVariableMultiplier() {
        return variableMultiplier;
    }

    private static int computeVariableMultiplier(ScriptChunk script) {
        if (script != null && script.file() != null) {
            DirectorFile file = script.file();
            if (file.isCapitalX()) {
                return 1;
            }
            if (file.getConfig() != null && file.getConfig().directorVersion() >= 500) {
                return 8;
            }
            return 6;
        }
        return 1; // Default when no file info available
    }

    private static ScriptNamesChunk resolveScriptNames(ScriptChunk script) {
        if (script == null || script.file() == null) {
            return null;
        }
        return script.file().getScriptNamesForScript(script);
    }

    // Stack operations (delegated to scope)

    public void push(Datum value) {
        scope.push(value);
    }

    public Datum pop() {
        return scope.pop();
    }

    public Datum peek() {
        return scope.peek();
    }

    public Datum peek(int depth) {
        return scope.peek(depth);
    }

    public void swap() {
        scope.swap();
    }

    // Local/param access

    public Datum getLocal(int index) {
        return scope.getLocal(index);
    }

    public void setLocal(int index, Datum value) {
        scope.setLocal(index, value);
        if (traceListener != null) {
            traceListener.onVariableSet("local", "local" + index, value);
        }
    }

    public Datum getParam(int index) {
        return scope.getParam(index);
    }

    public void setParam(int index, Datum value) {
        scope.setParam(index, value);
        if (traceListener != null) {
            traceListener.onVariableSet("param", "param" + index, value);
        }
    }

    // Global access

    public Datum getGlobal(String name) {
        return globalAccessor.getGlobal(name);
    }

    public void setGlobal(String name, Datum value) {
        globalAccessor.setGlobal(name, value);
        if (traceListener != null) {
            traceListener.onVariableSet("global", name, value);
        }
    }

    // Return handling

    public void setReturnValue(Datum value) {
        scope.setReturnValue(value);
    }

    public void setReturned(boolean returned) {
        scope.setReturned(returned);
    }

    // Name resolution - delegates to ScriptChunk which uses the correct cast lib's names

    public String resolveName(int nameId) {
        return scriptNames != null ? scriptNames.getName(nameId) : getScript().resolveName(nameId);
    }

    // Script/handler access

    public ScriptChunk getScript() {
        return scope.getScript();
    }

    public ScriptChunk.Handler getHandler() {
        return scope.getHandler();
    }

    public Datum getReceiver() {
        return scope.getReceiver();
    }

    public List<ScriptChunk.LiteralEntry> getLiterals() {
        return scope.getScript().literals();
    }

    // Jump handling

    public void jumpTo(int targetOffset) {
        int targetIndex;
        if (targetOffset == cachedJumpOffset) {
            targetIndex = cachedJumpIndex;
        } else {
            targetIndex = scope.getHandler().getInstructionIndex(targetOffset);
            cachedJumpOffset = targetOffset;
            cachedJumpIndex = targetIndex;
        }
        if (targetIndex >= 0) {
            scope.setBytecodeIndex(targetIndex);
        }
    }

    public int getInstructionOffset() {
        return instruction.offset();
    }

    // Function calls

    public ScriptChunk.Handler findLocalHandler(int index) {
        // LOCAL_CALL uses the bytecode argument as an index into the handlers array
        // (not as a vector position to match against)
        var handlers = getScript().handlers();
        if (index >= 0 && index < handlers.size()) {
            return handlers.get(index);
        }
        return null;
    }

    public HandlerRef findHandler(String handlerName) {
        return handlerFinder.findHandler(handlerName);
    }

    public Datum executeHandler(ScriptChunk script, ScriptChunk.Handler handler,
                                List<Datum> args, Datum receiver) {
        return handlerExecutor.executeHandler(script, handler, args, receiver);
    }

    public boolean isBuiltin(String name) {
        return builtins.contains(name);
    }

    public Datum invokeBuiltin(String name, List<Datum> args) {
        return builtinInvoker.invoke(name, args);
    }

    public Datum invokeBuiltinIfPresent(String name, List<Datum> args) {
        return builtins.invokeIfPresent(name, vm, args);
    }

    public Datum invokeBuiltinIfPresent(String name, Datum firstArg, List<Datum> restArgs) {
        var func = builtins.get(name);
        if (func == null) {
            return null;
        }
        ArrayList<Datum> args = new ArrayList<>(restArgs.size() + 1);
        args.add(firstArg);
        args.addAll(restArgs);
        return func.apply(vm, args);
    }

    public BuiltinRegistry getBuiltins() {
        return builtins;
    }

    // Helper to pop multiple args in reverse order

    public List<Datum> popArgs(int count) {
        if (count == 0) return new ArrayList<>(0);
        // Pop in stack order then reverse in-place — O(n) instead of O(n²) add(0)
        ArrayList<Datum> args = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            args.add(pop());
        }
        for (int i = 0, j = count - 1; i < j; i++, j--) {
            Datum tmp = args.get(i);
            args.set(i, args.get(j));
            args.set(j, tmp);
        }
        return args;
    }

    // Property tracing

    public void tracePropertySet(String propName, Datum value) {
        if (traceListener != null) {
            traceListener.onVariableSet("property", "me." + propName, value);
        }
    }

    // Exception helper

    public LingoException error(String message) {
        return new LingoException(message);
    }

    // Functional interfaces for callbacks

    @FunctionalInterface
    public interface HandlerExecutor {
        Datum executeHandler(ScriptChunk script, ScriptChunk.Handler handler,
                             List<Datum> args, Datum receiver);
    }

    @FunctionalInterface
    public interface HandlerFinder {
        HandlerRef findHandler(String name);
    }

    public interface GlobalAccessor {
        Datum getGlobal(String name);
        void setGlobal(String name, Datum value);
    }

    @FunctionalInterface
    public interface BuiltinInvoker {
        Datum invoke(String name, List<Datum> args);
    }

    @FunctionalInterface
    public interface CallStackFormatter {
        String format();
    }

    /**
     * Format the current Lingo call stack (delegates to LingoVM).
     */
    public String formatCallStack() {
        return callStackFormatter != null ? callStackFormatter.format() : "(no call stack available)";
    }
}
