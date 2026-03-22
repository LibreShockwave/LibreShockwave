package com.libreshockwave.vm.opcode;

import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.builtin.cast.CastLibProvider;

import java.util.ArrayList;
import java.util.List;

/**
 * Test helper for CallOpcodes method dispatch.
 * Provides access to the private method handlers for unit testing.
 */
public final class CallOpcodesTestHelper {

    private CallOpcodesTestHelper() {}

    /**
     * Call a method on a List.
     */
    public static Datum callListMethod(Datum.List list, String methodName, List<Datum> args) {
        String method = methodName.toLowerCase();
        return switch (method) {
            case "count" -> Datum.of(list.items().size());
            case "getat" -> {
                if (args.isEmpty()) yield Datum.VOID;
                int index = args.get(0).toInt() - 1;
                if (index >= 0 && index < list.items().size()) {
                    yield list.items().get(index);
                }
                yield Datum.VOID;
            }
            case "setat" -> {
                // setAt(list, position, value) - set value at position (1-indexed)
                // Like dirplayer-rs: pads with VOID if index > current length
                if (args.size() < 2) yield Datum.VOID;
                int index = args.get(0).toInt() - 1; // Convert to 0-indexed
                Datum value = args.get(1);
                if (index < 0) yield Datum.VOID;
                if (index < list.items().size()) {
                    list.items().set(index, value);
                } else {
                    // Pad with VOID values up to the target index
                    while (list.items().size() < index) {
                        list.items().add(Datum.VOID);
                    }
                    list.items().add(value);
                }
                yield Datum.VOID;
            }
            case "append", "add" -> {
                if (args.isEmpty()) yield Datum.VOID;
                list.items().add(args.get(0));
                yield Datum.VOID;
            }
            case "addat" -> {
                if (args.size() < 2) yield Datum.VOID;
                int index = args.get(0).toInt() - 1;
                Datum value = args.get(1);
                if (index < 0) index = 0;
                if (index > list.items().size()) index = list.items().size();
                list.items().add(index, value);
                yield Datum.VOID;
            }
            case "deleteat" -> {
                if (args.isEmpty()) yield Datum.VOID;
                int index = args.get(0).toInt() - 1;
                if (index >= 0 && index < list.items().size()) {
                    list.items().remove(index);
                }
                yield Datum.VOID;
            }
            case "getone", "findpos" -> {
                // Find 1-based index of value, returns 0 if not found
                if (args.isEmpty()) yield Datum.ZERO;
                Datum value = args.get(0);
                for (int i = 0; i < list.items().size(); i++) {
                    if (list.items().get(i).equals(value)) {
                        yield Datum.of(i + 1);
                    }
                }
                yield Datum.ZERO;
            }
            case "getlast" -> {
                // getLast(list) - return the last element
                if (list.items().isEmpty()) yield Datum.VOID;
                yield list.items().get(list.items().size() - 1);
            }
            case "deleteone" -> {
                // deleteOne(list, value) - remove first matching element
                if (args.isEmpty()) yield Datum.VOID;
                Datum value = args.get(0);
                for (int i = 0; i < list.items().size(); i++) {
                    if (list.items().get(i).equals(value)) {
                        list.items().remove(i);
                        break;
                    }
                }
                yield Datum.VOID;
            }
            case "join" -> {
                // join(list, separator) - concatenate elements into string
                String separator = args.isEmpty() ? "" : args.get(0).toStr();
                StringBuilder sb = new StringBuilder();
                for (int i = 0; i < list.items().size(); i++) {
                    if (i > 0) sb.append(separator);
                    sb.append(list.items().get(i).toStr());
                }
                yield Datum.of(sb.toString());
            }
            case "sort" -> {
                list.items().sort((a, b) -> {
                    if (a instanceof Datum.Int ai && b instanceof Datum.Int bi) {
                        return Integer.compare(ai.value(), bi.value());
                    }
                    return a.toStr().compareToIgnoreCase(b.toStr());
                });
                yield Datum.VOID;
            }
            case "duplicate" -> {
                yield new Datum.List(new ArrayList<>(list.items()));
            }
            default -> Datum.VOID;
        };
    }

    /**
     * Call a method on a PropList.
     */
    public static Datum callPropListMethod(Datum.PropList propList, String methodName, List<Datum> args) {
        String method = methodName.toLowerCase();
        return switch (method) {
            case "count" -> Datum.of(propList.size());
            case "getat" -> {
                if (args.isEmpty()) yield Datum.VOID;
                Datum keyOrIndex = args.get(0);
                if (keyOrIndex instanceof Datum.Str s) {
                    yield propList.getOrDefault(s.value(), false, Datum.VOID);
                } else if (keyOrIndex instanceof Datum.Symbol sym) {
                    yield propList.getOrDefault(sym.name(), true, Datum.VOID);
                } else {
                    int index = keyOrIndex.toInt() - 1;
                    if (index >= 0 && index < propList.size()) {
                        yield propList.getValue(index);
                    }
                    yield Datum.VOID;
                }
            }
            case "getprop", "getaprop" -> {
                if (args.isEmpty()) yield Datum.VOID;
                String key = args.get(0).toKeyName();
                yield propList.getOrDefault(key, Datum.VOID);
            }
            case "setprop", "setaprop" -> {
                if (args.size() < 2) yield Datum.VOID;
                Datum keyDatum = args.get(0);
                String key = keyDatum.toKeyName();
                boolean isSym = keyDatum instanceof Datum.Symbol;
                propList.put(key, isSym, args.get(1));
                yield Datum.VOID;
            }
            case "addprop" -> {
                if (args.size() < 2) yield Datum.VOID;
                String key = args.get(0).toKeyName();
                boolean addSym = args.get(0) instanceof Datum.Symbol;
                propList.add(key, args.get(1), addSym);
                yield Datum.VOID;
            }
            case "deleteprop" -> {
                if (args.isEmpty()) yield Datum.VOID;
                String key = args.get(0).toKeyName();
                propList.remove(key);
                yield Datum.VOID;
            }
            case "getpropat" -> {
                if (args.isEmpty()) yield Datum.VOID;
                int index = args.get(0).toInt() - 1;
                if (index >= 0 && index < propList.size()) {
                    yield Datum.symbol(propList.getKey(index));
                }
                yield Datum.VOID;
            }
            case "setat" -> {
                if (args.size() < 2) yield Datum.VOID;
                Datum setAtKey = args.get(0);
                String key = setAtKey.toKeyName();
                boolean setAtSym = setAtKey instanceof Datum.Symbol;
                propList.put(key, setAtSym, args.get(1));
                yield Datum.VOID;
            }
            case "findpos" -> {
                if (args.isEmpty()) yield Datum.ZERO;
                String key = args.get(0).toKeyName();
                int pos = propList.findPos(key);
                yield Datum.of(pos);
            }
            case "duplicate" -> {
                yield new Datum.PropList(new ArrayList<>(propList.entries()));
            }
            default -> Datum.VOID;
        };
    }

    /**
     * Call a method on a ScriptInstance.
     * The ExecutionContext is optional and only needed for handler dispatch.
     */
    public static Datum callScriptInstanceMethod(ExecutionContext ctx, Datum.ScriptInstance instance,
                                                  String methodName, List<Datum> args) {
        String method = methodName.toLowerCase();
        switch (method) {
            case "setat" -> {
                if (args.size() >= 2) {
                    String propName = getPropertyName(args.get(0));
                    instance.properties().put(propName, args.get(1));
                }
                return Datum.VOID;
            }
            case "setaprop" -> {
                if (args.size() >= 2) {
                    String propName = getPropertyName(args.get(0));
                    Datum value = args.get(1);
                    instance.properties().put(propName, value);

                    // Also update pObjectList if it exists (for Object Manager pattern)
                    Datum pObjectList = instance.properties().get("pObjectList");
                    if (pObjectList instanceof Datum.PropList objList) {
                        boolean objSym = args.get(0) instanceof Datum.Symbol;
                        objList.put(propName, objSym, value);
                    }
                }
                return Datum.VOID;
            }
            case "getat", "getaprop", "getprop" -> {
                if (args.isEmpty()) return Datum.VOID;
                String propName = getPropertyName(args.get(0));
                return getPropertyFromAncestorChain(instance, propName);
            }
            case "addprop" -> {
                if (args.size() >= 2) {
                    String propName = getPropertyName(args.get(0));
                    instance.properties().put(propName, args.get(1));
                }
                return Datum.VOID;
            }
            case "deleteprop" -> {
                if (args.isEmpty()) return Datum.VOID;
                String propName = getPropertyName(args.get(0));
                instance.properties().remove(propName);
                return Datum.VOID;
            }
            case "count" -> {
                return Datum.of(instance.properties().size());
            }
            case "ilk" -> {
                return new Datum.Symbol("instance");
            }
            case "addat" -> {
                // addAt for building ancestor chain - simplified version for testing
                if (args.size() >= 2) {
                    int position = args.get(0).toInt();
                    Datum classList = args.get(1);
                    if (position == 1 && classList instanceof Datum.List list && !list.items().isEmpty()) {
                        // For testing, we just set the first item as ancestor
                        Datum first = list.items().get(0);
                        if (first instanceof Datum.ScriptInstance ancestorInstance) {
                            instance.properties().put("ancestor", ancestorInstance);
                        }
                    }
                }
                return Datum.VOID;
            }
        }

        // For handler dispatch, we would need the execution context
        // This is tested through integration tests (HabboDebugTest)
        return Datum.VOID;
    }

    private static String getPropertyName(Datum datum) {
        return datum.toKeyName();
    }

    /**
     * Get a property from an instance, walking the ancestor chain if not found.
     */
    private static Datum getPropertyFromAncestorChain(Datum.ScriptInstance instance, String propName) {
        Datum.ScriptInstance current = instance;
        for (int i = 0; i < 100; i++) { // Safety limit
            // Exact match first
            if (current.properties().containsKey(propName)) {
                return current.properties().get(propName);
            }
            // Case-insensitive fallback (Director is case-insensitive)
            for (var entry : current.properties().entrySet()) {
                if (entry.getKey().equalsIgnoreCase(propName)) {
                    return entry.getValue();
                }
            }

            // Try ancestor
            Datum ancestor = current.properties().get("ancestor");
            if (ancestor instanceof Datum.ScriptInstance ancestorInstance) {
                current = ancestorInstance;
            } else {
                break;
            }
        }
        return Datum.VOID;
    }
}
