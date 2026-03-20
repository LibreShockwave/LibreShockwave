package com.libreshockwave.player;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.chunks.ScoreChunk;
import com.libreshockwave.player.debug.DebugController;
import com.libreshockwave.player.debug.DebugSnapshot;
import com.libreshockwave.player.debug.DebugStateListener;
import com.libreshockwave.vm.datum.Datum;

import java.util.LinkedHashMap;
import java.util.Map;

import javax.swing.*;
import javax.swing.filechooser.FileNameExtensionFilter;
import java.awt.*;
import java.awt.event.*;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.prefs.Preferences;

/**
 * Main window for the LibreShockwave Player.
 * Provides playback controls and stage rendering.
 */
public class PlayerFrame extends JFrame {

    private static final String PREF_LAST_FILE = "lastFile";
    private static final String PREF_LAST_URL = "lastUrl";
    private static final String PREF_LAST_DIR = "lastDirectory";
    private static final String PREF_BREAKPOINTS_PREFIX = "breakpoints:";
    private static final String PREF_EXTPARAMS_PREFIX = "extparams:";
    private final Preferences prefs = Preferences.userNodeForPackage(PlayerFrame.class);
    private final HttpClient httpClient = HttpClient.newBuilder()
            .followRedirects(HttpClient.Redirect.NORMAL)
            .connectTimeout(Duration.ofSeconds(30))
            .build();

    private Player player;
    private Timer playbackTimer;
    private StagePanel stagePanel;
    private JLabel statusLabel;
    private JLabel frameLabel;
    private JButton playButton;
    private JButton pauseButton;
    private JButton stopButton;
    private JButton stepButton;
    private JSlider frameSlider;
    private BytecodeDebuggerPanel debuggerPanel;
    private DebugController debugController;
    private DetailedStackWindow detailedStackWindow;
    private JSplitPane splitPane;
    private JToggleButton debugToggleButton;
    private JCheckBoxMenuItem debugMenuItem;
    private boolean debugVisible = true;
    private boolean useAsyncExecution = true;  // Use async execution for debugger support
    private Path lastOpenedFile;
    private String currentMovieKey;  // Key for saving/loading breakpoints and external params
    private Map<String, String> currentExternalParams = new LinkedHashMap<>();

    public PlayerFrame() {
        super("LibreShockwave Player");
        setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        initComponents();
        initMenuBar();
        loadLastFilePreference();
        pack();
        centerOnCurrentMonitor();
    }

    private void centerOnCurrentMonitor() {
        Point mouse = MouseInfo.getPointerInfo().getLocation();
        for (GraphicsDevice gd : GraphicsEnvironment.getLocalGraphicsEnvironment().getScreenDevices()) {
            Rectangle bounds = gd.getDefaultConfiguration().getBounds();
            if (bounds.contains(mouse)) {
                int x = bounds.x + (bounds.width - getWidth()) / 2;
                int y = bounds.y + (bounds.height - getHeight()) / 2;
                setLocation(x, y);
                return;
            }
        }
        setLocationRelativeTo(null);
    }

    private void loadLastFilePreference() {
        String lastFilePath = prefs.get(PREF_LAST_FILE, null);
        if (lastFilePath != null) {
            lastOpenedFile = Path.of(lastFilePath);
            if (lastOpenedFile.toFile().exists()) {
                statusLabel.setText("Last file: " + lastOpenedFile.getFileName() +
                    "  \u2022  Press Ctrl+O to open, or drag & drop a file");
            }
        }
    }

    private void saveLastFilePreference(Path path) {
        lastOpenedFile = path;
        prefs.put(PREF_LAST_FILE, path.toAbsolutePath().toString());
        prefs.put(PREF_LAST_DIR, path.getParent().toAbsolutePath().toString());
    }

    private void reopenLast() {
        // Check URL first (most recent if set)
        String lastUrl = prefs.get(PREF_LAST_URL, null);
        if (lastUrl != null && !lastUrl.isEmpty()) {
            openUrl(lastUrl);
            return;
        }

        // Fall back to file
        if (lastOpenedFile != null && lastOpenedFile.toFile().exists()) {
            openFile(lastOpenedFile);
        } else {
            JOptionPane.showMessageDialog(this,
                "No recent file or URL to reopen.",
                "Reopen Last",
                JOptionPane.INFORMATION_MESSAGE);
        }
    }

    private void openUrlDialog() {
        String lastUrl = prefs.get(PREF_LAST_URL, "");
        String url = (String) JOptionPane.showInputDialog(
            this,
            "Enter the URL of a Director file (.dcr, .dir, .dxr):",
            "Open URL",
            JOptionPane.PLAIN_MESSAGE,
            null,
            null,
            lastUrl
        );

        if (url != null && !url.trim().isEmpty()) {
            url = url.trim();
            // Add http:// if no protocol specified
            if (!url.toLowerCase().startsWith("http://") && !url.toLowerCase().startsWith("https://")) {
                url = "http://" + url;
            }
            openUrl(url);
        }
    }

    private void initComponents() {
        setLayout(new BorderLayout());

        // Stage panel (center)
        stagePanel = new StagePanel();
        stagePanel.setPreferredSize(new Dimension(640, 480));
        stagePanel.setBackground(Color.WHITE);

        // Bytecode debugger panel (right side)
        debugController = new DebugController();
        debuggerPanel = new BytecodeDebuggerPanel();
        debuggerPanel.setController(debugController);
        debugController.addListener(debuggerPanel);

        // Detailed stack window (separate toggleable window)
        detailedStackWindow = new DetailedStackWindow();
        debugController.addListener(detailedStackWindow);

        // Listen for breakpoint changes to save them
        debugController.addListener(new DebugStateListener() {
            @Override
            public void onPaused(DebugSnapshot snapshot) {}
            @Override
            public void onResumed() {}
            @Override
            public void onBreakpointsChanged() {
                saveBreakpoints();
            }
        });

        // Split pane for stage and debug
        splitPane = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT, stagePanel, debuggerPanel);
        splitPane.setResizeWeight(0.6);
        splitPane.setOneTouchExpandable(true);
        add(splitPane, BorderLayout.CENTER);

        // Control panel (bottom)
        JPanel controlPanel = new JPanel(new BorderLayout());

        // Transport controls
        JPanel transportPanel = new JPanel(new FlowLayout(FlowLayout.LEFT));

        playButton = new JButton("\u25B6");  // Play triangle
        playButton.setToolTipText("Play");
        playButton.addActionListener(e -> play());
        transportPanel.add(playButton);

        pauseButton = new JButton("\u23F8");  // Pause
        pauseButton.setToolTipText("Pause");
        pauseButton.addActionListener(e -> pause());
        pauseButton.setEnabled(false);
        transportPanel.add(pauseButton);

        stopButton = new JButton("\u23F9");  // Stop
        stopButton.setToolTipText("Stop");
        stopButton.addActionListener(e -> stop());
        stopButton.setEnabled(false);
        transportPanel.add(stopButton);

        JButton closeButton = new JButton("\u23CF");  // Eject symbol
        closeButton.setToolTipText("Close Movie (Ctrl+W)");
        closeButton.addActionListener(e -> closeMovie());
        transportPanel.add(closeButton);

        JButton restartButton = new JButton("\u21BB");  // Clockwise arrow symbol
        restartButton.setToolTipText("Restart Movie (Ctrl+R)");
        restartButton.addActionListener(e -> restartMovie());
        transportPanel.add(restartButton);

        transportPanel.add(Box.createHorizontalStrut(10));

        stepButton = new JButton("\u23ED");  // Next frame
        stepButton.setToolTipText("Step Frame");
        stepButton.addActionListener(e -> stepFrame());
        transportPanel.add(stepButton);

        transportPanel.add(Box.createHorizontalStrut(10));

        JToggleButton debugToggle = new JToggleButton("\uD83D\uDC1B", true);  // Bug emoji
        debugToggle.setToolTipText("Toggle Debug Panel (Ctrl+D)");
        debugToggle.addActionListener(e -> toggleDebugPanel(debugToggle.isSelected()));
        transportPanel.add(debugToggle);
        this.debugToggleButton = debugToggle;

        controlPanel.add(transportPanel, BorderLayout.WEST);

        // Frame slider
        JPanel sliderPanel = new JPanel(new BorderLayout());
        sliderPanel.setBorder(BorderFactory.createEmptyBorder(0, 10, 0, 10));

        frameSlider = new JSlider(1, 100, 1);
        frameSlider.setEnabled(false);
        frameSlider.addChangeListener(e -> {
            if (frameSlider.getValueIsAdjusting() && player != null) {
                player.goToFrame(frameSlider.getValue());
            }
        });
        sliderPanel.add(frameSlider, BorderLayout.CENTER);

        frameLabel = new JLabel("Frame: 1 / 1");
        frameLabel.setPreferredSize(new Dimension(120, 20));
        sliderPanel.add(frameLabel, BorderLayout.EAST);

        controlPanel.add(sliderPanel, BorderLayout.CENTER);

        add(controlPanel, BorderLayout.SOUTH);

        // Status bar
        statusLabel = new JLabel("No file loaded. Open a .dir, .dxr, or .dcr file.");
        statusLabel.setBorder(BorderFactory.createEmptyBorder(5, 10, 5, 10));
        add(statusLabel, BorderLayout.NORTH);

        // Keyboard shortcuts
        setupKeyboardShortcuts();
    }

    private void initMenuBar() {
        JMenuBar menuBar = new JMenuBar();

        // File menu
        JMenu fileMenu = new JMenu("File");
        fileMenu.setMnemonic(KeyEvent.VK_F);

        JMenuItem openItem = new JMenuItem("Open File...");
        openItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_O, InputEvent.CTRL_DOWN_MASK));
        openItem.addActionListener(e -> openFileDialog());
        fileMenu.add(openItem);

        JMenuItem openUrlItem = new JMenuItem("Open URL...");
        openUrlItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_U, InputEvent.CTRL_DOWN_MASK));
        openUrlItem.addActionListener(e -> openUrlDialog());
        fileMenu.add(openUrlItem);

        JMenuItem reopenItem = new JMenuItem("Reopen Last");
        reopenItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_O, InputEvent.CTRL_DOWN_MASK | InputEvent.SHIFT_DOWN_MASK));
        reopenItem.addActionListener(e -> reopenLast());
        fileMenu.add(reopenItem);

        fileMenu.addSeparator();

        JMenuItem closeItem = new JMenuItem("Close Movie");
        closeItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_W, InputEvent.CTRL_DOWN_MASK));
        closeItem.addActionListener(e -> closeMovie());
        fileMenu.add(closeItem);

        JMenuItem restartItem = new JMenuItem("Restart Movie");
        restartItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_R, InputEvent.CTRL_DOWN_MASK));
        restartItem.addActionListener(e -> restartMovie());
        fileMenu.add(restartItem);

        fileMenu.addSeparator();

        JMenuItem exitItem = new JMenuItem("Exit");
        exitItem.addActionListener(e -> System.exit(0));
        fileMenu.add(exitItem);

        menuBar.add(fileMenu);

        // Movie menu
        JMenu movieMenu = new JMenu("Movie");
        movieMenu.setMnemonic(KeyEvent.VK_M);

        JMenuItem extParamsItem = new JMenuItem("External Params...");
        extParamsItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_E, InputEvent.CTRL_DOWN_MASK));
        extParamsItem.addActionListener(e -> showExternalParamsDialog());
        movieMenu.add(extParamsItem);

        menuBar.add(movieMenu);

        // Playback menu
        JMenu playbackMenu = new JMenu("Playback");
        playbackMenu.setMnemonic(KeyEvent.VK_P);

        JMenuItem playItem = new JMenuItem("Play");
        playItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_SPACE, 0));
        playItem.addActionListener(e -> togglePlayPause());
        playbackMenu.add(playItem);

        JMenuItem stopItem = new JMenuItem("Stop");
        stopItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_ESCAPE, 0));
        stopItem.addActionListener(e -> stop());
        playbackMenu.add(stopItem);

        playbackMenu.addSeparator();

        JMenuItem stepItem = new JMenuItem("Step Forward");
        stepItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_RIGHT, 0));
        stepItem.addActionListener(e -> stepFrame());
        playbackMenu.add(stepItem);

        JMenuItem stepBackItem = new JMenuItem("Step Backward");
        stepBackItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_LEFT, 0));
        stepBackItem.addActionListener(e -> stepBackward());
        playbackMenu.add(stepBackItem);

        playbackMenu.addSeparator();

        JMenuItem goToFirstItem = new JMenuItem("Go to First Frame");
        goToFirstItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_HOME, 0));
        goToFirstItem.addActionListener(e -> goToFrame(1));
        playbackMenu.add(goToFirstItem);

        JMenuItem goToLastItem = new JMenuItem("Go to Last Frame");
        goToLastItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_END, 0));
        goToLastItem.addActionListener(e -> goToLastFrame());
        playbackMenu.add(goToLastItem);

        menuBar.add(playbackMenu);

        // View menu
        JMenu viewMenu = new JMenu("View");
        viewMenu.setMnemonic(KeyEvent.VK_V);

        debugMenuItem = new JCheckBoxMenuItem("Debug Panel", true);
        debugMenuItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_D, InputEvent.CTRL_DOWN_MASK));
        debugMenuItem.addActionListener(e -> toggleDebugPanel(debugMenuItem.isSelected()));
        viewMenu.add(debugMenuItem);

        JCheckBoxMenuItem detailedStackItem = new JCheckBoxMenuItem("Detailed Stack Window", false);
        detailedStackItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_S, InputEvent.CTRL_DOWN_MASK | InputEvent.SHIFT_DOWN_MASK));
        detailedStackItem.addActionListener(e -> toggleDetailedStackWindow(detailedStackItem.isSelected()));
        viewMenu.add(detailedStackItem);

        viewMenu.addSeparator();

        JMenuItem clearBpItem = new JMenuItem("Clear All Breakpoints");
        clearBpItem.addActionListener(e -> {
            debugController.clearAllBreakpoints();
            clearSavedBreakpoints();
        });
        viewMenu.add(clearBpItem);

        JMenuItem traceItem = new JMenuItem("Trace Handler...");
        traceItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_T, InputEvent.CTRL_DOWN_MASK | InputEvent.SHIFT_DOWN_MASK));
        traceItem.addActionListener(e -> showTraceHandlerDialog());
        viewMenu.add(traceItem);

        menuBar.add(viewMenu);

        // Help menu
        JMenu helpMenu = new JMenu("Help");
        helpMenu.setMnemonic(KeyEvent.VK_H);

        JMenuItem aboutItem = new JMenuItem("About");
        aboutItem.addActionListener(e -> showAbout());
        helpMenu.add(aboutItem);

        menuBar.add(helpMenu);

        setJMenuBar(menuBar);
    }

    private void setupKeyboardShortcuts() {
        // Space toggles play/pause
        getRootPane().registerKeyboardAction(
            e -> togglePlayPause(),
            KeyStroke.getKeyStroke(KeyEvent.VK_SPACE, 0),
            JComponent.WHEN_IN_FOCUSED_WINDOW
        );

        // Register debugger keyboard shortcuts (F5, F6, F10, F11, Shift+F11)
        debuggerPanel.registerKeyboardShortcuts(getRootPane());
    }

    // File operations

    public void openFileDialog() {
        JFileChooser chooser = new JFileChooser();
        chooser.setDialogTitle("Open Director File");
        chooser.setFileFilter(new FileNameExtensionFilter(
            "Director Files (*.dir, *.dxr, *.dcr, *.cct, *.cst)",
            "dir", "dxr", "dcr", "cct", "cst"
        ));

        // Start in the last used directory
        String lastDir = prefs.get(PREF_LAST_DIR, null);
        if (lastDir != null) {
            File dir = new File(lastDir);
            if (dir.exists() && dir.isDirectory()) {
                chooser.setCurrentDirectory(dir);
            }
        }

        // Pre-select the last file if it exists
        if (lastOpenedFile != null && lastOpenedFile.toFile().exists()) {
            chooser.setSelectedFile(lastOpenedFile.toFile());
        }

        if (chooser.showOpenDialog(this) == JFileChooser.APPROVE_OPTION) {
            openFile(chooser.getSelectedFile().toPath());
        }
    }

    public void openFile(Path path) {
        stop();

        statusLabel.setText("Loading: " + path.getFileName() + " ...");
        setCursor(Cursor.getPredefinedCursor(Cursor.WAIT_CURSOR));

        // Load file in background thread to avoid blocking the UI
        new SwingWorker<DirectorFile, Void>() {
            @Override
            protected DirectorFile doInBackground() throws Exception {
                return DirectorFile.load(path);
            }

            @Override
            protected void done() {
                setCursor(Cursor.getDefaultCursor());
                try {
                    DirectorFile file = get();
                    player = new Player(file);
                    player.setAudioBackend(new com.libreshockwave.player.audio.SwingAudioBackend());

                    // Save this file as the last opened (clear URL preference)
                    saveLastFilePreference(path);
                    prefs.remove(PREF_LAST_URL);

                    // Set movie key for breakpoint/extparam persistence
                    currentMovieKey = path.toAbsolutePath().toString();

                    // Load and apply saved external params
                    loadExternalParams(currentMovieKey);
                    player.setExternalParams(currentExternalParams);

                    // Auto-detect local HTTP root for localhost URL resolution
                    // (e.g., if file is under C:/xampp/htdocs/..., set htdocs as the root)
                    String absPath = path.toAbsolutePath().toString().replace('\\', '/');
                    int htdocsIdx = absPath.toLowerCase().indexOf("/htdocs/");
                    String httpRoot = null;
                    if (htdocsIdx >= 0) {
                        httpRoot = absPath.substring(0, htdocsIdx + "/htdocs".length());
                        player.getNetManager().setLocalHttpRoot(httpRoot);
                    }

                    // Auto-detect external params if none were saved by the user.
                    // Looks for gamedata/external_variables.txt and external_texts.txt
                    // under the HTTP root and builds the sw1-sw5 param strings.
                    if (currentExternalParams.isEmpty() && httpRoot != null) {
                        StringBuilder sw5 = new StringBuilder();
                        Path varsFile = Path.of(httpRoot, "gamedata", "external_variables.txt");
                        Path textsFile = Path.of(httpRoot, "gamedata", "external_texts.txt");
                        if (Files.exists(varsFile)) {
                            sw5.append("external.variables.txt=http://localhost/gamedata/external_variables.txt");
                        }
                        if (Files.exists(textsFile)) {
                            if (sw5.length() > 0) sw5.append(";");
                            sw5.append("external.texts.txt=http://localhost/gamedata/external_texts.txt");
                        }
                        if (sw5.length() > 0) {
                            currentExternalParams.put("sw1", "site.url=http://www.habbo.co.uk;url.prefix=http://www.habbo.co.uk");
                            currentExternalParams.put("sw2", "connection.info.host=localhost;connection.info.port=30001");
                            currentExternalParams.put("sw3", "client.reload.url=http://localhost/");
                            currentExternalParams.put("sw4", "connection.mus.host=localhost;connection.mus.port=38101");
                            currentExternalParams.put("sw5", sw5.toString());
                            player.setExternalParams(currentExternalParams);
                        }
                    }

                    // Update UI
                    setTitle("LibreShockwave Player - " + path.getFileName());
                    statusLabel.setText("Loaded: " + path.getFileName() +
                        " | Frames: " + player.getFrameCount() +
                        " | Tempo: " + player.getTempo() + " fps");

                    setupPlayerUI(file);

                    // Load saved breakpoints for this movie
                    loadBreakpoints(currentMovieKey);

                } catch (Exception e) {
                    String message = e.getCause() != null ? e.getCause().getMessage() : e.getMessage();
                    JOptionPane.showMessageDialog(PlayerFrame.this,
                        "Failed to load file: " + message,
                        "Error",
                        JOptionPane.ERROR_MESSAGE);
                    statusLabel.setText("Failed to load: " + path.getFileName());
                }
            }
        }.execute();
    }

    /**
     * Open a Director file from an HTTP/HTTPS URL.
     */
    public void openUrl(String url) {
        stop();

        statusLabel.setText("Loading from URL: " + url + " ...");
        setCursor(Cursor.getPredefinedCursor(Cursor.WAIT_CURSOR));

        // Load in background thread
        new SwingWorker<DirectorFile, Void>() {
            @Override
            protected DirectorFile doInBackground() throws Exception {
                HttpRequest request = HttpRequest.newBuilder()
                        .uri(URI.create(url))
                        .timeout(Duration.ofSeconds(60))
                        .GET()
                        .build();

                HttpResponse<byte[]> response = httpClient.send(request, HttpResponse.BodyHandlers.ofByteArray());

                if (response.statusCode() != 200) {
                    throw new IOException("HTTP error: " + response.statusCode());
                }

                return DirectorFile.load(response.body());
            }

            @Override
            protected void done() {
                setCursor(Cursor.getDefaultCursor());
                try {
                    DirectorFile file = get();
                    player = new Player(file);
                    player.setAudioBackend(new com.libreshockwave.player.audio.SwingAudioBackend());

                    // Set base path from URL for relative resource loading
                    String basePath = url.substring(0, url.lastIndexOf('/') + 1);
                    player.getNetManager().setBasePath(basePath);

                    // Save URL as last opened (clear file preference)
                    prefs.put(PREF_LAST_URL, url);
                    prefs.remove(PREF_LAST_FILE);

                    // Set movie key for breakpoint/extparam persistence
                    currentMovieKey = url;

                    // Load and apply saved external params
                    loadExternalParams(currentMovieKey);
                    player.setExternalParams(currentExternalParams);

                    // Get filename from URL
                    String fileName = url.substring(url.lastIndexOf('/') + 1);
                    setTitle("LibreShockwave Player - " + fileName);
                    statusLabel.setText("Loaded: " + fileName +
                        " | Frames: " + player.getFrameCount() +
                        " | Tempo: " + player.getTempo() + " fps");

                    setupPlayerUI(file);

                    // Load saved breakpoints for this movie
                    loadBreakpoints(currentMovieKey);

                } catch (Exception e) {
                    String message = e.getCause() != null ? e.getCause().getMessage() : e.getMessage();
                    JOptionPane.showMessageDialog(PlayerFrame.this,
                        "Failed to load URL: " + message,
                        "Error",
                        JOptionPane.ERROR_MESSAGE);
                    statusLabel.setText("Failed to load: " + url);
                }
            }
        }.execute();
    }

    /**
     * Common UI setup after loading a file or URL.
     */
    private void setupPlayerUI(DirectorFile file) {
        // Update stage canvas size (fixed, independent of window size)
        int width = file.getStageWidth();
        int height = file.getStageHeight();
        if (width > 0 && height > 0) {
            stagePanel.setStageSize(width, height);
            // Set minimum preferred size for the panel, but allow window to be larger
            stagePanel.setPreferredSize(new Dimension(
                Math.max(width, 640),
                Math.max(height, 480)
            ));
            pack();
        }

        // Update frame slider
        int frameCount = player.getFrameCount();
        if (frameCount > 0) {
            frameSlider.setMaximum(frameCount);
            frameSlider.setValue(1);
            frameSlider.setEnabled(true);
        }

        updateFrameLabel();

        // Set up player event listener
        player.setEventListener(event -> {
            SwingUtilities.invokeLater(this::updateFrameLabel);
        });

        // Set stage background color from movie config
        if (file.getConfig() != null) {
            player.getStageRenderer().setBackgroundColor(file.getConfig().stageColorRGB());
        }

        // Reset debug controller state before connecting to new player
        // This clears any stale state (stepMode, breakpoints, etc.) from previous session
        debugController.reset();

        // Connect debug controller to VM for tracing and debugging
        // The controller delegates trace events to the panel for UI display
        debugController.setDelegateListener(debuggerPanel);
        player.setDebugController(debugController);
        player.setDebugEnabled(true);

        // Set player reference for preloading casts
        debuggerPanel.setPlayer(player);

        // Populate debugger panel with script/handler list for browsing (from all cast libraries)
        debuggerPanel.setDirectorFile(file, player.getCastLibManager());

        stagePanel.setPlayer(player);

        // Preload all external casts so their scripts are available for debugging
        int preloadCount = player.preloadAllCasts();
        if (preloadCount > 0) {
            statusLabel.setText(statusLabel.getText() + " | Preloading " + preloadCount + " external cast(s)...");
        }

        // Refresh debugger panel and invalidate bitmap cache when external casts load.
        // Bitmaps from newly-loaded casts become available — clear the cache so
        // they're decoded on the next repaint instead of showing as missing.
        player.setCastLoadedListener(() -> {
            SwingUtilities.invokeLater(() -> {
                stagePanel.clearBitmapCache();
                stagePanel.repaint();
                debuggerPanel.refreshScriptList();
            });
        });

        updateButtonStates();
    }

    // Playback controls

    private void play() {
        if (player == null) return;

        // Start rendering immediately so the loading bar and initial sprites
        // are visible while prepareMovie() runs on the background thread.
        startPlaybackTimer();
        updateButtonStates();

        // Run prepareMovie async — rendering continues in the timer while this runs
        player.playAsync(() -> {
            SwingUtilities.invokeLater(this::updateButtonStates);
        });
    }

    private void pause() {
        if (player == null) return;

        player.pause();
        stopPlaybackTimer();
        updateButtonStates();
    }

    private void stop() {
        if (player == null) return;

        player.stop();
        stopPlaybackTimer();
        updateButtonStates();
        updateFrameLabel();
        stagePanel.repaint();
    }

    /**
     * Close/unload the current movie and reset the player.
     */
    private void closeMovie() {
        // Stop playback first
        stopPlaybackTimer();

        // Shutdown player if exists
        if (player != null) {
            player.shutdown();
            player = null;
        }

        // Reset UI
        stagePanel.setPlayer(null);
        stagePanel.repaint();

        frameSlider.setValue(1);
        frameSlider.setMaximum(100);
        frameSlider.setEnabled(false);

        updateButtonStates();
        updateFrameLabel();

        setTitle("LibreShockwave Player");
        statusLabel.setText("No file loaded. Open a .dir, .dxr, or .dcr file.");

        // Clear movie key (but don't clear saved breakpoints - they persist)
        currentMovieKey = null;

        // Reset debug controller and panel
        debugController.reset();
        debuggerPanel.setDirectorFile(null);
    }

    /**
     * Restart the current movie by closing and reopening it.
     */
    private void restartMovie() {
        closeMovie();
        reopenLast();
    }

    private void togglePlayPause() {
        if (player == null) return;

        if (player.getState() == PlayerState.PLAYING) {
            pause();
        } else {
            play();
        }
    }

    private void stepFrame() {
        if (player == null) return;

        // Use async execution to prevent UI freeze when debugger pauses
        player.stepFrameAsync(() -> {
            SwingUtilities.invokeLater(() -> {
                updateFrameLabel();
                stagePanel.repaint();
            });
        });
    }

    private void stepBackward() {
        if (player == null) return;

        int current = player.getCurrentFrame();
        if (current > 1) {
            player.goToFrame(current - 1);
            player.stepFrame();
            updateFrameLabel();
            stagePanel.repaint();
        }
    }

    private void goToFrame(int frame) {
        if (player == null) return;

        player.goToFrame(frame);
        if (player.getState() != PlayerState.PLAYING) {
            player.stepFrame();
        }
        updateFrameLabel();
        stagePanel.repaint();
    }

    private void goToLastFrame() {
        if (player == null) return;

        goToFrame(player.getFrameCount());
    }

    // Timer management

    private void startPlaybackTimer() {
        if (playbackTimer != null) {
            playbackTimer.stop();
        }

        int delay = 1000 / player.getTempo();
        playbackTimer = new Timer(delay, e -> {
            if (player == null) {
                stopPlaybackTimer();
                updateButtonStates();
                return;
            }

            // Check if debugger is paused - don't advance frame
            if (debugController.isPaused()) {
                return;
            }

            if (useAsyncExecution) {
                if (player.isVmRunning()) {
                    // VM is busy (prepareMovie or previous tick still running).
                    // Still repaint to show sprites set up during loading.
                    stagePanel.repaint();
                } else {
                    // VM is free — tick and repaint
                    player.tickAsync(() -> {
                        SwingUtilities.invokeLater(() -> {
                            updateFrameLabel();
                            stagePanel.repaint();
                            updateTimerDelay();
                        });
                    });
                }
            } else {
                // Synchronous execution (original behavior)
                if (player.tick()) {
                    updateFrameLabel();
                    stagePanel.repaint();
                    updateTimerDelay();
                } else {
                    stopPlaybackTimer();
                    updateButtonStates();
                }
            }
        });
        playbackTimer.start();
    }

    /**
     * Update the playback timer delay based on the current frame's effective tempo.
     * Tempo can change per frame via score tempo channel or puppetTempo.
     */
    private void updateTimerDelay() {
        if (playbackTimer == null || player == null) return;
        int newDelay = 1000 / player.getTempo();
        if (newDelay != playbackTimer.getDelay()) {
            playbackTimer.setDelay(newDelay);
        }
    }

    private void stopPlaybackTimer() {
        if (playbackTimer != null) {
            playbackTimer.stop();
            playbackTimer = null;
        }
    }

    // UI updates

    private void updateButtonStates() {
        boolean hasPlayer = player != null;
        PlayerState state = hasPlayer ? player.getState() : PlayerState.STOPPED;

        playButton.setEnabled(hasPlayer && state != PlayerState.PLAYING);
        pauseButton.setEnabled(hasPlayer && state == PlayerState.PLAYING);
        stopButton.setEnabled(hasPlayer && state != PlayerState.STOPPED);
        stepButton.setEnabled(hasPlayer);
    }

    private void updateFrameLabel() {
        if (player != null) {
            int current = player.getCurrentFrame();
            int total = player.getFrameCount();
            frameLabel.setText("Frame: " + current + " / " + total);

            if (!frameSlider.getValueIsAdjusting()) {
                frameSlider.setValue(current);
            }
        } else {
            frameLabel.setText("Frame: 1 / 1");
        }
    }

    private void showTraceHandlerDialog() {
        if (player == null) {
            JOptionPane.showMessageDialog(this, "No movie loaded.", "Trace Handler", JOptionPane.WARNING_MESSAGE);
            return;
        }
        var vm = player.getVM();
        var current = vm.getTracedHandlers();
        String currentStr = current.isEmpty() ? "" : String.join(", ", current);
        String input = JOptionPane.showInputDialog(this,
            "Enter handler names to trace (comma-separated), or clear to remove all:\n" +
            "Current: " + (currentStr.isEmpty() ? "(none)" : currentStr),
            "Trace Handler", JOptionPane.PLAIN_MESSAGE);
        if (input == null) return; // cancelled
        vm.clearTraceHandlers();
        if (!input.isBlank()) {
            for (String name : input.split(",")) {
                String trimmed = name.trim();
                if (!trimmed.isEmpty()) {
                    vm.addTraceHandler(trimmed);
                }
            }
        }
    }

    private void showAbout() {
        JOptionPane.showMessageDialog(this,
            "LibreShockwave Player\n\n" +
            "A Java-based player for Adobe Director/Macromedia Shockwave files.\n\n" +
            "Supports .dir, .dxr, .dcr, .cct, and .cst files.\n\n" +
            "Part of the LibreShockwave project.",
            "About",
            JOptionPane.INFORMATION_MESSAGE);
    }

    private void toggleDebugPanel(boolean visible) {
        debugVisible = visible;
        if (visible) {
            splitPane.setRightComponent(debuggerPanel);
            splitPane.setDividerLocation(0.6);
        } else {
            splitPane.setRightComponent(null);
        }
        splitPane.revalidate();
        // Keep toolbar button and menu item in sync
        if (debugToggleButton != null) debugToggleButton.setSelected(visible);
        if (debugMenuItem != null) debugMenuItem.setSelected(visible);
    }

    private void toggleDetailedStackWindow(boolean visible) {
        detailedStackWindow.setVisible(visible);
    }

    public BytecodeDebuggerPanel getDebuggerPanel() {
        return debuggerPanel;
    }

    public DebugController getDebugController() {
        return debugController;
    }

    // External params dialog and persistence

    private void showExternalParamsDialog() {
        ExternalParamsDialog dialog = new ExternalParamsDialog(this, currentExternalParams);
        Map<String, String> result = dialog.showDialog();
        if (result != null) {
            currentExternalParams = result;
            // Apply to current player if loaded
            if (player != null) {
                player.setExternalParams(currentExternalParams);
            }
            // Save to preferences
            saveExternalParams(currentMovieKey);
        }
    }

    private void saveExternalParams(String movieKey) {
        if (movieKey == null || movieKey.isEmpty()) {
            return;
        }
        String prefKey = PREF_EXTPARAMS_PREFIX + sanitizeKey(movieKey);
        if (currentExternalParams.isEmpty()) {
            prefs.remove(prefKey);
        } else {
            // Serialize as key1=value1\nkey2=value2\n...
            StringBuilder sb = new StringBuilder();
            for (var entry : currentExternalParams.entrySet()) {
                sb.append(entry.getKey()).append("=").append(entry.getValue()).append("\n");
            }
            prefs.put(prefKey, sb.toString());
        }
    }

    private void loadExternalParams(String movieKey) {
        currentExternalParams = new LinkedHashMap<>();
        if (movieKey == null || movieKey.isEmpty()) {
            return;
        }
        String prefKey = PREF_EXTPARAMS_PREFIX + sanitizeKey(movieKey);
        String serialized = prefs.get(prefKey, "");
        if (!serialized.isEmpty()) {
            for (String line : serialized.split("\n")) {
                int eq = line.indexOf('=');
                if (eq > 0) {
                    String key = line.substring(0, eq);
                    String value = line.substring(eq + 1);
                    currentExternalParams.put(key, value);
                }
            }
        }
    }

    // Breakpoint persistence

    /**
     * Save current breakpoints to preferences for the current movie.
     */
    private void saveBreakpoints() {
        if (currentMovieKey == null || currentMovieKey.isEmpty()) {
            return;
        }
        String serialized = debugController.serializeBreakpoints();
        String prefKey = PREF_BREAKPOINTS_PREFIX + sanitizeKey(currentMovieKey);
        if (serialized.isEmpty()) {
            prefs.remove(prefKey);
        } else {
            prefs.put(prefKey, serialized);
        }
    }

    /**
     * Load breakpoints from preferences for the given movie key.
     * Supports both new JSON format and legacy format.
     */
    private void loadBreakpoints(String movieKey) {
        if (movieKey == null || movieKey.isEmpty()) {
            return;
        }
        String prefKey = PREF_BREAKPOINTS_PREFIX + sanitizeKey(movieKey);
        String serialized = prefs.get(prefKey, "");
        if (!serialized.isEmpty()) {
            debugController.deserializeBreakpoints(serialized);
        }
    }

    /**
     * Clear saved breakpoints for the current movie.
     */
    private void clearSavedBreakpoints() {
        if (currentMovieKey == null || currentMovieKey.isEmpty()) {
            return;
        }
        String prefKey = PREF_BREAKPOINTS_PREFIX + sanitizeKey(currentMovieKey);
        prefs.remove(prefKey);
    }

    /**
     * Sanitize a key for use in preferences (remove problematic characters).
     */
    private String sanitizeKey(String key) {
        // Preferences keys have a max length and can't contain certain chars
        // Use a hash for long keys, and replace problematic characters
        if (key.length() > 80) {
            return "hash_" + key.hashCode();
        }
        return key.replace("/", "_").replace("\\", "_").replace(":", "_");
    }
}
