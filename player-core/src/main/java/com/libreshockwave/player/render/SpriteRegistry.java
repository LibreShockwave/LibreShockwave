package com.libreshockwave.player.render;

import com.libreshockwave.chunks.ScoreChunk;
import com.libreshockwave.player.sprite.SpriteState;

import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Registry of runtime sprite states.
 * Tracks sprite properties that can be modified by scripts (position, visibility, etc.).
 * Supports both Score-based sprites and dynamically created/puppeted sprites.
 */
public class SpriteRegistry {

    private final Map<Integer, SpriteState> sprites = new ConcurrentHashMap<>();
    private int revision;

    /**
     * Get or create a sprite state for a channel from Score data.
     * If the sprite doesn't exist, creates it from the channel data.
     */
    public SpriteState getOrCreate(int channel, ScoreChunk.ChannelData data) {
        SpriteState state = sprites.get(channel);
        if (state == null) {
            state = new SpriteState(channel, data);
            sprites.put(channel, state);
        }
        return state;
    }

    /**
     * Get or create a dynamic sprite for a channel (no Score data required).
     * Used when scripts puppet a sprite channel or set properties on it.
     */
    public SpriteState getOrCreateDynamic(int channel) {
        SpriteState state = sprites.get(channel);
        if (state == null) {
            state = new SpriteState(channel);
            sprites.put(channel, state);
        }
        return state;
    }

    /**
     * Get a sprite state by channel, or null if not registered.
     */
    public SpriteState get(int channel) {
        return sprites.get(channel);
    }

    /**
     * Update a sprite's score-driven properties for new frames.
     */
    public void updateFromScore(int channel, ScoreChunk.ChannelData data) {
        SpriteState state = sprites.get(channel);
        if (state != null && !state.isPuppet() && !state.hasDynamicMember()) {
            state.syncFromScore(data);
        }
    }

    /**
     * Remove a sprite when it leaves the stage.
     */
    public void remove(int channel) {
        sprites.remove(channel);
    }

    /**
     * Clear all sprites (on movie stop/reset).
     */
    public void clear() {
        sprites.clear();
    }

    /**
     * Clear dynamic sprite bindings that still reference a retired member slot.
     * This prevents recycled Habbo bitmap-bin members from leaking into stale sprites.
     */
    public boolean clearDynamicMemberBindings(int castLib, int memberNum) {
        boolean changed = false;
        for (SpriteState state : sprites.values()) {
            if (!state.hasDynamicMember()) {
                continue;
            }
            if (state.getEffectiveCastLib() == castLib && state.getEffectiveCastMember() == memberNum) {
                state.clearDynamicMember();
                changed = true;
            }
        }
        if (changed) {
            bumpRevision();
        }
        return changed;
    }

    /**
     * Check if a channel has a registered sprite.
     */
    public boolean contains(int channel) {
        return sprites.containsKey(channel);
    }

    /**
     * Get all dynamic/puppeted sprites that should be rendered.
     * Includes sprites with dynamic members AND puppeted sprites (even without
     * explicit members). In Director, puppeted sprites are always rendered -
     * the window system uses them for both hit testing and visual display
     * (e.g., color swatches with bgColor set).
     */
    public List<SpriteState> getDynamicSprites() {
        return sprites.values().stream()
            .filter(s -> s.hasDynamicMember() || s.isDynamic() || s.isPuppet())
            .toList();
    }

    /**
     * Get all registered sprites.
     */
    public Map<Integer, SpriteState> getAll() {
        return sprites;
    }

    /**
     * Increment revision counter to signal that sprite state has changed.
     * Used by SoftwareRenderer cache to detect dynamic sprite changes
     * in single-frame movies where the frame number never changes.
     */
    public void bumpRevision() {
        revision++;
    }

    /**
     * Get the current sprite revision counter.
     */
    public int getRevision() {
        return revision;
    }
}
