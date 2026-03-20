package com.libreshockwave.util;

/**
 * Minimal callback interface used instead of java.util.function.Supplier
 * for TeaVM-compatible lazy/value access.
 */
public interface ValueProvider<T> {
    T get();
}
