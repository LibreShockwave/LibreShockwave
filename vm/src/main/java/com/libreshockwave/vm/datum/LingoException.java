package com.libreshockwave.vm.datum;

import com.libreshockwave.vm.LingoVM;

import java.util.List;

/**
 * Exception thrown during Lingo script execution.
 * Carries the Lingo call stack at the point of the error.
 */
public class LingoException extends RuntimeException {

    private final String handlerName;
    private final int bytecodeOffset;
    private List<LingoVM.CallStackFrame> lingoCallStack;

    public LingoException(String message) {
        super(message);
        this.handlerName = null;
        this.bytecodeOffset = -1;
    }

    public LingoException(String message, String handlerName, int bytecodeOffset) {
        super(message + " at " + handlerName + " [" + bytecodeOffset + "]");
        this.handlerName = handlerName;
        this.bytecodeOffset = bytecodeOffset;
    }

    public LingoException(String message, Throwable cause) {
        super(message, cause);
        this.handlerName = null;
        this.bytecodeOffset = -1;
    }

    public String getHandlerName() {
        return handlerName;
    }

    public int getBytecodeOffset() {
        return bytecodeOffset;
    }

    /**
     * Get the Lingo call stack captured when this exception was thrown.
     * Returns null if no call stack was captured.
     */
    public List<LingoVM.CallStackFrame> getLingoCallStack() {
        return lingoCallStack;
    }

    /**
     * Attach the Lingo call stack to this exception.
     */
    public void setLingoCallStack(List<LingoVM.CallStackFrame> callStack) {
        this.lingoCallStack = callStack;
    }

    /**
     * Format the Lingo call stack as a human-readable string.
     * Returns null if no call stack was captured.
     */
    public String formatLingoCallStack() {
        if (lingoCallStack == null || lingoCallStack.isEmpty()) {
            return null;
        }
        StringBuilder sb = new StringBuilder("Lingo call stack:\n");
        for (LingoVM.CallStackFrame frame : lingoCallStack) {
            LingoVM.appendCallStackFrame(sb, frame);
        }
        return sb.toString();
    }
}
