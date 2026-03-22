package com.libreshockwave.vm.builtin;

import com.libreshockwave.vm.builtin.cast.CastLibBuiltins;
import com.libreshockwave.vm.builtin.data.ConstructorBuiltins;
import com.libreshockwave.vm.builtin.data.ListBuiltins;
import com.libreshockwave.vm.builtin.data.TypeBuiltins;
import com.libreshockwave.vm.builtin.flow.ControlFlowBuiltins;
import com.libreshockwave.vm.builtin.math.MathBuiltins;
import com.libreshockwave.vm.builtin.media.ImageBuiltins;
import com.libreshockwave.vm.builtin.media.SoundBuiltins;
import com.libreshockwave.vm.builtin.net.ExternalParamBuiltins;
import com.libreshockwave.vm.builtin.net.NetBuiltins;
import com.libreshockwave.vm.builtin.sprite.SpriteBuiltins;
import com.libreshockwave.vm.builtin.string.OutputBuiltins;
import com.libreshockwave.vm.builtin.string.StringBuiltins;
import com.libreshockwave.vm.builtin.timeout.TimeoutBuiltins;
import com.libreshockwave.vm.builtin.xtra.XtraBuiltins;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.LingoVM;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.function.BiFunction;

/**
 * Registry of built-in Lingo functions.
 */
public class BuiltinRegistry {

    private final Map<String, BiFunction<LingoVM, List<Datum>, Datum>> builtins = new HashMap<>();

    public BuiltinRegistry() {
        MathBuiltins.register(builtins);
        StringBuiltins.register(builtins);
        ListBuiltins.register(builtins);
        ConstructorBuiltins.register(builtins);
        OutputBuiltins.register(builtins);
        NetBuiltins.register(builtins);
        XtraBuiltins.register(builtins);
        CastLibBuiltins.register(builtins);
        SpriteBuiltins.register(builtins);
        TypeBuiltins.register(builtins);
        ControlFlowBuiltins.register(builtins);
        TimeoutBuiltins.register(builtins);
        ExternalParamBuiltins.register(builtins);
        ImageBuiltins.register(builtins);
        SoundBuiltins.register(builtins);
    }

    /**
     * Check if a builtin function exists.
     * Lingo is case-insensitive, so we normalize to lowercase.
     */
    public boolean contains(String name) {
        return builtins.containsKey(name.toLowerCase());
    }

    /**
     * Invoke a builtin function.
     * Lingo is case-insensitive, so we normalize to lowercase.
     */
    public Datum invoke(String name, LingoVM vm, List<Datum> args) {
        var func = builtins.get(name.toLowerCase());
        if (func != null) {
            return func.apply(vm, args);
        }
        System.err.println("[LingoVM] Missing builtin: " + name);
        return Datum.VOID;
    }

    /**
     * Register a custom builtin function.
     * Names are normalized to lowercase for case-insensitive lookup.
     */
    public void register(String name, BiFunction<LingoVM, List<Datum>, Datum> func) {
        builtins.put(name.toLowerCase(), func);
    }

    /**
     * Get the underlying map for direct access (used by pass() builtin).
     */
    public Map<String, BiFunction<LingoVM, List<Datum>, Datum>> getMap() {
        return builtins;
    }
}
