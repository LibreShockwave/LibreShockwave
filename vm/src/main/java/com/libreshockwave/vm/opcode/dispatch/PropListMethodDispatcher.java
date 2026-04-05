package com.libreshockwave.vm.opcode.dispatch;

import com.libreshockwave.vm.datum.Datum;

import java.util.List;

/**
 * Handles method calls on property lists.
 * Uses equalsIgnoreCase to avoid toLowerCase String allocation on the hot count path.
 */
public final class PropListMethodDispatcher {

    private PropListMethodDispatcher() {}

    public static Datum dispatch(Datum.PropList propList, String methodName, List<Datum> args) {
        // Fast path for most common operations (no allocation)
        if ("count".equalsIgnoreCase(methodName)) {
            // count(propList) -> number of entries
            // count(propList, #prop) -> count of the sub-property value (list.prop.count)
            if (!args.isEmpty()) {
                Datum sub = propList.getOrDefault(args.get(0).toKeyName(), Datum.VOID);
                if (sub instanceof Datum.List subList) return Datum.of(subList.items().size());
                if (sub instanceof Datum.PropList subProp) return Datum.of(subProp.size());
                return Datum.ZERO;
            }
            return Datum.of(propList.size());
        }

        return switch (methodName.toLowerCase()) {
            case "getprop", "getaprop", "getproperty" -> {
                if (args.isEmpty()) yield Datum.VOID;
                Datum value = propList.getOrDefault(args.get(0).toKeyName(), Datum.VOID);
                // getProp(propList, #prop, index) -> propList.prop[index]
                if (args.size() >= 2 && value instanceof Datum.List subList) {
                    int index = args.get(1).toInt() - 1; // 1-indexed
                    if (index >= 0 && index < subList.items().size()) {
                        yield subList.items().get(index);
                    }
                    yield Datum.VOID;
                }
                yield value;
            }
            case "setprop", "setaprop" -> {
                if (args.size() < 2) yield Datum.VOID;
                Datum keyDatum = args.get(0);
                propList.put(keyDatum.toKeyName(), keyDatum instanceof Datum.Symbol, args.get(1));
                yield Datum.VOID;
            }
            case "addprop" -> {
                if (args.size() < 2) yield Datum.VOID;
                Datum keyDatum = args.get(0);
                // addProp always appends -> allows duplicate keys
                propList.add(keyDatum.toKeyName(), args.get(1), keyDatum instanceof Datum.Symbol);
                yield Datum.VOID;
            }
            case "getat" -> {
                if (args.isEmpty()) yield Datum.VOID;
                Datum keyOrIndex = args.get(0);
                if (keyOrIndex instanceof Datum.Str s) {
                    yield propList.getOrDefault(s.value(), false, Datum.VOID);
                }
                if (keyOrIndex instanceof Datum.Symbol sym) {
                    yield propList.getOrDefault(sym.name(), true, Datum.VOID);
                }
                int index = keyOrIndex.toInt() - 1;
                if (index >= 0 && index < propList.size()) {
                    yield propList.getValue(index);
                }
                yield Datum.VOID;
            }
            case "setat" -> {
                if (args.size() < 2) yield Datum.VOID;
                Datum keyOrIndex = args.get(0);
                Datum value = args.get(1);
                if (keyOrIndex instanceof Datum.Int intKey) {
                    int index = intKey.value() - 1;
                    if (index >= 0 && index < propList.size()) {
                        propList.setValue(index, value);
                    }
                } else {
                    propList.putTyped(keyOrIndex.toKeyName(), keyOrIndex instanceof Datum.Symbol, value);
                }
                yield Datum.VOID;
            }
            case "getone" -> {
                // getOne(propList, value) - find the property NAME where the value matches
                // Returns the key (as symbol) or 0 if not found
                if (args.isEmpty()) yield Datum.ZERO;
                Datum searchValue = args.get(0);
                for (Datum.PropEntry entry : propList.entries()) {
                    if (entry.value().lingoEquals(searchValue)) {
                        yield Datum.symbol(entry.key());
                    }
                }
                yield Datum.ZERO;
            }
            case "deleteprop" -> {
                if (args.isEmpty()) yield Datum.VOID;
                Datum keyDatum = args.get(0);
                propList.remove(keyDatum.toKeyName(), keyDatum instanceof Datum.Symbol);
                yield Datum.VOID;
            }
            case "findpos" -> {
                if (args.isEmpty()) yield Datum.VOID;
                int pos = propList.findPos(args.get(0).toKeyName());
                yield pos > 0 ? Datum.of(pos) : Datum.VOID;
            }
            case "getpropat" -> {
                if (args.isEmpty()) yield Datum.VOID;
                int index = args.get(0).toInt() - 1;
                if (index >= 0 && index < propList.size()) {
                    yield Datum.symbol(propList.getKey(index));
                }
                yield Datum.VOID;
            }
            case "deleteat" -> {
                if (args.isEmpty()) yield Datum.VOID;
                int index = args.get(0).toInt() - 1;
                if (index >= 0 && index < propList.size()) {
                    propList.removeAt(index);
                }
                yield Datum.VOID;
            }
            case "getlast" -> propList.isEmpty() ? Datum.VOID : propList.getValue(propList.size() - 1);
            case "getfirst" -> propList.isEmpty() ? Datum.VOID : propList.getValue(0);
            case "duplicate" ->
                    // Deep copy: Director's duplicate() creates independent copies of nested structures.
                    propList.deepCopy();
            default -> Datum.VOID;
        };
    }
}
