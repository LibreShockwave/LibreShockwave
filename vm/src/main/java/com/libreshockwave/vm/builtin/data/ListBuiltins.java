package com.libreshockwave.vm.builtin.data;

import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.LingoVM;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.function.BiFunction;

/**
 * List and PropList builtin functions.
 * Handles global getAt/setAt/getaProp/setaProp/addProp/deleteProp calls.
 * Matches dirplayer-rs manager.rs handler dispatch.
 */
public final class ListBuiltins {

    private ListBuiltins() {}

    public static void register(Map<String, BiFunction<LingoVM, List<Datum>, Datum>> builtins) {
        builtins.put("count", ListBuiltins::count);
        builtins.put("getat", ListBuiltins::getAt);
        builtins.put("setat", ListBuiltins::setAt);
        builtins.put("addat", ListBuiltins::addAt);
        builtins.put("deleteat", ListBuiltins::deleteAt);
        builtins.put("append", ListBuiltins::append);
        builtins.put("getaprop", ListBuiltins::getaProp);
        builtins.put("setaprop", ListBuiltins::setaProp);
        builtins.put("addprop", ListBuiltins::addProp);
        builtins.put("deleteprop", ListBuiltins::deleteProp);
        builtins.put("getpropat", ListBuiltins::getPropAt);
        builtins.put("findpos", ListBuiltins::findPos);
        builtins.put("getone", ListBuiltins::getOne);
        builtins.put("getpos", ListBuiltins::getOne);
        builtins.put("deleteone", ListBuiltins::deleteOne);
        builtins.put("sort", ListBuiltins::sort);
        builtins.put("listp", ListBuiltins::listP);
        builtins.put("list", ListBuiltins::listConstructor);
        builtins.put("getlast", ListBuiltins::getLast);
    }

    private static Datum count(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) return Datum.ZERO;
        Datum a = args.get(0);
        if (a instanceof Datum.List l) {
            return Datum.of(l.items().size());
        } else if (a instanceof Datum.PropList p) {
            return Datum.of(p.size());
        }
        return Datum.ZERO;
    }

    /**
     * getAt(container, keyOrIndex) - Get element from list or proplist.
     * For List: integer index (1-based).
     * For PropList: integer index (1-based positional) or symbol/string key.
     */
    private static Datum getAt(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return Datum.VOID;
        Datum container = args.get(0);
        Datum keyOrIndex = args.get(1);
        if (container instanceof Datum.List l) {
            int index = keyOrIndex.toInt() - 1;
            if (index >= 0 && index < l.items().size()) {
                return l.items().get(index);
            }
            return Datum.VOID;
        }

        if (container instanceof Datum.PropList pl) {
            if (keyOrIndex instanceof Datum.Symbol sym) {
                return pl.getOrDefault(sym.name(), true, Datum.VOID);
            }
            if (keyOrIndex instanceof Datum.Str s) {
                return pl.getOrDefault(s.value(), false, Datum.VOID);
            }
            // Integer positional access (1-based)
            int index = keyOrIndex.toInt() - 1;
            if (index >= 0 && index < pl.size()) {
                return pl.getValue(index);
            }
            return Datum.VOID;
        }

        if (container instanceof Datum.Point point) {
            int index = keyOrIndex.toInt();
            return switch (index) {
                case 1 -> Datum.of(point.x());
                case 2 -> Datum.of(point.y());
                default -> Datum.VOID;
            };
        }

        if (container instanceof Datum.Rect rect) {
            int index = keyOrIndex.toInt();
            return switch (index) {
                case 1 -> Datum.of(rect.left());
                case 2 -> Datum.of(rect.top());
                case 3 -> Datum.of(rect.right());
                case 4 -> Datum.of(rect.bottom());
                default -> Datum.VOID;
            };
        }

        return Datum.VOID;
    }

    /**
     * setAt(container, keyOrIndex, value) - Set element in list or proplist.
     * For List: integer index (1-based).
     * For PropList: integer index (1-based positional) or symbol/string key.
     */
    private static Datum setAt(LingoVM vm, List<Datum> args) {
        if (args.size() < 3) return Datum.VOID;
        Datum container = args.get(0);
        Datum keyOrIndex = args.get(1);
        Datum value = args.get(2);
        if (container instanceof Datum.List l) {
            int index = keyOrIndex.toInt() - 1;
            if (index >= 0 && index < l.items().size()) {
                l.items().set(index, value);
            }
            return Datum.VOID;
        }

        if (container instanceof Datum.PropList pl) {
            // Type-aware set for symbols and strings
            if (keyOrIndex instanceof Datum.Symbol sym) {
                pl.putTyped(sym.name(), true, value);
                return Datum.VOID;
            }
            if (keyOrIndex instanceof Datum.Str s) {
                pl.putTyped(s.value(), false, value);
                return Datum.VOID;
            }
            // Integer positional set (1-based)
            int index = keyOrIndex.toInt() - 1;
            if (index >= 0 && index < pl.size()) {
                pl.setValue(index, value);
            }
            return Datum.VOID;
        }

        // Rect and Point are mutable - setAt modifies them in place (Director behavior)
        if (container instanceof Datum.Rect rect) {
            int index = keyOrIndex.toInt();
            int val = value.toInt();
            rect.setComponent(index, val);
            return Datum.VOID;
        }

        if (container instanceof Datum.Point point) {
            int index = keyOrIndex.toInt();
            int val = value.toInt();
            point.setComponent(index, val);
            return Datum.VOID;
        }

        return Datum.VOID;
    }

    /**
     * append(list, value) - add value to the end of a list.
     */
    private static Datum append(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return Datum.VOID;
        Datum datum = args.get(0);
        if (datum instanceof Datum.List list) {
            list.items().add(args.get(1));
        }
        return Datum.VOID;
    }

    /**
     * addAt(list, position, value) - insert value at position in a list.
     */
    private static Datum addAt(LingoVM vm, List<Datum> args) {
        if (args.size() < 3) return Datum.VOID;
        Datum datum = args.get(0);
        if (!(datum instanceof Datum.List list)) {
            return Datum.VOID;
        }
        int position = args.get(1).toInt() - 1;
        Datum value = args.get(2);
        if (position < 0) position = 0;
        if (position > list.items().size()) position = list.items().size();
        list.items().add(position, value);
        return Datum.VOID;
    }

    /**
     * deleteAt(container, position) - delete element at position.
     */
    private static Datum deleteAt(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return Datum.VOID;
        Datum container = args.get(0);
        int position = args.get(1).toInt() - 1;

        if (container instanceof Datum.List l) {
            if (position >= 0 && position < l.items().size()) {
                l.items().remove(position);
            }
        } else if (container instanceof Datum.PropList pl) {
            if (position >= 0 && position < pl.size()) {
                pl.removeAt(position);
            }
        }
        return Datum.VOID;
    }

    /**
     * getaProp(propList, key) - Get property by key from a PropList.
     */
    private static Datum getaProp(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return Datum.VOID;
        Datum container = args.get(0);
        if (!(container instanceof Datum.PropList pl)) return Datum.VOID;

        Datum keyDatum = args.get(1);
        String key = keyDatum.toKeyName();
        return pl.getOrDefault(key, keyDatum instanceof Datum.Symbol, Datum.VOID);
    }

    /**
     * setaProp(propList, key, value) - Set property by key in a PropList.
     */
    private static Datum setaProp(LingoVM vm, List<Datum> args) {
        if (args.size() < 3) return Datum.VOID;
        Datum container = args.get(0);
        if (!(container instanceof Datum.PropList pl)) return Datum.VOID;

        Datum keyDatum = args.get(1);
        String key = keyDatum.toKeyName();
        boolean isSym = keyDatum instanceof Datum.Symbol;
        pl.put(key, isSym, args.get(2));
        return Datum.VOID;
    }

    /**
     * addProp(propList, key, value) - Add property to a PropList (allows duplicates).
     */
    private static Datum addProp(LingoVM vm, List<Datum> args) {
        if (args.size() < 3) return Datum.VOID;
        Datum container = args.get(0);
        if (!(container instanceof Datum.PropList pl)) return Datum.VOID;

        String key = args.get(1).toKeyName();
        boolean isSym = args.get(1) instanceof Datum.Symbol;
        pl.add(key, args.get(2), isSym);
        return Datum.VOID;
    }

    /**
     * deleteProp(propList, key) - Delete property from a PropList.
     */
    private static Datum deleteProp(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return Datum.VOID;
        Datum container = args.get(0);
        if (!(container instanceof Datum.PropList pl)) return Datum.VOID;

        Datum keyDatum = args.get(1);
        pl.remove(keyDatum.toKeyName(), keyDatum instanceof Datum.Symbol);
        return Datum.VOID;
    }

    /**
     * getPropAt(propList, position) - Get the key at a position (1-based).
     */
    private static Datum getPropAt(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return Datum.VOID;
        Datum container = args.get(0);
        if (!(container instanceof Datum.PropList pl)) return Datum.VOID;

        int index = args.get(1).toInt() - 1;
        if (index >= 0 && index < pl.size()) {
            return Datum.symbol(pl.getKey(index));
        }
        return Datum.VOID;
    }

    /**
     * findPos(propList, key) - Find position of key in PropList (1-based).
     */
    private static Datum findPos(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return Datum.VOID;
        Datum container = args.get(0);
        if (!(container instanceof Datum.PropList pl)) return Datum.VOID;

        String key = args.get(1).toKeyName();
        int pos = pl.findPos(key);
        return pos > 0 ? Datum.of(pos) : Datum.VOID;
    }

    /**
     * getOne(list, value) - Find position of value in list (1-based).
     */
    private static Datum getOne(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return Datum.ZERO;
        Datum container = args.get(0);
        Datum target = args.get(1);

        if (container instanceof Datum.List l) {
            for (int i = 0; i < l.items().size(); i++) {
                if (l.items().get(i).lingoEquals(target)) {
                    return Datum.of(i + 1);
                }
            }
        }
        return Datum.ZERO;
    }

    /**
     * deleteOne(list, value) - Delete first occurrence of value from list.
     */
    private static Datum deleteOne(LingoVM vm, List<Datum> args) {
        if (args.size() < 2) return Datum.VOID;
        Datum container = args.get(0);
        Datum target = args.get(1);

        if (container instanceof Datum.List l) {
            for (int i = 0; i < l.items().size(); i++) {
                if (l.items().get(i).lingoEquals(target)) {
                    l.items().remove(i);
                    break;
                }
            }
        }
        return Datum.VOID;
    }

    /**
     * sort(list) - Sort a list in place.
     */
    private static Datum sort(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) return Datum.VOID;
        Datum container = args.get(0);
        if (container instanceof Datum.List l) {
            l.items().sort((a, b) -> {
                if (a instanceof Datum.Int ai && b instanceof Datum.Int bi) {
                    return Integer.compare(ai.value(), bi.value());
                }
                return a.toStr().compareToIgnoreCase(b.toStr());
            });
        }
        return Datum.VOID;
    }

    /**
     * listP(value) - Returns true if value is a list or proplist.
     */
    private static Datum listP(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) return Datum.FALSE;
        Datum v = args.get(0);
        return (v instanceof Datum.List || v instanceof Datum.PropList) ? Datum.TRUE : Datum.FALSE;
    }

    /**
     * list(args...) - Creates a new linear list from arguments.
     * list() returns [], list(1,2,3) returns [1,2,3].
     */
    private static Datum listConstructor(LingoVM vm, List<Datum> args) {
        return Datum.list(new ArrayList<>(args));
    }

    /**
     * getLast(container) - Returns the last element of a list or proplist.
     */
    private static Datum getLast(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) return Datum.VOID;
        Datum container = args.get(0);
        if (container instanceof Datum.List l) {
            if (l.items().isEmpty()) return Datum.VOID;
            return l.items().get(l.items().size() - 1);
        }
        if (container instanceof Datum.PropList pl) {
            if (pl.isEmpty()) return Datum.VOID;
            return pl.getValue(pl.size() - 1);
        }
        return Datum.VOID;
    }
}
