package com.libreshockwave.vm.util;

import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.datum.Datum;

import java.util.List;

/**
 * Utility class for walking ancestor chains in script instances.
 * Consolidates the duplicated ancestor chain traversal logic from
 * CallOpcodes and PropertyOpcodes.
 */
public final class AncestorChainWalker {

    public static final int MAX_ANCESTOR_DEPTH = 100;

    private AncestorChainWalker() {}

    /**
     * Find and invoke a handler on a script instance, walking the ancestor chain.
     * The handler is invoked with the original instance as the 'me' receiver,
     * even if the handler is found on an ancestor.
     *
     * @param vm The Lingo VM
     * @param instance The script instance (also used as 'me' receiver)
     * @param handlerName The handler to find
     * @param args Arguments to pass to the handler
     * @return true if the handler was found and invoked, false if not found
     */
    public static boolean invokeHandler(LingoVM vm, Datum.ScriptInstance instance,
                                         String handlerName, List<Datum> args) {
        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) return false;

        Datum.ScriptInstance current = instance;
        for (int i = 0; i < MAX_ANCESTOR_DEPTH; i++) {
            Datum scriptRefDatum = current.properties().get(Datum.PROP_SCRIPT_REF);
            CastLibProvider.HandlerLocation location;

            if (scriptRefDatum instanceof Datum.ScriptRef sr) {
                location = provider.findHandlerInScript(sr.castLibNum(), sr.memberNum(), handlerName);
            } else {
                location = provider.findHandlerInScript(current.scriptId(), handlerName);
            }

            if (location != null && location.script() instanceof ScriptChunk script
                    && location.handler() instanceof ScriptChunk.Handler handler) {
                vm.executeHandler(script, handler, args, instance);
                return true;
            }

            // Walk to ancestor
            Datum ancestor = current.properties().get(Datum.PROP_ANCESTOR);
            if (ancestor instanceof Datum.ScriptInstance ancestorInstance) {
                current = ancestorInstance;
            } else {
                break;
            }
        }
        return false;
    }

    /**
     * Check whether a handler exists on a script instance or any ancestor.
     * Uses the same scriptRef/ancestor lookup path as invokeHandler().
     */
    public static boolean hasHandler(Datum.ScriptInstance instance, String handlerName) {
        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) return false;

        Datum.ScriptInstance current = instance;
        for (int i = 0; i < MAX_ANCESTOR_DEPTH; i++) {
            Datum scriptRefDatum = current.properties().get(Datum.PROP_SCRIPT_REF);
            CastLibProvider.HandlerLocation location;

            if (scriptRefDatum instanceof Datum.ScriptRef sr) {
                location = provider.findHandlerInScript(sr.castLibNum(), sr.memberNum(), handlerName);
            } else {
                location = provider.findHandlerInScript(current.scriptId(), handlerName);
            }

            if (location != null && location.script() instanceof ScriptChunk script
                    && location.handler() instanceof ScriptChunk.Handler) {
                return true;
            }

            // Walk to ancestor
            Datum ancestor = current.properties().get(Datum.PROP_ANCESTOR);
            if (ancestor instanceof Datum.ScriptInstance ancestorInstance) {
                current = ancestorInstance;
            } else {
                break;
            }
        }
        return false;
    }

    /**
     * Get a property from a script instance, walking the ancestor chain if not found.
     * @param instance The script instance to start searching from
     * @param propName The property name to look for
     * @return The property value, or VOID if not found
     */
    public static Datum getProperty(Datum.ScriptInstance instance, String propName) {
        Datum.ScriptInstance current = instance;
        for (int i = 0; i < MAX_ANCESTOR_DEPTH; i++) {
            // Exact match first (fast path)
            if (current.properties().containsKey(propName)) {
                return current.properties().get(propName);
            }
            // Case-insensitive fallback (Director is case-insensitive)
            Datum ciResult = getCaseInsensitive(current, propName);
            if (ciResult != null) return ciResult;

            // Try ancestor
            Datum ancestor = current.properties().get(Datum.PROP_ANCESTOR);
            if (ancestor instanceof Datum.ScriptInstance ancestorInstance) {
                current = ancestorInstance;
            } else {
                break;
            }
        }
        return Datum.VOID;
    }

    private static Datum getCaseInsensitive(Datum.ScriptInstance instance, String propName) {
        for (var entry : instance.properties().entrySet()) {
            if (entry.getKey().equalsIgnoreCase(propName)) {
                return entry.getValue();
            }
        }
        return null;
    }

    /**
     * Check if a property exists in the script instance or its ancestor chain.
     * @param instance The script instance to start searching from
     * @param propName The property name to look for
     * @return true if the property exists somewhere in the chain
     */
    public static boolean hasProperty(Datum.ScriptInstance instance, String propName) {
        Datum.ScriptInstance current = instance;
        for (int i = 0; i < MAX_ANCESTOR_DEPTH; i++) {
            if (current.properties().containsKey(propName)) {
                return true;
            }
            // Case-insensitive fallback (Director is case-insensitive)
            if (getCaseInsensitive(current, propName) != null) {
                return true;
            }

            // Try ancestor
            Datum ancestor = current.properties().get(Datum.PROP_ANCESTOR);
            if (ancestor instanceof Datum.ScriptInstance ancestorInstance) {
                current = ancestorInstance;
            } else {
                break;
            }
        }
        return false;
    }

    /**
     * Find the script instance in the ancestor chain that owns a property.
     * @param instance The script instance to start searching from
     * @param propName The property name to look for
     * @return The instance that owns the property, or null if not found
     */
    public static Datum.ScriptInstance findOwner(Datum.ScriptInstance instance, String propName) {
        Datum.ScriptInstance current = instance;
        for (int i = 0; i < MAX_ANCESTOR_DEPTH; i++) {
            if (current.properties().containsKey(propName)) {
                return current;
            }
            // Case-insensitive fallback (Director is case-insensitive)
            if (getCaseInsensitive(current, propName) != null) {
                return current;
            }

            // Try ancestor
            Datum ancestor = current.properties().get(Datum.PROP_ANCESTOR);
            if (ancestor instanceof Datum.ScriptInstance ancestorInstance) {
                current = ancestorInstance;
            } else {
                break;
            }
        }
        return null;
    }

    /**
     * Get the ancestor at a specific depth.
     * @param instance The script instance to start from
     * @param depth The depth to traverse (1 = immediate ancestor)
     * @return The ancestor at the specified depth, or null if not found
     */
    public static Datum.ScriptInstance getAncestorAtDepth(Datum.ScriptInstance instance, int depth) {
        if (depth < 1) {
            return null;
        }

        Datum.ScriptInstance current = instance;
        for (int i = 0; i < depth && i < MAX_ANCESTOR_DEPTH; i++) {
            Datum ancestor = current.properties().get(Datum.PROP_ANCESTOR);
            if (ancestor instanceof Datum.ScriptInstance ancestorInstance) {
                current = ancestorInstance;
            } else {
                return null;
            }
        }
        return current;
    }

    /**
     * Set a property on a script instance, walking the ancestor chain to find the owner.
     * If the property exists on the current instance or an ancestor, sets it there.
     * If not found anywhere, adds to the current instance.
     * @param instance The script instance to start searching from
     * @param propName The property name to set
     * @param value The value to set
     */
    public static void setProperty(Datum.ScriptInstance instance, String propName, Datum value) {
        // Match dirplayer-rs: ancestor can only be set to a ScriptInstance, not VOID.
        // In Director, setting ancestor to VOID is a no-op (type validation fails).
        if (propName.equals(Datum.PROP_ANCESTOR) && !(value instanceof Datum.ScriptInstance)) {
            return; // Skip — ancestor must be a ScriptInstance
        }

        Datum.ScriptInstance owner = findOwner(instance, propName);
        if (owner != null) {
            // Use the actual key in the map (may differ in case from propName)
            String actualKey = findActualKey(owner, propName);
            owner.properties().put(actualKey, value);
        } else {
            // Property not declared anywhere - add to current instance
            instance.properties().put(propName, value);
        }
    }

    /**
     * Find the actual key in the properties map that matches propName (case-insensitive).
     */
    private static String findActualKey(Datum.ScriptInstance instance, String propName) {
        if (instance.properties().containsKey(propName)) {
            return propName;
        }
        for (String key : instance.properties().keySet()) {
            if (key.equalsIgnoreCase(propName)) {
                return key;
            }
        }
        return propName;
    }

    /**
     * Count the depth of the ancestor chain.
     * @param instance The script instance to start from
     * @return The number of ancestors (0 if no ancestors)
     */
    public static int getAncestorDepth(Datum.ScriptInstance instance) {
        int depth = 0;
        Datum.ScriptInstance current = instance;

        for (int i = 0; i < MAX_ANCESTOR_DEPTH; i++) {
            Datum ancestor = current.properties().get(Datum.PROP_ANCESTOR);
            if (ancestor instanceof Datum.ScriptInstance ancestorInstance) {
                depth++;
                current = ancestorInstance;
            } else {
                break;
            }
        }
        return depth;
    }
}
