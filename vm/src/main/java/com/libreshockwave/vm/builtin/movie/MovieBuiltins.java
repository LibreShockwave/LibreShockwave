package com.libreshockwave.vm.builtin.movie;

import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.datum.Datum;

import java.util.List;
import java.util.Map;
import java.util.function.BiFunction;

/**
 * Movie-level builtins that are emitted as EXT_CALL by some Director bytecode.
 */
public final class MovieBuiltins {

    private MovieBuiltins() {}

    public static void register(Map<String, BiFunction<LingoVM, List<Datum>, Datum>> builtins) {
        builtins.put("label", MovieBuiltins::label);
        builtins.put("marker", MovieBuiltins::marker);
    }

    private static Datum label(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.ZERO;
        }

        MoviePropertyProvider provider = MoviePropertyProvider.getProvider();
        if (provider == null) {
            return Datum.ZERO;
        }

        String labelName;
        if (args.size() >= 2 && args.get(0) instanceof Datum.MovieRef) {
            labelName = args.get(1).toStr();
        } else {
            labelName = args.get(0).toStr();
        }

        return Datum.of(Math.max(provider.getFrameForLabel(labelName), 0));
    }

    private static Datum marker(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.ZERO;
        }

        MoviePropertyProvider provider = MoviePropertyProvider.getProvider();
        if (provider == null) {
            return Datum.ZERO;
        }

        Datum markerArg = args.size() >= 2 && args.get(0) instanceof Datum.MovieRef
                ? args.get(1)
                : args.get(0);

        if (markerArg instanceof Datum.Str str) {
            return Datum.of(Math.max(provider.getFrameForLabel(str.value()), 0));
        }

        return Datum.of(Math.max(provider.getMarkerFrame(markerArg.toInt()), 0));
    }
}
