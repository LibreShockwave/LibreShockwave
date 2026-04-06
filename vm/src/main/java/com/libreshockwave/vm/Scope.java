package com.libreshockwave.vm;

import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.vm.datum.Datum;

import java.util.Arrays;
import java.util.ArrayList;
import java.util.List;

/**
 * Execution scope for a handler call.
 * Represents a single stack frame in the call stack.
 * Similar to dirplayer-rs scope.rs.
 *
 * Optimized for WASM: uses arrays instead of HashMap/ArrayDeque to minimize
 * allocations and GC pressure. Each handler invocation creates one Scope;
 * during the dump handler that's ~4497 scopes, so keeping allocation light matters.
 */
public final class Scope {

    private final ScriptChunk script;
    private final ScriptChunk.Handler handler;
    private final List<Datum> originalArguments;
    private final Datum[] locals;          // indexed by local variable index
    private Datum[] modifiedParams;        // lazy: null until first SET_PARAM
    private final Datum receiver;          // 'me' in behavior/parent scripts

    // Array-based stack (avoids ArrayDeque overhead)
    private Datum[] stack;
    private int stackTop;                  // index of next push slot (size = stackTop)

    private int bytecodeIndex;
    private Datum returnValue;
    private boolean returned;

    // Loop tracking: array-based stack of return indices
    private int[] loopReturnStack;
    private int loopReturnTop;

    private static final int INITIAL_STACK_SIZE = 16;
    private static final int INITIAL_LOOP_SIZE = 4;

    public Scope(ScriptChunk script, ScriptChunk.Handler handler, List<Datum> arguments, Datum receiver) {
        this.script = script;
        this.handler = handler;
        this.originalArguments = arguments;  // trust callers — avoid List.copyOf allocation
        this.receiver = receiver != null ? receiver : Datum.VOID;
        this.bytecodeIndex = 0;
        this.returnValue = Datum.VOID;
        this.returned = false;

        // Array-based locals: much cheaper than HashMap (no autoboxing, no Entry nodes)
        int localCount = handler.localCount();
        this.locals = new Datum[localCount];
        Arrays.fill(this.locals, Datum.VOID);

        // Stack and loop return arrays allocated lazily at small initial size
        this.stack = new Datum[INITIAL_STACK_SIZE];
        this.stackTop = 0;
        this.loopReturnStack = null;  // lazy — many handlers have no loops
        this.loopReturnTop = 0;
    }

    // Script and handler access

    public ScriptChunk getScript() {
        return script;
    }

    public ScriptChunk.Handler getHandler() {
        return handler;
    }

    public List<Datum> getArguments() {
        return originalArguments;
    }

    /**
     * Arguments as they should be displayed to users.
     * Excludes an implicit prepended receiver and reflects SET_PARAM changes.
     */
    public List<Datum> getDisplayArguments() {
        int offset = getDisplayArgumentOffset();
        int explicitCount = Math.max(0, originalArguments.size() - offset);
        if (explicitCount == 0) {
            return List.of();
        }
        List<Datum> args = new ArrayList<>(explicitCount);
        for (int i = 0; i < explicitCount; i++) {
            int originalIndex = i + offset;
            if (modifiedParams != null && i >= 0 && i < modifiedParams.length && modifiedParams[i] != null) {
                args.add(modifiedParams[i]);
            } else if (originalIndex >= 0 && originalIndex < originalArguments.size()) {
                args.add(originalArguments.get(originalIndex));
            } else {
                args.add(Datum.VOID);
            }
        }
        return List.copyOf(args);
    }

    public Datum getReceiver() {
        return receiver;
    }

    // Bytecode position

    public int getBytecodeIndex() {
        return bytecodeIndex;
    }

    public void setBytecodeIndex(int index) {
        this.bytecodeIndex = index;
    }

    public void advanceBytecodeIndex() {
        this.bytecodeIndex++;
    }

    public boolean hasMoreInstructions() {
        return bytecodeIndex < handler.instructions().size();
    }

    public ScriptChunk.Handler.Instruction getCurrentInstruction() {
        if (bytecodeIndex >= 0 && bytecodeIndex < handler.instructions().size()) {
            return handler.instructions().get(bytecodeIndex);
        }
        return null;
    }

    // Stack operations (array-based for minimal allocation)

    public void push(Datum value) {
        if (stackTop >= stack.length) {
            stack = Arrays.copyOf(stack, stack.length * 2);
        }
        stack[stackTop++] = value;
    }

    public Datum pop() {
        if (stackTop <= 0) return Datum.VOID;
        Datum val = stack[--stackTop];
        stack[stackTop] = null; // help GC
        return val;
    }

    public Datum peek() {
        return stackTop > 0 ? stack[stackTop - 1] : Datum.VOID;
    }

    public Datum peek(int depth) {
        int idx = stackTop - 1 - depth;
        return (idx >= 0 && idx < stackTop) ? stack[idx] : Datum.VOID;
    }

    public int stackSize() {
        return stackTop;
    }

    public void swap() {
        if (stackTop >= 2) {
            Datum tmp = stack[stackTop - 1];
            stack[stackTop - 1] = stack[stackTop - 2];
            stack[stackTop - 2] = tmp;
        }
    }

    // Parameter access
    // In Lingo bytecode, parameters are 0-indexed from the EXPLICIT arguments:
    //   param0 = first explicit argument (tNum), NOT 'me'
    //   param1 = second explicit argument (tColor), etc.
    // For parent script methods, the receiver ('me') may be prepended to the args list
    // by the caller, so we need to skip it when computing param indices.
    // The handler's argCount does NOT include 'me', so paramOffset compensates.

    /** Offset for param indices: 1 if receiver is prepended but not in argCount. */
    private int paramOffset = -1;

    private int getParamOffset() {
        if (paramOffset < 0) {
            // If the receiver is in effectiveArgs[0] but the handler's argCount doesn't
            // count it (i.e., handler's first declared param is NOT 'me'), offset by 1.
            if (receiver != null && !receiver.isVoid()
                    && !originalArguments.isEmpty()
                    && originalArguments.getFirst() == receiver
                    && !isFirstParamDeclaredMe()) {
                paramOffset = 1;
            } else {
                paramOffset = 0;
            }
        }
        return paramOffset;
    }

    private int getDisplayArgumentOffset() {
        if (receiver != null && !receiver.isVoid()
                && !originalArguments.isEmpty()
                && originalArguments.getFirst() == receiver) {
            return 1;
        }
        return 0;
    }

    private boolean isFirstParamDeclaredMe() {
        if (handler.argNameIds().isEmpty() || script == null || script.file() == null) {
            return false;
        }
        var names = script.file().getScriptNamesForScript(script);
        if (names == null) {
            return false;
        }
        String firstName = names.getName(handler.argNameIds().getFirst());
        return "me".equalsIgnoreCase(firstName);
    }

    public Datum getParam(int index) {
        int actualIndex = index + getParamOffset();
        // Check if param was modified via SET_PARAM
        if (modifiedParams != null && index >= 0 && index < modifiedParams.length && modifiedParams[index] != null) {
            return modifiedParams[index];
        }
        // Otherwise return original argument with offset
        if (actualIndex >= 0 && actualIndex < originalArguments.size()) {
            return originalArguments.get(actualIndex);
        }
        return Datum.VOID;
    }

    public void setParam(int index, Datum value) {
        if (modifiedParams == null) {
            // Lazy allocation on first SET_PARAM
            int size = Math.max(index + 1, originalArguments.size());
            modifiedParams = new Datum[size];
        } else if (index >= modifiedParams.length) {
            modifiedParams = Arrays.copyOf(modifiedParams, index + 1);
        }
        modifiedParams[index] = value;
    }

    // Local variable access

    public Datum getLocal(int index) {
        if (index >= 0 && index < locals.length) {
            return locals[index];
        }
        return Datum.VOID;
    }

    public void setLocal(int index, Datum value) {
        if (index >= 0 && index < locals.length) {
            locals[index] = value;
        }
        // Silently ignore out-of-bounds — matches previous HashMap behavior
    }

    // Return handling

    public boolean isReturned() {
        return returned;
    }

    public void setReturned(boolean returned) {
        this.returned = returned;
    }

    public Datum getReturnValue() {
        return returnValue;
    }

    public void setReturnValue(Datum value) {
        this.returnValue = value;
        this.returned = true;
    }

    // Loop handling (array-based)

    public void pushLoopReturnIndex(int index) {
        if (loopReturnStack == null) {
            loopReturnStack = new int[INITIAL_LOOP_SIZE];
        } else if (loopReturnTop >= loopReturnStack.length) {
            loopReturnStack = Arrays.copyOf(loopReturnStack, loopReturnStack.length * 2);
        }
        loopReturnStack[loopReturnTop++] = index;
    }

    public int popLoopReturnIndex() {
        return loopReturnTop > 0 ? loopReturnStack[--loopReturnTop] : -1;
    }

    public boolean isInLoop() {
        return loopReturnTop > 0;
    }

    @Override
    public String toString() {
        String handlerName = "handler#" + handler.nameId();
        return "Scope{" + handlerName + ", bytecodeIndex=" + bytecodeIndex +
               ", stackSize=" + stackTop + ", returned=" + returned + "}";
    }
}
