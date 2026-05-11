package com.libreshockwave.vm.opcode.dispatch;

import com.libreshockwave.vm.datum.Datum;

import java.math.BigInteger;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Native fast path for Habbo R31's HugeInt15 parent script.
 *
 * The original Lingo script implements decimal big-integer math in base 10000.
 * Running its modular exponentiation through the bytecode interpreter is slow
 * enough to trip the handler watchdog during login, so the crypto-hot methods
 * are mirrored here with BigInteger while preserving the instance properties.
 */
final class HugeInt15NativeDispatcher {
    private static final String DATA_PROP = "pData_NxIhNARqldyJyY2PfT03dK8t9OLUR";
    private static final String NEGATIVE_PROP = "pNegative";
    private static final String BASE_PROP = "pBase";
    private static final String DIGITS_PROP = "pDigits";
    private static final String SCRIPT_PROP = "pScript";
    private static final BigInteger BASE = BigInteger.valueOf(10000);
    private static final AtomicInteger NEXT_INSTANCE_ID = new AtomicInteger(-100000);

    private HugeInt15NativeDispatcher() {}

    static DispatchResult dispatch(Datum.ScriptInstance instance, String methodName, List<Datum> args) {
        if (!isHugeInt15Instance(instance)) {
            return DispatchResult.notHandled();
        }

        return switch (methodName.toLowerCase()) {
            case "powmod" -> powMod(instance, args);
            case "modulo" -> modulo(instance, args);
            case "div" -> div(instance, args);
            case "getstring" -> DispatchResult.handled(Datum.of(toBigInteger(instance).toString()));
            case "getbytearray" -> DispatchResult.handled(getByteArray(instance));
            case "getintarray" -> getIntArray(instance, args);
            default -> DispatchResult.notHandled();
        };
    }

    private static DispatchResult powMod(Datum.ScriptInstance instance, List<Datum> args) {
        if (args.size() < 2 || !(args.get(0) instanceof Datum.ScriptInstance power)
                || !(args.get(1) instanceof Datum.ScriptInstance divider)) {
            return DispatchResult.notHandled();
        }

        BigInteger base = toBigInteger(instance);
        BigInteger exponent = toBigInteger(power);
        BigInteger modulus = toBigInteger(divider);
        if (modulus.signum() == 0) {
            return DispatchResult.handled(Datum.VOID);
        }
        BigInteger result = base.modPow(exponent, modulus.abs());
        return DispatchResult.handled(newLike(instance, result));
    }

    private static DispatchResult modulo(Datum.ScriptInstance instance, List<Datum> args) {
        if (args.isEmpty() || !(args.get(0) instanceof Datum.ScriptInstance divider)) {
            return DispatchResult.notHandled();
        }

        BigInteger modulus = toBigInteger(divider);
        if (modulus.signum() == 0) {
            return DispatchResult.handled(Datum.VOID);
        }
        return DispatchResult.handled(newLike(instance, toBigInteger(instance).mod(modulus.abs())));
    }

    private static DispatchResult div(Datum.ScriptInstance instance, List<Datum> args) {
        if (args.isEmpty() || !(args.get(0) instanceof Datum.ScriptInstance divider)) {
            return DispatchResult.notHandled();
        }

        BigInteger divisor = toBigInteger(divider);
        if (divisor.signum() == 0) {
            return DispatchResult.handled(Datum.VOID);
        }

        BigInteger[] qr = toBigInteger(instance).divideAndRemainder(divisor);
        boolean returnModulo = args.size() >= 2 && args.get(1).isTruthy();
        boolean keepResult = args.size() >= 3 && args.get(2).isTruthy();
        if (returnModulo) {
            if (keepResult) {
                setBigInteger(instance, qr[0]);
            }
            // HugeInt15.div normalizes a zero remainder to [0] before returning it.
            // getByteArray() immediately indexes getIntArray()[1] on that result.
            return DispatchResult.handled(newLike(instance, qr[1], true));
        }
        return DispatchResult.handled(newLike(instance, qr[0]));
    }

    private static boolean isHugeInt15Instance(Datum.ScriptInstance instance) {
        Map<String, Datum> props = instance.properties();
        return props.containsKey(DATA_PROP)
                && props.containsKey(NEGATIVE_PROP)
                && props.containsKey(BASE_PROP)
                && props.containsKey(DIGITS_PROP)
                && props.containsKey(SCRIPT_PROP);
    }

    private static DispatchResult getIntArray(Datum.ScriptInstance instance, List<Datum> args) {
        if (!args.isEmpty() && args.get(0).isTruthy()) {
            return DispatchResult.notHandled();
        }
        Datum data = instance.properties().get(DATA_PROP);
        if (!(data instanceof Datum.List list)) {
            return DispatchResult.handled(new Datum.List(List.of()));
        }
        List<Datum> result = new ArrayList<>(list.items().size());
        for (int i = list.items().size() - 1; i >= 0; i--) {
            result.add(list.items().get(i));
        }
        return DispatchResult.handled(new Datum.List(result));
    }

    private static Datum getByteArray(Datum.ScriptInstance instance) {
        BigInteger value = toBigInteger(instance).abs();
        if (value.signum() == 0) {
            return new Datum.List(List.of(Datum.ZERO));
        }

        ArrayList<Datum> bytes = new ArrayList<>();
        BigInteger base = BigInteger.valueOf(256);
        while (value.signum() != 0) {
            BigInteger[] divRem = value.divideAndRemainder(base);
            bytes.add(0, Datum.of(divRem[1].intValue()));
            value = divRem[0];
        }
        return new Datum.List(bytes);
    }

    private static BigInteger toBigInteger(Datum.ScriptInstance instance) {
        Datum data = instance.properties().get(DATA_PROP);
        if (!(data instanceof Datum.List list) || list.items().isEmpty()) {
            return BigInteger.ZERO;
        }

        BigInteger value = BigInteger.ZERO;
        BigInteger multiplier = BigInteger.ONE;
        for (Datum item : list.items()) {
            value = value.add(BigInteger.valueOf(item.toInt()).multiply(multiplier));
            multiplier = multiplier.multiply(BASE);
        }
        Datum negative = instance.properties().get(NEGATIVE_PROP);
        return negative != null && negative.isTruthy() ? value.negate() : value;
    }

    private static Datum.ScriptInstance newLike(Datum.ScriptInstance template, BigInteger value) {
        return newLike(template, value, false);
    }

    private static Datum.ScriptInstance newLike(Datum.ScriptInstance template, BigInteger value, boolean zeroAsSingleDigit) {
        Map<String, Datum> props = new LinkedHashMap<>(template.properties());
        Datum.ScriptInstance instance = new Datum.ScriptInstance(NEXT_INSTANCE_ID.getAndDecrement(), props);
        setBigInteger(instance, value, zeroAsSingleDigit);
        return instance;
    }

    private static void setBigInteger(Datum.ScriptInstance instance, BigInteger value) {
        setBigInteger(instance, value, false);
    }

    private static void setBigInteger(Datum.ScriptInstance instance, BigInteger value, boolean zeroAsSingleDigit) {
        BigInteger abs = value.abs();
        List<Datum> items = new ArrayList<>();
        while (abs.signum() != 0) {
            BigInteger[] divRem = abs.divideAndRemainder(BASE);
            items.add(Datum.of(divRem[1].intValue()));
            abs = divRem[0];
        }
        if (items.isEmpty() && zeroAsSingleDigit) {
            items.add(Datum.ZERO);
        }
        instance.properties().put(DATA_PROP, new Datum.List(items));
        instance.properties().put(NEGATIVE_PROP, value.signum() < 0 ? Datum.TRUE : Datum.FALSE);
    }

    record DispatchResult(boolean handled, Datum value) {
        static DispatchResult handled(Datum value) {
            return new DispatchResult(true, value);
        }

        static DispatchResult notHandled() {
            return new DispatchResult(false, Datum.VOID);
        }
    }
}
