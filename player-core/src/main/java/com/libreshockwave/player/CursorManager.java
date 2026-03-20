package com.libreshockwave.player;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.player.cast.CastLibManager;
import com.libreshockwave.player.cast.CastMember;
import com.libreshockwave.player.event.EventDispatcher;
import com.libreshockwave.player.input.HitTester;
import com.libreshockwave.player.input.InputState;
import com.libreshockwave.player.render.pipeline.StageRenderer;
import com.libreshockwave.player.sprite.SpriteState;

import java.util.function.IntSupplier;
import java.util.function.Supplier;

/**
 * Manages cursor display: detects cursor type based on mouse position and sprite state,
 * and resolves custom bitmap cursors with mask support.
 * Extracted from Player to separate cursor concerns.
 */
public class CursorManager {

    private final StageRenderer stageRenderer;
    private final InputState inputState;
    private final CastLibManager castLibManager;
    private final BitmapResolver bitmapResolver;
    private final IntSupplier currentFrameSupplier;
    private final Supplier<EventDispatcher> eventDispatcherSupplier;

    public CursorManager(StageRenderer stageRenderer, InputState inputState,
                         CastLibManager castLibManager, BitmapResolver bitmapResolver,
                         IntSupplier currentFrameSupplier,
                         Supplier<EventDispatcher> eventDispatcherSupplier) {
        this.stageRenderer = stageRenderer;
        this.inputState = inputState;
        this.castLibManager = castLibManager;
        this.bitmapResolver = bitmapResolver;
        this.currentFrameSupplier = currentFrameSupplier;
        this.eventDispatcherSupplier = eventDispatcherSupplier;
    }

    /**
     * Get the cursor type for the current mouse position.
     * Returns Director cursor codes: -1=arrow, 0=default, 1=ibeam, 2=crosshair, 3=crossbar, 4=wait
     * Returns 5 for custom bitmap cursor (call getCursorBitmap() to get the image).
     * Returns 6 for pointer/hand (button members).
     */
    public int getCursorAtMouse() {
        int mouseH = inputState.getMouseH();
        int mouseV = inputState.getMouseV();
        int hitChannel = HitTester.hitTest(stageRenderer, currentFrameSupplier.getAsInt(), mouseH, mouseV);
        if (hitChannel > 0) {
            SpriteState sprite = stageRenderer.getSpriteRegistry().get(hitChannel);
            if (sprite != null) {
                // Check for bitmap cursor first
                if (sprite.hasBitmapCursor()) {
                    return 5; // custom bitmap cursor
                }
                // Auto-detect editable text fields and buttons — Director shows ibeam
                // for editable text/field members and pointer for buttons, regardless
                // of sprite cursor. The Event Broker Behavior sets cursor=-1 (arrow) on
                // all window sprites, but Director still overrides for these types.
                int castLibNum = sprite.getEffectiveCastLib();
                int memberNum = sprite.getEffectiveCastMember();
                if (memberNum > 0) {
                    CastMember member = castLibManager.getDynamicMember(castLibNum, memberNum);
                    if (member != null) {
                        if (member.isEditable() && member.getMemberType() == MemberType.TEXT) {
                            return 1; // ibeam for editable text
                        }
                        if (member.getMemberType() == MemberType.BUTTON) {
                            return 6; // pointer/hand for buttons
                        }
                    }
                }
                int spriteCursor = sprite.getCursor();
                if (spriteCursor != 0) {
                    return spriteCursor;
                }
                EventDispatcher dispatcher = eventDispatcherSupplier != null ? eventDispatcherSupplier.get() : null;
                if (dispatcher != null && dispatcher.isSpriteMouseInteractive(hitChannel)) {
                    return 6; // pointer/hand for generic interactive sprites
                }
            }
        }
        return -1; // default arrow when not over any sprite
    }

    /**
     * Get the current custom cursor bitmap for rendering.
     * Returns null if no bitmap cursor is active.
     * The bitmap's regPoint defines the hotspot offset.
     */
    public Bitmap getCursorBitmap() {
        int mouseH = inputState.getMouseH();
        int mouseV = inputState.getMouseV();
        int hitChannel = HitTester.hitTest(stageRenderer, currentFrameSupplier.getAsInt(), mouseH, mouseV);
        if (hitChannel <= 0) return null;

        SpriteState sprite = stageRenderer.getSpriteRegistry().get(hitChannel);
        if (sprite == null || !sprite.hasBitmapCursor()) return null;

        int encodedMember = sprite.getCursorMemberNum();
        int castLibNum = (encodedMember >> 16) & 0xFFFF;
        int memberNum = encodedMember & 0xFFFF;
        if (castLibNum == 0) castLibNum = 1; // default cast lib

        // Try to find and decode the cursor member bitmap
        CastMemberChunk chunk = castLibManager.getCastMember(castLibNum, memberNum);
        if (chunk == null) return null;

        Bitmap cursorBmp = bitmapResolver.decodeBitmap(chunk).orElse(null);
        if (cursorBmp == null) return null;

        // Decode the mask bitmap if present.
        // Director cursors use a separate mask bitmap to define transparency:
        //   mask white/background (palette index 0) = transparent
        //   mask non-white = opaque (show cursor pixel)
        // Without a mask, the cursor bitmap is fully opaque.
        int encodedMask = sprite.getCursorMaskNum();
        if (encodedMask != 0) {
            int maskCastLib = (encodedMask >> 16) & 0xFFFF;
            int maskMemberNum = encodedMask & 0xFFFF;
            if (maskCastLib == 0) maskCastLib = 1;

            CastMemberChunk maskChunk = castLibManager.getCastMember(maskCastLib, maskMemberNum);
            if (maskChunk != null) {
                Bitmap maskBmp = bitmapResolver.decodeBitmap(maskChunk).orElse(null);
                if (maskBmp != null) {
                    cursorBmp = applyCursorMask(cursorBmp, maskBmp);
                }
            }
        }

        return cursorBmp;
    }

    /**
     * Apply a mask bitmap to a cursor bitmap, producing a 32-bit ARGB result.
     * Mask pixels that are white (0xFFFFFF) become transparent in the output.
     * Mask pixels that are non-white make the corresponding cursor pixel opaque.
     */
    private Bitmap applyCursorMask(Bitmap cursor, Bitmap mask) {
        int w = cursor.getWidth();
        int h = cursor.getHeight();
        int[] cursorPixels = cursor.getPixels();
        int[] maskPixels = mask.getPixels();
        int[] result = new int[w * h];

        int mw = mask.getWidth();
        int mh = mask.getHeight();

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int ci = y * w + x;
                int cursorRgb = cursorPixels[ci] & 0xFFFFFF;

                // Check mask bounds — out-of-bounds = transparent
                if (x < mw && y < mh) {
                    int mi = y * mw + x;
                    int maskRgb = maskPixels[mi] & 0xFFFFFF;
                    // Mask white (0xFFFFFF) = transparent, non-white = opaque
                    if (maskRgb == 0xFFFFFF) {
                        result[ci] = 0x00000000;
                    } else {
                        result[ci] = 0xFF000000 | cursorRgb;
                    }
                } else {
                    result[ci] = 0x00000000;
                }
            }
        }

        return new Bitmap(w, h, 32, result);
    }

    /**
     * Get the cursor bitmap's registration point (hotspot).
     * Returns [regX, regY] or null if no bitmap cursor is active.
     */
    public int[] getCursorRegPoint() {
        int mouseH = inputState.getMouseH();
        int mouseV = inputState.getMouseV();
        int hitChannel = HitTester.hitTest(stageRenderer, currentFrameSupplier.getAsInt(), mouseH, mouseV);
        if (hitChannel <= 0) return null;

        SpriteState sprite = stageRenderer.getSpriteRegistry().get(hitChannel);
        if (sprite == null || !sprite.hasBitmapCursor()) return null;

        int encodedMember = sprite.getCursorMemberNum();
        int castLibNum = (encodedMember >> 16) & 0xFFFF;
        int memberNum = encodedMember & 0xFFFF;
        if (castLibNum == 0) castLibNum = 1;

        CastMemberChunk chunk = castLibManager.getCastMember(castLibNum, memberNum);
        if (chunk == null) return new int[]{0, 0};

        return new int[]{chunk.regPointX(), chunk.regPointY()};
    }
}
