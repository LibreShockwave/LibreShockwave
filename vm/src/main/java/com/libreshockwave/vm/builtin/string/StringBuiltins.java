package com.libreshockwave.vm.builtin.string;

import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.LingoVM;

import java.util.List;
import java.util.Map;
import java.util.function.BiFunction;

/**
 * String-related builtin functions.
 */
public final class StringBuiltins {

    private StringBuiltins() {}

    public static void register(Map<String, BiFunction<LingoVM, List<Datum>, Datum>> builtins) {
        builtins.put("string", StringBuiltins::string);
        builtins.put("length", StringBuiltins::length);
        builtins.put("chars", StringBuiltins::chars);
        builtins.put("chartonum", StringBuiltins::charToNum);
        builtins.put("numtochar", StringBuiltins::numToChar);
        builtins.put("offset", StringBuiltins::offset);
        builtins.put("getpref", StringBuiltins::getPref);
        builtins.put("setpref", StringBuiltins::setPref);
    }

    private static Datum string(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) return Datum.EMPTY_STRING;
        return Datum.of(args.get(0).toStr());
    }

    private static Datum length(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) return Datum.ZERO;
        Datum a = args.get(0);
        if (a instanceof Datum.Str s) {
            return Datum.of(s.value().length());
        } else if (a instanceof Datum.List l) {
            return Datum.of(l.items().size());
        } else if (a instanceof Datum.PropList p) {
            return Datum.of(p.size());
        }
        // For Symbol and other types, convert to string and return string length
        // (Lingo's length() is a string function that coerces its argument)
        return Datum.of(a.toStr().length());
    }

    private static Datum chars(LingoVM vm, List<Datum> args) {
        if (args.size() < 3) return Datum.EMPTY_STRING;
        String str = args.get(0).toStr();
        int start = args.get(1).toInt() - 1; // Lingo is 1-indexed
        int end = args.get(2).toInt();
        if (start < 0) start = 0;
        if (end > str.length()) end = str.length();
        if (start >= end) return Datum.EMPTY_STRING;
        return Datum.of(str.substring(start, end));
    }

    private static Datum charToNum(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) return Datum.ZERO;
        String s = args.get(0).toStr();
        if (s.isEmpty()) return Datum.ZERO;
        return Datum.of((int) s.charAt(0));
    }

    private static Datum numToChar(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) return Datum.EMPTY_STRING;
        int code = args.get(0).toInt();
        return Datum.of(String.valueOf((char) code));
    }

    /**
     * offset(substring, string)
     * Returns the 1-indexed position of substring in string, or 0 if not found.
     * Case-insensitive search using regionMatches to avoid toLowerCase() allocations.
     */
    private static Datum offset(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return Datum.ZERO;
        String substr = args.get(0).toStr();
        String str = args.get(1).toStr();
        if (substr.isEmpty()) return Datum.ZERO;

        // Case-insensitive search without creating temporary lowercase strings
        int sLen = substr.length();
        int limit = str.length() - sLen;
        for (int i = 0; i <= limit; i++) {
            if (str.regionMatches(true, i, substr, 0, sLen)) {
                return Datum.of(i + 1); // 1-indexed
            }
        }
        return Datum.ZERO;
    }

    private static Datum getPref(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) return Datum.VOID;
        String key = args.get(0).toStr();
        if (key.isEmpty()) return Datum.VOID;
        return vm.getPref(key);
    }

    private static Datum setPref(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return Datum.VOID;
        String key = args.get(0).toStr();
        if (key.isEmpty()) return Datum.VOID;
        return vm.setPref(key, args.get(1));
    }
}
