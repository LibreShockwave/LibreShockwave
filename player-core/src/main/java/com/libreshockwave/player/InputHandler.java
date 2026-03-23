package com.libreshockwave.player;

import com.libreshockwave.cast.MemberType;
import com.libreshockwave.player.cast.CastLibManager;
import com.libreshockwave.player.cast.CastMember;
import com.libreshockwave.player.event.EventDispatcher;
import com.libreshockwave.player.input.HitTester;
import com.libreshockwave.player.input.InputEvent;
import com.libreshockwave.player.input.InputState;
import com.libreshockwave.player.render.output.TextRenderer;
import com.libreshockwave.player.render.pipeline.StageRenderer;
import com.libreshockwave.player.sprite.SpriteState;
import com.libreshockwave.util.IntValueProvider;
import com.libreshockwave.util.ValueProvider;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Handles all input processing: mouse/keyboard event dispatch, rollover tracking,
 * text editing, caret/selection management, and clipboard operations.
 * Extracted from Player to separate input concerns.
 */
public class InputHandler {

    private final InputState inputState;
    private final StageRenderer stageRenderer;
    private final CastLibManager castLibManager;
    private final IntValueProvider currentFrameSupplier;
    private final ValueProvider<EventDispatcher> eventDispatcherSupplier;

    // Rollover tracking for mouseEnter/mouseLeave/mouseWithin events
    private int previousRolloverSprite = 0;

    public InputHandler(InputState inputState, StageRenderer stageRenderer,
                        CastLibManager castLibManager, IntValueProvider currentFrameSupplier,
                        ValueProvider<EventDispatcher> eventDispatcherSupplier) {
        this.inputState = inputState;
        this.stageRenderer = stageRenderer;
        this.castLibManager = castLibManager;
        this.currentFrameSupplier = currentFrameSupplier;
        this.eventDispatcherSupplier = eventDispatcherSupplier;
    }

    // --- Public input event entry points (called by UI layer) ---

    /**
     * Update the mouse position (stage coordinates).
     * Called by the UI layer on mouse move.
     */
    public void onMouseMove(int stageX, int stageY) {
        inputState.setMousePosition(stageX, stageY);
        // Update rollover sprite
        int hit = hitTest(stageX, stageY);
        inputState.setRolloverSprite(hit);

        // Drag-selection: extend selection while mouse is down over focused editable field
        if (inputState.isMouseDown()) {
            int focusChannel = inputState.getKeyboardFocusSprite();
            if (focusChannel > 0) {
                SpriteState sprite = stageRenderer.getSpriteRegistry().get(focusChannel);
                if (sprite != null) {
                    int memberNum = sprite.getEffectiveCastMember();
                    if (memberNum > 0) {
                        CastMember member = castLibManager.getDynamicMember(
                                sprite.getEffectiveCastLib(), memberNum);
                        if (member != null && member.isEditable()) {
                            int spriteX = sprite.getLocH() - member.getRegPointX();
                            int spriteY = sprite.getLocV() - member.getRegPointY();
                            int charPos = member.locToCharPos(stageX - spriteX, stageY - spriteY, sprite.getWidth());
                            inputState.setSelEnd(charPos);
                            inputState.resetCaretBlink();
                        }
                    }
                }
            }
        }
    }

    /**
     * Handle a mouse button press. Queues a MOUSE_DOWN event for dispatch during tick.
     */
    public void onMouseDown(int stageX, int stageY, boolean rightButton) {
        inputState.setMousePosition(stageX, stageY);
        if (rightButton) {
            inputState.setRightMouseDown(true);
            inputState.queueEvent(InputEvent.rightMouseDown(stageX, stageY));
        } else {
            inputState.setMouseDown(true);
            int hit = hitTest(stageX, stageY);
            inputState.setClickOnSprite(hit);
            inputState.setClickLoc(stageX, stageY);
            inputState.queueEvent(InputEvent.mouseDown(stageX, stageY));
        }
    }

    /**
     * Handle a mouse button release. Queues a MOUSE_UP event for dispatch during tick.
     */
    public void onMouseUp(int stageX, int stageY, boolean rightButton) {
        inputState.setMousePosition(stageX, stageY);
        if (rightButton) {
            inputState.setRightMouseDown(false);
            inputState.queueEvent(InputEvent.rightMouseUp(stageX, stageY));
        } else {
            inputState.setMouseDown(false);
            inputState.queueEvent(InputEvent.mouseUp(stageX, stageY));
        }
    }

    /**
     * Handle a key press. Queues a KEY_DOWN event for dispatch during tick.
     */
    public void onKeyDown(int directorKeyCode, String keyChar, boolean shift, boolean ctrl, boolean alt) {
        inputState.setLastKey(keyChar);
        inputState.setLastKeyCode(directorKeyCode);
        inputState.setShiftDown(shift);
        inputState.setControlDown(ctrl);
        inputState.setAltDown(alt);
        inputState.queueEvent(InputEvent.keyDown(directorKeyCode, keyChar));
    }

    /**
     * Handle a key release. Queues a KEY_UP event for dispatch during tick.
     */
    public void onKeyUp(int directorKeyCode, String keyChar, boolean shift, boolean ctrl, boolean alt) {
        inputState.setShiftDown(shift);
        inputState.setControlDown(ctrl);
        inputState.setAltDown(alt);
        inputState.queueEvent(InputEvent.keyUp(directorKeyCode, keyChar));
    }

    // --- Tick-time input processing ---

    /**
     * Process queued input events. Called at the beginning of each tick
     * before frame execution, so Lingo scripts see the events.
     * Also dispatches mouseEnter/mouseLeave/mouseWithin based on rollover state.
     */
    public void processInputEvents() {
        InputEvent event;
        boolean hadEvents = false;
        while ((event = inputState.pollEvent()) != null) {
            dispatchInputEvent(event);
            hadEvents = true;
        }

        // Dispatch mouseEnter/mouseLeave/mouseWithin based on current rollover sprite
        dispatchRolloverEvents();

        // Bump sprite revision so WASM SoftwareRenderer re-renders after input
        // (input handlers may change member.text or other visual properties)
        if (hadEvents) {
            stageRenderer.getSpriteRegistry().bumpRevision();
        }

        // Drive caret blink counter when a field is focused
        if (inputState.getKeyboardFocusSprite() > 0) {
            inputState.incrementCaretBlink();
        }
    }

    /**
     * Dispatch mouseEnter, mouseLeave, and mouseWithin events based on rollover tracking.
     */
    private void dispatchRolloverEvents() {
        EventDispatcher dispatcher = eventDispatcherSupplier.get();
        int currentRollover = inputState.getRolloverSprite();

        if (currentRollover != previousRolloverSprite) {
            if (previousRolloverSprite > 0) {
                dispatcher.dispatchSpriteEvent(previousRolloverSprite, PlayerEvent.MOUSE_LEAVE, List.of());
            }
            if (currentRollover > 0) {
                dispatcher.dispatchSpriteEvent(currentRollover, PlayerEvent.MOUSE_ENTER, List.of());
            }
            previousRolloverSprite = currentRollover;
        }

        // mouseWithin fires every frame while mouse is over a sprite
        if (currentRollover > 0) {
            dispatcher.dispatchSpriteEvent(currentRollover, PlayerEvent.MOUSE_WITHIN, List.of());
        }
    }

    /**
     * Dispatch a single input event to the appropriate Lingo handlers.
     */
    private void dispatchInputEvent(InputEvent event) {
        EventDispatcher dispatcher = eventDispatcherSupplier.get();
        switch (event.type()) {
            case MOUSE_DOWN -> {
                int hitSprite = hitTest(event.stageX(), event.stageY());
                // Director D6+: if the previously clicked sprite is different from
                // the current one, send mouseUpOutSide to the old sprite (ScummVM behavior).
                int lastClicked = inputState.getClickOnSprite();
                if (lastClicked > 0 && lastClicked != hitSprite) {
                    dispatcher.dispatchSpriteEvent(lastClicked, "mouseUpOutSide", List.of());
                }
                // Track which sprite was clicked so mouseUp can target it
                inputState.setClickOnSprite(hitSprite);
                // Built-in Director behavior: clicking on an editable text/field sprite
                // automatically sets keyboardFocusSprite to that sprite's channel.
                // Clicking elsewhere clears the keyboard focus.
                autoFocusEditableField(hitSprite, event.stageX(), event.stageY());
                // Dispatch mouseDown to sprites at the click coordinates.
                // Stop if a handler calls stopEvent() (e.g. avatar click should not
                // also trigger floor walk).
                dispatcher.resetEventStopped();
                List<Integer> hitChannels = hitTestAll(event.stageX(), event.stageY());
                for (int ch : hitChannels) {
                    dispatcher.dispatchSpriteEvent(ch, PlayerEvent.MOUSE_DOWN, List.of());
                    if (dispatcher.isEventStopped()) break;
                }
                dispatcher.dispatchGlobalEvent(PlayerEvent.MOUSE_DOWN, List.of());
            }
            case MOUSE_UP -> {
                // Dispatch mouseUp to sprites at the release coordinates.
                dispatcher.resetEventStopped();
                int pressedSprite = inputState.getClickOnSprite();
                List<Integer> hitChannels = hitTestAll(event.stageX(), event.stageY());
                for (int ch : hitChannels) {
                    dispatcher.dispatchSpriteEvent(ch, PlayerEvent.MOUSE_UP, List.of());
                    if (dispatcher.isEventStopped()) break;
                }
                // If the originally-pressed sprite isn't in the hit list,
                // still deliver mouseUp to it (Event Broker / Navigator pattern).
                if (pressedSprite > 0 && !hitChannels.contains(pressedSprite)
                        && dispatcher.spriteHasHandler(pressedSprite, PlayerEvent.MOUSE_UP.getHandlerName())) {
                    dispatcher.dispatchSpriteEvent(pressedSprite, PlayerEvent.MOUSE_UP, List.of());
                }
                inputState.setClickOnSprite(0);
                dispatcher.dispatchGlobalEvent(PlayerEvent.MOUSE_UP, List.of());
            }
            case RIGHT_MOUSE_DOWN -> {
                dispatcher.dispatchGlobalEvent(PlayerEvent.RIGHT_MOUSE_DOWN, List.of());
            }
            case RIGHT_MOUSE_UP -> {
                dispatcher.dispatchGlobalEvent(PlayerEvent.RIGHT_MOUSE_UP, List.of());
            }
            case KEY_DOWN -> {
                // Restore per-event key state so Lingo's "the key" / "the keyCode" read correctly
                inputState.setLastKey(event.keyChar());
                inputState.setLastKeyCode(event.keyCode());
                int focusSprite = inputState.getKeyboardFocusSprite();
                // Built-in editable field keyboard handling (Director inserts typed chars into member.text)
                if (focusSprite > 0) {
                    handleEditableFieldInput(focusSprite, event.keyChar());
                    dispatcher.dispatchSpriteEvent(focusSprite, PlayerEvent.KEY_DOWN, List.of());
                }
                // Director dispatches keyDown to focused sprite → frame → movie scripts
                dispatcher.dispatchFrameAndMovieEvent(PlayerEvent.KEY_DOWN, List.of());
            }
            case KEY_UP -> {
                inputState.setLastKey(event.keyChar());
                inputState.setLastKeyCode(event.keyCode());
                int focusSprite = inputState.getKeyboardFocusSprite();
                if (focusSprite > 0) {
                    dispatcher.dispatchSpriteEvent(focusSprite, PlayerEvent.KEY_UP, List.of());
                }
                dispatcher.dispatchFrameAndMovieEvent(PlayerEvent.KEY_UP, List.of());
            }
        }
    }

    // --- Focus management ---

    /**
     * Built-in Director behavior: clicking on an editable text/field sprite
     * automatically sets keyboardFocusSprite to that sprite's channel.
     */
    private void autoFocusEditableField(int hitChannel, int stageX, int stageY) {
        if (hitChannel > 0) {
            SpriteState sprite = stageRenderer.getSpriteRegistry().get(hitChannel);
            if (sprite != null) {
                int castLibNum = sprite.getEffectiveCastLib();
                int memberNum = sprite.getEffectiveCastMember();
                if (memberNum > 0) {
                    CastMember member = castLibManager.getDynamicMember(castLibNum, memberNum);
                    if (member != null && member.isEditable()
                            && (member.getMemberType() == MemberType.TEXT)) {
                        inputState.setKeyboardFocusSprite(hitChannel);
                        int spriteX = sprite.getLocH() - member.getRegPointX();
                        int spriteY = sprite.getLocV() - member.getRegPointY();
                        int localX = stageX - spriteX;
                        int localY = stageY - spriteY;
                        int charPos = member.locToCharPos(localX, localY, sprite.getWidth());
                        inputState.setSelStart(charPos);
                        inputState.setSelEnd(charPos);
                        inputState.resetCaretBlink();
                        return;
                    }
                }
            }
        }
        // Clicked on non-editable sprite or empty stage — clear focus
        inputState.setKeyboardFocusSprite(0);
    }

    private int hitTest(int stageX, int stageY) {
        EventDispatcher dispatcher = eventDispatcherSupplier.get();
        return HitTester.hitTest(stageRenderer, currentFrameSupplier.getAsInt(), stageX, stageY,
                channel -> dispatcher != null && dispatcher.isSpriteMouseInteractive(channel));
    }

    private List<Integer> hitTestAll(int stageX, int stageY) {
        EventDispatcher dispatcher = eventDispatcherSupplier.get();
        return HitTester.hitTestAll(stageRenderer, currentFrameSupplier.getAsInt(), stageX, stageY,
                channel -> dispatcher != null && dispatcher.isSpriteMouseInteractive(channel));
    }

    // --- Text editing ---

    /**
     * Built-in Director behavior: Tab cycles keyboard focus to the next (or previous
     * with Shift+Tab) editable text sprite, ordered by channel number.
     */
    private void tabToNextField(int currentChannel, boolean reverse) {
        List<Integer> editableChannels = new ArrayList<>();
        for (var entry : stageRenderer.getSpriteRegistry().getAll().entrySet()) {
            int ch = entry.getKey();
            SpriteState s = entry.getValue();
            if (s == null) continue;
            int memberNum = s.getEffectiveCastMember();
            if (memberNum <= 0) continue;
            CastMember m = castLibManager.getDynamicMember(s.getEffectiveCastLib(), memberNum);
            if (m != null && m.isEditable() && m.getMemberType() == MemberType.TEXT) {
                editableChannels.add(ch);
            }
        }
        if (editableChannels.isEmpty()) return;
        Collections.sort(editableChannels);

        int idx = editableChannels.indexOf(currentChannel);
        int next;
        if (reverse) {
            next = idx <= 0 ? editableChannels.size() - 1 : idx - 1;
        } else {
            next = idx < 0 || idx >= editableChannels.size() - 1 ? 0 : idx + 1;
        }
        int nextChannel = editableChannels.get(next);
        inputState.setKeyboardFocusSprite(nextChannel);
        CastMember nextMember = castLibManager.getDynamicMember(
                stageRenderer.getSpriteRegistry().get(nextChannel).getEffectiveCastLib(),
                stageRenderer.getSpriteRegistry().get(nextChannel).getEffectiveCastMember());
        if (nextMember != null) {
            String t = nextMember.getTextContent();
            inputState.setSelStart(0);
            inputState.setSelEnd(t != null ? t.length() : 0);
        }
        inputState.resetCaretBlink();
    }

    /**
     * Built-in Director behavior: when keyboardFocusSprite is set and the sprite's
     * member is an editable field/text, the engine inserts typed characters into member.text.
     */
    private void handleEditableFieldInput(int channel, String keyChar) {
        SpriteState sprite = stageRenderer.getSpriteRegistry().get(channel);
        if (sprite == null) return;

        int castLibNum = sprite.getEffectiveCastLib();
        int memberNum = sprite.getEffectiveCastMember();
        if (memberNum <= 0) return;

        CastMember member = castLibManager.getDynamicMember(castLibNum, memberNum);
        if (member == null) return;

        MemberType type = member.getMemberType();
        if (type != MemberType.TEXT && type != MemberType.BUTTON) return;
        if (!member.isEditable()) return;

        String text = member.getTextContent();
        if (text == null) text = "";

        int selStart = Math.max(0, Math.min(inputState.getSelStart(), text.length()));
        int selEnd = Math.max(0, Math.min(inputState.getSelEnd(), text.length()));
        int selMin = Math.min(selStart, selEnd);
        int selMax = Math.max(selStart, selEnd);
        int keyCode = inputState.getLastKeyCode();

        if (keyCode == 51) {
            // Backspace
            if (selMin != selMax) {
                text = text.substring(0, selMin) + text.substring(selMax);
                selStart = selEnd = selMin;
            } else if (selStart > 0) {
                text = text.substring(0, selStart - 1) + text.substring(selStart);
                selStart = selEnd = selStart - 1;
            }
            member.setDynamicText(text);
        } else if (keyCode == 123) {
            // Left arrow
            selStart = selEnd = Math.max(0, selMin - (selMin == selMax ? 1 : 0));
        } else if (keyCode == 124) {
            // Right arrow
            selStart = selEnd = Math.min(text.length(), selMax + (selMin == selMax ? 1 : 0));
        } else if (keyCode == 48) {
            // Tab — built-in Director behavior: cycle focus to next editable text field
            tabToNextField(channel, inputState.isShiftDown());
            return;
        } else if (keyCode == 36) {
            // Return — don't insert into text; let Lingo handle it
            return;
        } else if (keyChar != null && keyChar.length() == 1 && keyChar.charAt(0) >= ' ') {
            // Printable character (exclude control chars like \t, \r)
            text = text.substring(0, selMin) + keyChar + text.substring(selMax);
            selStart = selEnd = selMin + 1;
            member.setDynamicText(text);
        } else {
            // Non-printable, non-handled key — no caret reset needed
            return;
        }

        inputState.setSelStart(selStart);
        inputState.setSelEnd(selEnd);
        inputState.resetCaretBlink();
    }

    // --- Caret and selection ---

    /**
     * Helper: get the focused editable field's sprite and member.
     * Returns null if no focused editable field.
     */
    private Object[] getFocusedFieldInfo() {
        int focusChannel = inputState.getKeyboardFocusSprite();
        if (focusChannel <= 0) return null;

        SpriteState sprite = stageRenderer.getSpriteRegistry().get(focusChannel);
        if (sprite == null) return null;

        int castLibNum = sprite.getEffectiveCastLib();
        int memberNum = sprite.getEffectiveCastMember();
        if (memberNum <= 0) return null;

        CastMember member = castLibManager.getDynamicMember(castLibNum, memberNum);
        if (member == null || !member.isEditable()) return null;

        return new Object[]{sprite, member};
    }

    /**
     * Get caret rendering info for the currently focused editable field.
     * Returns {x, y, height} in stage coordinates, or null if no caret should be shown.
     */
    public int[] getCaretInfo() {
        if (!inputState.isCaretVisible()) return null;

        Object[] info = getFocusedFieldInfo();
        if (info == null) return null;
        SpriteState sprite = (SpriteState) info[0];
        CastMember member = (CastMember) info[1];

        String text = member.getTextContent();
        if (text == null) text = "";

        int selStart = Math.max(0, Math.min(inputState.getSelStart(), text.length()));
        int selEnd = Math.max(0, Math.min(inputState.getSelEnd(), text.length()));
        if (selStart != selEnd) return null;

        TextRenderer renderer = member.getTextRenderer();
        if (renderer == null) return null;

        int[] pos = renderer.charPosToLoc(text, selStart,
                member.getTextFont(), member.getTextFontSize(),
                member.getTextFontStyle(), member.getTextFixedLineSpace(),
                member.getTextAlignment(), sprite.getWidth());
        if (pos == null) return null;

        int lineHeight = renderer.getLineHeight(member.getTextFont(),
                member.getTextFontSize(), member.getTextFontStyle(),
                member.getTextFixedLineSpace());

        int spriteX = sprite.getLocH() - member.getRegPointX();
        int spriteY = sprite.getLocV() - member.getRegPointY();
        int caretX = spriteX + pos[0];
        int caretY = spriteY + pos[1];

        return new int[]{caretX, caretY, lineHeight};
    }

    /**
     * Get selection highlight rectangles for the focused editable field.
     * Returns array of {x, y, w, h} quads in stage coordinates, or null if no selection.
     */
    public int[] getSelectionInfo() {
        Object[] info = getFocusedFieldInfo();
        if (info == null) return null;
        SpriteState sprite = (SpriteState) info[0];
        CastMember member = (CastMember) info[1];

        String text = member.getTextContent();
        if (text == null) text = "";

        int selStart = Math.max(0, Math.min(inputState.getSelStart(), text.length()));
        int selEnd = Math.max(0, Math.min(inputState.getSelEnd(), text.length()));
        if (selStart == selEnd) return null;

        int selMin = Math.min(selStart, selEnd);
        int selMax = Math.max(selStart, selEnd);

        TextRenderer renderer = member.getTextRenderer();
        if (renderer == null) return null;

        String font = member.getTextFont();
        int fontSize = member.getTextFontSize();
        String fontStyle = member.getTextFontStyle();
        int fls = member.getTextFixedLineSpace();

        int lineHeight = renderer.getLineHeight(font, fontSize, fontStyle, fls);
        int spriteX = sprite.getLocH() - member.getRegPointX();
        int spriteY = sprite.getLocV() - member.getRegPointY();

        String alignment = member.getTextAlignment();
        int fieldWidth = sprite.getWidth();
        int[] startPos = renderer.charPosToLoc(text, selMin, font, fontSize, fontStyle, fls, alignment, fieldWidth);
        int[] endPos = renderer.charPosToLoc(text, selMax, font, fontSize, fontStyle, fls, alignment, fieldWidth);
        if (startPos == null || endPos == null) return null;

        int startLineY = startPos[1];
        int endLineY = endPos[1];

        if (startLineY == endLineY) {
            return new int[]{
                spriteX + startPos[0], spriteY + startLineY,
                endPos[0] - startPos[0], lineHeight
            };
        }

        // Multi-line: first line from startX to sprite right edge,
        // middle lines full width, last line from left edge to endX
        List<int[]> rects = new ArrayList<>();
        int spriteWidth = sprite.getWidth();

        rects.add(new int[]{
            spriteX + startPos[0], spriteY + startLineY,
            spriteWidth - startPos[0], lineHeight
        });
        for (int midY = startLineY + lineHeight; midY < endLineY; midY += lineHeight) {
            rects.add(new int[]{spriteX, spriteY + midY, spriteWidth, lineHeight});
        }
        rects.add(new int[]{spriteX, spriteY + endLineY, endPos[0], lineHeight});

        int[] result = new int[rects.size() * 4];
        for (int i = 0; i < rects.size(); i++) {
            System.arraycopy(rects.get(i), 0, result, i * 4, 4);
        }
        return result;
    }

    // --- Clipboard operations ---

    /**
     * Handle pasted text from clipboard.
     */
    public void onPasteText(String pasteText) {
        int focusChannel = inputState.getKeyboardFocusSprite();
        if (focusChannel <= 0) return;

        SpriteState sprite = stageRenderer.getSpriteRegistry().get(focusChannel);
        if (sprite == null) return;

        int castLibNum = sprite.getEffectiveCastLib();
        int memberNum = sprite.getEffectiveCastMember();
        if (memberNum <= 0) return;

        CastMember member = castLibManager.getDynamicMember(castLibNum, memberNum);
        if (member == null || !member.isEditable()) return;

        String text = member.getTextContent();
        if (text == null) text = "";

        int selStart = Math.max(0, Math.min(inputState.getSelStart(), text.length()));
        int selEnd = Math.max(0, Math.min(inputState.getSelEnd(), text.length()));
        int selMin = Math.min(selStart, selEnd);
        int selMax = Math.max(selStart, selEnd);

        text = text.substring(0, selMin) + pasteText + text.substring(selMax);
        int newPos = selMin + pasteText.length();
        inputState.setSelStart(newPos);
        inputState.setSelEnd(newPos);
        inputState.resetCaretBlink();
        member.setDynamicText(text);
        stageRenderer.getSpriteRegistry().bumpRevision();
    }

    /**
     * Get selected text from the focused editable field.
     */
    public String getSelectedText() {
        int focusChannel = inputState.getKeyboardFocusSprite();
        if (focusChannel <= 0) return null;

        SpriteState sprite = stageRenderer.getSpriteRegistry().get(focusChannel);
        if (sprite == null) return null;

        int castLibNum = sprite.getEffectiveCastLib();
        int memberNum = sprite.getEffectiveCastMember();
        if (memberNum <= 0) return null;

        CastMember member = castLibManager.getDynamicMember(castLibNum, memberNum);
        if (member == null) return null;

        String text = member.getTextContent();
        if (text == null || text.isEmpty()) return "";

        int selStart = Math.max(0, Math.min(inputState.getSelStart(), text.length()));
        int selEnd = Math.max(0, Math.min(inputState.getSelEnd(), text.length()));
        if (selStart != selEnd) {
            int selMin = Math.min(selStart, selEnd);
            int selMax = Math.max(selStart, selEnd);
            return text.substring(selMin, selMax);
        }
        return text; // No selection → copy all
    }

    /**
     * Cut selected text: returns the selected text and deletes it from the field.
     */
    public String cutSelectedText() {
        int focusChannel = inputState.getKeyboardFocusSprite();
        if (focusChannel <= 0) return null;

        SpriteState sprite = stageRenderer.getSpriteRegistry().get(focusChannel);
        if (sprite == null) return null;

        int castLibNum = sprite.getEffectiveCastLib();
        int memberNum = sprite.getEffectiveCastMember();
        if (memberNum <= 0) return null;

        CastMember member = castLibManager.getDynamicMember(castLibNum, memberNum);
        if (member == null || !member.isEditable()) return null;

        String text = member.getTextContent();
        if (text == null || text.isEmpty()) return "";

        int selStart = Math.max(0, Math.min(inputState.getSelStart(), text.length()));
        int selEnd = Math.max(0, Math.min(inputState.getSelEnd(), text.length()));
        int selMin = Math.min(selStart, selEnd);
        int selMax = Math.max(selStart, selEnd);

        String cutText;
        if (selMin != selMax) {
            cutText = text.substring(selMin, selMax);
            text = text.substring(0, selMin) + text.substring(selMax);
            inputState.setSelStart(selMin);
            inputState.setSelEnd(selMin);
        } else {
            cutText = text;
            text = "";
            inputState.setSelStart(0);
            inputState.setSelEnd(0);
        }
        inputState.resetCaretBlink();
        member.setDynamicText(text);
        stageRenderer.getSpriteRegistry().bumpRevision();
        return cutText;
    }

    /**
     * Select all text in the focused editable field.
     */
    public void selectAll() {
        int focusChannel = inputState.getKeyboardFocusSprite();
        if (focusChannel <= 0) return;

        SpriteState sprite = stageRenderer.getSpriteRegistry().get(focusChannel);
        if (sprite == null) return;

        int castLibNum = sprite.getEffectiveCastLib();
        int memberNum = sprite.getEffectiveCastMember();
        if (memberNum <= 0) return;

        CastMember member = castLibManager.getDynamicMember(castLibNum, memberNum);
        if (member == null) return;

        String text = member.getTextContent();
        int len = text != null ? text.length() : 0;
        inputState.setSelStart(0);
        inputState.setSelEnd(len);
        inputState.resetCaretBlink();
    }
}
