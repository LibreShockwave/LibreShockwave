package com.libreshockwave.vm.builtin.math;

import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.LingoVM;

import java.util.List;
import java.util.Map;
import java.util.function.BiFunction;

/**
 * Math-related builtin functions.
 */
public final class MathBuiltins {

    private MathBuiltins() {}

    public static void register(Map<String, BiFunction<LingoVM, List<Datum>, Datum>> builtins) {
        builtins.put("abs", MathBuiltins::abs);
        builtins.put("sqrt", MathBuiltins::sqrt);
        builtins.put("sin", MathBuiltins::sin);
        builtins.put("cos", MathBuiltins::cos);
        builtins.put("random", MathBuiltins::random);
        builtins.put("integer", MathBuiltins::integer);
        builtins.put("float", MathBuiltins::toFloat);
        builtins.put("bitand", MathBuiltins::bitAnd);
        builtins.put("bitor", MathBuiltins::bitOr);
        builtins.put("bitxor", MathBuiltins::bitXor);
        builtins.put("bitnot", MathBuiltins::bitNot);
        builtins.put("power", MathBuiltins::power);
        builtins.put("min", MathBuiltins::min);
        builtins.put("max", MathBuiltins::max);
    }

    private static Datum abs(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) return Datum.ZERO;
        Datum a = args.get(0);
        if (a.isFloat()) return Datum.of(Math.abs(a.toDouble()));
        return Datum.of(Math.abs(a.toInt()));
    }

    private static Datum sqrt(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) return Datum.ZERO;
        return Datum.of(Math.sqrt(args.get(0).toDouble()));
    }

    private static Datum sin(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) return Datum.ZERO;
        return Datum.of(Math.sin(Math.toRadians(args.get(0).toDouble())));
    }

    private static Datum cos(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) return Datum.ZERO;
        return Datum.of(Math.cos(Math.toRadians(args.get(0).toDouble())));
    }

    private static Datum random(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) return Datum.of(1);
        int max = args.get(0).toInt();
        if (max <= 0) return Datum.of(1);
        return Datum.of((int) (Math.random() * max) + 1);
    }

    /**
     * integer(value)
     * Converts a value to an integer by rounding to nearest (Director behavior).
     * - For floats: rounds to nearest integer (not truncation!)
     * - For numeric strings: converts to integer
     * - For empty strings: returns 0 (Director-compatible coercion used by protocol parsers)
     * - For non-numeric strings: returns VOID
     *   This is critical for variable.index parsing: the dump handler uses
     *   integerp(integer(val)) to decide if a value is numeric. Returning 0
     *   instead of VOID makes "h" look like the integer 0, corrupting variables
     *   like human.size.64=h → human.size.64=0.
     */
    private static Datum integer(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) return Datum.ZERO;
        Datum arg = args.get(0);

        if (arg instanceof Datum.Str str) {
            String trimmed = str.value().trim();
            if (trimmed.isEmpty()) return Datum.ZERO;
            try {
                return Datum.of(Integer.parseInt(trimmed));
            } catch (NumberFormatException e) {
                try {
                    return Datum.of((int) Math.round(Double.parseDouble(trimmed)));
                } catch (NumberFormatException e2) {
                    return Datum.VOID;
                }
            }
        }

        // For other types, round to nearest (Director's integer() rounds, not truncates)
        return Datum.of((int) Math.round(arg.toDouble()));
    }

    private static Datum bitAnd(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return Datum.ZERO;
        return Datum.of(args.get(0).toInt() & args.get(1).toInt());
    }

    private static Datum bitOr(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return Datum.ZERO;
        return Datum.of(args.get(0).toInt() | args.get(1).toInt());
    }

    private static Datum bitXor(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return Datum.ZERO;
        return Datum.of(args.get(0).toInt() ^ args.get(1).toInt());
    }

    private static Datum bitNot(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) return Datum.ZERO;
        return Datum.of(~args.get(0).toInt());
    }

    private static Datum min(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return args.isEmpty() ? Datum.ZERO : args.get(0);
        Datum a = args.get(0), b = args.get(1);
        if (a.isFloat() || b.isFloat()) {
            return Datum.of(Math.min(a.toDouble(), b.toDouble()));
        }
        return Datum.of(Math.min(a.toInt(), b.toInt()));
    }

    private static Datum max(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return args.isEmpty() ? Datum.ZERO : args.get(0);
        Datum a = args.get(0), b = args.get(1);
        if (a.isFloat() || b.isFloat()) {
            return Datum.of(Math.max(a.toDouble(), b.toDouble()));
        }
        return Datum.of(Math.max(a.toInt(), b.toInt()));
    }

    private static Datum power(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return Datum.ZERO;
        double base = args.get(0).toDouble();
        double exponent = args.get(1).toDouble();
        double result = Math.pow(base, exponent);
        if (result == (int) result && !args.get(0).isFloat() && !args.get(1).isFloat()) {
            return Datum.of((int) result);
        }
        return Datum.of(result);
    }

    /**
     * float(value)
     * Converts a value to a floating-point number.
     * - For integers: converts to float (1 -> 1.0)
     * - For floats: returns as-is
     * - For numeric strings: converts to float ("1.5" -> 1.5)
     * - For non-numeric strings: returns the original string unchanged
     *   (this allows floatp(float(x)) to correctly return false for non-numeric strings)
     */
    private static Datum toFloat(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) return Datum.of(0.0);
        Datum arg = args.get(0);

        // For strings, only convert if it's actually numeric
        if (arg instanceof Datum.Str str) {
            try {
                return Datum.of(Double.parseDouble(str.value().trim()));
            } catch (NumberFormatException e) {
                // Return the original string unchanged if not a valid number
                return arg;
            }
        }

        // For other types, use standard conversion
        return Datum.of(arg.toDouble());
    }
}
