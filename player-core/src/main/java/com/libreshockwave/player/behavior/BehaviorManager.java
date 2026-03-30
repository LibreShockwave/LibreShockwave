package com.libreshockwave.player.behavior;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.chunks.ScriptNamesChunk;
import com.libreshockwave.player.cast.CastLib;
import com.libreshockwave.player.cast.CastLibManager;
import com.libreshockwave.player.score.ScoreBehaviorRef;

import java.util.*;

/**
 * Manages behavior instances for sprites and frames.
 * Handles creation, lookup, and lifecycle of behavior instances.
 */
public class BehaviorManager {

    private final DirectorFile file;
    private CastLibManager castLibManager;

    // Behavior instances by ID
    private final Map<Integer, BehaviorInstance> instancesById;

    // Behavior instances by sprite channel
    private final Map<Integer, List<BehaviorInstance>> instancesByChannel;

    // Current frame script instance (cached)
    private BehaviorInstance frameScriptInstance;
    private int frameScriptFrame = -1;

    // Debug logging
    private boolean debugEnabled = false;

    public BehaviorManager(DirectorFile file) {
        this.file = file;
        this.instancesById = new HashMap<>();
        this.instancesByChannel = new HashMap<>();
    }

    public void setCastLibManager(CastLibManager castLibManager) {
        this.castLibManager = castLibManager;
    }

    public void setDebugEnabled(boolean enabled) {
        this.debugEnabled = enabled;
    }

    /**
     * Create a behavior instance for a sprite channel.
     * @param behaviorRef The behavior reference from the score
     * @param channel The sprite channel (0 for frame, 1+ for sprites)
     * @return The created behavior instance, or null if script not found
     */
    public BehaviorInstance createInstance(ScoreBehaviorRef behaviorRef, int channel) {
        if (file == null || behaviorRef == null) return null;

        // Find the script for this behavior
        ScriptChunk script = findScript(behaviorRef.castLib(), behaviorRef.castMember());
        if (script == null) {
            if (debugEnabled) {
                System.err.println("[BehaviorManager] Script not found for " + behaviorRef);
            }
            return null;
        }

        BehaviorInstance instance = new BehaviorInstance(script, behaviorRef, channel);

        // Apply parameters if any
        applyParameters(instance, behaviorRef);

        // Register instance
        instancesById.put(instance.getId(), instance);
        instancesByChannel.computeIfAbsent(channel, k -> new ArrayList<>()).add(instance);

        if (debugEnabled) {
            System.out.println("[BehaviorManager] Created " + instance);
        }

        return instance;
    }

    /**
     * Create or get the frame script instance for the given frame.
     */
    public BehaviorInstance getOrCreateFrameScript(ScoreBehaviorRef behaviorRef, int frame) {
        // Return cached if same frame
        if (frameScriptInstance != null && frameScriptFrame == frame) {
            return frameScriptInstance;
        }

        // Clear old frame script
        if (frameScriptInstance != null) {
            removeInstance(frameScriptInstance);
        }

        // Create new frame script instance
        frameScriptInstance = createInstance(behaviorRef, 0);
        frameScriptFrame = frame;

        return frameScriptInstance;
    }

    /**
     * Get the current frame script instance.
     */
    public BehaviorInstance getFrameScriptInstance() {
        return frameScriptInstance;
    }

    /**
     * Clear the frame script instance (called on frame exit).
     */
    public void clearFrameScript() {
        if (frameScriptInstance != null) {
            removeInstance(frameScriptInstance);
            frameScriptInstance = null;
            frameScriptFrame = -1;
        }
    }

    /**
     * Get all behavior instances for a sprite channel.
     */
    public List<BehaviorInstance> getInstancesForChannel(int channel) {
        return instancesByChannel.getOrDefault(channel, List.of());
    }

    /**
     * Get a behavior instance by ID.
     */
    public BehaviorInstance getInstance(int id) {
        return instancesById.get(id);
    }

    /**
     * Remove a behavior instance.
     */
    public void removeInstance(BehaviorInstance instance) {
        if (instance == null) return;

        instancesById.remove(instance.getId());

        List<BehaviorInstance> channelInstances = instancesByChannel.get(instance.getSpriteNum());
        if (channelInstances != null) {
            channelInstances.remove(instance);
        }

        if (debugEnabled) {
            System.out.println("[BehaviorManager] Removed " + instance);
        }
    }

    /**
     * Remove all behavior instances for a channel.
     */
    public void removeInstancesForChannel(int channel) {
        List<BehaviorInstance> instances = instancesByChannel.remove(channel);
        if (instances != null) {
            for (BehaviorInstance instance : instances) {
                instancesById.remove(instance.getId());
            }
        }
    }

    /**
     * Clear all behavior instances.
     */
    public void clear() {
        instancesById.clear();
        instancesByChannel.clear();
        frameScriptInstance = null;
        frameScriptFrame = -1;
    }

    /**
     * Get all active behavior instances (for event dispatch).
     */
    public List<BehaviorInstance> getAllInstances() {
        return new ArrayList<>(instancesById.values());
    }

    /**
     * Get all sprite behavior instances (excluding frame behaviors).
     */
    public List<BehaviorInstance> getSpriteInstances() {
        List<BehaviorInstance> result = new ArrayList<>();
        List<Integer> channels = new ArrayList<>(instancesByChannel.keySet());
        Collections.sort(channels);
        for (Integer channel : channels) {
            if (channel == null || channel <= 0) {
                continue;
            }
            List<BehaviorInstance> instances = instancesByChannel.get(channel);
            if (instances == null || instances.isEmpty()) {
                continue;
            }
            result.addAll(instances);
        }
        return result;
    }

    // Helper methods

    private ScriptChunk findScript(int castLibNum, int castMember) {
        // Try main DCR first
        CastMemberChunk member = file.getCastMemberByNumber(castLibNum, castMember);
        if (member != null && member.isScript()) {
            int scriptId = member.scriptId();
            if (debugEnabled) {
                System.out.println("[BehaviorManager] Looking for script id=" + scriptId + " (member name: " + member.name() + ")");
            }
            ScriptChunk script = file.getScriptByContextId(scriptId);
            if (script != null) {
                if (debugEnabled) {
                    System.out.println("[BehaviorManager] Found script \"" + member.name() + "\" #" + script.id() + " type=" + script.getScriptType());
                }
                return script;
            }
        }

        // Try external casts via CastLibManager
        if (castLibManager != null) {
            CastLib castLib = castLibManager.getCastLibs().get(castLibNum);
            if (castLib != null && castLib.isLoaded()) {
                ScriptChunk script = castLib.getScript(castMember);
                if (script != null) {
                    if (debugEnabled) {
                        System.out.println("[BehaviorManager] Found script in external cast lib " + castLibNum + " member=" + castMember);
                    }
                    return script;
                }
            }
        }

        if (debugEnabled) {
            System.err.println("[BehaviorManager] No script found for castLib=" + castLibNum + " member=" + castMember);
        }
        return null;
    }

    private void applyParameters(BehaviorInstance instance, ScoreBehaviorRef behaviorRef) {
        if (!behaviorRef.hasParameters()) return;

        // Apply saved parameters to the instance properties
        // Parameters are typically [#prop1: value1, #prop2: value2]
        List<com.libreshockwave.vm.datum.Datum> params = behaviorRef.parameters();
        for (com.libreshockwave.vm.datum.Datum param : params) {
            if (param instanceof com.libreshockwave.vm.datum.Datum.PropList propList) {
                for (com.libreshockwave.vm.datum.Datum.PropEntry entry : propList.entries()) {
                    instance.setProperty(entry.key(), entry.value());
                }
            }
        }
    }
}
