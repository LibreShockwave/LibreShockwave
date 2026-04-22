package com.libreshockwave.vm.builtin.movie;

import com.libreshockwave.vm.datum.Datum;

/**
 * Interface for movie property access.
 * Implemented by Player in player-core to provide access to movie-level properties.
 *
 * Movie properties are Lingo's "the" expressions like:
 * - the frame
 * - the moviePath
 * - the stageRight
 * - the exitLock
 * etc.
 */
public interface MoviePropertyProvider {

    /**
     * Get a movie property value.
     * @param propName The property name (e.g., "frame", "moviePath", "stageRight")
     * @return The property value, or VOID if not found
     */
    Datum getMovieProp(String propName);

    /**
     * Set a movie property value.
     * @param propName The property name
     * @param value The value to set
     * @return true if the property was set, false if read-only or unknown
     */
    boolean setMovieProp(String propName, Datum value);

    /**
     * Get the current item delimiter character.
     * Uses a cached char to avoid Map lookup + String conversion on every call.
     * (Critical for WASM: the dump() handler calls this ~18,000 times.)
     */
    default char getItemDelimiter() {
        return ItemDelimiterCache._char;
    }

    /**
     * Set the item delimiter character.
     * Updates both the movie property and the fast cache.
     */
    default void setItemDelimiter(char delimiter) {
        ItemDelimiterCache._char = delimiter;
        setMovieProp("itemDelimiter", Datum.of(String.valueOf(delimiter)));
    }

    /** Static cache for itemDelimiter — avoids Map lookup + Datum.toStr() per call. */
    final class ItemDelimiterCache {
        public static char _char = ',';
    }

    /**
     * Get a stage object property (e.g., (the stage).rect, (the stage).title).
     * @param propName The property name
     * @return The property value, or VOID if not found
     */
    default Datum getStageProp(String propName) {
        return getMovieProp(propName);
    }

    /**
     * Set a stage object property (e.g., (the stage).title = "...").
     * @param propName The property name
     * @param value The value to set
     * @return true if the property was set
     */
    default boolean setStageProp(String propName, Datum value) {
        return setMovieProp(propName, value);
    }

    /**
     * Go to a specific frame number.
     * @param frame The 1-based frame number to go to
     */
    default void goToFrame(int frame) {
        // Default: no-op
    }

    /**
     * Go to a labeled frame.
     * @param label The frame label to go to
     */
    default void goToLabel(String label) {
        // Default: no-op
    }

    default void gotoNetPage(String url, String target) {
    }

    /**
     * Retrieve and play a network movie, returning a host-defined request id.
     */
    default int gotoNetMovie(String url) {
        return -1;
    }

    // Thread-local provider for VM access
    ThreadLocal<MoviePropertyProvider> CURRENT = new ThreadLocal<>();

    static void setProvider(MoviePropertyProvider provider) {
        CURRENT.set(provider);
    }

    static void clearProvider() {
        CURRENT.remove();
    }

    static MoviePropertyProvider getProvider() {
        return CURRENT.get();
    }
}
