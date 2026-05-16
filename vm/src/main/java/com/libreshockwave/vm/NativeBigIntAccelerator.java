package com.libreshockwave.vm;

import com.libreshockwave.vm.datum.Datum;

import java.math.BigInteger;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

final class NativeBigIntAccelerator {
    private static int nextSyntheticId = -1_000_000;

    private NativeBigIntAccelerator() {
    }

    static Datum tryPowMod(Datum receiver, List<Datum> args) {
        if (!(receiver instanceof Datum.ScriptInstance instance) || args.size() < 2) {
            return null;
        }
        Shape shape = Shape.from(instance);
        if (shape == null
                || !(args.get(0) instanceof Datum.ScriptInstance power)
                || !(args.get(1) instanceof Datum.ScriptInstance divider)) {
            return null;
        }
        Shape powerShape = Shape.from(power);
        Shape dividerShape = Shape.from(divider);
        if (powerShape == null || dividerShape == null
                || powerShape.base != shape.base || dividerShape.base != shape.base) {
            return null;
        }

        BigInteger modulus = toBigInteger(dividerShape);
        if (modulus.signum() == 0) {
            return null;
        }
        BigInteger exponent = toBigInteger(powerShape);
        if (exponent.signum() < 0) {
            return null;
        }
        BigInteger result = toBigInteger(shape).modPow(exponent, modulus);
        return fromBigInteger(instance, shape, result);
    }

    private static BigInteger toBigInteger(Shape shape) {
        BigInteger base = BigInteger.valueOf(shape.base);
        BigInteger value = BigInteger.ZERO;
        List<Datum> items = shape.data.items();
        for (int i = items.size() - 1; i >= 0; i--) {
            value = value.multiply(base).add(BigInteger.valueOf(items.get(i).toInt()));
        }
        return shape.negative ? value.negate() : value;
    }

    private static Datum fromBigInteger(Datum.ScriptInstance template, Shape shape, BigInteger value) {
        boolean negative = value.signum() < 0;
        BigInteger remaining = value.abs();
        BigInteger base = BigInteger.valueOf(shape.base);
        List<Datum> items = new ArrayList<>();
        while (remaining.signum() != 0) {
            BigInteger[] divRem = remaining.divideAndRemainder(base);
            items.add(Datum.of(divRem[1].intValue()));
            remaining = divRem[0];
        }

        Map<String, Datum> props = new LinkedHashMap<>(template.properties());
        props.put(shape.dataProperty, new Datum.List(items));
        props.put("pNegative", negative ? Datum.ONE : Datum.ZERO);
        props.put("pBase", Datum.of(shape.base));
        props.put("pDigits", Datum.of(shape.digits));
        return new Datum.ScriptInstance(nextSyntheticId--, props);
    }

    private record Shape(String dataProperty, Datum.List data, int base, int digits, boolean negative) {
        static Shape from(Datum.ScriptInstance instance) {
            Map<String, Datum> props = instance.properties();
            Datum baseDatum = props.get("pBase");
            Datum digitsDatum = props.get("pDigits");
            Datum negativeDatum = props.get("pNegative");
            if (baseDatum == null || digitsDatum == null || negativeDatum == null) {
                return null;
            }
            int base = baseDatum.toInt();
            int digits = digitsDatum.toInt();
            if (base <= 1 || digits <= 0) {
                return null;
            }
            String dataProperty = null;
            Datum.List data = null;
            for (Map.Entry<String, Datum> entry : props.entrySet()) {
                if (entry.getKey().startsWith("pData_") && entry.getValue() instanceof Datum.List list) {
                    dataProperty = entry.getKey();
                    data = list;
                    break;
                }
            }
            if (dataProperty == null) {
                return null;
            }
            return new Shape(dataProperty, data, base, digits, negativeDatum.toInt() != 0);
        }
    }
}
