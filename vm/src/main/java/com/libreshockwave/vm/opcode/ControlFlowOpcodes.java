package com.libreshockwave.vm.opcode;

import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.vm.datum.Datum;

import java.util.Map;

/**
 * Control flow opcodes (return, jump, conditional).
 */
public final class ControlFlowOpcodes {

    private ControlFlowOpcodes() {}

    public static void register(Map<Opcode, OpcodeHandler> handlers) {
        handlers.put(Opcode.RET, ControlFlowOpcodes::ret);
        handlers.put(Opcode.RET_FACTORY, ControlFlowOpcodes::retFactory);
        handlers.put(Opcode.JMP, ControlFlowOpcodes::jmp);
        handlers.put(Opcode.JMP_IF_Z, ControlFlowOpcodes::jmpIfZ);
        handlers.put(Opcode.END_REPEAT, ControlFlowOpcodes::endRepeat);
    }

    private static boolean ret(ExecutionContext ctx) {
        Datum value = ctx.pop();
        ctx.setReturnValue(value);
        return true;
    }

    private static boolean retFactory(ExecutionContext ctx) {
        ctx.setReturnValue(Datum.VOID);
        return true;
    }

    private static boolean jmp(ExecutionContext ctx) {
        int target = ctx.getInstructionOffset() + ctx.getArgument();
        ctx.jumpTo(target);
        return false; // Don't advance, we already set the position
    }

    private static boolean jmpIfZ(ExecutionContext ctx) {
        Datum cond = ctx.pop();
        if (!cond.isTruthy()) {
            int target = ctx.getInstructionOffset() + ctx.getArgument();
            ctx.jumpTo(target);
            return false; // Don't advance, we already set the position
        }
        return true;
    }

    private static boolean endRepeat(ExecutionContext ctx) {
        int target = ctx.getInstructionOffset() - ctx.getArgument();
        ctx.jumpTo(target);
        return false; // Don't advance, we already set the position
    }
}
