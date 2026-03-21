package com.libreshockwave.vm.opcode;

import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.vm.datum.Datum;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * List and property list opcodes.
 */
public final class ListOpcodes {

    private ListOpcodes() {}

    public static void register(Map<Opcode, OpcodeHandler> handlers) {
        handlers.put(Opcode.PUSH_LIST, ListOpcodes::pushList);
        handlers.put(Opcode.PUSH_PROP_LIST, ListOpcodes::pushPropList);
        handlers.put(Opcode.PUSH_ARG_LIST, ListOpcodes::pushArgList);
        handlers.put(Opcode.PUSH_ARG_LIST_NO_RET, ListOpcodes::pushArgListNoRet);
    }

    private static boolean pushList(ExecutionContext ctx) {
        // Pop one ArgList from the stack and convert to List
        Datum argListDatum = ctx.pop();
        List<Datum> items;
        if (argListDatum instanceof Datum.ArgList al) {
            items = new ArrayList<>(al.items());
        } else if (argListDatum instanceof Datum.ArgListNoRet al) {
            items = new ArrayList<>(al.items());
        } else {
            // Fallback: single item
            items = new ArrayList<>();
            if (!argListDatum.isVoid()) {
                items.add(argListDatum);
            }
        }
        ctx.push(Datum.list(items));
        return true;
    }

    private static boolean pushPropList(ExecutionContext ctx) {
        // Pop one ArgList from the stack, split into key-value pairs
        Datum argListDatum = ctx.pop();
        Datum.PropList pl = new Datum.PropList();
        List<Datum> items;
        if (argListDatum instanceof Datum.ArgList al) {
            items = al.items();
        } else if (argListDatum instanceof Datum.ArgListNoRet al) {
            items = al.items();
        } else {
            items = List.of();
        }
        // Items come in key, value, key, value order — add() preserves duplicates
        for (int i = 0; i + 1 < items.size(); i += 2) {
            Datum key = items.get(i);
            Datum value = items.get(i + 1);
            pl.add(key.toKeyName(), value, key instanceof Datum.Symbol);
        }
        ctx.push(pl);
        return true;
    }

    private static boolean pushArgList(ExecutionContext ctx) {
        int count = ctx.getArgument();
        List<Datum> items = ctx.popArgs(count);
        ctx.push(new Datum.ArgList(items));
        return true;
    }

    private static boolean pushArgListNoRet(ExecutionContext ctx) {
        int count = ctx.getArgument();
        List<Datum> items = ctx.popArgs(count);
        ctx.push(new Datum.ArgListNoRet(items));
        return true;
    }
}
