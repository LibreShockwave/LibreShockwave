package com.libreshockwave.player.wasm;

import org.teavm.interop.Address;
import org.teavm.interop.Export;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.cast.TextInfo;
import com.libreshockwave.player.cast.CastMember;
import com.libreshockwave.player.cast.CastLib;
import com.libreshockwave.player.render.pipeline.RenderSprite;
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
    private static final Queue<String> pendingGotoNetMovies = new ArrayDeque<>();
    private static int nextGotoNetMovieRequestId = 1;

    private static final Set<String> failedCasts = new HashSet<>();

    // Debug log: accumulates messages; read via getDebugLog() export
    static final StringBuilder debugLog = new StringBuilder(1024);

    private static boolean isDebugLoggingEnabled() {
        return DebugConfig.isDebugPlaybackEnabled();
    }

    private static void appendDebug(String msg) {
        if (!isDebugLoggingEnabled() || msg == null || msg.isEmpty()) {
            return;
        }
        debugLog.append(msg);
    }

    /** Append a timestamped debug message (accessible from player-wasm package). */
    static void log(String msg) {
        if (!isDebugLoggingEnabled() || msg == null || msg.isEmpty()) {
            return;
        }
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

    static void enqueueGotoNetMovie(String url) {
        synchronized (pendingGotoNetMovies) {
            pendingGotoNetMovies.offer(url != null ? url : "");
        }
    }

    public static void main(String[] args) {
        DirectorFile.setJpegDecoder(WasmJpegDecoder::decode);

        // Replace System.out/err with non-synchronized PrintStream.
        // Java's PrintStream uses synchronized(this) on every println() call,
        // which triggers ClassCastException in TeaVM WASM's monitorEnterSync.
        PrintStream unsync = new PrintStream(new OutputStream() {
            @Override public void write(int b) { }
            @Override public void write(byte[] b, int off, int len) { }
        }) {
            @Override public void println(String x) { log(x); }
            @Override public void print(String x) { appendDebug(x); }
            @Override public void println(Object x) { log(String.valueOf(x)); }
            @Override public void println() { appendDebug("\n"); }
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

    @Export(name = "getStringBufferCapacity")
    public static int getStringBufferCapacity() {
        return stringBuffer.length;
    }

    @Export(name = "setInitialBuiltinSymbol")
    public static void setInitialBuiltinSymbol(int keyLen, int valueLen) {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null || keyLen <= 0 || valueLen <= 0) return;
        String key = new String(stringBuffer, 0, keyLen, StandardCharsets.UTF_8);
        String value = new String(stringBuffer, keyLen, valueLen, StandardCharsets.UTF_8);
        wasmPlayer.getPlayer().setInitialBuiltinVariable(key, Datum.symbol(value));
    }

    @Export(name = "setMovieProperty")
    public static void setMovieProperty(int keyLen, int valueLen) {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null || keyLen <= 0) return;
        String key = new String(stringBuffer, 0, keyLen, StandardCharsets.UTF_8);
        String value = new String(stringBuffer, keyLen, Math.max(0, valueLen), StandardCharsets.UTF_8);
        wasmPlayer.getPlayer().getMovieProperties().setMovieProp(key, Datum.of(value));
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

    @Export(name = "readNextGotoNetMovie")
    public static int readNextGotoNetMovie() {
        String next;
        synchronized (pendingGotoNetMovies) {
            next = pendingGotoNetMovies.poll();
        }
        if (next == null) {
            return 0;
        }

        byte[] urlBytes = next.getBytes(StandardCharsets.UTF_8);
        int len = Math.min(urlBytes.length, stringBuffer.length);
        System.arraycopy(urlBytes, 0, stringBuffer, 0, len);
        return len;
    }

    // === Movie loading ===

    /**
     * Load a movie from the movie buffer.
     * basePath must already be written to stringBuffer.
     * @return (width << 16) | height, or 0 on failure
     */
    @Export(name = "loadMovie")
    public static int loadMovie(int movieSize, int basePathLen) {
        DirectorFile.setJpegDecoder(WasmJpegDecoder::decode);
        WasmJpegDecoder.reset();

        String basePath = "";
        if (basePathLen > 0) {
            basePath = new String(stringBuffer, 0, basePathLen);
        }

        byte[] data = new byte[movieSize];
        System.arraycopy(movieBuffer, 0, data, 0, movieSize);

        if (wasmPlayer != null) {
            if (wasmPlayer.getNetProvider() != null) {
                wasmPlayer.getNetProvider().completeMovieNavigationTasks();
            }
            wasmPlayer.shutdown();
        }
        synchronized (pendingGotoNetPages) {
            pendingGotoNetPages.clear();
        }
        synchronized (pendingGotoNetMovies) {
            pendingGotoNetMovies.clear();
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
                            if (wasmPlayer.getPlayer().loadExternalCastFromCachedData(
                                    castLibNumber,
                                    cached,
                                    wasmPlayer::bumpCastRevision)) {
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
            // Set step limit to catch infinite loops. Large startup handlers in
            // real clients can legitimately do multi-megabyte text conversion,
            // so keep enough headroom for those while still bounding runaway code.
            if (wasmPlayer.getPlayer() != null) {
                wasmPlayer.getPlayer().getVM().setStepLimit(50_000_000);
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

    @Export(name = "getPendingJpegDecodeCount")
    public static int getPendingJpegDecodeCount() {
        return WasmJpegDecoder.pendingCount();
    }

    @Export(name = "getPendingJpegDecodeId")
    public static int getPendingJpegDecodeId(int index) {
        return WasmJpegDecoder.pendingId(index);
    }

    @Export(name = "getPendingJpegDecodeData")
    public static int getPendingJpegDecodeData(int id) {
        return WasmJpegDecoder.prepareData(id);
    }

    @Export(name = "getPendingJpegDecodeDataAddress")
    public static int getPendingJpegDecodeDataAddress() {
        byte[] data = WasmJpegDecoder.currentData();
        return data != null ? Address.ofData(data).toInt() : 0;
    }

    @Export(name = "deliverJpegDecodeResult")
    public static void deliverJpegDecodeResult(int id, int width, int height, int dataLen) {
        byte[] rgba = new byte[Math.max(0, dataLen)];
        if (netBuffer != null && dataLen > 0) {
            System.arraycopy(netBuffer, 0, rgba, 0, Math.min(dataLen, netBuffer.length));
        }
        WasmJpegDecoder.deliverDecoded(id, width, height, rgba);
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
        lastError = null;
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
        byte[] bytes = debugLog.toString().getBytes(StandardCharsets.UTF_8);
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
     * Dump baked sprites intersecting the main window region for pixel-match diagnostics.
     */
    @Export(name = "getWindowSpriteDiagnostics")
    public static int getWindowSpriteDiagnostics() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return 0;
        var renderer = wasmPlayer.getPlayer().getStageRenderer();
        if (renderer == null || renderer.getLastBakedSprites() == null) return 0;

        StringBuilder sb = new StringBuilder(32768);
        var file = wasmPlayer.getPlayer().getFile();
        if (file != null && file.getConfig() != null) {
            sb.append("movieColorDepth=").append(file.getConfig().bgColor())
                    .append(" stageColor=").append(Integer.toHexString(file.getConfig().stageColorRGB() & 0xFFFFFF))
                    .append('\n');
        }
        appendAsianCatalogueProbe(sb);
        for (RenderSprite sprite : renderer.getLastBakedSprites()) {
            if (!intersects(sprite.getX(), sprite.getY(), sprite.getWidth(), sprite.getHeight(),
                    40, 0, 930, 500)) {
                continue;
            }
            Bitmap baked = sprite.getBakedBitmap();
            int white = 0;
            int f0 = 0;
            int black = 0;
            int transparent = 0;
            int translucent = 0;
            int minAlpha = 255;
            int maxAlpha = 0;
            int first = 0;
            int bw = 0;
            int bh = 0;
            if (baked != null) {
                bw = baked.getWidth();
                bh = baked.getHeight();
                int[] pixels = baked.getPixels();
                if (pixels != null && pixels.length > 0) {
                    first = pixels[0];
                    for (int pixel : pixels) {
                        int alpha = (pixel >>> 24) & 0xFF;
                        int rgb = pixel & 0xFFFFFF;
                        minAlpha = Math.min(minAlpha, alpha);
                        maxAlpha = Math.max(maxAlpha, alpha);
                        if (alpha == 0) {
                            transparent++;
                        } else if (alpha < 255) {
                            translucent++;
                        } else if (rgb == 0xFFFFFF) {
                            white++;
                        } else if (rgb == 0xF0F0F0) {
                            f0++;
                        } else if (rgb == 0) {
                            black++;
                        }
                    }
                }
            }
            CastMemberChunk cast = sprite.getCastMember();
            CastMember dyn = sprite.getDynamicMember();
            Bitmap dynBitmap = dyn != null ? dyn.getBitmap() : null;
            sb.append("ch=").append(sprite.getChannel())
                    .append(" z=").append(sprite.getLocZ())
                    .append(" loc=").append(sprite.getX()).append(',').append(sprite.getY())
                    .append(' ').append(sprite.getWidth()).append('x').append(sprite.getHeight())
                    .append(" type=").append(sprite.getType())
                    .append(" ink=").append(sprite.getInk())
                    .append(" blend=").append(sprite.getBlend())
                    .append(" back=").append(Integer.toHexString(sprite.getBackColor() & 0xFFFFFF))
                    .append(" dyn=").append(sprite.getDynamicMember() != null)
                    .append(" member=").append(sprite.getMemberName())
                    .append(" castName=").append(cast != null ? cast.name() : "")
                    .append(" castId=").append(cast != null ? cast.id().value() : -1)
                    .append(" dynName=").append(dyn != null ? dyn.getName() : "")
                    .append(" dynNum=").append(dyn != null ? dyn.getMemberNumber() : -1)
                    .append(" dynScript=").append(dynBitmap != null && dynBitmap.isScriptModified())
                    .append(" dynBmp=").append(dynBitmap != null ? dynBitmap.getWidth() : 0)
                    .append('x').append(dynBitmap != null ? dynBitmap.getHeight() : 0)
                    .append(" dynDepth=").append(dynBitmap != null ? dynBitmap.getBitDepth() : 0)
                    .append(" dynPal=").append(dynBitmap != null && dynBitmap.getImagePalette() != null
                            ? dynBitmap.getImagePalette().getName() : "")
                    .append(" dynPalRef=").append(dynBitmap != null ? paletteRefSummary(dynBitmap) : "")
                    .append(" dynFirst=").append(dynBitmap != null && dynBitmap.getPixels().length > 0
                            ? Integer.toHexString(dynBitmap.getPixels()[0]) : "0")
                    .append(" baked=").append(bw).append('x').append(bh)
                    .append(" first=").append(Integer.toHexString(first))
                    .append(" alpha=").append(minAlpha).append('-').append(maxAlpha)
                    .append(" translucent=").append(translucent)
                    .append(" white=").append(white)
                    .append(" f0=").append(f0)
                    .append(" black=").append(black)
                    .append(" transparent=").append(transparent)
                    .append('\n');
            appendCatalogueTextProbe(sb, dyn);
            appendCatalogueBitmapBounds(sb, dyn, dynBitmap);
            appendCataloguePixelSamples(sb, sprite, dyn, dynBitmap, baked);
        }
        byte[] bytes = sb.toString().getBytes(StandardCharsets.UTF_8);
        int len = Math.min(bytes.length, stringBuffer.length);
        System.arraycopy(bytes, 0, stringBuffer, 0, len);
        return len;
    }

    /**
     * Dump visible text-like sprite contents for browser visual tests.
     */
    @Export(name = "getVisibleTextDiagnostics")
    public static int getVisibleTextDiagnostics() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return 0;
        var renderer = wasmPlayer.getPlayer().getStageRenderer();
        if (renderer == null || renderer.getLastBakedSprites() == null) return 0;

        StringBuilder sb = new StringBuilder(32768);
        for (RenderSprite sprite : renderer.getLastBakedSprites()) {
            CastMemberChunk cast = sprite.getCastMember();
            CastMember dyn = sprite.getDynamicMember();
            sb.append("ch=").append(sprite.getChannel())
                    .append(" loc=").append(sprite.getX()).append(',').append(sprite.getY())
                    .append(' ').append(sprite.getWidth()).append('x').append(sprite.getHeight())
                    .append(" type=").append(sprite.getType())
                    .append(" ink=").append(sprite.getInk())
                    .append(" fore=").append(sprite.getForeColor())
                    .append(" back=").append(sprite.getBackColor())
                    .append(" hasFore=").append(sprite.hasForeColor())
                    .append(" hasBack=").append(sprite.hasBackColor())
                    .append(" member=").append(sprite.getMemberName())
                    .append(" castName=").append(cast != null ? cast.name() : "")
                    .append(" dynName=").append(dyn != null ? dyn.getName() : "")
                    .append(" dynType=").append(dyn != null ? dyn.getMemberType() : "")
                    .append('\n');
            appendStaticTextProbe(sb, cast);
            String text = dyn != null ? dyn.getTextContent() : null;
            if (text == null || text.isEmpty()) {
                continue;
            }
            sb.append("  text=\"").append(escapeDiagnosticText(text)).append('"')
                    .append('\n');
        }
        byte[] bytes = sb.toString().getBytes(StandardCharsets.UTF_8);
        int len = Math.min(bytes.length, stringBuffer.length);
        System.arraycopy(bytes, 0, stringBuffer, 0, len);
        return len;
    }

    private static void appendStaticTextProbe(StringBuilder sb, CastMemberChunk cast) {
        if (cast == null || (!cast.isText() && !cast.isTextXtra())) {
            return;
        }
        DirectorFile file = cast.file();
        if (file == null) {
            return;
        }

        TextInfo info = TextInfo.parse(cast.specificData());
        sb.append("  textInfo align=").append(info.textAlign())
                .append(" size=").append(info.width()).append('x').append(info.height())
                .append(" bg=").append(info.bgRed()).append(',')
                .append(info.bgGreen()).append(',').append(info.bgBlue())
                .append(" wrap=").append(info.isWordWrap())
                .append(" specificLen=").append(cast.specificData() != null ? cast.specificData().length : 0)
                .append(" specificHead=").append(hexHead(cast.specificData(), 24))
                .append('\n');

        var xmed = file.getXmedStyledTextForMember(cast);
        if (xmed != null) {
            sb.append("  xmed text=\"").append(escapeDiagnosticText(xmed.text())).append('"')
                    .append(" font=").append(xmed.fontName())
                    .append(" size=").append(xmed.fontSize())
                    .append(" align=").append(xmed.alignment())
                    .append(" rect=").append(xmed.width()).append('x').append(xmed.height())
                    .append(" color=").append(xmed.colorR()).append(',')
                    .append(xmed.colorG()).append(',').append(xmed.colorB())
                    .append(" aa=").append(xmed.antialias()).append('/').append(xmed.antiAliasThreshold())
                    .append(" bold=").append(xmed.memberBold())
                    .append('\n');
        }

        var textChunk = file.getTextForMember(cast);
        if (textChunk == null) {
            return;
        }
        sb.append("  stxt text=\"").append(escapeDiagnosticText(textChunk.text())).append('"')
                .append(" runs=").append(textChunk.runs().size())
                .append('\n');
        int runIndex = 0;
        for (var run : textChunk.runs()) {
            if (runIndex >= 4) {
                break;
            }
            sb.append("    run").append(runIndex)
                    .append(" start=").append(run.startOffset())
                    .append(" end=").append(run.endOffset())
                    .append(" fontId=").append(run.fontId())
                    .append(" size=").append(run.fontSize())
                    .append(" style=").append(run.fontStyle())
                    .append(" color=").append(run.colorR()).append(',')
                    .append(run.colorG()).append(',').append(run.colorB())
                    .append('\n');
            runIndex++;
        }
    }

    private static String hexHead(byte[] data, int maxBytes) {
        if (data == null || data.length == 0) {
            return "";
        }
        StringBuilder sb = new StringBuilder(Math.min(data.length, maxBytes) * 2);
        int limit = Math.min(data.length, maxBytes);
        for (int i = 0; i < limit; i++) {
            int value = data[i] & 0xFF;
            if (value < 0x10) {
                sb.append('0');
            }
            sb.append(Integer.toHexString(value));
        }
        return sb.toString();
    }

    /**
     * Dump startup state that explains why Habbo component threads did or did not initialize.
     */
    @Export(name = "getBootstrapDiagnostics")
    public static int getBootstrapDiagnostics() {
        if (wasmPlayer == null || wasmPlayer.getPlayer() == null) return 0;
        StringBuilder sb = new StringBuilder(32768);
        var player = wasmPlayer.getPlayer();
        var castManager = player.getCastLibManager();

        sb.append("state=").append(player.getState())
                .append(" frame=").append(player.getCurrentFrame())
                .append(" casts=").append(castManager.getCastLibCount())
                .append('\n');
        String systemProps = castManager.getFieldValue("System Props", 0);
        String threadIndexField = findVariableValue(systemProps, "thread.index.field");
        sb.append("thread.index.field=").append(threadIndexField).append('\n');
        appendTruncated(sb, "System Props", systemProps, 3000);

        for (int i = 1; i <= castManager.getCastLibCount(); i++) {
            CastLib castLib = castManager.getCastLib(i);
            if (castLib == null) {
                continue;
            }
            sb.append("cast#").append(i)
                    .append(" name=").append(castLib.getName())
                    .append(" file=").append(castLib.getFileName())
                    .append(" loaded=").append(castLib.isLoaded())
                    .append(" fetched=").append(castLib.isFetched())
                    .append(" members=").append(safeMemberCount(castLib))
                    .append('\n');
            if (!threadIndexField.isEmpty()) {
                String threadIndex = castManager.getFieldValue(threadIndexField, i);
                if (!threadIndex.isEmpty()) {
                    appendTruncated(sb, "cast#" + i + " " + threadIndexField, threadIndex, 3000);
                }
            }
        }

        QueuedNetProvider netProvider = wasmPlayer.getNetProvider();
        if (netProvider != null) {
            appendTruncated(sb, "netProvider", netProvider.getDebugStatus(), 8000);
        }

        if (!player.getVM().getGlobals().isEmpty()) {
            sb.append("globals:\n");
            int count = 0;
            for (var entry : player.getVM().getGlobals().entrySet()) {
                if (count++ >= 80) {
                    sb.append("  ...\n");
                    break;
                }
                sb.append("  ").append(entry.getKey()).append('=').append(entry.getValue()).append('\n');
                if (entry.getValue() instanceof Datum.ScriptInstance instance && !instance.properties().isEmpty()) {
                    for (var prop : instance.properties().entrySet()) {
                        sb.append("    .").append(prop.getKey()).append('=').append(prop.getValue()).append('\n');
                    }
                }
            }
        }

        byte[] bytes = sb.toString().getBytes(StandardCharsets.UTF_8);
        int len = Math.min(bytes.length, stringBuffer.length);
        System.arraycopy(bytes, 0, stringBuffer, 0, len);
        return len;
    }

    private static int safeMemberCount(CastLib castLib) {
        try {
            return castLib.getMemberCount();
        } catch (Throwable ignored) {
            return -1;
        }
    }

    private static void appendTruncated(StringBuilder sb, String title, String value, int maxChars) {
        if (value == null || value.isEmpty()) {
            return;
        }
        sb.append(title).append(":\n");
        String normalized = value.replace('\r', '\n');
        if (normalized.length() > maxChars) {
            sb.append(normalized, 0, maxChars).append("\n...\n");
        } else {
            sb.append(normalized).append('\n');
        }
    }

    private static String findVariableValue(String text, String key) {
        if (text == null || text.isEmpty() || key == null || key.isEmpty()) {
            return "";
        }
        String[] lines = text.replace('\r', '\n').split("\n");
        for (String line : lines) {
            String trimmed = line.trim();
            if (trimmed.startsWith("#")) {
                continue;
            }
            int eq = trimmed.indexOf('=');
            if (eq <= 0) {
                continue;
            }
            if (trimmed.substring(0, eq).trim().equalsIgnoreCase(key)) {
                return trimmed.substring(eq + 1).trim();
            }
        }
        return "";
    }

    private static String escapeDiagnosticText(String text) {
        return text.replace("\\", "\\\\")
                .replace("\r", "\\r")
                .replace("\n", "\\n")
                .replace("\"", "\\\"");
    }

    private static void appendCatalogueBitmapBounds(StringBuilder sb, CastMember dyn, Bitmap bitmap) {
        if (dyn == null || bitmap == null || dyn.getName() == null) {
            return;
        }
        String name = dyn.getName();
        if (!name.startsWith("Catalogue_catalog_")
                && !name.equals("Catalogue_ctlg_pages")
                && !name.equals("Catalogue_ctlg_productstrip")
                && !name.equals("Catalogue_ctlg_header_text")
                && !name.equals("Catalogue_ctlg_header_img")) {
            return;
        }

        int minX = bitmap.getWidth();
        int minY = bitmap.getHeight();
        int maxX = -1;
        int maxY = -1;
        int opaque = 0;
        int translucent = 0;
        int black = 0;
        int nonWhite = 0;
        for (int y = 0; y < bitmap.getHeight(); y++) {
            for (int x = 0; x < bitmap.getWidth(); x++) {
                int pixel = bitmap.getPixel(x, y);
                int alpha = (pixel >>> 24) & 0xFF;
                int rgb = pixel & 0xFFFFFF;
                if (alpha == 0) {
                    continue;
                }
                if (alpha == 255) {
                    opaque++;
                } else {
                    translucent++;
                }
                if (rgb == 0) {
                    black++;
                }
                if (rgb != 0xFFFFFF) {
                    nonWhite++;
                    minX = Math.min(minX, x);
                    minY = Math.min(minY, y);
                    maxX = Math.max(maxX, x);
                    maxY = Math.max(maxY, y);
                }
            }
        }
        sb.append("bounds ").append(name)
                .append(" nonWhite=").append(nonWhite)
                .append(" black=").append(black)
                .append(" opaque=").append(opaque)
                .append(" translucent=").append(translucent)
                .append(" box=");
        if (maxX >= 0) {
            sb.append(minX).append(',').append(minY).append('-').append(maxX).append(',').append(maxY);
        } else {
            sb.append("empty");
        }
        sb.append('\n');
    }

    private static void appendCatalogueTextProbe(StringBuilder sb, CastMember dyn) {
        if (dyn == null || dyn.getName() == null) {
            return;
        }
        String name = dyn.getName();
        if (!name.equals("Catalogue_ctlg_header_text")
                && !name.equals("Catalogue_ctlg_description")
                && !name.equals("Catalogue_ctlg_selectproduct")
                && !name.startsWith("Catalogue_catalog_")) {
            return;
        }
        String text = dyn.getTextContent();
        if (text == null || text.isEmpty()) {
            return;
        }
        sb.append("text ").append(name)
                .append(" font=").append(dyn.getProp("font"))
                .append(" size=").append(dyn.getProp("fontSize"))
                .append(" style=").append(dyn.getProp("fontStyle"))
                .append(" wrap=").append(dyn.getProp("wordWrap"))
                .append(" fixedLineSpace=").append(dyn.getProp("fixedLineSpace"))
                .append(" topSpacing=").append(dyn.getProp("topSpacing"))
                .append(" len=").append(text.length())
                .append(" value=\"").append(text.replace("\r", "\\r").replace("\n", "\\n")).append('"')
                .append('\n');
    }

    private static void appendCataloguePixelSamples(StringBuilder sb, RenderSprite sprite, CastMember dyn,
                                                    Bitmap dynBitmap, Bitmap baked) {
        if (sprite == null || dyn == null || dyn.getName() == null) {
            return;
        }
        String name = dyn.getName();
        if (!name.startsWith("Catalogue_")) {
            return;
        }
        int[][] points = {
                {523, 36}, {524, 37}, {315, 51}, {235, 117},
                {241, 195}, {405, 203}, {360, 455}
        };
        boolean wroteHeader = false;
        for (int[] point : points) {
            int x = point[0];
            int y = point[1];
            if (x < sprite.getX() || y < sprite.getY()
                    || x >= sprite.getX() + sprite.getWidth()
                    || y >= sprite.getY() + sprite.getHeight()) {
                continue;
            }
            if (!wroteHeader) {
                sb.append("samples ").append(name)
                        .append(" loc=").append(sprite.getX()).append(',').append(sprite.getY())
                        .append(" size=").append(sprite.getWidth()).append('x').append(sprite.getHeight())
                        .append(" ink=").append(sprite.getInk()).append(" blend=").append(sprite.getBlend());
                wroteHeader = true;
            }
            int lx = x - sprite.getX();
            int ly = y - sprite.getY();
            int dynPixel = dynBitmap != null && lx >= 0 && ly >= 0
                    && lx < dynBitmap.getWidth() && ly < dynBitmap.getHeight()
                    ? dynBitmap.getPixel(lx, ly) : 0;
            int bakedPixel = baked != null && lx >= 0 && ly >= 0
                    && lx < baked.getWidth() && ly < baked.getHeight()
                    ? baked.getPixel(lx, ly) : 0;
            sb.append(" p").append(x).append(',').append(y)
                    .append(" dyn=").append(Integer.toHexString(dynPixel))
                    .append(" baked=").append(Integer.toHexString(bakedPixel));
        }
        if (wroteHeader) {
            sb.append('\n');
        }
    }

    private static void appendAsianCatalogueProbe(StringBuilder sb) {
        try {
            var metallic = com.libreshockwave.bitmap.Palette.getBuiltIn(
                    com.libreshockwave.bitmap.Palette.METALLIC);
            sb.append("probe metallic64=")
                    .append(Integer.toHexString(metallic.getColor(64) & 0xFFFFFF))
                    .append(" metallic110=")
                    .append(Integer.toHexString(metallic.getColor(110) & 0xFFFFFF))
                    .append('\n');

            var castLibManager = wasmPlayer.getPlayer().getCastLibManager();
            CastMember member = castLibManager != null
                    ? castLibManager.findCastMemberByName("cn_sofa_small")
                    : null;
            Bitmap bitmap = member != null ? member.getBitmap() : null;
            CastMemberChunk chunk = member != null ? member.getChunk() : null;
            DirectorFile sourceFile = chunk != null ? chunk.file() : null;
            com.libreshockwave.cast.BitmapInfo info = chunk != null
                    ? com.libreshockwave.cast.BitmapInfo.parse(chunk)
                    : null;
            var resolvedPalette = sourceFile != null && info != null
                    ? sourceFile.resolvePalette(info.paletteId())
                    : null;
            sb.append("probe cn_sofa_small member=")
                    .append(member != null ? member.getCastLibNumber() : -1)
                    .append(':')
                    .append(member != null ? member.getMemberNumber() : -1)
                    .append(" dir=")
                    .append(sourceFile != null && sourceFile.getConfig() != null
                            ? sourceFile.getConfig().directorVersion() : -1)
                    .append(" infoPal=")
                    .append(info != null ? info.paletteId() : 0)
                    .append(" resolved=")
                    .append(resolvedPalette != null ? resolvedPalette.getName() : "")
                    .append(" pal=")
                    .append(bitmap != null && bitmap.getImagePalette() != null
                            ? bitmap.getImagePalette().getName() : "")
                    .append(" size=")
                    .append(bitmap != null ? bitmap.getWidth() : 0)
                    .append('x')
                    .append(bitmap != null ? bitmap.getHeight() : 0);
            if (bitmap != null) {
                appendProbeColorCount(sb, bitmap, 0x33FFFF);
                appendProbeColorCount(sb, bitmap, 0xFFE1C2);
                appendProbeColorCount(sb, bitmap, 0xD9BBA1);
                appendProbeColorCount(sb, bitmap, 0x51201F);
            }
            sb.append('\n');
            appendNamedBitmapProbe(sb, castLibManager, "ctlg.pagelist.left");
            appendNamedBitmapProbe(sb, castLibManager, "ctlg.pagelist.left.active");
            appendNamedBitmapProbe(sb, castLibManager, "tree_basicslot_unselected");
            appendNamedBitmapProbe(sb, castLibManager, "tree_basicslot_selected");
            appendNamedBitmapProbe(sb, castLibManager, "tree_col1_unselected");
            appendNamedBitmapProbe(sb, castLibManager, "tree_col1_selected");
            appendNamedBitmapProbe(sb, castLibManager, "katalogi_ikoni.furni");
            appendNamedBitmapProbe(sb, castLibManager, "testarrow.down");
            appendNamedBitmapProbe(sb, castLibManager, "testarrow.right");
            appendNamedBitmapProbe(sb, castLibManager, "button.e.left.active");
            appendNamedBitmapProbe(sb, castLibManager, "button.e.middle.active");
            appendNamedBitmapProbe(sb, castLibManager, "move_button_icon");
            appendNamedBitmapProbe(sb, castLibManager, "rotate_button_icon");
            appendNamedBitmapProbe(sb, castLibManager, "pick/wave_button_icon");
            appendNamedBitmapProbe(sb, castLibManager, "delete_button_icon");
            appendNamedBitmapProbe(sb, castLibManager, "icon.button.text");
            appendNamedBitmapProbe(sb, castLibManager, "obj.disp.actions_move.button");
            appendNamedBitmapProbe(sb, castLibManager, "obj.disp.actions_rotate.button");
            appendNamedBitmapProbe(sb, castLibManager, "obj.disp.actions_pick.button");
            appendNamedBitmapProbe(sb, castLibManager, "obj.disp.actions_delete.button");

            CastMemberChunk fileChunk = castLibManager != null
                    ? castLibManager.getCastMemberByName("cn_sofa_small")
                    : null;
            DirectorFile fileSource = fileChunk != null ? fileChunk.file() : null;
            com.libreshockwave.cast.BitmapInfo fileInfo = fileChunk != null
                    ? com.libreshockwave.cast.BitmapInfo.parse(fileChunk)
                    : null;
            Bitmap fileBitmap = null;
            if (fileSource != null && fileChunk != null) {
                fileBitmap = fileSource.decodeBitmap(fileChunk).orElse(null);
            }
            sb.append("probe file cn_sofa_small chunk=")
                    .append(fileChunk != null ? fileChunk.id().value() : -1)
                    .append(" dir=")
                    .append(fileSource != null && fileSource.getConfig() != null
                            ? fileSource.getConfig().directorVersion() : -1)
                    .append(" infoPal=")
                    .append(fileInfo != null ? fileInfo.paletteId() : 0)
                    .append(" pal=")
                    .append(fileBitmap != null && fileBitmap.getImagePalette() != null
                            ? fileBitmap.getImagePalette().getName() : "")
                    .append(" size=")
                    .append(fileBitmap != null ? fileBitmap.getWidth() : 0)
                    .append('x')
                    .append(fileBitmap != null ? fileBitmap.getHeight() : 0);
            if (fileBitmap != null) {
                appendProbeColorCount(sb, fileBitmap, 0x33FFFF);
                appendProbeColorCount(sb, fileBitmap, 0xFFE1C2);
                appendProbeColorCount(sb, fileBitmap, 0xD9BBA1);
                appendProbeColorCount(sb, fileBitmap, 0x51201F);
            }
            sb.append('\n');

            if (castLibManager != null) {
                var cast3 = castLibManager.getCastLib(3);
                if (cast3 != null) {
                    sb.append("probe castlib3 name=")
                            .append(cast3.getName())
                            .append(" file=")
                            .append(cast3.getFileName())
                            .append(" state=")
                            .append(cast3.getState())
                            .append(" count=")
                            .append(cast3.getMemberCount())
                            .append(" chunks=")
                            .append(cast3.getMemberChunks().size())
                            .append('\n');
                    int shown = 0;
                    for (var memberEntry : cast3.getMemberChunks().entrySet()) {
                        int memberNumber = memberEntry.getKey();
                        if (memberNumber >= 10240 && memberNumber <= 10310 && shown++ < 20) {
                            CastMemberChunk candidate = memberEntry.getValue();
                            sb.append("probe castlib3chunk ")
                                    .append(memberNumber)
                                    .append(':')
                                    .append(candidate != null ? candidate.name() : "")
                                    .append("#")
                                    .append(candidate != null ? candidate.id().value() : -1)
                                    .append('\n');
                        }
                    }
                }
                for (var entry : castLibManager.getCastLibs().entrySet()) {
                    var castLib = entry.getValue();
                    if (castLib == null || !castLib.isLoaded()) {
                        continue;
                    }
                    int hits = 0;
                    StringBuilder names = new StringBuilder();
                    for (var memberEntry : castLib.getMemberChunks().entrySet()) {
                        CastMemberChunk candidate = memberEntry.getValue();
                        String name = candidate != null ? candidate.name() : null;
                        if (name != null && name.toLowerCase().contains("sofa")) {
                            if (hits++ > 0) {
                                names.append(',');
                            }
                            names.append(memberEntry.getKey())
                                    .append(':')
                                    .append(name)
                                    .append("#")
                                    .append(candidate.id().value());
                        }
                    }
                    if (hits > 0 || (castLib.getName() != null
                            && castLib.getName().toLowerCase().contains("sofa"))) {
                        sb.append("probe castlib ")
                                .append(entry.getKey())
                                .append(" name=")
                                .append(castLib.getName())
                                .append(" file=")
                                .append(castLib.getFileName())
                                .append(" chunks=")
                                .append(names)
                                .append('\n');
                    }
                }
            }
        } catch (Throwable t) {
            sb.append("probe error=").append(t.getClass().getSimpleName()).append('\n');
        }
    }

    private static void appendProbeColorCount(StringBuilder sb, Bitmap bitmap, int rgb) {
        int count = 0;
        for (int pixel : bitmap.getPixels()) {
            if ((pixel & 0xFFFFFF) == (rgb & 0xFFFFFF)
                    && ((pixel >>> 24) & 0xFF) != 0) {
                count++;
            }
        }
        sb.append(' ')
                .append(Integer.toHexString(rgb & 0xFFFFFF))
                .append('=')
                .append(count);
    }

    private static void appendNamedBitmapProbe(StringBuilder sb, com.libreshockwave.player.cast.CastLibManager castLibManager,
                                               String name) {
        try {
            CastMember member = castLibManager != null ? castLibManager.findCastMemberByName(name) : null;
            Bitmap bitmap = member != null ? member.getBitmap() : null;
            CastMemberChunk chunk = member != null ? member.getChunk() : null;
            DirectorFile sourceFile = chunk != null ? chunk.file() : null;
            com.libreshockwave.cast.BitmapInfo info = chunk != null && chunk.isBitmap()
                    ? com.libreshockwave.cast.BitmapInfo.parse(chunk)
                    : null;
            com.libreshockwave.bitmap.Palette resolvedPalette = sourceFile != null && info != null
                    ? sourceFile.resolvePalette(info.paletteId())
                    : null;
            sb.append("probe bitmap ").append(name)
                    .append(" member=")
                    .append(member != null ? member.getCastLibNumber() : -1)
                    .append(':')
                    .append(member != null ? member.getMemberNumber() : -1)
                    .append(" infoPal=")
                    .append(info != null ? info.paletteId() : 0)
                    .append(" resolved=")
                    .append(resolvedPalette != null ? resolvedPalette.getName() : "")
                    .append(" imagePal=")
                    .append(bitmap != null && bitmap.getImagePalette() != null
                            ? bitmap.getImagePalette().getName() : "")
                    .append(" size=")
                    .append(bitmap != null ? bitmap.getWidth() : 0)
                    .append('x')
                    .append(bitmap != null ? bitmap.getHeight() : 0)
                    .append(" depth=")
                    .append(bitmap != null ? bitmap.getBitDepth() : 0)
                    .append(" nativeAlpha=")
                    .append(bitmap != null && bitmap.isNativeAlpha())
                    .append(" transparent=")
                    .append(bitmap != null && bitmap.hasTransparentPixels())
                    .append(" translucent=")
                    .append(bitmap != null && bitmap.hasTranslucentPixels());
            if (bitmap != null) {
                appendBitmapStats(sb, bitmap);
                com.libreshockwave.bitmap.Palette imagePalette = bitmap.getImagePalette();
                if (imagePalette != null && ("tree_basicslot_unselected".equals(name)
                        || "tree_col1_unselected".equals(name))) {
                    appendNearestPaletteProbe(sb, imagePalette, 0x000000);
                    appendNearestPaletteProbe(sb, imagePalette, 0xF0F0F0);
                    appendNearestPaletteProbe(sb, imagePalette, 0xE4E4E4);
                    appendNearestPaletteProbe(sb, imagePalette, 0xFFE6DF);
                    appendNearestPaletteProbe(sb, imagePalette, 0x67A7A8);
                }
            }
            if (member != null && "icon.button.text".equals(name)) {
                sb.append(" props text=\"")
                        .append(member.getProp("text").toStr().replace("\r", "\\r").replace("\n", "\\n"))
                        .append("\" rect=")
                        .append(member.getProp("rect"))
                        .append(" height=")
                        .append(member.getProp("height"))
                        .append(" font=")
                        .append(member.getProp("font"))
                        .append(" size=")
                        .append(member.getProp("fontsize"))
                        .append(" style=")
                        .append(member.getProp("fontstyle"))
                        .append(" align=")
                        .append(member.getProp("alignment"))
                        .append(" fixedLineSpace=")
                        .append(member.getProp("fixedlinespace"));
                Datum image = member.getProp("image");
                Bitmap rendered = image instanceof Datum.ImageRef ir ? ir.bitmap() : null;
                sb.append(" renderedSize=")
                        .append(rendered != null ? rendered.getWidth() : 0)
                        .append('x')
                        .append(rendered != null ? rendered.getHeight() : 0);
                if (rendered != null) {
                    appendBitmapStats(sb, rendered);
                }
            }
            sb.append('\n');
        } catch (Exception ignored) {
            sb.append("probe bitmap ").append(name).append(" error\n");
        }
    }

    private static void appendBitmapStats(StringBuilder sb, Bitmap bitmap) {
        byte[] paletteIndices = bitmap.getPaletteIndices();
        int black = 0;
        int nonWhiteOpaque = 0;
        int transparent = 0;
        int minX = bitmap.getWidth();
        int minY = bitmap.getHeight();
        int maxX = -1;
        int maxY = -1;
        for (int y = 0; y < bitmap.getHeight(); y++) {
            for (int x = 0; x < bitmap.getWidth(); x++) {
                int pixel = bitmap.getPixel(x, y);
                int alpha = (pixel >>> 24) & 0xFF;
                int rgb = pixel & 0xFFFFFF;
                if (alpha == 0) {
                    transparent++;
                } else if (rgb != 0xFFFFFF) {
                    nonWhiteOpaque++;
                    minX = Math.min(minX, x);
                    minY = Math.min(minY, y);
                    maxX = Math.max(maxX, x);
                    maxY = Math.max(maxY, y);
                    if (rgb == 0) {
                        black++;
                    }
                }
            }
        }
        sb.append(" black=").append(black)
                .append(" nonWhite=").append(nonWhiteOpaque)
                .append(" transparent=").append(transparent)
                .append(" bounds=")
                .append(maxX >= 0 ? minX : -1).append(',')
                .append(maxY >= 0 ? minY : -1).append(',')
                .append(maxX).append(',')
                .append(maxY);
        for (int x = 0; x < Math.min(4, bitmap.getWidth()); x++) {
            sb.append(" p").append(x).append('=')
                    .append(Integer.toHexString(bitmap.getPixel(x, 0)));
            if (paletteIndices != null && x < paletteIndices.length) {
                sb.append("/i").append(paletteIndices[x] & 0xFF);
            }
        }
    }

    private static void appendNearestPaletteProbe(StringBuilder sb, com.libreshockwave.bitmap.Palette palette, int rgb) {
        int index = palette.nearestIndex(rgb);
        sb.append(" nearest")
                .append(Integer.toHexString(rgb & 0xFFFFFF))
                .append("=i")
                .append(index)
                .append('/')
                .append(Integer.toHexString(palette.getColor(index) & 0xFFFFFF));
    }

    private static boolean intersects(int x, int y, int w, int h,
                                      int rx, int ry, int rw, int rh) {
        return w > 0 && h > 0
                && x < rx + rw && x + w > rx
                && y < ry + rh && y + h > ry;
    }

    private static String paletteRefSummary(Bitmap bitmap) {
        if (bitmap.getPaletteRefSystemName() != null) {
            return bitmap.getPaletteRefSystemName();
        }
        if (bitmap.getPaletteRefCastLib() >= 1 && bitmap.getPaletteRefMemberNum() >= 1) {
            return bitmap.getPaletteRefCastLib() + ":" + bitmap.getPaletteRefMemberNum();
        }
        return "";
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
        byte[] bytes = req.wireBytes();
        int len = Math.min(bytes.length, stringBuffer.length);
        System.arraycopy(bytes, 0, stringBuffer, 0, len);
        return len;
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
            byte[] data = new byte[dataLen];
            System.arraycopy(stringBuffer, 0, data, 0, dataLen);
            b.deliverMessageBytes(instanceId, data);
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

    static void reportScriptError(String message, com.libreshockwave.vm.datum.LingoException error) {
        StringBuilder sb = new StringBuilder();
        sb.append("[ScriptError] ");
        if (message != null && !message.isEmpty()) {
            sb.append(message);
        } else if (error != null && error.getMessage() != null && !error.getMessage().isEmpty()) {
            sb.append(error.getMessage());
        } else {
            sb.append("Unhandled script error");
        }

        if (error != null) {
            String detail = error.getMessage();
            if (detail != null && !detail.isEmpty() && (message == null || !message.equals(detail))) {
                sb.append(": ").append(detail);
            }
            String stack = error.formatLingoCallStack();
            if (stack != null && !stack.isBlank()) {
                sb.append('\n').append(stack);
            }
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

    private static int writeLatin1ToStringBuffer(String s) {
        if (s == null || s.isEmpty()) return 0;
        byte[] bytes = s.getBytes(StandardCharsets.ISO_8859_1);
        int len = Math.min(bytes.length, stringBuffer.length);
        System.arraycopy(bytes, 0, stringBuffer, 0, len);
        return len;
    }

}
