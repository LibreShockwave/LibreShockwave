package com.libreshockwave.player.wasm;

import org.teavm.interop.Address;
import org.teavm.interop.Export;

import com.libreshockwave.util.FileUtil;
import com.libreshockwave.vm.DebugConfig;
import com.libreshockwave.vm.datum.Datum;

import java.io.OutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Queue;
import java.util.Set;

/**
 * Single entry point for the WASM player.
 * All @Export static methods are callable from JavaScript.
 * Zero @Import annotations — WASM is a pure computation engine.
 *
 * Data exchange uses shared byte[] buffers with raw memory addresses.
 * JS writes data into buffers, calls exports; WASM reads from buffers.
 * WASM writes results into buffers; JS reads via memory addresses.
 */
public class WasmEntry {

    private static WasmPlayer wasmPlayer;
    private static String lastError = null;

    // Shared buffers for JS <-> WASM data transfer
    private static byte[] movieBuffer;
    private static byte[] stringBuffer = new byte[65536];
    private static byte[] netBuffer;
    private static final Queue<String[]> pendingGotoNetPages = new ArrayDeque<>();

    private static final Set<String> failedCasts = new HashSet<>();

    // Debug log: accumulates messages; read via getDebugLog() export
    static final StringBuilder debugLog = new StringBuilder(1024);

    /** Append a timestamped debug message (accessible from player-wasm package). */
    static void log(String msg) {
        debugLog.append(msg).append('\n');
    }

    static void enqueueGotoNetPage(String url, String target) {
        synchronized (pendingGotoNetPages) {
            pendingGotoNetPages.offer(new String[] {
                    url != null ? url : "",
                    target != null ? target : ""
            });
        }
    }

    public static void main(String[] args) {
        // Replace System.out/err with non-synchronized PrintStream.
        // Java's PrintStream uses synchronized(this) on every println() call,
        // which triggers ClassCastException in TeaVM WASM's monitorEnterSync.
        PrintStream unsync = new PrintStream(new OutputStream() {
            @Override public void write(int b) { }
            @Override public void write(byte[] b, int off, int len) { }
        }) {
            @Override public void println(String x) { log(x); }
            @Override public void print(String x) { debugLog.append(x); }
            @Override public void println(Object x) { log(String.valueOf(x)); }
            @Override public void println() { debugLog.append('\n'); }
        };
        System.setOut(unsync);
        System.setErr(unsync);
    }

    // === Buffer management ===

    @Export(name = "allocateBuffer")
    public static int allocateBuffer(int size) {
        movieBuffer = new byte[size];
        return Address.ofData(movieBuffer).toInt();
    }

    @Export(name = "getStringBufferAddress")
    public static int getStringBufferAddress() {
        return Address.ofData(stringBuffer).toInt();
    }

    @Export(name = "readNextGotoNetPage")
    public static int readNextGotoNetPage() {
        String[] next;
        synchronized (pendingGotoNetPages) {
            next = pendingGotoNetPages.poll();
        }
        if (next == null) {
            return 0;
        }

        byte[] urlBytes = next[0].getBytes(StandardCharsets.UTF_8);
        byte[] targetBytes = next[1].getBytes(StandardCharsets.UTF_8);

        int maxUrlLen = Math.min(urlBytes.length, 0xFFFF);
        int maxTargetLen = Math.min(targetBytes.length, 0xFFFF);
        if (maxUrlLen + maxTargetLen > stringBuffer.length) {
            maxTargetLen = Math.max(0, stringBuffer.length - maxUrlLen);
        }

        System.arraycopy(urlBytes, 0, stringBuffer, 0, maxUrlLen);
        System.arraycopy(targetBytes, 0, stringBuffer, maxUrlLen, maxTargetLen);
        return (maxUrlLen << 16) | maxTargetLen;
    }

    // === Movie loading ===

    /**
     * Load a movie from the movie buffer.
     * basePath must already be written to stringBuffer.
     * @return (width << 16) | height, or 0 on failure
     */
    @Export(name = "loadMovie")
    public static int loadMovie(int movieSize, int basePathLen) {
        String basePath = "";
        if (basePathLen > 0) {
            basePath = new String(stringBuffer, 0, basePathLen);
        }

        byte[] data = new byte[movieSize];
        System.arraycopy(movieBuffer, 0, data, 0, movieSize);

        if (wasmPlayer != null) {
            wasmPlayer.shutdown();
        }

        wasmPlayer = new WasmPlayer();
        if (!wasmPlayer.loadMovie(data, basePath,
                (castLibNumber, fileName) -> {
                    // Try to load directly from CastLibManager's cache (instant, same tick).
                    // This avoids a 1-tick delay that causes "Cast number expected" errors
                    // when objectmanager runs before cast data arrives via JS round-trip.
                    String baseName = FileUtil.getFileNameWithoutExtension(
                            FileUtil.getFileName(fileName));
                    var castLibManager = wasmPlayer.getPlayer().getCastLibManager();
                    byte[] cached = castLibManager.getCachedExternalData(baseName);
                    if (cached != null) {
                        try {
                            if (castLibManager.setExternalCastData(castLibNumber, cached)) {
                                wasmPlayer.getPlayer().getBitmapCache().clear();
                                wasmPlayer.bumpCastRevision();
                                log("castDataRequestCallback: loaded " + baseName + " from cache (cast#" + castLibNumber + ")");
                                return;
                            }
                        } catch (Throwable e) {
                            log("castDataRequestCallback: cache load failed for " + baseName + ": " + e);
                            failedCasts.add(baseName);
                        }
                    }
                    log("castDataRequestCallback: " + baseName + " not in cache (cast#" + castLibNumber + ")");
                })) {
            return 0;
        }

        // Wire up error handler depth tracing
        if (wasmPlayer.getPlayer() != null) {
            wasmPlayer.getPlayer().getVM().setErrorHandlerSkipCallback(msg -> log("[EH] " + msg));
        }

        int w = wasmPlayer.getStageWidth();
        int h = wasmPlayer.getStageHeight();
        return (w << 16) | h;
    }

    // === Playback ===

    /**
     * Set the per-handler instruction step limit. 0 = unlimited (the default).
     */
    @Export(name = "setVmStepLimit")
    public static void setVmStepLimit(int limit) {
        if (wasmPlayer != null && wasmPlayer.getPlayer() != null) {
            wasmPlayer.getPlayer().getVM().setStepLimit(limit);
        }
    }

    /**
     * Enable or disable debug playback logging (handler calls, error stack traces).
     * @param enabled 1 = enabled, 0 = disabled
     */
    @Export(name = "setDebugPlaybackEnabled")
    public static void setDebugPlaybackEnabled(int enabled) {
        DebugConfig.setDebugPlaybackEnabled(enabled != 0);
    }

    /**
     * Add a function trace hook. Handler name is in stringBuffer[0..nameLen).
     * When the traced handler is called, its args and call stack are printed.
     */
    @Export(name = "addTraceHandler")
    public static void addTraceHandler(int nameLen) {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null || nameLen <= 0) return;
        String name = new String(stringBuffer, 0, nameLen);
        wasmPlayer.getPlayer().getVM().addTraceHandler(name);
    }

    /**
     * Remove a function trace hook. Handler name is in stringBuffer[0..nameLen).
     */
    @Export(name = "removeTraceHandler")
    public static void removeTraceHandler(int nameLen) {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null || nameLen <= 0) return;
        String name = new String(stringBuffer, 0, nameLen);
        wasmPlayer.getPlayer().getVM().removeTraceHandler(name);
    }

    /**
     * Clear all function trace hooks.
     */
    @Export(name = "clearTraceHandlers")
    public static void clearTraceHandlers() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return;
        wasmPlayer.getPlayer().getVM().clearTraceHandlers();
    }

    /**
     * Preload all external casts (queue fetch requests before play).
     * @return number of casts queued for loading
     */
    @Export(name = "preloadCasts")
    public static int preloadCasts() {
        if (wasmPlayer == null) return 0;
        try {
            return wasmPlayer.preloadCasts();
        } catch (Throwable e) {
            captureError("preloadCasts", e);
            return 0;
        }
    }

    @Export(name = "play")
    public static void play() {
        if (wasmPlayer == null) return;
        try {
            lastError = null;
            // Set step limit to catch infinite loops. The dump handler runs ~292K
            // instructions; 5M gives 17x headroom while catching infinite loops fast.
            if (wasmPlayer.getPlayer() != null) {
                wasmPlayer.getPlayer().getVM().setStepLimit(5_000_000);
            }
            log("play() called, frame before=" + wasmPlayer.getCurrentFrame());
            wasmPlayer.play();
            log("play() done, frame after=" + wasmPlayer.getCurrentFrame());
        } catch (Throwable e) {
            captureError("play", e);
        }
    }

    /**
     * Advance one frame.
     * @return 1 if still playing/paused, 0 if stopped
     */
    @Export(name = "tick")
    public static int tick() {
        if (wasmPlayer == null) return 0;
        try {
            lastError = null;
            boolean result = wasmPlayer.tick();
            return result ? 1 : 0;
        } catch (Throwable e) {
            captureError("tick", e);
            return 1; // Keep animation loop alive
        }
    }

    @Export(name = "pause")
    public static void pause() {
        if (wasmPlayer != null) wasmPlayer.pause();
    }

    @Export(name = "stop")
    public static void stop() {
        if (wasmPlayer != null) wasmPlayer.stop();
    }

    @Export(name = "goToFrame")
    public static void goToFrame(int frame) {
        if (wasmPlayer != null) wasmPlayer.goToFrame(frame);
    }

    @Export(name = "stepForward")
    public static void stepForward() {
        if (wasmPlayer != null) wasmPlayer.stepFrame();
    }

    @Export(name = "stepBackward")
    public static void stepBackward() {
        if (wasmPlayer != null) {
            int frame = wasmPlayer.getCurrentFrame();
            if (frame > 1) {
                wasmPlayer.goToFrame(frame - 1);
            }
        }
    }

    // === State queries ===

    @Export(name = "getCurrentFrame")
    public static int getCurrentFrame() {
        return wasmPlayer != null ? wasmPlayer.getCurrentFrame() : 0;
    }

    @Export(name = "getFrameCount")
    public static int getFrameCount() {
        return wasmPlayer != null ? wasmPlayer.getFrameCount() : 0;
    }

    @Export(name = "getTempo")
    public static int getTempo() {
        return wasmPlayer != null ? wasmPlayer.getTempo() : 15;
    }

    @Export(name = "setPuppetTempo")
    public static void setPuppetTempo(int tempo) {
        if (wasmPlayer != null) {
            wasmPlayer.setPuppetTempo(tempo);
        }
    }

    /**
     * Get the number of active sprites in the current frame, without baking bitmaps.
     * @return sprite count, or 0 if not playing
     */
    @Export(name = "getSpriteCount")
    public static int getSpriteCount() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return 0;
        try {
            return wasmPlayer.getPlayer().getStageRenderer()
                    .getSpritesForFrame(wasmPlayer.getPlayer().getCurrentFrame()).size();
        } catch (Throwable e) {
            captureError("getSpriteCount", e);
            return 0;
        }
    }

    /**
     * Get the cursor type for the current mouse position.
     * @return 0 = default, 1 = text (caret)
     */
    @Export(name = "getCursorType")
    public static int getCursorType() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return 0;
        try {
            return wasmPlayer.getPlayer().getCursorManager().getCursorAtMouse();
        } catch (Throwable e) {
            return 0;
        }
    }

    @Export(name = "getStageWidth")
    public static int getStageWidth() {
        return wasmPlayer != null ? wasmPlayer.getStageWidth() : 640;
    }

    @Export(name = "getStageHeight")
    public static int getStageHeight() {
        return wasmPlayer != null ? wasmPlayer.getStageHeight() : 480;
    }

    // === Full-frame rendering ===

    /** RGBA buffer holding the last rendered frame. */
    private static byte[] renderBuffer;

    /**
     * Render the current frame into an RGBA buffer via SoftwareRenderer.
     * JS reads the pixel data from getRenderBufferAddress().
     * @return buffer byte length (width * height * 4), or 0 on failure
     */
    @Export(name = "render")
    public static int render() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return 0;
        try {
            SoftwareRenderer renderer = wasmPlayer.getSoftwareRenderer();
            if (renderer == null) return 0;

            var snapshot = wasmPlayer.getPlayer().getFrameSnapshot();
            int spriteRev = wasmPlayer.getPlayer().getStageRenderer()
                    .getSpriteRegistry().getRevision();
            byte[] frameRgba = renderer.render(snapshot, wasmPlayer.getCastRevision(), spriteRev);

            // Base frame only — cursor is composited on the main thread at 60fps
            renderBuffer = frameRgba;
            return renderBuffer.length;
        } catch (Throwable e) {
            captureError("render", e);
            return 0;
        }
    }

    /**
     * Get the memory address of the last rendered RGBA buffer.
     * @return address, or 0 if no frame has been rendered
     */
    @Export(name = "getRenderBufferAddress")
    public static int getRenderBufferAddress() {
        return renderBuffer != null ? Address.ofData(renderBuffer).toInt() : 0;
    }

    // === Cursor bitmap exports (composited on main thread at 60fps) ===

    /** RGBA buffer holding the cursor bitmap for the main thread to composite. */
    private static byte[] cursorBitmapBuffer;
    private static int cursorBitmapWidth;
    private static int cursorBitmapHeight;
    private static int cursorBitDepth;
    private static int cursorRegX;
    private static int cursorRegY;

    /**
     * Update the cursor bitmap buffer from the current cursor state.
     * Call once per tick. Returns non-zero if a bitmap cursor is active.
     */
    @Export(name = "updateCursorBitmap")
    public static int updateCursorBitmap() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) {
            cursorBitmapBuffer = null;
            return 0;
        }
        try {
            com.libreshockwave.bitmap.Bitmap cursorBmp = wasmPlayer.getPlayer().getCursorManager().getCursorBitmap();
            if (cursorBmp == null) {
                cursorBitmapBuffer = null;
                return 0;
            }
            int w = cursorBmp.getWidth();
            int h = cursorBmp.getHeight();
            int[] pixels = cursorBmp.getPixels();
            int depth = cursorBmp.getBitDepth();

            int[] regPoint = wasmPlayer.getPlayer().getCursorManager().getCursorRegPoint();
            cursorRegX = regPoint != null ? regPoint[0] : 0;
            cursorRegY = regPoint != null ? regPoint[1] : 0;
            cursorBitmapWidth = w;
            cursorBitmapHeight = h;
            cursorBitDepth = depth;

            // Convert ARGB int[] to RGBA byte[] with transparency applied
            int len = w * h * 4;
            if (cursorBitmapBuffer == null || cursorBitmapBuffer.length != len) {
                cursorBitmapBuffer = new byte[len];
            }
            for (int i = 0; i < pixels.length; i++) {
                int pixel = pixels[i];
                int a = (pixel >> 24) & 0xFF;
                int r = (pixel >> 16) & 0xFF;
                int g = (pixel >> 8) & 0xFF;
                int b = pixel & 0xFF;

                if (depth <= 8) {
                    // Palette-based: white = transparent, everything else = opaque
                    if (r == 255 && g == 255 && b == 255) {
                        a = 0; r = 0; g = 0; b = 0;
                    } else {
                        a = 255;
                    }
                } else {
                    // 32-bit: use alpha channel as-is
                    if (a == 0) { r = 0; g = 0; b = 0; }
                }

                int off = i * 4;
                cursorBitmapBuffer[off]     = (byte) r;
                cursorBitmapBuffer[off + 1] = (byte) g;
                cursorBitmapBuffer[off + 2] = (byte) b;
                cursorBitmapBuffer[off + 3] = (byte) a;
            }
            return 1;
        } catch (Throwable e) {
            cursorBitmapBuffer = null;
            return 0;
        }
    }

    @Export(name = "getCursorBitmapWidth")
    public static int getCursorBitmapWidth() { return cursorBitmapWidth; }

    @Export(name = "getCursorBitmapHeight")
    public static int getCursorBitmapHeight() { return cursorBitmapHeight; }

    @Export(name = "getCursorBitDepth")
    public static int getCursorBitDepth() { return cursorBitDepth; }

    @Export(name = "getCursorRegPointX")
    public static int getCursorRegPointX() { return cursorRegX; }

    @Export(name = "getCursorRegPointY")
    public static int getCursorRegPointY() { return cursorRegY; }

    @Export(name = "getCursorBitmapAddress")
    public static int getCursorBitmapAddress() {
        return cursorBitmapBuffer != null ? Address.ofData(cursorBitmapBuffer).toInt() : 0;
    }

    @Export(name = "getCursorBitmapLength")
    public static int getCursorBitmapLength() {
        return cursorBitmapBuffer != null ? cursorBitmapBuffer.length : 0;
    }

    // === Caret info (JS reads for text cursor rendering) ===

    private static int[] caretInfo;

    @Export(name = "isCaretVisible")
    public static int isCaretVisible() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return 0;
        caretInfo = wasmPlayer.getPlayer().getInputHandler().getCaretInfo();
        return caretInfo != null ? 1 : 0;
    }

    @Export(name = "getCaretX")
    public static int getCaretX() { return caretInfo != null ? caretInfo[0] : 0; }

    @Export(name = "getCaretY")
    public static int getCaretY() { return caretInfo != null ? caretInfo[1] : 0; }

    @Export(name = "getCaretHeight")
    public static int getCaretHeight() { return caretInfo != null ? caretInfo[2] : 0; }

    // Selection highlight rectangles (array of x,y,w,h quads)
    private static int[] selectionInfo;

    /** Call first to cache selection info. Returns number of highlight rectangles. */
    @Export(name = "getSelectionRectCount")
    public static int getSelectionRectCount() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) { selectionInfo = null; return 0; }
        selectionInfo = wasmPlayer.getPlayer().getInputHandler().getSelectionInfo();
        return selectionInfo != null ? selectionInfo.length / 4 : 0;
    }

    @Export(name = "getSelectionRectX")
    public static int getSelectionRectX(int index) { return selectionInfo != null && index * 4 < selectionInfo.length ? selectionInfo[index * 4] : 0; }

    @Export(name = "getSelectionRectY")
    public static int getSelectionRectY(int index) { return selectionInfo != null && index * 4 + 1 < selectionInfo.length ? selectionInfo[index * 4 + 1] : 0; }

    @Export(name = "getSelectionRectW")
    public static int getSelectionRectW(int index) { return selectionInfo != null && index * 4 + 2 < selectionInfo.length ? selectionInfo[index * 4 + 2] : 0; }

    @Export(name = "getSelectionRectH")
    public static int getSelectionRectH(int index) { return selectionInfo != null && index * 4 + 3 < selectionInfo.length ? selectionInfo[index * 4 + 3] : 0; }

    // === Paste text (JS sends clipboard text to WASM) ===

    @Export(name = "pasteText")
    public static void pasteText(int textLen) {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return;
        String text = textLen > 0 ? new String(stringBuffer, 0, Math.min(textLen, stringBuffer.length)) : "";
        if (!text.isEmpty()) wasmPlayer.getPlayer().getInputHandler().onPasteText(text);
    }

    // === Copy text (JS reads selected text from WASM) ===

    @Export(name = "getSelectedTextLength")
    public static int getSelectedTextLength() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return 0;
        String text = wasmPlayer.getPlayer().getInputHandler().getSelectedText();
        if (text == null || text.isEmpty()) return 0;
        byte[] utf8 = text.getBytes();
        int len = Math.min(utf8.length, stringBuffer.length);
        System.arraycopy(utf8, 0, stringBuffer, 0, len);
        return len;
    }

    // === Cut text (copies selected text to clipboard and deletes it) ===

    @Export(name = "cutSelectedText")
    public static int cutSelectedText() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return 0;
        String text = wasmPlayer.getPlayer().getInputHandler().cutSelectedText();
        if (text == null || text.isEmpty()) return 0;
        byte[] utf8 = text.getBytes();
        int len = Math.min(utf8.length, stringBuffer.length);
        System.arraycopy(utf8, 0, stringBuffer, 0, len);
        return len;
    }

    // === Select all text in focused field ===

    @Export(name = "selectAll")
    public static void selectAll() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return;
        wasmPlayer.getPlayer().getInputHandler().selectAll();
    }

    // === Network polling (JS reads pending requests from WASM) ===

    /**
     * Get number of pending fetch requests.
     */
    @Export(name = "getPendingFetchCount")
    public static int getPendingFetchCount() {
        QueuedNetProvider net = netProvider();
        return net != null ? net.getPendingRequests().size() : 0;
    }

    @Export(name = "getPendingFetchTaskId")
    public static int getPendingFetchTaskId(int index) {
        QueuedNetProvider net = netProvider();
        if (net == null) return 0;
        QueuedNetProvider.PendingRequest req = net.getRequest(index);
        return req != null ? req.taskId : 0;
    }

    @Export(name = "getPendingFetchUrl")
    public static int getPendingFetchUrl(int index) {
        QueuedNetProvider net = netProvider();
        if (net == null) return 0;
        QueuedNetProvider.PendingRequest req = net.getRequest(index);
        return req != null ? writeToStringBuffer(req.url) : 0;
    }

    /** @return 0=GET, 1=POST */
    @Export(name = "getPendingFetchMethod")
    public static int getPendingFetchMethod(int index) {
        QueuedNetProvider net = netProvider();
        if (net == null) return 0;
        QueuedNetProvider.PendingRequest req = net.getRequest(index);
        return req != null && "POST".equals(req.method) ? 1 : 0;
    }

    @Export(name = "getPendingFetchPostData")
    public static int getPendingFetchPostData(int index) {
        QueuedNetProvider net = netProvider();
        if (net == null) return 0;
        QueuedNetProvider.PendingRequest req = net.getRequest(index);
        return req != null ? writeToStringBuffer(req.postData) : 0;
    }

    @Export(name = "getPendingFetchFallbackCount")
    public static int getPendingFetchFallbackCount(int index) {
        QueuedNetProvider net = netProvider();
        if (net == null) return 0;
        QueuedNetProvider.PendingRequest req = net.getRequest(index);
        if (req == null || req.fallbacks == null || req.fallbacks.length <= 1) return 0;
        return req.fallbacks.length - 1; // first entry is the primary URL
    }

    @Export(name = "getPendingFetchFallbackUrl")
    public static int getPendingFetchFallbackUrl(int index, int fallbackIndex) {
        QueuedNetProvider net = netProvider();
        if (net == null) return 0;
        QueuedNetProvider.PendingRequest req = net.getRequest(index);
        if (req == null || req.fallbacks == null) return 0;
        int actualIndex = fallbackIndex + 1; // skip primary URL at [0]
        if (actualIndex >= req.fallbacks.length) return 0;
        return writeToStringBuffer(req.fallbacks[actualIndex]);
    }

    /**
     * Clear pending requests after JS has read them.
     */
    @Export(name = "drainPendingFetches")
    public static void drainPendingFetches() {
        QueuedNetProvider net = netProvider();
        if (net != null) net.drainPendingRequests();
    }

    // === Network delivery (JS delivers fetch results to WASM) ===

    @Export(name = "allocateNetBuffer")
    public static int allocateNetBuffer(int size) {
        netBuffer = new byte[size];
        return Address.ofData(netBuffer).toInt();
    }

    /**
     * Deliver a successful fetch result.
     * Data must already be written to netBuffer.
     * If the fetched URL is a cast file (.cct/.cst), the data is also
     * cached and parsed in CastLibManager so it's available immediately
     * when Lingo later sets castLib.fileName.
     */
    @Export(name = "deliverFetchResult")
    public static void deliverFetchResult(int taskId, int dataSize) {
        try {
            lastError = null;
            QueuedNetProvider net = netProvider();
            if (net == null || netBuffer == null) return;

            byte[] data = new byte[dataSize];
            System.arraycopy(netBuffer, 0, data, 0, dataSize);
            // onFetchComplete fires the fetchCompleteCallback which routes
            // cast files to Player.onNetFetchComplete → CastLibManager
            net.onFetchComplete(taskId, data);
        } catch (Throwable e) {
            captureError("deliverFetchResult", e);
        }
    }

    /**
     * Mark a fetch task as done without storing data in WASM.
     * Reports the byte count for Lingo's bytesSoFar check.
     * URL must be written to stringBuffer before calling.
     */
    @Export(name = "deliverFetchStatus")
    public static void deliverFetchStatus(int taskId, int urlLen, int byteCount) {
        try {
            lastError = null;
            QueuedNetProvider net = netProvider();
            if (net == null) return;

            String url = urlLen > 0 ? new String(stringBuffer, 0, urlLen) : null;
            log("fetchStatus: taskId=" + taskId + " url=" + url + " bytes=" + byteCount);

            // Mark the net task as done with byte count but no stored data
            net.onFetchStatusComplete(taskId, byteCount);
        } catch (Throwable e) {
            captureError("deliverFetchStatus", e);
        }
    }



    /**
     * Deliver a fetch error.
     */
    @Export(name = "deliverFetchError")
    public static void deliverFetchError(int taskId, int status) {
        try {
            lastError = null;
            QueuedNetProvider net = netProvider();
            if (net != null) {
                net.onFetchError(taskId, status);
            }
        } catch (Throwable e) {
            captureError("deliverFetchError", e);
        }
    }

    // === External parameters ===

    /**
     * Set an external parameter (Shockwave PARAM tag).
     * Key is at stringBuffer[0..keyLen), value at stringBuffer[keyLen..keyLen+valueLen).
     */
    @Export(name = "setExternalParam")
    public static void setExternalParam(int keyLen, int valueLen) {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return;
        String key = new String(stringBuffer, 0, keyLen);
        String value = new String(stringBuffer, keyLen, valueLen);
        Map<String, String> current = new LinkedHashMap<>(wasmPlayer.getPlayer().getExternalParams());
        current.put(key, value);
        wasmPlayer.getPlayer().setExternalParams(current);
    }

    @Export(name = "clearExternalParams")
    public static void clearExternalParams() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return;
        wasmPlayer.getPlayer().setExternalParams(null);
    }

    // === Error tracking ===

    /**
     * Get the last error message.
     * @return byte length written to stringBuffer, or 0 if no error
     */
    @Export(name = "getLastError")
    public static int getLastError() {
        if (lastError == null) return 0;
        byte[] bytes = lastError.getBytes();
        int len = Math.min(bytes.length, stringBuffer.length);
        System.arraycopy(bytes, 0, stringBuffer, 0, len);
        return len;
    }

    /**
     * Read accumulated debug log messages.
     * Clears the log after reading.
     * @return byte length written to stringBuffer, or 0 if log is empty
     */
    @Export(name = "getDebugLog")
    public static int getDebugLog() {
        if (debugLog.length() == 0) return 0;
        byte[] bytes = debugLog.toString().getBytes();
        int len = Math.min(bytes.length, stringBuffer.length);
        System.arraycopy(bytes, 0, stringBuffer, 0, len);
        debugLog.setLength(0);
        return len;
    }

    // === Input events ===

    /**
     * Update mouse position (stage coordinates).
     * Called by JS on mousemove.
     */
    @Export(name = "mouseMove")
    public static void mouseMove(int stageX, int stageY) {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return;
        wasmPlayer.getPlayer().getInputHandler().onMouseMove(stageX, stageY);
    }

    private static String lastDebugHitInfo = "";
    private static String lastDebugSpriteInfo = "";

    /**
     * Debug: dump star sprite state (channels 33-49).
     * Logs behavior instances, sprite state, and render sprite info.
     */
    @Export(name = "debugStarState")
    public static void debugStarState() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) { log("debugStarState: no player"); return; }
        var player = wasmPlayer.getPlayer();
        var registry = player.getStageRenderer().getSpriteRegistry();
        var behMgr = player.getFrameContext().getBehaviorManager();
        int frame = player.getCurrentFrame();
        StringBuilder sb = new StringBuilder();
        sb.append("STAR STATE frame=").append(frame).append('\n');
        int totalBeh = 0;
        for (int ch = 33; ch <= 49; ch++) {
            var state = registry.get(ch);
            var instances = behMgr.getInstancesForChannel(ch);
            totalBeh += instances.size();
            if (state != null || !instances.isEmpty()) {
                sb.append("  ch=").append(ch);
                if (state != null) {
                    sb.append(" loc=(").append(state.getLocH()).append(',').append(state.getLocV()).append(')')
                      .append(' ').append(state.getWidth()).append('x').append(state.getHeight())
                      .append(" puppet=").append(state.isPuppet())
                      .append(" dynMem=").append(state.hasDynamicMember())
                      .append(" eCL=").append(state.getEffectiveCastLib())
                      .append("/M").append(state.getEffectiveCastMember());
                } else {
                    sb.append(" NO_STATE");
                }
                if (!instances.isEmpty()) {
                    for (var inst : instances) {
                        sb.append(" beh=").append(inst.getProperties());
                    }
                }
                sb.append('\n');
            }
        }
        sb.append("  totalBehaviors=").append(totalBeh).append(" spriteRev=").append(registry.getRevision());
        log(sb.toString());
    }

    /**
     * Debug: dump info about sprites at the given stage coordinates.
     * Returns the hit sprite channel, or 0 if no hit.
     * Stores detailed info in lastDebugHitInfo (readable via getDebugHitInfo).
     */
    @Export(name = "debugHitTest")
    public static int debugHitTest(int stageX, int stageY) {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) { lastDebugHitInfo = "no player"; return -1; }
        var renderer = wasmPlayer.getPlayer().getStageRenderer();
        if (renderer == null) { lastDebugHitInfo = "no renderer"; return -2; }
        int frame = wasmPlayer.getPlayer().getCurrentFrame();

        var sprites = renderer.getLastBakedSprites();
        if (sprites == null || sprites.isEmpty()) {
            sprites = renderer.getSpritesForFrame(frame);
        }

        StringBuilder sb = new StringBuilder();
        sb.append("(").append(stageX).append(",").append(stageY)
          .append(") total=").append(sprites.size());

        // If stageX < 0, dump ALL sprites; otherwise only those at the point
        boolean dumpAll = (stageX < 0);

        int hitChannel = 0;
        for (int i = sprites.size() - 1; i >= 0; i--) {
            var sprite = sprites.get(i);
            if (!sprite.isVisible()) continue;
            if (sprite.getChannel() <= 0) continue;

            int left = sprite.getX();
            int top = sprite.getY();
            int right = left + sprite.getWidth();
            int bottom = top + sprite.getHeight();

            boolean overlaps = stageX >= left && stageX < right && stageY >= top && stageY < bottom;

            if (dumpAll || overlaps) {
                sb.append("|ch").append(sprite.getChannel())
                  .append(",i").append(sprite.getInkMode().code())
                  .append(",t").append(sprite.getType())
                  .append(",b").append(sprite.hasBehaviors() ? 1 : 0)
                  .append(",r").append(left).append(":").append(top)
                  .append(":").append(right).append(":").append(bottom);

                if (overlaps) {
                    // Check pixel transparency at click point
                    var baked = sprite.getBakedBitmap();
                    if (baked != null && baked.getPixels() != null) {
                        int localX = stageX - left;
                        int localY = stageY - top;
                        int bw = baked.getWidth(), bh = baked.getHeight();
                        int sw = sprite.getWidth(), sh = sprite.getHeight();
                        int bx = (sw > 0 && sw != bw) ? (localX * bw / sw) : localX;
                        int by = (sh > 0 && sh != bh) ? (localY * bh / sh) : localY;
                        if (bx >= 0 && bx < bw && by >= 0 && by < bh) {
                            int idx = by * bw + bx;
                            if (idx >= 0 && idx < baked.getPixels().length) {
                                int alpha = (baked.getPixels()[idx] >> 24) & 0xFF;
                                sb.append(",a").append(alpha);
                            }
                        }
                    } else {
                        sb.append(",noBmp");
                    }

                    if (hitChannel == 0) {
                        var ink = sprite.getInkMode();
                        if (ink == com.libreshockwave.id.InkMode.COPY
                            || sprite.getType() == com.libreshockwave.player.render.pipeline.RenderSprite.SpriteType.TEXT
                            || sprite.getType() == com.libreshockwave.player.render.pipeline.RenderSprite.SpriteType.BUTTON
                            || sprite.hasBehaviors()) {
                            hitChannel = sprite.getChannel();
                        }
                    }
                }
            }
        }
        sb.append("|hit=").append(hitChannel);
        lastDebugHitInfo = sb.toString();
        return hitChannel;
    }

    /**
     * Debug: dump sprite + member edit info for a specific channel.
     * Returns number of bytes written to stringBuffer.
     */
    @Export(name = "debugSpriteInfo")
    public static int debugSpriteInfo(int channel) {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) {
            return writeToStringBuffer("debugSpriteInfo: no player");
        }

        var player = wasmPlayer.getPlayer();
        var sprite = player.getStageRenderer().getSpriteRegistry().get(channel);
        if (sprite == null) {
            return writeToStringBuffer("debugSpriteInfo: no sprite at channel " + channel);
        }

        int castLib = sprite.getEffectiveCastLib();
        int memberNum = sprite.getEffectiveCastMember();
        var member = player.getCastLibManager().getDynamicMember(castLib, memberNum);

        StringBuilder sb = new StringBuilder();
        sb.append("ch=").append(channel)
          .append(" castLib=").append(castLib)
          .append(" member=").append(memberNum)
          .append(" visible=").append(sprite.isVisible())
          .append(" ink=").append(sprite.getInk())
          .append(" blend=").append(sprite.getBlend());

        if (member == null) {
            sb.append(" member=null");
            lastDebugSpriteInfo = sb.toString();
            return writeToStringBuffer(lastDebugSpriteInfo);
        }

        sb.append(" type=").append(member.getMemberType())
          .append(" editable=").append(member.isEditable())
          .append(" name=").append(member.getName());

        String text = member.getTextContent();
        if (text != null) {
            String sample = text.length() > 64 ? text.substring(0, 64) + "..." : text;
            sb.append(" text=\"").append(sample.replace('\n', ' ')).append("\"");
        }

        Datum color = member.getProp("color");
        sb.append(" color=").append(color);

        var bmp = member.renderTextToImage();
        if (bmp != null && bmp.getPixels() != null) {
            int nonTransparent = 0;
            int[] pixels = bmp.getPixels();
            for (int p : pixels) {
                if (((p >>> 24) & 0xFF) != 0) nonTransparent++;
            }
            sb.append(" bmp=").append(bmp.getWidth()).append("x").append(bmp.getHeight())
              .append(" nonTransparent=").append(nonTransparent).append("/").append(pixels.length);
        } else {
            sb.append(" bmp=null");
        }

        lastDebugSpriteInfo = sb.toString();
        return writeToStringBuffer(lastDebugSpriteInfo);
    }

    /**
     * Get the length of the last debug hit test info string.
     */
    @Export(name = "getDebugHitInfoLen")
    public static int getDebugHitInfoLen() {
        return lastDebugHitInfo.length();
    }

    /**
     * Copy the last debug hit test info to the string buffer.
     * Returns length of the string.
     */
    @Export(name = "getDebugHitInfo")
    public static int getDebugHitInfo() {
        byte[] bytes = lastDebugHitInfo.getBytes();
        int bufAddr = getStringBufferAddress();
        org.teavm.interop.Address addr = org.teavm.interop.Address.fromInt(bufAddr);
        for (int i = 0; i < bytes.length && i < 4096; i++) {
            addr.add(i).putByte(bytes[i]);
        }
        return bytes.length;
    }

    /**
     * Get the last event dispatch info (for debugging).
     * Returns the string length written to the string buffer.
     */
    @Export(name = "getLastDispatchInfo")
    public static int getLastDispatchInfo() {
        String info = com.libreshockwave.player.event.EventDispatcher.lastDispatchInfo;
        if (info == null || info.isEmpty()) return 0;
        byte[] bytes = info.getBytes();
        int bufAddr = getStringBufferAddress();
        org.teavm.interop.Address addr = org.teavm.interop.Address.fromInt(bufAddr);
        for (int i = 0; i < bytes.length && i < 4096; i++) {
            addr.add(i).putByte(bytes[i]);
        }
        return bytes.length;
    }

    /**
     * Handle mouse button press.
     * @param button 0=left, 2=right (matching JS MouseEvent.button)
     */
    @Export(name = "mouseDown")
    public static void mouseDown(int stageX, int stageY, int button) {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return;
        wasmPlayer.getPlayer().getInputHandler().onMouseDown(stageX, stageY, button == 2);
    }

    /**
     * Handle mouse button release.
     * @param button 0=left, 2=right (matching JS MouseEvent.button)
     */
    @Export(name = "mouseUp")
    public static void mouseUp(int stageX, int stageY, int button) {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return;
        wasmPlayer.getPlayer().getInputHandler().onMouseUp(stageX, stageY, button == 2);
    }

    /**
     * Handle browser/canvas focus loss.
     */
    @Export(name = "blur")
    public static void blur() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return;
        wasmPlayer.getPlayer().getInputHandler().onBlur();
    }

    /**
     * Handle key press.
     * @param browserKeyCode browser KeyboardEvent.keyCode
     * @param keyCharLen length of key character string in stringBuffer
     * @param modifiers bit flags: 1=shift, 2=ctrl, 4=alt
     */
    @Export(name = "keyDown")
    public static void keyDown(int browserKeyCode, int keyCharLen, int modifiers) {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return;
        String keyChar = keyCharLen > 0 ? new String(stringBuffer, 0, keyCharLen) : "";
        int directorCode = com.libreshockwave.player.input.DirectorKeyCodes.fromBrowserKeyCode(browserKeyCode);
        wasmPlayer.getPlayer().getInputHandler().onKeyDown(directorCode, keyChar,
                (modifiers & 1) != 0, (modifiers & 2) != 0, (modifiers & 4) != 0);
    }

    /**
     * Handle key release.
     * @param browserKeyCode browser KeyboardEvent.keyCode
     * @param keyCharLen length of key character string in stringBuffer
     * @param modifiers bit flags: 1=shift, 2=ctrl, 4=alt
     */
    @Export(name = "keyUp")
    public static void keyUp(int browserKeyCode, int keyCharLen, int modifiers) {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return;
        String keyChar = keyCharLen > 0 ? new String(stringBuffer, 0, keyCharLen) : "";
        int directorCode = com.libreshockwave.player.input.DirectorKeyCodes.fromBrowserKeyCode(browserKeyCode);
        wasmPlayer.getPlayer().getInputHandler().onKeyUp(directorCode, keyChar,
                (modifiers & 1) != 0, (modifiers & 2) != 0, (modifiers & 4) != 0);
    }

    // === Diagnostic exports ===

    /**
     * Get the number of active timeouts (for test diagnostics).
     */
    @Export(name = "getTimeoutCount")
    public static int getTimeoutCount() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return -1;
        return wasmPlayer.getPlayer().getTimeoutManager().getTimeoutCount();
    }

    /**
     * Get timeout names as comma-separated string, written to stringBuffer.
     * @return byte length written, or 0 if none
     */
    @Export(name = "getTimeoutNames")
    public static int getTimeoutNames() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return 0;
        var names = wasmPlayer.getPlayer().getTimeoutManager().getTimeoutNames();
        if (names.isEmpty()) return 0;
        byte[] bytes = String.join(",", names).getBytes();
        int len = Math.min(bytes.length, stringBuffer.length);
        System.arraycopy(bytes, 0, stringBuffer, 0, len);
        return len;
    }

    /**
     * Get player state name (STOPPED/PLAYING/PAUSED), written to stringBuffer.
     * @return byte length written
     */
    @Export(name = "getPlayerState")
    public static int getPlayerState() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return 0;
        byte[] bytes = wasmPlayer.getPlayer().getState().name().getBytes();
        int len = Math.min(bytes.length, stringBuffer.length);
        System.arraycopy(bytes, 0, stringBuffer, 0, len);
        return len;
    }

    /**
     * Get pending network request count (requests queued in QueuedNetProvider).
     */
    @Export(name = "getPendingNetCount")
    public static int getPendingNetCount() {
        if (wasmPlayer == null) return -1;
        QueuedNetProvider np = wasmPlayer.getNetProvider();
        return np != null ? np.getPendingRequests().size() : -1;
    }

    /**
     * Get the current Lingo call stack as a formatted string, written to stringBuffer.
     * Safe to call at any time (returns 0 when no handlers are executing).
     * @return byte length written to stringBuffer, or 0 if call stack is empty
     */
    @Export(name = "getCallStack")
    public static int getCallStack() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return 0;
        String stack = wasmPlayer.getPlayer().formatLingoCallStack();
        if (stack == null || stack.isEmpty()) return 0;
        byte[] bytes = stack.getBytes();
        int len = Math.min(bytes.length, stringBuffer.length);
        System.arraycopy(bytes, 0, stringBuffer, 0, len);
        return len;
    }

    // === Multiuser Xtra: JS polls pending requests, delivers events ===

    private static WasmMultiuserBridge musBridge() {
        return wasmPlayer != null ? wasmPlayer.getMusBridge() : null;
    }

    @Export(name = "getMusPendingCount")
    public static int getMusPendingCount() {
        WasmMultiuserBridge b = musBridge();
        return b != null ? b.getPendingRequests().size() : 0;
    }

    /** @return request type: 0=connect, 1=send, 2=disconnect */
    @Export(name = "getMusPendingType")
    public static int getMusPendingType(int index) {
        WasmMultiuserBridge b = musBridge();
        if (b == null) return -1;
        WasmMultiuserBridge.PendingRequest req = b.getRequest(index);
        return req != null ? req.type : -1;
    }

    @Export(name = "getMusPendingInstanceId")
    public static int getMusPendingInstanceId(int index) {
        WasmMultiuserBridge b = musBridge();
        if (b == null) return 0;
        WasmMultiuserBridge.PendingRequest req = b.getRequest(index);
        return req != null ? req.instanceId : 0;
    }

    /** Write host to stringBuffer. @return length */
    @Export(name = "getMusPendingHost")
    public static int getMusPendingHost(int index) {
        WasmMultiuserBridge b = musBridge();
        if (b == null) return 0;
        WasmMultiuserBridge.PendingRequest req = b.getRequest(index);
        return req != null ? writeToStringBuffer(req.host) : 0;
    }

    @Export(name = "getMusPendingPort")
    public static int getMusPendingPort(int index) {
        WasmMultiuserBridge b = musBridge();
        if (b == null) return 0;
        WasmMultiuserBridge.PendingRequest req = b.getRequest(index);
        return req != null ? req.port : 0;
    }

    /** Write send data (raw content) to stringBuffer. @return length */
    @Export(name = "getMusPendingSendData")
    public static int getMusPendingSendData(int index) {
        WasmMultiuserBridge b = musBridge();
        if (b == null) return 0;
        WasmMultiuserBridge.PendingRequest req = b.getRequest(index);
        if (req == null || req.type != WasmMultiuserBridge.REQ_SEND) return 0;
        return writeToStringBuffer(req.content);
    }

    @Export(name = "drainMusPending")
    public static void drainMusPending() {
        WasmMultiuserBridge b = musBridge();
        if (b != null) b.drainPendingRequests();
    }

    /** JS calls this when a WebSocket connection is established. */
    @Export(name = "musDeliverConnected")
    public static void musDeliverConnected(int instanceId) {
        WasmMultiuserBridge b = musBridge();
        if (b != null) b.notifyConnected(instanceId);
    }

    /** JS calls this when a WebSocket is closed. */
    @Export(name = "musDeliverDisconnected")
    public static void musDeliverDisconnected(int instanceId) {
        WasmMultiuserBridge b = musBridge();
        if (b != null) b.notifyDisconnected(instanceId);
    }

    /** JS calls this on WebSocket error. */
    @Export(name = "musDeliverError")
    public static void musDeliverError(int instanceId, int errorCode) {
        WasmMultiuserBridge b = musBridge();
        if (b != null) b.notifyError(instanceId, errorCode);
    }

    /**
     * JS calls this when a message arrives on a WebSocket.
     * The raw message content is in stringBuffer; delivered as content with default fields.
     */
    @Export(name = "musDeliverMessage")
    public static void musDeliverMessage(int instanceId, int dataLen) {
        WasmMultiuserBridge b = musBridge();
        if (b == null) return;
        try {
            String data = new String(stringBuffer, 0, dataLen);
            b.deliverMessage(instanceId, 0, "", "", data);
        } catch (Throwable e) {
            captureError("musDeliverMessage", e);
        }
    }

    // === Test/debug exports ===

    /**
     * Trigger a test Lingo error to exercise the movie's alertHook error dialog.
     * Fires the VM's alertHook with a test error message.
     * @return 1 if alertHook was found and invoked, 0 otherwise
     */
    @Export(name = "triggerTestError")
    public static int triggerTestError() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return 0;
        try {
            boolean handled = wasmPlayer.getPlayer().fireTestError(
                    "Script error: Test error triggered for dialog appearance check");
            log("[triggerTestError] alertHook fired, handled=" + handled);
            return handled ? 1 : 0;
        } catch (Throwable e) {
            captureError("triggerTestError", e);
            return 0;
        }
    }

    // === Audio command queue (for Web Audio API playback from JS main thread) ===

    private static byte[] audioBuffer;

    @Export(name = "getAudioPendingCount")
    public static int getAudioPendingCount() {
        if (wasmPlayer == null || wasmPlayer.getAudioBackend() == null) return 0;
        return wasmPlayer.getAudioBackend().getPendingCount();
    }

    /**
     * Get the action for the pending sound command at index.
     * Returns string in stringBuffer: "play", "stop", "volume"
     */
    @Export(name = "getAudioPendingAction")
    public static int getAudioPendingAction(int index) {
        if (wasmPlayer == null || wasmPlayer.getAudioBackend() == null) return 0;
        WasmAudioBackend.SoundCommand cmd = wasmPlayer.getAudioBackend().getPending(index);
        if (cmd == null) return 0;
        return writeToStringBuffer(cmd.action());
    }

    @Export(name = "getAudioPendingChannel")
    public static int getAudioPendingChannel(int index) {
        if (wasmPlayer == null || wasmPlayer.getAudioBackend() == null) return 0;
        WasmAudioBackend.SoundCommand cmd = wasmPlayer.getAudioBackend().getPending(index);
        return cmd != null ? cmd.channelNum() : 0;
    }

    @Export(name = "getAudioPendingFormat")
    public static int getAudioPendingFormat(int index) {
        if (wasmPlayer == null || wasmPlayer.getAudioBackend() == null) return 0;
        WasmAudioBackend.SoundCommand cmd = wasmPlayer.getAudioBackend().getPending(index);
        if (cmd == null || cmd.format() == null) return 0;
        return writeToStringBuffer(cmd.format());
    }

    @Export(name = "getAudioPendingLoopCount")
    public static int getAudioPendingLoopCount(int index) {
        if (wasmPlayer == null || wasmPlayer.getAudioBackend() == null) return 0;
        WasmAudioBackend.SoundCommand cmd = wasmPlayer.getAudioBackend().getPending(index);
        return cmd != null ? cmd.loopCount() : 0;
    }

    @Export(name = "getAudioPendingVolume")
    public static int getAudioPendingVolume(int index) {
        if (wasmPlayer == null || wasmPlayer.getAudioBackend() == null) return 0;
        WasmAudioBackend.SoundCommand cmd = wasmPlayer.getAudioBackend().getPending(index);
        return cmd != null ? cmd.volume() : 0;
    }

    /**
     * Get the audio data for a pending play command.
     * Copies to audioBuffer and returns the length. JS reads from audioBuffer address.
     */
    @Export(name = "getAudioPendingData")
    public static int getAudioPendingData(int index) {
        if (wasmPlayer == null || wasmPlayer.getAudioBackend() == null) return 0;
        WasmAudioBackend.SoundCommand cmd = wasmPlayer.getAudioBackend().getPending(index);
        if (cmd == null || cmd.audioData() == null) return 0;
        byte[] data = cmd.audioData();
        // Allocate/grow buffer if needed
        if (audioBuffer == null || audioBuffer.length < data.length) {
            audioBuffer = new byte[data.length];
        }
        System.arraycopy(data, 0, audioBuffer, 0, data.length);
        return data.length;
    }

    @Export(name = "getAudioBufferAddress")
    public static int getAudioBufferAddress() {
        if (audioBuffer == null) return 0;
        return Address.ofData(audioBuffer).toInt();
    }

    @Export(name = "drainAudioPending")
    public static void drainAudioPending() {
        if (wasmPlayer != null && wasmPlayer.getAudioBackend() != null) {
            wasmPlayer.getAudioBackend().drainPending();
        }
    }

    @Export(name = "audioNotifyStopped")
    public static void audioNotifyStopped(int channelNum) {
        if (wasmPlayer != null && wasmPlayer.getAudioBackend() != null) {
            wasmPlayer.getAudioBackend().notifyStopped(channelNum);
        }
    }

    // === Internal helpers ===

    private static void captureError(String context, Throwable e) {
        StringBuilder sb = new StringBuilder();
        sb.append("[").append(context).append("] ").append(e.getClass().getName());
        if (e.getMessage() != null) {
            sb.append(": ").append(e.getMessage());
        }
        Throwable cause = e.getCause();
        int depth = 0;
        while (cause != null && depth < 5) {
            sb.append(" <- ").append(cause.getClass().getName());
            if (cause.getMessage() != null) {
                sb.append(": ").append(cause.getMessage());
            }
            cause = cause.getCause();
            depth++;
        }
        lastError = sb.toString();
    }

    private static QueuedNetProvider netProvider() {
        return wasmPlayer != null ? wasmPlayer.getNetProvider() : null;
    }

    private static int writeToStringBuffer(String s) {
        if (s == null || s.isEmpty()) return 0;
        byte[] bytes = s.getBytes();
        int len = Math.min(bytes.length, stringBuffer.length);
        System.arraycopy(bytes, 0, stringBuffer, 0, len);
        return len;
    }

}
