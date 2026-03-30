package com.libreshockwave.vm.builtin.string;

import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.DebugConfig;
import com.libreshockwave.vm.LingoVM;

import java.util.List;
import java.util.Map;
import java.util.function.BiFunction;

/**
 * Output builtin functions (put, alert, pass).
 */
public final class OutputBuiltins {

    private OutputBuiltins() {}

    public static void register(Map<String, BiFunction<LingoVM, List<Datum>, Datum>> builtins) {
        builtins.put("put", OutputBuiltins::put);
        builtins.put("alert", OutputBuiltins::alert);
        // error() is defined by Habbo's Error API movie script and must not be shadowed.
        // Note: pass is registered separately by LingoVM since it needs the passCallback
    }

    private static Datum put(LingoVM vm, List<Datum> args) {
        StringBuilder sb = new StringBuilder();
        for (Datum arg : args) {
            sb.append(arg.toStr()).append(' ');
        }
        String text = sb.toString().trim();
        if (!DebugConfig.isDebugPlaybackEnabled()) {
            return Datum.VOID;
        }
        System.out.println("[PUT] " + text);
        return Datum.VOID;
    }

    private static Datum alert(LingoVM vm, List<Datum> args) {
        String msg = args.isEmpty() ? "" : args.get(0).toStr();
        // Try alertHook first — if it handles the alert, suppress the default output
        if (vm.fireAlertHook(msg)) {
            return Datum.VOID;
        }
        System.out.println("[ALERT] " + msg);
        return Datum.VOID;
    }
}
