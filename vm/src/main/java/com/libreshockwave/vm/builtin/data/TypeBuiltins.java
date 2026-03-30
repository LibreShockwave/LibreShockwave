package com.libreshockwave.vm.builtin.data;

import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.util.LingoValueParser;

import java.util.List;
import java.util.Map;
import java.util.function.BiFunction;

/**
 * Type-checking and conversion builtin functions.
 * Includes: objectp, voidp, value, script, ilk, listp, stringp, integerp, floatp, symbolp
 */
public final class TypeBuiltins {

    private TypeBuiltins() {}

    public static void register(Map<String, BiFunction<LingoVM, List<Datum>, Datum>> builtins) {
        builtins.put("objectp", TypeBuiltins::objectp);
        builtins.put("voidp", TypeBuiltins::voidp);
        builtins.put("value", TypeBuiltins::value);
        builtins.put("script", TypeBuiltins::script);
        builtins.put("ilk", TypeBuiltins::ilk);
        builtins.put("listp", TypeBuiltins::listp);
        builtins.put("stringp", TypeBuiltins::stringp);
        builtins.put("integerp", TypeBuiltins::integerp);
        builtins.put("floatp", TypeBuiltins::floatp);
        builtins.put("symbolp", TypeBuiltins::symbolp);
        builtins.put("symbol", TypeBuiltins::symbol);
        builtins.put("callancestor", AncestorCallHandler::call);
    }

    /**
     * objectp(value)
     * Returns TRUE if the value is an object (script instance, list, proplist, etc.)
     * Returns FALSE for void, integers, floats, strings, and symbols.
     */
    private static Datum objectp(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.FALSE;
        }
        Datum value = args.get(0);
        boolean isObject = !(value.isVoid()
                || value.isNumber()
                || value.isSymbol()
                || value.isString());
        return isObject ? Datum.TRUE : Datum.FALSE;
    }

    /**
     * voidp(value)
     * Returns TRUE if the value is VOID.
     */
    private static Datum voidp(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.TRUE;
        }
        Datum value = args.get(0);
        return value.isVoid() ? Datum.TRUE : Datum.FALSE;
    }

    /**
     * value(expression)
     * Evaluates a string expression or returns the value as-is.
     * For strings, attempts to parse and evaluate as Lingo.
     * For non-strings, returns the value unchanged.
     *
     * Director behavior: "Expressions that Lingo cannot parse will produce unexpected
     * results, but will not produce Lingo errors. The result is the value of the
     * initial portion of the expression up to the first syntax error found in the string."
     *
     * Examples:
     * - value("3 5") returns 3 (parses first token, stops at syntax error)
     * - value("penny") returns VOID (unknown identifier has no value)
     * - value("[\"cat\", \"dog\"]") returns ["cat", "dog"]
     */
    private static Datum value(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.VOID;
        }
        Datum arg = args.get(0);

        if (arg instanceof Datum.FieldText fieldText) {
            CastLibProvider provider = CastLibProvider.getProvider();
            if (provider != null) {
                Datum parsed = provider.getFieldParsedValue(fieldText.castLibNum(), fieldText.memberNum(), vm);
                if (!parsed.isVoid()) {
                    return parsed;
                }
            }
            return LingoValueParser.parseWithPartial(fieldText.value(), vm);
        }

        // For non-strings, return as-is
        if (!arg.isString()) {
            return arg;
        }

        String raw = arg.toStr();
        Datum parsed = LingoValueParser.parseWithPartial(raw, vm);
        if (!parsed.isVoid()) {
            return parsed;
        }

        // Director movies often store class names in variables as bare multi-word strings
        // (for example "Broker Manager Class") and then resolve them through value(...).
        // If the expression parser cannot evaluate the string, but a member with the exact
        // name exists, preserve the original string so downstream script()/createObject()
        // calls can still resolve it.
        String trimmed = raw.trim();
        if (trimmed.indexOf(' ') >= 0) {
            CastLibProvider provider = CastLibProvider.getProvider();
            if (provider != null) {
                Datum memberRef = provider.getMemberByName(0, trimmed);
                if (memberRef instanceof Datum.CastMemberRef) {
                    return Datum.of(trimmed);
                }
            }
        }

        return parsed;
    }

    /**
     * script(identifier)
     * Returns a ScriptRef for the specified script name or number.
     * The script can then be used with new() to create instances.
     *
     * If a list of names is passed, returns the ScriptRef for the FIRST valid script found.
     * This is used in Director for parent script lookup with fallback classes.
     */
    private static Datum script(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.VOID;
        }

        Datum identifier = args.get(0);
        CastLibProvider provider = CastLibProvider.getProvider();

        if (identifier instanceof Datum.Str str) {
            // Find script by name
            if (provider != null) {
                Datum memberRef = provider.getMemberByName(0, str.value());
                if (memberRef instanceof Datum.CastMemberRef cmr) {
                    return new Datum.ScriptRef(cmr.castLib(), cmr.member());
                }
            }
        } else if (identifier instanceof Datum.Symbol sym) {
            // Find script by symbol name
            if (provider != null) {
                Datum memberRef = provider.getMemberByName(0, sym.name());
                if (memberRef instanceof Datum.CastMemberRef cmr) {
                    return new Datum.ScriptRef(cmr.castLib(), cmr.member());
                }
            }
        } else if (identifier instanceof Datum.Int num) {
            // Find script by number
            // If the number is a slot number (high bits set), decode it
            // Slot number format: (castLib << 16) | (memberNum & 0xFFFF)
            if (provider != null) {
                int value = num.value();
                int castLib, memberNum;
                if (value > 65535) {
                    // This is a slot number - decode it
                    castLib = value >> 16;
                    memberNum = value & 0xFFFF;
                } else {
                    // Regular member number - assume cast lib 1
                    castLib = 1;
                    memberNum = value;
                }
                return Datum.ScriptRef.of(castLib, memberNum);
            }
        } else if (identifier instanceof Datum.CastMemberRef cmr) {
            // Already a cast member reference
            return new Datum.ScriptRef(cmr.castLib(), cmr.member());
        } else if (identifier instanceof Datum.List list) {
            // List of script names - return the first valid one found
            // This is used for class hierarchies like ["Manager Template Class", "Variable Container Class"]
            for (Datum item : list.items()) {
                Datum result = script(vm, List.of(item));
                if (!result.isVoid()) {
                    return result;
                }
            }
        }

        return Datum.VOID;
    }

    /**
     * ilk(value) or ilk(value, type)
     * Returns the type of a value, or checks if value matches type.
     */
    private static Datum ilk(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.symbol("void");
        }

        Datum value = args.get(0);
        String typeName = getIlkType(value);

        // If second argument provided, check if types match
        // Director docs: ilk(propList, #list) and ilk(propList, #propList) both return TRUE
        if (args.size() >= 2) {
            Datum checkType = args.get(1);
            String checkName = checkType.toKeyName();
            if (typeName.equalsIgnoreCase(checkName)) return Datum.TRUE;
            // Additional type aliases from Scripting Reference:
            // propList matches #list; list matches #linearList; int/float match #number
            // rect/point match #list; instances match #object
            if ("list".equalsIgnoreCase(checkName) && ("propList".equalsIgnoreCase(typeName)
                    || "rect".equalsIgnoreCase(typeName) || "point".equalsIgnoreCase(typeName))) return Datum.TRUE;
            if ("linearList".equalsIgnoreCase(checkName) && "list".equalsIgnoreCase(typeName)) return Datum.TRUE;
            if ("number".equalsIgnoreCase(checkName) && ("integer".equalsIgnoreCase(typeName)
                    || "float".equalsIgnoreCase(typeName))) return Datum.TRUE;
            if ("object".equalsIgnoreCase(checkName) && ("instance".equalsIgnoreCase(typeName)
                    || "member".equalsIgnoreCase(typeName) || "xtra".equalsIgnoreCase(typeName)
                    || "xtraInstance".equalsIgnoreCase(typeName) || "script".equalsIgnoreCase(typeName)
                    || "castLib".equalsIgnoreCase(typeName) || "sprite".equalsIgnoreCase(typeName)
                    || "stage".equalsIgnoreCase(typeName) || "image".equalsIgnoreCase(typeName))) return Datum.TRUE;
            return Datum.FALSE;
        }

        return Datum.symbol(typeName);
    }

    public static String getIlkType(Datum value) {
        return switch (value) {
            case Datum.Void v -> "void";
            case Datum.Int i -> "integer";
            case Datum.Float f -> "float";
            case Datum.Str s -> "string";
            case Datum.FieldText ft -> "string";
            case Datum.Symbol s -> "symbol";
            case Datum.List l -> "list";
            case Datum.PropList p -> "propList";
            case Datum.Point p -> "point";
            case Datum.Rect r -> "rect";
            case Datum.Color c -> "color";
            case Datum.ImageRef ir -> "image";
            case Datum.SpriteRef s -> "sprite";
            case Datum.CastMemberRef c -> "member";
            case Datum.CastLibRef c -> "castLib";
            case Datum.ScriptInstance s -> "instance";
            case Datum.SoundChannel s -> "instance";
            case Datum.ScriptRef s -> "script";
            case Datum.XtraRef x -> "xtra";
            case Datum.XtraInstance x -> "xtraInstance";
            case Datum.StageRef s -> "stage";
            default -> "object";
        };
    }

    /**
     * listp(value)
     * Returns TRUE if value is a list (linear list or property list).
     * Matches dirplayer-rs: both Datum::List and Datum::PropList return true.
     */
    private static Datum listp(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.FALSE;
        }
        Datum value = args.get(0);
        return (value instanceof Datum.List || value instanceof Datum.PropList) ? Datum.TRUE : Datum.FALSE;
    }

    /**
     * stringp(value)
     * Returns TRUE if value is a string.
     */
    private static Datum stringp(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.FALSE;
        }
        return args.get(0).isString() ? Datum.TRUE : Datum.FALSE;
    }

    /**
     * integerp(value)
     * Returns TRUE if value is an integer.
     */
    private static Datum integerp(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.FALSE;
        }
        return args.get(0) instanceof Datum.Int ? Datum.TRUE : Datum.FALSE;
    }

    /**
     * floatp(value)
     * Returns TRUE if value is a float.
     */
    private static Datum floatp(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.FALSE;
        }
        return args.get(0) instanceof Datum.Float ? Datum.TRUE : Datum.FALSE;
    }

    /**
     * symbol(value)
     * Converts a string to a symbol. Matches dirplayer-rs behavior:
     * - If already a symbol, returns it unchanged
     * - If a string starting with "#", returns Symbol("#")
     * - If an empty string, returns Symbol("")
     * - Otherwise, creates Symbol from the string content
     */
    private static Datum symbol(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.VOID;
        }
        Datum arg = args.get(0);
        if (arg instanceof Datum.Symbol) {
            return arg;
        }
        if (arg instanceof Datum.Str str) {
            String s = str.value();
            if (s.isEmpty()) {
                return Datum.symbol("");
            }
            if (s.startsWith("#")) {
                return Datum.symbol(s.substring(1));
            }
            return Datum.symbol(s);
        }
        return Datum.VOID;
    }

    /**
     * symbolp(value)
     * Returns TRUE if value is a symbol.
     */
    private static Datum symbolp(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.FALSE;
        }
        return args.get(0) instanceof Datum.Symbol ? Datum.TRUE : Datum.FALSE;
    }

}
