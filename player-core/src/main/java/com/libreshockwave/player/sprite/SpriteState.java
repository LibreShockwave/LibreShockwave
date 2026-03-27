package com.libreshockwave.player.sprite;

import com.libreshockwave.chunks.ScoreChunk;
import com.libreshockwave.id.ChannelId;
import com.libreshockwave.id.InkMode;
import com.libreshockwave.vm.datum.Datum;

import java.util.ArrayList;
import java.util.List;

/**
 * Holds runtime state for a sprite on the stage.
 * Tracks position, size, and visibility that can be modified by scripts.
 * Supports both Score-based sprites and dynamically puppeted sprites.
 */
public class SpriteState {
    private final ChannelId channelId;
    private final ScoreChunk.ChannelData initialData;  // null for dynamic sprites

    private int locH;
    private int locV;
    private int locZ;
    private int width;
    private int height;
    private boolean visible = true;
    private boolean puppet = false;
    private InkMode inkMode = InkMode.COPY;
    private int blend = 100;
    private int trails = 0;
    private int stretch = 0;
    private int foreColor = 0;
    private int backColor = 0xFFFFFF;
    private boolean hasForeColor = false;
    private boolean hasBackColor = false;
    private boolean hasSizeChanged = false;
    private boolean inkExplicitlySet = false;
    private boolean blendExplicitlySet = false;
    private boolean trailsExplicitlySet = false;
    private boolean stretchExplicitlySet = false;
    private boolean scoreDefaultsApplied = false;
    private boolean flipH = false;
    private boolean flipV = false;
    private double rotation = 0.0;
    private double skew = 0.0;
    private int cursor = 0; // Director cursor: -1=arrow, 0=default, 1=ibeam, 2=crosshair, 3=crossbar, 4=wait
    private int cursorMemberNum = 0; // Encoded member number for bitmap cursor (castLib<<16 | memberNum)
    private int cursorMaskNum = 0;   // Encoded member number for cursor mask

    // Script instance list (behaviors attached dynamically via Lingo)
    private List<Datum> scriptInstanceList = new ArrayList<>();

    // Dynamic member assignment (overrides Score data when set)
    private int dynamicCastLib = -1;
    private int dynamicCastMember = -1;
    private boolean hasDynamicMember = false;

    /**
     * Create from Score data (traditional Score-based sprite).
     */
    public SpriteState(int channel, ScoreChunk.ChannelData data) {
        this.channelId = new ChannelId(channel);
        this.initialData = data;
        this.locH = data.posX();
        this.locV = data.posY();
        this.width = data.width();
        this.height = data.height();
        this.inkMode = InkMode.fromCode(data.ink());
        this.trails = data.trails();
        this.stretch = data.stretch();
        // Director stores score blend inverted in byte 21: 255 means 0%, 0 means 100%.
        if (this.inkMode == InkMode.BLEND && data.blendByte() > 0) {
            this.blend = Math.round((255 - data.blendByte()) * 100f / 255f);
        }
        this.foreColor = data.resolvedForeColor();
        this.backColor = data.resolvedBackColor();
    }

    /**
     * Create a dynamic/puppeted sprite (no Score data).
     */
    public SpriteState(int channel) {
        this.channelId = new ChannelId(channel);
        this.initialData = null;
        this.puppet = true;
    }

    public ChannelId getChannelId() { return channelId; }
    public int getChannel() { return channelId.value(); }
    public int getLocH() { return locH; }
    public int getLocV() { return locV; }
    public int getLocZ() { return locZ; }
    public int getWidth() { return width; }
    public int getHeight() { return height; }
    public boolean isVisible() { return visible; }
    public boolean isPuppet() { return puppet; }
    public InkMode getInkMode() { return inkMode; }
    public int getInk() { return inkMode.code(); }
    public int getBlend() { return blend; }
    public int getTrails() { return trails; }
    public int getStretch() { return stretch; }
    public int getForeColor() { return foreColor; }
    public int getBackColor() { return backColor; }
    public boolean hasForeColor() { return hasForeColor; }
    public boolean hasBackColor() { return hasBackColor; }

    public void setLocH(int locH) { this.locH = locH; }
    public void setLocV(int locV) { this.locV = locV; }
    public void setLocZ(int locZ) { this.locZ = locZ; }
    public void setWidth(int width) { this.width = width; this.hasSizeChanged = true; }
    public void setHeight(int height) { this.height = height; this.hasSizeChanged = true; }
    public void setVisible(boolean visible) { this.visible = visible; }
    public void setPuppet(boolean puppet) { this.puppet = puppet; }
    public void setInk(int ink) { this.inkMode = InkMode.fromCode(ink); this.inkExplicitlySet = true; }
    public void setInkMode(InkMode ink) { this.inkMode = ink; this.inkExplicitlySet = true; }
    public void setBlend(int blend) { this.blend = blend; this.blendExplicitlySet = true; }
    public void setTrails(int trails) { this.trails = trails; this.trailsExplicitlySet = true; }
    public void setStretch(int stretch) { this.stretch = stretch; this.stretchExplicitlySet = true; }
    public boolean isFlipH() { return flipH; }
    public boolean isFlipV() { return flipV; }
    public void setFlipH(boolean flipH) { this.flipH = flipH; }
    public void setFlipV(boolean flipV) { this.flipV = flipV; }
    public double getRotation() { return rotation; }
    public double getSkew() { return skew; }
    public void setRotation(double rotation) { this.rotation = rotation; }
    public void setSkew(double skew) { this.skew = skew; }
    public int getCursor() { return cursor; }
    public void setCursor(int cursor) { this.cursor = cursor; this.cursorMemberNum = 0; this.cursorMaskNum = 0; }
    public int getCursorMemberNum() { return cursorMemberNum; }
    public int getCursorMaskNum() { return cursorMaskNum; }
    public boolean hasBitmapCursor() { return cursorMemberNum != 0; }
    public void setCursorMembers(int member, int mask) {
        this.cursorMemberNum = member;
        this.cursorMaskNum = mask;
        this.cursor = 0;
    }
    public void setForeColor(int foreColor) { this.foreColor = foreColor; this.hasForeColor = true; }
    public void setBackColor(int backColor) { this.backColor = backColor; this.hasBackColor = true; }

    public List<Datum> getScriptInstanceList() { return scriptInstanceList; }
    public boolean hasScriptBehaviors() { return !scriptInstanceList.isEmpty(); }
    public void setScriptInstanceList(List<Datum> list) {
        this.scriptInstanceList = list != null ? new ArrayList<>(list) : new ArrayList<>();
    }

    /**
     * Position snapshot record — avoids int[] arrays which TeaVM 0.13 WASM
     * code-gen reorders during element assignment.
     */
    public record PositionSnapshot(int locH, int locV, int locZ, int width, int height) {}

    /**
     * Atomically capture all mutable position fields to prevent torn reads
     * when the VM thread updates position mid-render.
     */
    public PositionSnapshot snapshotPosition() {
        return new PositionSnapshot(locH, locV, locZ, width, height);
    }

    /**
     * Set a dynamic cast member (overrides Score data).
     */
    public void setDynamicMember(int castLib, int member) {
        this.dynamicCastLib = castLib;
        this.dynamicCastMember = member;
        this.hasDynamicMember = true;
    }

    /**
     * Clear a dynamic cast member override and fall back to Score data.
     */
    public void clearDynamicMember() {
        this.dynamicCastLib = -1;
        this.dynamicCastMember = -1;
        this.hasDynamicMember = false;
    }

    public void resetReleasedSpriteTransforms() {
        this.flipH = false;
        this.flipV = false;
        this.rotation = 0.0;
        this.skew = 0.0;
    }

    /**
     * Get the effective cast library number (dynamic or from Score).
     */
    public int getEffectiveCastLib() {
        if (hasDynamicMember) {
            return dynamicCastLib;
        }
        return initialData != null ? initialData.castLib() : 0;
    }

    /**
     * Get the effective cast member number (dynamic or from Score).
     */
    public int getEffectiveCastMember() {
        if (hasDynamicMember) {
            return dynamicCastMember;
        }
        return initialData != null ? initialData.castMember() : 0;
    }

    public boolean hasDynamicMember() { return hasDynamicMember; }
    public boolean isDynamic() { return initialData == null; }
    public boolean hasSizeChanged() { return hasSizeChanged; }

    /**
     * Apply intrinsic dimensions from a member (e.g., bitmap width/height).
     * Only applies if the script hasn't explicitly set width/height.
     */
    public void applyIntrinsicSize(int w, int h) {
        if (!hasSizeChanged && w > 0 && h > 0) {
            this.width = w;
            this.height = h;
        }
    }

    /**
     * Apply Score-data properties as defaults for a dynamically-created sprite.
     * In Director, when a behavior puppets a sprite, the Score's ink, colors,
     * and blend serve as base values; the behavior overrides only what it sets.
     * Called once when score data is first encountered for a dynamic state.
     */
    public void applyScoreDefaults(ScoreChunk.ChannelData data) {
        if (scoreDefaultsApplied || data == null) return;
        scoreDefaultsApplied = true;
        if (!inkExplicitlySet) {
            this.inkMode = InkMode.fromCode(data.ink());
        }
        if (!blendExplicitlySet && this.inkMode == InkMode.BLEND && data.blendByte() > 0) {
            this.blend = Math.round((255 - data.blendByte()) * 100f / 255f);
        }
        if (!trailsExplicitlySet) {
            this.trails = data.trails();
        }
        if (!stretchExplicitlySet) {
            this.stretch = data.stretch();
        }
        if (!hasForeColor) {
            this.foreColor = data.resolvedForeColor();
            this.hasForeColor = true;
        }
        if (!hasBackColor) {
            this.backColor = data.resolvedBackColor();
            this.hasBackColor = true;
        }
    }

    /**
     * Synchronize score-driven properties for a score sprite, preserving Lingo overrides.
     */
    public void syncFromScore(ScoreChunk.ChannelData data) {
        if (data == null) return;
        this.locH = data.posX();
        this.locV = data.posY();
        if (!hasSizeChanged && data.width() > 0 && data.height() > 0) {
            this.width = data.width();
            this.height = data.height();
        }
        if (!inkExplicitlySet) {
            this.inkMode = InkMode.fromCode(data.ink());
        }
        if (!blendExplicitlySet && InkMode.fromCode(data.ink()) == InkMode.BLEND && data.blendByte() > 0) {
            this.blend = Math.round((255 - data.blendByte()) * 100f / 255f);
        }
        if (!trailsExplicitlySet) {
            this.trails = data.trails();
        }
        if (!stretchExplicitlySet) {
            this.stretch = data.stretch();
        }
        if (!hasForeColor) {
            this.foreColor = data.resolvedForeColor();
        }
        if (!hasBackColor) {
            this.backColor = data.resolvedBackColor();
        }
    }

    public ScoreChunk.ChannelData getInitialData() { return initialData; }
}
