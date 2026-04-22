package com.libreshockwave.player.wasm;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.player.Player;
import com.libreshockwave.player.PlayerState;

/**
 * Thin wrapper around Player for WASM execution.
 * Follows the same simple pattern as the desktop player:
 * loadMovie → play → tick loop. External casts load asynchronously
 * and the Lingo state machine handles them naturally.
 */
public class WasmPlayer {

    private Player player;
    private QueuedNetProvider netProvider;
    private WasmMultiuserBridge musBridge;
    private WasmAudioBackend audioBackend;
    private SoftwareRenderer softwareRenderer;
    private int castRevision;

    /**
     * Load a Director movie from raw bytes.
     * @return true if loaded successfully
     */
    public boolean loadMovie(byte[] data, String basePath,
                             java.util.function.BiConsumer<Integer, String> castDataRequestCallback) {
        DirectorFile file;
        try {
            file = DirectorFile.load(data);
        } catch (Exception e) {
            return false;
        }

        netProvider = new QueuedNetProvider(basePath);
        player = new Player(file, netProvider, castDataRequestCallback);
        player.getMovieProperties().setGotoNetPageHandler(WasmEntry::enqueueGotoNetPage);
        player.getMovieProperties().setGotoNetMovieHandler(WasmEntry::enqueueGotoNetMovie);
        player.setErrorListener(WasmEntry::reportScriptError);

        // When a fetch completes, cache cast files in CastLibManager so they're
        // available immediately when Lingo sets castLib.fileName.
        netProvider.setFetchCompleteCallback((fetchUrl, fetchData) ->
                player.onNetFetchComplete(fetchUrl, fetchData));

        musBridge = new WasmMultiuserBridge();
        player.registerMultiuserXtra(musBridge);
        audioBackend = new WasmAudioBackend();
        player.setAudioBackend(audioBackend);
        softwareRenderer = null;
        castRevision = 0;

        return true;
    }

    /**
     * Advance one frame. Returns false only when STOPPED (keeps JS loop alive for PAUSED).
     * Catches exceptions to prevent JS animation loop death.
     */
    public boolean tick() {
        if (player == null) return false;
        PlayerState state = player.getState();
        if (state == PlayerState.STOPPED) return false;
        if (state == PlayerState.PAUSED) return true;

        try {
            PlayerState before = player.getState();
            boolean result = player.tick();
            PlayerState after = player.getState();
            if (before != after) {
                WasmEntry.log("state: " + before + " -> " + after);
            }
            return result;
        } catch (Throwable e) {
            WasmEntry.log("tick error: " + e.getClass().getName() + ": " + e.getMessage());
            StringBuilder sb = new StringBuilder();
            for (StackTraceElement ste : e.getStackTrace()) {
                sb.append("  at ").append(ste).append('\n');
                if (sb.length() > 2000) break;
            }
            WasmEntry.log(sb.toString());
            return true;
        }
    }

    /**
     * Queue fetch requests for all external casts before play().
     * Since preloadAllCasts marks casts as fetching, the call inside
     * prepareMovie() becomes a no-op — avoiding duplicate work.
     */
    public int preloadCasts() {
        if (player == null) return 0;
        return player.preloadAllCasts();
    }

    public void play() {
        if (player == null) return;
        player.play();
    }

    public void pause() {
        if (player != null) player.pause();
    }

    public void stop() {
        if (player != null) player.stop();
    }

    public void goToFrame(int frame) {
        if (player != null) player.goToFrame(frame);
    }

    public void stepFrame() {
        if (player != null) player.stepFrame();
    }

    public int getCurrentFrame() {
        return player != null ? player.getCurrentFrame() : 0;
    }

    public int getFrameCount() {
        return player != null ? player.getFrameCount() : 0;
    }

    public int getTempo() {
        return player != null ? player.getTempo() : 15;
    }

    public void setPuppetTempo(int tempo) {
        if (player != null) {
            player.getMovieProperties().setPuppetTempo(tempo);
        }
    }

    public int getStageWidth() {
        return player != null ? player.getStageRenderer().getStageWidth() : 640;
    }

    public int getStageHeight() {
        return player != null ? player.getStageRenderer().getStageHeight() : 480;
    }

    public Player getPlayer() {
        return player;
    }

    public QueuedNetProvider getNetProvider() {
        return netProvider;
    }

    public WasmMultiuserBridge getMusBridge() {
        return musBridge;
    }

    public WasmAudioBackend getAudioBackend() {
        return audioBackend;
    }

    public SoftwareRenderer getSoftwareRenderer() {
        if (softwareRenderer == null && player != null) {
            softwareRenderer = new SoftwareRenderer(getStageWidth(), getStageHeight());
        }
        return softwareRenderer;
    }

    public int getCastRevision() {
        return castRevision;
    }

    public void bumpCastRevision() {
        castRevision++;
        if (softwareRenderer != null) {
            softwareRenderer.invalidate();
        }
    }

    public DirectorFile getFile() {
        return player != null ? player.getFile() : null;
    }

    public void shutdown() {
        if (player != null) {
            player.shutdown();
        }
    }
}
