package com.libreshockwave.player.debug.ui;

import com.libreshockwave.player.debug.Breakpoint;

import java.util.Set;

/**
 * Display item for a bytecode instruction in the debugger.
 * Contains the instruction data and display state (breakpoints, current line).
 */
public class InstructionDisplayItem {
    private static final Set<String> CALL_OPCODES = Set.of("EXT_CALL", "OBJ_CALL", "LOCAL_CALL");

    private final int offset;
    private final int index;
    private final String opcode;
    private final int argument;
    private final String annotation;
    private boolean hasBreakpoint;
    private Breakpoint breakpoint;  // Full breakpoint info for rendering
    private boolean isCurrent;
    private boolean navigable;  // True if call target exists in the CCT (not a builtin)
    private boolean lingoLine;  // True if this item represents a decompiled Lingo line

    public InstructionDisplayItem(int offset, int index, String opcode, int argument,
                                   String annotation, boolean hasBreakpoint) {
        this.offset = offset;
        this.index = index;
        this.opcode = opcode;
        this.argument = argument;
        this.annotation = annotation;
        this.hasBreakpoint = hasBreakpoint;
        this.breakpoint = null;
        this.isCurrent = false;
    }

    public int getOffset() {
        return offset;
    }

    public int getIndex() {
        return index;
    }

    public String getOpcode() {
        return opcode;
    }

    public int getArgument() {
        return argument;
    }

    public String getAnnotation() {
        return annotation;
    }

    public boolean hasBreakpoint() {
        return hasBreakpoint;
    }

    public void setHasBreakpoint(boolean hasBreakpoint) {
        this.hasBreakpoint = hasBreakpoint;
    }

    public Breakpoint getBreakpoint() {
        return breakpoint;
    }

    public void setBreakpoint(Breakpoint breakpoint) {
        this.breakpoint = breakpoint;
    }

    public boolean isCurrent() {
        return isCurrent;
    }

    public void setCurrent(boolean current) {
        isCurrent = current;
    }

    public boolean isNavigable() {
        return navigable;
    }

    public void setNavigable(boolean navigable) {
        this.navigable = navigable;
    }

    public boolean isLingoLine() {
        return lingoLine;
    }

    public void setLingoLine(boolean lingoLine) {
        this.lingoLine = lingoLine;
    }

    /**
     * Check if this instruction is a call opcode (EXT_CALL, OBJ_CALL, LOCAL_CALL).
     */
    public boolean isCallInstruction() {
        return CALL_OPCODES.contains(opcode);
    }

    /**
     * Check if this instruction is a call that can be navigated to (exists in CCT, not a builtin).
     */
    public boolean isNavigableCall() {
        return isCallInstruction() && navigable && getCallTargetName() != null;
    }

    /**
     * Extract the handler name from the annotation (e.g., "<myHandler()>" -> "myHandler").
     */
    public String getCallTargetName() {
        if (annotation == null || annotation.isEmpty()) {
            return null;
        }
        // Annotation format is "<handlerName()>"
        if (annotation.startsWith("<") && annotation.endsWith("()>")) {
            return annotation.substring(1, annotation.length() - 3);
        }
        return null;
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder();
        sb.append(String.format("[%3d] %-14s", offset, opcode));
        if (argument != 0) {
            sb.append(String.format(" %-4d", argument));
        } else {
            sb.append("     ");
        }
        if (annotation != null && !annotation.isEmpty()) {
            sb.append(" ").append(annotation);
        }
        return sb.toString();
    }
}
