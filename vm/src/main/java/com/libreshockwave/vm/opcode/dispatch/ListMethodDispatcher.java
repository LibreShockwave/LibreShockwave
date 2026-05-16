package com.libreshockwave.vm.opcode.dispatch;

import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.datum.LingoException;

import java.util.ArrayList;
import java.util.List;

/**
 * Handles method calls on linear lists.
 */
public final class ListMethodDispatcher {

    private ListMethodDispatcher() {}

    public static Datum dispatch(Datum.List list, String methodName, List<Datum> args) {
        java.util.List<Datum> items = list.items();
        // Fast paths for common list methods used in numeric and collection-heavy scripts.
        if ("getAt".equalsIgnoreCase(methodName)) {
            if (args.isEmpty()) return Datum.VOID;
            Datum indexDatum = args.get(0);
            int index = (indexDatum instanceof Datum.Int i ? i.value() : indexDatum.toInt()) - 1;
            if (index < 0 || index >= items.size()) {
                throw new LingoException("getAt: index " + (index + 1)
                    + " out of range (list size: " + items.size()
                    + ", list: " + list + ")");
            }
            return items.get(index);
        }
        if ("setAt".equalsIgnoreCase(methodName)) {
            if (args.size() < 2) return Datum.VOID;
            Datum indexDatum = args.get(0);
            int index = (indexDatum instanceof Datum.Int i ? i.value() : indexDatum.toInt()) - 1;
            Datum value = args.get(1);
            if (index < 0) return Datum.VOID;
            if (index < items.size()) {
                items.set(index, value);
            } else {
                while (items.size() < index) {
                    items.add(Datum.VOID);
                }
                items.add(value);
            }
            return Datum.VOID;
        }
        if ("count".equalsIgnoreCase(methodName)) return Datum.of(items.size());
        if ("append".equalsIgnoreCase(methodName) || "add".equalsIgnoreCase(methodName)) {
            if (!args.isEmpty()) {
                items.add(args.get(0));
            }
            return Datum.VOID;
        }
        if ("addAt".equalsIgnoreCase(methodName)) {
            if (args.size() < 2) return Datum.VOID;
            Datum indexDatum = args.get(0);
            int index = (indexDatum instanceof Datum.Int i ? i.value() : indexDatum.toInt()) - 1;
            Datum value = args.get(1);
            if (index < 0) index = 0;
            if (index >= items.size()) {
                items.add(value);
            } else {
                items.add(index, value);
            }
            return Datum.VOID;
        }
        String method = methodName.toLowerCase();
        return switch (method) {
            case "getat" -> {
                if (args.isEmpty()) yield Datum.VOID;
                int index = args.get(0).toInt() - 1; // 1-indexed
                if (index < 0 || index >= items.size()) {
                    throw new LingoException("getAt: index " + (index + 1)
                        + " out of range (list size: " + items.size()
                        + ", list: " + list + ")");
                }
                yield items.get(index);
            }
            case "setat" -> {
                // setAt(list, position, value) - set value at position (1-indexed)
                // Like dirplayer-rs: pads with VOID if index > current length
                if (args.size() < 2) yield Datum.VOID;
                int index = args.get(0).toInt() - 1; // Convert to 0-indexed
                Datum value = args.get(1);
                if (index < 0) yield Datum.VOID;
                if (index < items.size()) {
                    items.set(index, value);
                } else {
                    // Pad with VOID values up to the target index
                    while (items.size() < index) {
                        items.add(Datum.VOID);
                    }
                    items.add(value);
                }
                yield Datum.VOID;
            }
            case "append", "add" -> {
                if (args.isEmpty()) yield Datum.VOID;
                items.add(args.get(0));
                yield Datum.VOID;
            }
            case "addat" -> {
                // addAt(list, position, value) - insert value at position (1-indexed)
                if (args.size() < 2) yield Datum.VOID;
                int index = args.get(0).toInt() - 1; // Convert to 0-indexed
                Datum value = args.get(1);
                if (index < 0) index = 0;
                if (index >= items.size()) {
                    items.add(value);
                } else {
                    items.add(index, value);
                }
                yield Datum.VOID;
            }
            case "deleteat" -> {
                if (args.isEmpty()) yield Datum.VOID;
                int index = args.get(0).toInt() - 1;
                if (index >= 0 && index < items.size()) {
                    items.remove(index);
                }
                yield Datum.VOID;
            }
            case "getone", "findpos", "getpos" -> {
                // Find 1-based index of value, returns 0 if not found
                if (args.isEmpty()) yield Datum.ZERO;
                Datum value = args.get(0);
                for (int i = 0; i < items.size(); i++) {
                    if (items.get(i).lingoEquals(value)) {
                        yield Datum.of(i + 1);
                    }
                }
                yield Datum.ZERO;
            }
            case "getlast" -> {
                // getLast(list) - return the last element
                if (items.isEmpty()) yield Datum.VOID;
                yield items.get(items.size() - 1);
            }
            case "deleteone" -> {
                // deleteOne(list, value) - remove first matching element
                if (args.isEmpty()) yield Datum.FALSE;
                Datum value = args.get(0);
                boolean found = false;
                for (int i = 0; i < items.size(); i++) {
                    if (items.get(i).lingoEquals(value)) {
                        items.remove(i);
                        found = true;
                        break;
                    }
                }
                yield found ? Datum.TRUE : Datum.FALSE;
            }
            case "join" -> {
                // join(list, separator) - concatenate elements into string
                String separator = args.isEmpty() ? "&" : args.get(0).toStr();
                StringBuilder sb = new StringBuilder();
                for (int i = 0; i < items.size(); i++) {
                    if (i > 0) sb.append(separator);
                    sb.append(items.get(i).toStr());
                }
                yield Datum.of(sb.toString());
            }
            case "sort" -> {
                // Sort list in place (simple implementation)
                items.sort((a, b) -> {
                    if (a instanceof Datum.Int ai && b instanceof Datum.Int bi) {
                        return Integer.compare(ai.value(), bi.value());
                    }
                    return a.toStr().compareToIgnoreCase(b.toStr());
                });
                yield Datum.VOID;
            }
            case "duplicate" -> {
                // Deep copy: Director's duplicate() creates independent copies of nested structures.
                // Shallow copy causes shared-cache corruption (e.g., Layout Parser cache in Habbo).
                yield list.deepCopy();
            }
            default -> Datum.VOID;
        };
    }
}
