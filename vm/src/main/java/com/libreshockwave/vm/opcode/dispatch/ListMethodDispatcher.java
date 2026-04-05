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
        // Fast path for count (most common)
        if ("count".equalsIgnoreCase(methodName)) return Datum.of(list.items().size());
        String method = methodName.toLowerCase();
        return switch (method) {
            case "getat" -> {
                if (args.isEmpty()) yield Datum.VOID;
                int index = args.get(0).toInt() - 1; // 1-indexed
                if (index < 0 || index >= list.items().size()) {
                    throw new LingoException("getAt: index " + (index + 1)
                        + " out of range (list size: " + list.items().size()
                        + ", list: " + list + ")");
                }
                yield list.items().get(index);
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
                // addAt(list, position, value) - insert value at position (1-indexed)
                if (args.size() < 2) yield Datum.VOID;
                int index = args.get(0).toInt() - 1; // Convert to 0-indexed
                Datum value = args.get(1);
                if (index < 0) index = 0;
                if (index >= list.items().size()) {
                    list.items().add(value);
                } else {
                    list.items().add(index, value);
                }
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
            case "getone", "findpos", "getpos" -> {
                // Find 1-based index of value, returns 0 if not found
                if (args.isEmpty()) yield Datum.ZERO;
                Datum value = args.get(0);
                for (int i = 0; i < list.items().size(); i++) {
                    if (list.items().get(i).lingoEquals(value)) {
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
                if (args.isEmpty()) yield Datum.FALSE;
                Datum value = args.get(0);
                boolean found = false;
                for (int i = 0; i < list.items().size(); i++) {
                    if (list.items().get(i).lingoEquals(value)) {
                        list.items().remove(i);
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
                for (int i = 0; i < list.items().size(); i++) {
                    if (i > 0) sb.append(separator);
                    sb.append(list.items().get(i).toStr());
                }
                yield Datum.of(sb.toString());
            }
            case "sort" -> {
                // Sort list in place (simple implementation)
                list.items().sort((a, b) -> {
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
