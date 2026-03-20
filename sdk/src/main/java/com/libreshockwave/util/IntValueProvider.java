package com.libreshockwave.util;

/**
 * Minimal callback interface used instead of java.util.function.IntSupplier
 * for TeaVM-compatible integer access.
 */
public interface IntValueProvider {
    int getAsInt();
}
