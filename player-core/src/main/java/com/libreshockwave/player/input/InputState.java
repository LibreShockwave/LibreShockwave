package com.libreshockwave.player.input;

import java.util.LinkedList;
import java.util.Queue;

/**
 * Tracks mouse and keyboard input state for the Director player.
 * Updated by the UI thread (Swing EDT or browser JS), read by the VM thread.
 * Fields are non-volatile: WASM/TeaVM target is single-threaded.
 */
public class InputState {

    // Mouse position (stage coordinates)
    private int mouseH;
    private int mouseV;

    // Mouse button state
    private boolean mouseDown;
    private boolean rightMouseDown;

    // Last click info
    private int clickOnSprite;  // sprite channel of last click
    private int clickLocH;
    private int clickLocV;
    private long lastClickTime;
    private int lastClickLocH;
    private int lastClickLocV;
    private boolean doubleClick;

    // Rollover tracking
    private int rolloverSprite;  // sprite channel mouse is currently over

    // Keyboard state
    private String lastKey = "";     // character of last key pressed
    private int lastKeyCode;         // Director Mac keycode
    private boolean shiftDown;
    private boolean controlDown;     // Ctrl on Windows = command on Mac
    private boolean altDown;         // Alt on Windows = option on Mac

    // Keyboard focus
    private int keyboardFocusSprite;

    // Selection state (for text fields)
    private int selStart;
    private int selEnd;

    // Caret blink counter (incremented each tick when a field is focused)
    private int caretBlinkCounter;

    // Event queue — input events queued by UI thread, processed by VM thread during tick.
    // Using LinkedList instead of ConcurrentLinkedQueue for TeaVM WASM compatibility.
    // WASM is single-threaded; on desktop, queue/poll don't race on the same element.
    private final Queue<InputEvent> eventQueue = new LinkedList<>();

    // --- Mouse position ---

    public int getMouseH() { return mouseH; }
    public int getMouseV() { return mouseV; }

    public void setMousePosition(int h, int v) {
        this.mouseH = h;
        this.mouseV = v;
    }

    // --- Mouse buttons ---

    public boolean isMouseDown() { return mouseDown; }
    public boolean isRightMouseDown() { return rightMouseDown; }

    public void setMouseDown(boolean down) { this.mouseDown = down; }
    public void setRightMouseDown(boolean down) { this.rightMouseDown = down; }

    // --- Click info ---

    public int getClickOnSprite() { return clickOnSprite; }
    public void setClickOnSprite(int channel) { this.clickOnSprite = channel; }

    public int getClickLocH() { return clickLocH; }
    public int getClickLocV() { return clickLocV; }
    public void setClickLoc(int h, int v) { this.clickLocH = h; this.clickLocV = v; }

    public boolean isDoubleClick() { return doubleClick; }
    public void updateDoubleClick(int h, int v) {
        long now = System.currentTimeMillis();
        // Standard double-click threshold: 500ms and ~5 pixels
        boolean timeMatch = (now - lastClickTime) <= 500;
        boolean distMatch = Math.abs(h - lastClickLocH) <= 5 && Math.abs(v - lastClickLocV) <= 5;

        this.doubleClick = timeMatch && distMatch;
        this.lastClickTime = now;
        this.lastClickLocH = h;
        this.lastClickLocV = v;
    }

    // --- Rollover ---

    public int getRolloverSprite() { return rolloverSprite; }
    public void setRolloverSprite(int channel) { this.rolloverSprite = channel; }

    // --- Keyboard ---

    public String getLastKey() { return lastKey; }
    public void setLastKey(String key) { this.lastKey = key != null ? key : ""; }

    public int getLastKeyCode() { return lastKeyCode; }
    public void setLastKeyCode(int code) { this.lastKeyCode = code; }

    public boolean isShiftDown() { return shiftDown; }
    public void setShiftDown(boolean down) { this.shiftDown = down; }

    public boolean isControlDown() { return controlDown; }
    public void setControlDown(boolean down) { this.controlDown = down; }

    public boolean isAltDown() { return altDown; }
    public void setAltDown(boolean down) { this.altDown = down; }

    // --- Keyboard focus ---

    public int getKeyboardFocusSprite() { return keyboardFocusSprite; }
    public void setKeyboardFocusSprite(int channel) { this.keyboardFocusSprite = channel; }

    // --- Selection ---

    public int getSelStart() { return selStart; }
    public void setSelStart(int pos) { this.selStart = pos; }

    public int getSelEnd() { return selEnd; }
    public void setSelEnd(int pos) { this.selEnd = pos; }

    // --- Caret blink ---

    private int caretBlinkRate = 8; // ticks per half-cycle (~530ms at 15fps)
    public void incrementCaretBlink() { caretBlinkCounter++; }
    public void resetCaretBlink() { caretBlinkCounter = 0; }
    /** Update blink rate based on movie tempo (fps). Targets ~530ms per half-cycle. */
    public void setCaretBlinkRate(int tempo) { caretBlinkRate = Math.max(1, (tempo * 530 + 500) / 1000); }
    /** Visible during first half of each blink cycle. */
    public boolean isCaretVisible() { return keyboardFocusSprite > 0 && (caretBlinkCounter / caretBlinkRate) % 2 == 0; }

    // --- Event queue ---

    public void queueEvent(InputEvent event) {
        eventQueue.add(event);
    }

    public InputEvent pollEvent() {
        return eventQueue.poll();
    }

    public boolean hasEvents() {
        return !eventQueue.isEmpty();
    }
}
