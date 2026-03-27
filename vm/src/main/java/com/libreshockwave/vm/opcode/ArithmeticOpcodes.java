package com.libreshockwave.vm.opcode;

import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.vm.datum.Datum;

import java.util.Map;

/**
 * Arithmetic operation opcodes.
 */
public final class ArithmeticOpcodes {

    private ArithmeticOpcodes() {}

    public static void register(Map<Opcode, OpcodeHandler> handlers) {
        handlers.put(Opcode.ADD, ArithmeticOpcodes::add);
        handlers.put(Opcode.SUB, ArithmeticOpcodes::sub);
        handlers.put(Opcode.MUL, ArithmeticOpcodes::mul);
        handlers.put(Opcode.DIV, ArithmeticOpcodes::div);
        handlers.put(Opcode.MOD, ArithmeticOpcodes::mod);
        handlers.put(Opcode.INV, ArithmeticOpcodes::inv);
    }

    private static boolean add(ExecutionContext ctx) {
        Datum b = ctx.pop();
        Datum a = ctx.pop();

        // Point + Point/List/scalar arithmetic (Director supports this)
        if (a instanceof Datum.Point pa) {
            int dx = 0, dy = 0;
            if (b instanceof Datum.Point pb) {
                dx = pb.x(); dy = pb.y();
            } else if (b instanceof Datum.List list && list.items().size() >= 2) {
                dx = list.items().get(0).toInt();
                dy = list.items().get(1).toInt();
            } else {
                dx = b.toInt(); dy = b.toInt();
            }
            ctx.push(new Datum.Point(pa.x() + dx, pa.y() + dy));
            return true;
        }
        if (b instanceof Datum.Point pb && a instanceof Datum.List list && list.items().size() >= 2) {
            ctx.push(new Datum.Point(list.items().get(0).toInt() + pb.x(), list.items().get(1).toInt() + pb.y()));
            return true;
        }

        // Rect + Rect/List/scalar arithmetic
        if (a instanceof Datum.Rect ra) {
            int dl, dt, dr, dbottom;
            if (b instanceof Datum.Rect rb) {
                dl = rb.left(); dt = rb.top(); dr = rb.right(); dbottom = rb.bottom();
            } else if (b instanceof Datum.List list && list.items().size() >= 4) {
                dl = list.items().get(0).toInt(); dt = list.items().get(1).toInt();
                dr = list.items().get(2).toInt(); dbottom = list.items().get(3).toInt();
            } else {
                // Scalar: add to all components (Director: rect(60,40,120,200) + 80 = rect(140,120,200,280))
                int s = b.toInt();
                dl = s; dt = s; dr = s; dbottom = s;
            }
            ctx.push(new Datum.Rect(ra.left() + dl, ra.top() + dt, ra.right() + dr, ra.bottom() + dbottom));
            return true;
        }

        // List + List element-wise arithmetic (Director supports this)
        if (a instanceof Datum.List la && b instanceof Datum.List lb) {
            var itemsA = la.items();
            var itemsB = lb.items();
            int size = Math.min(itemsA.size(), itemsB.size());
            var result = new java.util.ArrayList<Datum>(size);
            for (int i = 0; i < size; i++) {
                Datum ai = itemsA.get(i), bi = itemsB.get(i);
                if (ai.isFloat() || bi.isFloat()) {
                    result.add(Datum.of(ai.toDouble() + bi.toDouble()));
                } else {
                    result.add(Datum.of(ai.toInt() + bi.toInt()));
                }
            }
            ctx.push(new Datum.List(result));
            return true;
        }

        // Color + Color: per-channel addition with clamping (Director behavior)
        if (a instanceof Datum.Color ca && b instanceof Datum.Color cb) {
            ctx.push(new Datum.Color(
                    Math.min(255, ca.r() + cb.r()),
                    Math.min(255, ca.g() + cb.g()),
                    Math.min(255, ca.b() + cb.b())));
            return true;
        }

        if (a.isFloat() || b.isFloat()) {
            ctx.push(Datum.of(a.toDouble() + b.toDouble()));
        } else {
            ctx.push(Datum.of(a.toInt() + b.toInt()));
        }
        return true;
    }

    private static boolean sub(ExecutionContext ctx) {
        Datum b = ctx.pop();
        Datum a = ctx.pop();

        // Point - Point/List arithmetic
        if (a instanceof Datum.Point pa) {
            int dx = 0, dy = 0;
            if (b instanceof Datum.Point pb) {
                dx = pb.x(); dy = pb.y();
            } else if (b instanceof Datum.List list && list.items().size() >= 2) {
                dx = list.items().get(0).toInt();
                dy = list.items().get(1).toInt();
            } else {
                dx = b.toInt(); dy = b.toInt();
            }
            ctx.push(new Datum.Point(pa.x() - dx, pa.y() - dy));
            return true;
        }

        // Rect - Rect/List/scalar arithmetic
        if (a instanceof Datum.Rect ra) {
            int dl, dt, dr, dbottom;
            if (b instanceof Datum.Rect rb) {
                dl = rb.left(); dt = rb.top(); dr = rb.right(); dbottom = rb.bottom();
            } else if (b instanceof Datum.List list && list.items().size() >= 4) {
                dl = list.items().get(0).toInt(); dt = list.items().get(1).toInt();
                dr = list.items().get(2).toInt(); dbottom = list.items().get(3).toInt();
            } else {
                int s = b.toInt();
                dl = s; dt = s; dr = s; dbottom = s;
            }
            ctx.push(new Datum.Rect(ra.left() - dl, ra.top() - dt, ra.right() - dr, ra.bottom() - dbottom));
            return true;
        }

        // List - List element-wise arithmetic (Director supports this)
        if (a instanceof Datum.List la && b instanceof Datum.List lb) {
            var itemsA = la.items();
            var itemsB = lb.items();
            int size = Math.min(itemsA.size(), itemsB.size());
            var result = new java.util.ArrayList<Datum>(size);
            for (int i = 0; i < size; i++) {
                Datum ai = itemsA.get(i), bi = itemsB.get(i);
                if (ai.isFloat() || bi.isFloat()) {
                    result.add(Datum.of(ai.toDouble() - bi.toDouble()));
                } else {
                    result.add(Datum.of(ai.toInt() - bi.toInt()));
                }
            }
            ctx.push(new Datum.List(result));
            return true;
        }

        // Color - Color: per-channel subtraction with clamping (Director behavior)
        if (a instanceof Datum.Color ca && b instanceof Datum.Color cb) {
            ctx.push(new Datum.Color(
                    Math.max(0, ca.r() - cb.r()),
                    Math.max(0, ca.g() - cb.g()),
                    Math.max(0, ca.b() - cb.b())));
            return true;
        }

        if (a.isFloat() || b.isFloat()) {
            ctx.push(Datum.of(a.toDouble() - b.toDouble()));
        } else {
            ctx.push(Datum.of(a.toInt() - b.toInt()));
        }
        return true;
    }

    private static boolean mul(ExecutionContext ctx) {
        Datum b = ctx.pop();
        Datum a = ctx.pop();

        // Point * scalar
        if (a instanceof Datum.Point pa) {
            double s = b.toDouble();
            ctx.push(new Datum.Point((int) (pa.x() * s), (int) (pa.y() * s)));
            return true;
        }

        // Scalar * Point
        if (b instanceof Datum.Point pb) {
            double s = a.toDouble();
            ctx.push(new Datum.Point((int) (pb.x() * s), (int) (pb.y() * s)));
            return true;
        }

        // Rect * scalar (Director: rect(10,20,30,40) * 2 = rect(20,40,60,80))
        if (a instanceof Datum.Rect ra) {
            double s = b.toDouble();
            ctx.push(new Datum.Rect(
                    (int) (ra.left() * s),
                    (int) (ra.top() * s),
                    (int) (ra.right() * s),
                    (int) (ra.bottom() * s)));
            return true;
        }

        // Scalar * Rect
        if (b instanceof Datum.Rect rb) {
            double s = a.toDouble();
            ctx.push(new Datum.Rect(
                    (int) (rb.left() * s),
                    (int) (rb.top() * s),
                    (int) (rb.right() * s),
                    (int) (rb.bottom() * s)));
            return true;
        }

        // List * scalar element-wise (used heavily by Habbo movement interpolation).
        if (a instanceof Datum.List la && !(b instanceof Datum.List)) {
            ctx.push(scaleList(la, b.toDouble(), b.isFloat()));
            return true;
        }

        // Scalar * List element-wise
        if (b instanceof Datum.List lb && !(a instanceof Datum.List)) {
            ctx.push(scaleList(lb, a.toDouble(), a.isFloat()));
            return true;
        }

        if (a.isFloat() || b.isFloat()) {
            ctx.push(Datum.of(a.toDouble() * b.toDouble()));
        } else {
            ctx.push(Datum.of(a.toInt() * b.toInt()));
        }
        return true;
    }

    private static boolean div(ExecutionContext ctx) {
        Datum b = ctx.pop();
        Datum a = ctx.pop();
        double bVal = b.toDouble();
        if (bVal == 0) {
            throw ctx.error("Division by zero");
        }

        // Point / scalar
        if (a instanceof Datum.Point pa) {
            double s = b.toDouble();
            ctx.push(new Datum.Point((int) (pa.x() / s), (int) (pa.y() / s)));
            return true;
        }

        // Rect / scalar (Director: rect(60,40,120,200) / 3 = rect(20,13,40,66))
        if (a instanceof Datum.Rect ra) {
            double s = b.toDouble();
            ctx.push(new Datum.Rect(
                    (int) (ra.left() / s),
                    (int) (ra.top() / s),
                    (int) (ra.right() / s),
                    (int) (ra.bottom() / s)));
            return true;
        }

        // List / scalar element-wise.
        if (a instanceof Datum.List la) {
            ctx.push(divideList(la, b));
            return true;
        }

        // Director: int / int = int (truncated toward zero)
        if (!a.isFloat() && !b.isFloat()) {
            ctx.push(Datum.of(a.toInt() / b.toInt()));
        } else {
            ctx.push(Datum.of(a.toDouble() / bVal));
        }
        return true;
    }

    private static boolean mod(ExecutionContext ctx) {
        Datum b = ctx.pop();
        Datum a = ctx.pop();
        int bVal = b.toInt();
        if (bVal == 0) {
            throw ctx.error("Modulo by zero");
        }
        ctx.push(Datum.of(a.toInt() % bVal));
        return true;
    }

    private static boolean inv(ExecutionContext ctx) {
        Datum a = ctx.pop();
        if (a instanceof Datum.Point pa) {
            ctx.push(new Datum.Point(-pa.x(), -pa.y()));
        } else if (a instanceof Datum.Rect ra) {
            ctx.push(new Datum.Rect(-ra.left(), -ra.top(), -ra.right(), -ra.bottom()));
        } else if (a.isFloat()) {
            ctx.push(Datum.of(-a.toDouble()));
        } else {
            ctx.push(Datum.of(-a.toInt()));
        }
        return true;
    }

    private static Datum.List scaleList(Datum.List list, double scalar, boolean scalarIsFloat) {
        var items = list.items();
        var result = new java.util.ArrayList<Datum>(items.size());
        for (Datum item : items) {
            if (item.isFloat() || scalarIsFloat) {
                result.add(Datum.of(item.toDouble() * scalar));
            } else {
                result.add(Datum.of((int) (item.toInt() * scalar)));
            }
        }
        return new Datum.List(result);
    }

    private static Datum.List divideList(Datum.List list, Datum divisor) {
        var items = list.items();
        double scalar = divisor.toDouble();
        boolean divisorIsFloat = divisor.isFloat();
        var result = new java.util.ArrayList<Datum>(items.size());
        for (Datum item : items) {
            if (item.isFloat() || divisorIsFloat) {
                result.add(Datum.of(item.toDouble() / scalar));
            } else {
                result.add(Datum.of(item.toInt() / divisor.toInt()));
            }
        }
        return new Datum.List(result);
    }
}
