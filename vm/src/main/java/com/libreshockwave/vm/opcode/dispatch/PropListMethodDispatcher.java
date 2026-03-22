package com.libreshockwave.vm.opcode.dispatch;

import com.libreshockwave.vm.datum.Datum;

import java.util.List;

/**
 * Handles method calls on property lists.
 * Uses equalsIgnoreCase to avoid toLowerCase String allocation.
 */
public final class PropListMethodDispatcher {

    private PropListMethodDispatcher() {}

    public static Datum dispatch(Datum.PropList propList, String methodName, List<Datum> args) {
        // Fast path for most common operations (no allocation)
        if ("count".equalsIgnoreCase(methodName)) {
            // count(propList) → number of entries
            // count(propList, #prop) → count of the sub-property value (list.prop.count)
            if (!args.isEmpty()) {
                Datum sub = propList.getOrDefault(args.get(0).toKeyName(), Datum.VOID);
                if (sub instanceof Datum.List subList) return Datum.of(subList.items().size());
                if (sub instanceof Datum.PropList subProp) return Datum.of(subProp.size());
                return Datum.ZERO;
            }
            return Datum.of(propList.size());
        }
        if ("getprop".equalsIgnoreCase(methodName) || "getaprop".equalsIgnoreCase(methodName)
                || "getproperty".equalsIgnoreCase(methodName)) {
            if (args.isEmpty()) return Datum.VOID;
            String key = args.get(0).toKeyName();
            Datum value = propList.getOrDefault(key, Datum.VOID);
            // getProp(propList, #prop, index) → propList.prop[index]
            if (args.size() >= 2 && value instanceof Datum.List subList) {
                int index = args.get(1).toInt() - 1; // 1-indexed
                if (index >= 0 && index < subList.items().size()) {
                    return subList.items().get(index);
                }
                return Datum.VOID;
            }
            return value;
        }
        if ("setprop".equalsIgnoreCase(methodName) || "setaprop".equalsIgnoreCase(methodName)) {
            if (args.size() < 2) return Datum.VOID;
            String key = args.get(0).toKeyName();
            // Type-unaware: matches first entry by key, preserves existing type flag
            propList.put(key, args.get(1));
            return Datum.VOID;
        }
        if ("addprop".equalsIgnoreCase(methodName)) {
            if (args.size() < 2) return Datum.VOID;
            String key = args.get(0).toKeyName();
            boolean isSym = args.get(0) instanceof Datum.Symbol;
            // addProp always appends — allows duplicate keys
            propList.add(key, args.get(1), isSym);
            return Datum.VOID;
        }
        if ("getat".equalsIgnoreCase(methodName)) {
            if (args.isEmpty()) return Datum.VOID;
            Datum keyOrIndex = args.get(0);
            if (keyOrIndex instanceof Datum.Str s) {
                // String key: skip entries that are explicitly symbol-keyed AND have
                // different case. This prevents "Room_interface" from matching
                // #room_interface while still allowing same-case lookups and lookups
                // on entries stored via type-unaware paths (isSymbolKey=false).
                String key = s.value();
                for (Datum.PropEntry e : propList.entries()) {
                    if (e.key().equalsIgnoreCase(key)) {
                        // Skip symbol entries where the case doesn't match exactly
                        if (e.isSymbolKey() && !e.key().equals(key)) continue;
                        return e.value();
                    }
                }
                return Datum.VOID;
            } else if (keyOrIndex instanceof Datum.Symbol sym) {
                return propList.getOrDefault(sym.name(), Datum.VOID);
            } else {
                int index = keyOrIndex.toInt() - 1;
                if (index >= 0 && index < propList.size()) {
                    return propList.getValue(index);
                }
                return Datum.VOID;
            }
        }
        if ("setat".equalsIgnoreCase(methodName)) {
            if (args.size() < 2) return Datum.VOID;
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
            return Datum.VOID;
        }
        if ("getone".equalsIgnoreCase(methodName)) {
            // getOne(propList, value) - find the property NAME where the value matches
            // Returns the key (as symbol) or 0 if not found
            if (args.isEmpty()) return Datum.ZERO;
            Datum searchValue = args.get(0);
            for (Datum.PropEntry entry : propList.entries()) {
                if (entry.value().lingoEquals(searchValue)) {
                    return Datum.symbol(entry.key());
                }
            }
            return Datum.ZERO;
        }
        if ("deleteprop".equalsIgnoreCase(methodName)) {
            if (args.isEmpty()) return Datum.VOID;
            String key = args.get(0).toKeyName();
            propList.remove(key);
            return Datum.VOID;
        }
        if ("findpos".equalsIgnoreCase(methodName)) {
            if (args.isEmpty()) return Datum.VOID;
            String key = args.get(0).toKeyName();
            int pos = propList.findPos(key);
            return pos > 0 ? Datum.of(pos) : Datum.VOID;
        }
        if ("getpropat".equalsIgnoreCase(methodName)) {
            if (args.isEmpty()) return Datum.VOID;
            int index = args.get(0).toInt() - 1;
            if (index >= 0 && index < propList.size()) return Datum.symbol(propList.getKey(index));
            return Datum.VOID;
        }
        if ("deleteat".equalsIgnoreCase(methodName)) {
            if (args.isEmpty()) return Datum.VOID;
            int index = args.get(0).toInt() - 1;
            if (index >= 0 && index < propList.size()) propList.removeAt(index);
            return Datum.VOID;
        }
        if ("getlast".equalsIgnoreCase(methodName)) {
            if (propList.isEmpty()) return Datum.VOID;
            return propList.getValue(propList.size() - 1);
        }
        if ("getfirst".equalsIgnoreCase(methodName)) {
            if (propList.isEmpty()) return Datum.VOID;
            return propList.getValue(0);
        }
        if ("duplicate".equalsIgnoreCase(methodName)) {
            // Deep copy: Director's duplicate() creates independent copies of nested structures.
            return propList.deepCopy();
        }
        return Datum.VOID;
    }
}
