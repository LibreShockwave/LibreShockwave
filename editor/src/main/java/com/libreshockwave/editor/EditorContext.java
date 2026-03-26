package com.libreshockwave.editor;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.editor.audio.EditorAudioBackend;
import com.libreshockwave.editor.selection.SelectionManager;
import com.libreshockwave.player.Player;
import com.libreshockwave.player.PlayerState;
import com.libreshockwave.player.debug.DebugController;
import com.libreshockwave.player.debug.DebugSnapshot;
import com.libreshockwave.player.debug.DebugStateListener;

import javax.swing.*;
import java.beans.PropertyChangeEvent;
import java.beans.PropertyChangeListener;
import java.beans.PropertyChangeSupport;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.prefs.Preferences;

/**
 * Central shared state for the editor.
 * Holds the current DirectorFile, Player, DebugController, selection state, and playback timer.
 * All panels observe this via PropertyChangeEvents.
 */
public class EditorContext {

    public static final String PROP_FILE = "file";
    public static final String PROP_FRAME = "currentFrame";
    public static final String PROP_PLAYING = "playing";
    public static final String PROP_CASTS_LOADED = "castsLoaded";

    private static final String PREF_BREAKPOINTS_PREFIX = "breakpoints:";
    private final Preferences prefs = Preferences.userNodeForPackage(EditorContext.class);

    private final PropertyChangeSupport pcs = new PropertyChangeSupport(this);
    private final SelectionManager selectionManager = new SelectionManager();

    private DirectorFile file;
    private Player player;
    private DebugController debugController;
    private Timer playbackTimer;
    private int currentFrame = 1;
    private Path currentPath;
    private String currentMovieKey;

    public void addPropertyChangeListener(PropertyChangeListener listener) {
        pcs.addPropertyChangeListener(listener);
    }

    public void removePropertyChangeListener(PropertyChangeListener listener) {
        pcs.removePropertyChangeListener(listener);
    }

    public SelectionManager getSelectionManager() {
        return selectionManager;
    }

    public DirectorFile getFile() {
        return file;
    }

    public Player getPlayer() {
        return player;
    }

    public DebugController getDebugController() {
        return debugController;
    }

    public int getCurrentFrame() {
        return currentFrame;
    }

    public Path getCurrentPath() {
        return currentPath;
    }


    public void openFile(Path path) {
        closeFile();
        try {
            DirectorFile newFile = DirectorFile.load(path);
            Player newPlayer = new Player(newFile);
            this.currentPath = path;

            // Wire audio backend
            newPlayer.setAudioBackend(new EditorAudioBackend());

            // Wire debug controller
            DebugController newDebugController = new DebugController();
            newPlayer.setDebugController(newDebugController);
            newPlayer.setDebugEnabled(true);

            // Listen for breakpoint changes to save them
            newDebugController.addListener(new DebugStateListener() {
                @Override
                public void onPaused(DebugSnapshot snapshot) {}
                @Override
                public void onResumed() {}
                @Override
                public void onBreakpointsChanged() {
                    saveBreakpoints();
                }
            });

            // Auto-detect local HTTP root for localhost URL resolution
            String absPath = path.toAbsolutePath().toString().replace('\\', '/');
            int htdocsIdx = absPath.toLowerCase().indexOf("/htdocs/");
            String httpRoot = null;
            if (htdocsIdx >= 0) {
                httpRoot = absPath.substring(0, htdocsIdx + "/htdocs".length());
                newPlayer.getNetManager().setLocalHttpRoot(httpRoot);
            }

            // Load saved external params, or fall back to auto-detected Habbo defaults
            String absPathKey = path.toAbsolutePath().toString();
            Map<String, String> savedParams = com.libreshockwave.editor.Preferences.get()
                .getMovieParams(absPathKey);
            if (savedParams != null) {
                newPlayer.setExternalParams(savedParams);
            } else if (httpRoot != null) {
                Map<String, String> habboParams = buildDefaultHabboParams();
                if (!habboParams.isEmpty()) {
                    newPlayer.setExternalParams(habboParams);
                }
            }

            DirectorFile oldFile = this.file;
            this.file = newFile;
            this.player = newPlayer;
            this.debugController = newDebugController;
            this.currentFrame = 1;
            this.currentMovieKey = path.toAbsolutePath().toString();

            pcs.firePropertyChange(PROP_FILE, oldFile, newFile);

            // Track in recent projects
            com.libreshockwave.editor.Preferences.get()
                .addRecentProject(path.toAbsolutePath().toString());

            // Load saved breakpoints for this movie
            loadBreakpoints(currentMovieKey);

            // Preload all external casts so their scripts are available for debugging
            newPlayer.preloadAllCasts();

            // Notify all panels when external casts finish loading
            newPlayer.setCastLoadedListener(() -> {
                SwingUtilities.invokeLater(() -> {
                    pcs.firePropertyChange(PROP_CASTS_LOADED, false, true);
                });
            });

        } catch (Exception e) {
            JOptionPane.showMessageDialog(null,
                "Failed to load file: " + e.getMessage(),
                "Error", JOptionPane.ERROR_MESSAGE);
        }
    }

    public void closeFile() {
        stopPlayback();
        if (player != null) {
            player.shutdown();
        }
        DirectorFile oldFile = this.file;
        this.file = null;
        this.player = null;
        this.debugController = null;
        this.currentPath = null;
        this.currentMovieKey = null;
        this.currentFrame = 1;
        selectionManager.clearSelection();
        if (oldFile != null) {
            pcs.firePropertyChange(PROP_FILE, oldFile, null);
        }
    }

    public void setCurrentFrame(int frame) {
        int oldFrame = this.currentFrame;
        this.currentFrame = frame;
        if (player != null) {
            player.goToFrame(frame);
            player.stepFrame();
        }
        pcs.firePropertyChange(PROP_FRAME, oldFrame, frame);
    }

    // Playback controls

    public void play() {
        if (player == null) return;

        // Start rendering timer immediately
        startPlaybackTimer();
        pcs.firePropertyChange(PROP_PLAYING, false, true);

        // Run prepareMovie async so debugger pauses don't freeze UI
        player.playAsync(() -> {
            SwingUtilities.invokeLater(() -> {
                // State updated via timer
            });
        });
    }

    public void stop() {
        if (player == null) return;
        stopPlayback();
        player.stop();
        pcs.firePropertyChange(PROP_PLAYING, true, false);
    }

    public void rewind() {
        if (player == null) return;
        stopPlayback();
        player.stop();
        setCurrentFrame(1);
        pcs.firePropertyChange(PROP_PLAYING, true, false);
    }

    public void stepForward() {
        if (player == null) return;
        player.stepFrameAsync(() -> {
            SwingUtilities.invokeLater(() -> {
                int oldFrame = currentFrame;
                currentFrame = player.getCurrentFrame();
                pcs.firePropertyChange(PROP_FRAME, oldFrame, currentFrame);
            });
        });
    }

    public void stepBackward() {
        if (player == null) return;
        int current = player.getCurrentFrame();
        if (current > 1) {
            setCurrentFrame(current - 1);
        }
    }

    public boolean isPlaying() {
        return playbackTimer != null && playbackTimer.isRunning();
    }

    private void startPlaybackTimer() {
        if (playbackTimer != null) {
            playbackTimer.stop();
        }
        int delay = 1000 / player.getTempo();
        playbackTimer = new Timer(delay, e -> {
            if (player == null) {
                stopPlayback();
                return;
            }

            // Don't advance frame while debugger is paused
            if (debugController != null && debugController.isPaused()) {
                return;
            }

            if (player.isVmRunning()) {
                // VM is busy — still fire frame change for repaint.
                // Use PropertyChangeEvent directly so the event fires even when
                // the frame number hasn't changed (PropertyChangeSupport suppresses
                // events when old == new via the convenience method).
                pcs.firePropertyChange(new PropertyChangeEvent(
                    this, PROP_FRAME, null, currentFrame));
            } else {
                player.tickAsync(() -> {
                    SwingUtilities.invokeLater(() -> {
                        int oldFrame = currentFrame;
                        currentFrame = player.getCurrentFrame();
                        // Always fire the event even when the frame hasn't changed
                        // (e.g. "go the frame" holds on the same frame). The stage
                        // still needs to repaint for interactive updates.
                        pcs.firePropertyChange(new PropertyChangeEvent(
                            this, PROP_FRAME, null, currentFrame));
                        updateTimerDelay();
                    });
                });
            }
        });
        playbackTimer.start();
    }

    private void updateTimerDelay() {
        if (playbackTimer == null || player == null) return;
        int newDelay = 1000 / player.getTempo();
        if (newDelay != playbackTimer.getDelay()) {
            playbackTimer.setDelay(newDelay);
        }
    }

    private void stopPlayback() {
        if (playbackTimer != null) {
            playbackTimer.stop();
            playbackTimer = null;
        }
    }

    // External params

    public Map<String, String> getExternalParams() {
        if (player != null) {
            return player.getExternalParams();
        }
        return Map.of();
    }

    public void setExternalParams(Map<String, String> params) {
        if (player != null) {
            player.setExternalParams(params);
        }
        if (currentPath != null) {
            com.libreshockwave.editor.Preferences.get()
                .setMovieParams(currentPath.toAbsolutePath().toString(), params);
        }
    }

    // Default Habbo params

    private Map<String, String> buildDefaultHabboParams() {
        Map<String, String> params = new LinkedHashMap<>();

            params.put("sw1", "site.url=http://www.habbo.co.uk;url.prefix=http://www.habbo.co.uk");
            params.put("sw2", "connection.info.host=au.h4bbo.net;connection.info.port=30101");
            params.put("sw3", "client.reload.url=https://sandbox.h4bbo.net/");
            params.put("sw4", "connection.mus.host=au.h4bbo.net;connection.mus.port=38101");
            params.put("sw5", "external.variables.txt=https://sandbox.h4bbo.net/gamedata/external_variables.txt;external.texts.txt=https://sandbox.h4bbo.net/gamedata/external_texts.txt");

        return params;
    }

    // Breakpoint persistence

    private void saveBreakpoints() {
        if (currentMovieKey == null || currentMovieKey.isEmpty()) return;
        String serialized = debugController.serializeBreakpoints();
        String prefKey = PREF_BREAKPOINTS_PREFIX + sanitizeKey(currentMovieKey);
        if (serialized.isEmpty()) {
            prefs.remove(prefKey);
        } else {
            prefs.put(prefKey, serialized);
        }
    }

    private void loadBreakpoints(String movieKey) {
        if (movieKey == null || movieKey.isEmpty()) return;
        String prefKey = PREF_BREAKPOINTS_PREFIX + sanitizeKey(movieKey);
        String serialized = prefs.get(prefKey, "");
        if (!serialized.isEmpty()) {
            debugController.deserializeBreakpoints(serialized);
        }
    }

    public void clearSavedBreakpoints() {
        if (currentMovieKey == null || currentMovieKey.isEmpty()) return;
        String prefKey = PREF_BREAKPOINTS_PREFIX + sanitizeKey(currentMovieKey);
        prefs.remove(prefKey);
    }

    private String sanitizeKey(String key) {
        if (key.length() > 80) {
            return "hash_" + key.hashCode();
        }
        return key.replace("/", "_").replace("\\", "_").replace(":", "_");
    }
}
