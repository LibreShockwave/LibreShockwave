package com.libreshockwave.vm.opcode;

import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.vm.datum.Datum;

import java.util.Map;

/**
 * Comparison operation opcodes.
 */
public final class ComparisonOpcodes {

    private ComparisonOpcodes() {}

    public static void register(Map<Opcode, OpcodeHandler> handlers) {
        handlers.put(Opcode.LT, ComparisonOpcodes::lt);
        handlers.put(Opcode.LT_EQ, ComparisonOpcodes::ltEq);
        handlers.put(Opcode.GT, ComparisonOpcodes::gt);
        handlers.put(Opcode.GT_EQ, ComparisonOpcodes::gtEq);
        handlers.put(Opcode.EQ, ComparisonOpcodes::eq);
        handlers.put(Opcode.NT_EQ, ComparisonOpcodes::ntEq);
    }

    private static boolean lt(ExecutionContext ctx) {
        Datum b = ctx.pop();
        Datum a = ctx.pop();
        ctx.push(lessThan(a, b) ? Datum.TRUE : Datum.FALSE);
        return true;
    }

    private static boolean ltEq(ExecutionContext ctx) {
        Datum b = ctx.pop();
        Datum a = ctx.pop();
        ctx.push(lessThanOrEqual(a, b) ? Datum.TRUE : Datum.FALSE);
        return true;
    }

    private static boolean gt(ExecutionContext ctx) {
        Datum b = ctx.pop();
        Datum a = ctx.pop();
        ctx.push(greaterThan(a, b) ? Datum.TRUE : Datum.FALSE);
        return true;
    }

    private static boolean gtEq(ExecutionContext ctx) {
        Datum b = ctx.pop();
        Datum a = ctx.pop();
        ctx.push(greaterThanOrEqual(a, b) ? Datum.TRUE : Datum.FALSE);
        return true;
    }

    private static boolean eq(ExecutionContext ctx) {
        Datum b = ctx.pop();
        Datum a = ctx.pop();
        ctx.push(datumEquals(a, b) ? Datum.TRUE : Datum.FALSE);
        return true;
    }

    private static boolean ntEq(ExecutionContext ctx) {
        Datum b = ctx.pop();
        Datum a = ctx.pop();
        ctx.push(!datumEquals(a, b) ? Datum.TRUE : Datum.FALSE);
        return true;
    }

    private static boolean datumEquals(Datum a, Datum b) {
        if (a == b) {
            return true;
        }
        if (a instanceof Datum.Int ai && b instanceof Datum.Int bi) {
            return ai.value() == bi.value();
        }
        if (a instanceof Datum.Float af && b instanceof Datum.Float bf) {
            return af.value() == bf.value();
        }
        if (a instanceof Datum.Int ai && b instanceof Datum.Float bf) {
            return ai.value() == bf.value();
        }
        if (a instanceof Datum.Float af && b instanceof Datum.Int bi) {
            return af.value() == bi.value();
        }
        return a.lingoEquals(b);
    }

    private static boolean lessThan(Datum a, Datum b) {
        if (a instanceof Datum.Int ai && b instanceof Datum.Int bi) {
            return ai.value() < bi.value();
        }
        if (a instanceof Datum.Float af && b instanceof Datum.Float bf) {
            return af.value() < bf.value();
        }
        if (a instanceof Datum.Int ai && b instanceof Datum.Float bf) {
            return ai.value() < bf.value();
        }
        if (a instanceof Datum.Float af && b instanceof Datum.Int bi) {
            return af.value() < bi.value();
        }
        return a.toDouble() < b.toDouble();
    }

    private static boolean lessThanOrEqual(Datum a, Datum b) {
        if (a instanceof Datum.Int ai && b instanceof Datum.Int bi) {
            return ai.value() <= bi.value();
        }
        if (a instanceof Datum.Float af && b instanceof Datum.Float bf) {
            return af.value() <= bf.value();
        }
        if (a instanceof Datum.Int ai && b instanceof Datum.Float bf) {
            return ai.value() <= bf.value();
        }
        if (a instanceof Datum.Float af && b instanceof Datum.Int bi) {
            return af.value() <= bi.value();
        }
        return a.toDouble() <= b.toDouble();
    }

    private static boolean greaterThan(Datum a, Datum b) {
        if (a instanceof Datum.Int ai && b instanceof Datum.Int bi) {
            return ai.value() > bi.value();
        }
        if (a instanceof Datum.Float af && b instanceof Datum.Float bf) {
            return af.value() > bf.value();
        }
        if (a instanceof Datum.Int ai && b instanceof Datum.Float bf) {
            return ai.value() > bf.value();
        }
        if (a instanceof Datum.Float af && b instanceof Datum.Int bi) {
            return af.value() > bi.value();
        }
        return a.toDouble() > b.toDouble();
    }

    private static boolean greaterThanOrEqual(Datum a, Datum b) {
        if (a instanceof Datum.Int ai && b instanceof Datum.Int bi) {
            return ai.value() >= bi.value();
        }
        if (a instanceof Datum.Float af && b instanceof Datum.Float bf) {
            return af.value() >= bf.value();
        }
        if (a instanceof Datum.Int ai && b instanceof Datum.Float bf) {
            return ai.value() >= bf.value();
        }
        if (a instanceof Datum.Float af && b instanceof Datum.Int bi) {
            return af.value() >= bi.value();
        }
        return a.toDouble() >= b.toDouble();
    }
}
