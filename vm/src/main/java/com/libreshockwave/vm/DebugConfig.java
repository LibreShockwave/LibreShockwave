package com.libreshockwave.vm;

/**
 * Global debug configuration for the Lingo VM.
 * Controls debug playback logging (handler calls, error stack traces).
 * Disabled by default; can be toggled from Java or WASM JS.
 */
public final class DebugConfig {

    private static boolean debugPlaybackEnabled = false;

    private DebugConfig() {}

    public static boolean isDebugPlaybackEnabled() {
        return debugPlaybackEnabled;
    }

    public static void setDebugPlaybackEnabled(boolean enabled) {
        debugPlaybackEnabled = enabled;
    }
}
