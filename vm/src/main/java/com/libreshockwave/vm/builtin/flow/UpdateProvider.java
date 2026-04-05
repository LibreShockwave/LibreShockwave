package com.libreshockwave.vm.builtin.flow;

import com.libreshockwave.vm.datum.Datum;

/**
 * Interface for registering objects that receive per-frame 'update' messages.
 * Runtime service used by engine code that wants direct update registration.
 */
public interface UpdateProvider {

    /**
     * Register an object to receive 'update' messages.
     * @param target The object to register (typically a ScriptInstance)
     */
    void receiveUpdate(Datum target);

    /**
     * Unregister an object from receiving 'update' messages.
     * @param target The object to unregister
     */
    void removeUpdate(Datum target);

    // Thread-local provider for VM access
    ThreadLocal<UpdateProvider> CURRENT = new ThreadLocal<>();

    static void setProvider(UpdateProvider provider) {
        CURRENT.set(provider);
    }

    static void clearProvider() {
        CURRENT.remove();
    }

    static UpdateProvider getProvider() {
        return CURRENT.get();
    }
}
