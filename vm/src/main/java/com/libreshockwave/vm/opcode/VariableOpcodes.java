package com.libreshockwave.vm.opcode;

import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.vm.datum.Datum;

import java.util.Map;

/**
 * Variable access opcodes (local, param, global).
 */
public final class VariableOpcodes {

    private VariableOpcodes() {}

    public static void register(Map<Opcode, OpcodeHandler> handlers) {
        handlers.put(Opcode.GET_LOCAL, VariableOpcodes::getLocal);
        handlers.put(Opcode.SET_LOCAL, VariableOpcodes::setLocal);
        handlers.put(Opcode.GET_PARAM, VariableOpcodes::getParam);
        handlers.put(Opcode.SET_PARAM, VariableOpcodes::setParam);
        handlers.put(Opcode.GET_GLOBAL, VariableOpcodes::getGlobal);
        handlers.put(Opcode.GET_GLOBAL2, VariableOpcodes::getGlobal);
        handlers.put(Opcode.SET_GLOBAL, VariableOpcodes::setGlobal);
        handlers.put(Opcode.SET_GLOBAL2, VariableOpcodes::setGlobal);
    }

    private static boolean getLocal(ExecutionContext ctx) {
        int index = ctx.getScaledArgument();
        ctx.push(ctx.getLocal(index));
        return true;
    }

    private static boolean setLocal(ExecutionContext ctx) {
        int index = ctx.getScaledArgument();
        Datum value = ctx.pop();
        ctx.setLocal(index, value);
        return true;
    }

    private static boolean getParam(ExecutionContext ctx) {
        int index = ctx.getScaledArgument();
        ctx.push(ctx.getParam(index));
        return true;
    }

    private static boolean setParam(ExecutionContext ctx) {
        int index = ctx.getScaledArgument();
        Datum value = ctx.pop();
        ctx.setParam(index, value);
        return true;
    }

    private static boolean getGlobal(ExecutionContext ctx) {
        String name = ctx.resolveName(ctx.getArgument());
        ctx.push(ctx.getGlobal(name));
        return true;
    }

    private static boolean setGlobal(ExecutionContext ctx) {
        String name = ctx.resolveName(ctx.getArgument());
        Datum value = ctx.pop();
        ctx.setGlobal(name, value);
        return true;
    }
}
