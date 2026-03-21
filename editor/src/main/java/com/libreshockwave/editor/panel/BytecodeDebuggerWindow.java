package com.libreshockwave.editor.panel;

import com.libreshockwave.DirectorFile;
import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.editor.EditorContext;
import com.libreshockwave.editor.debug.ui.*;
import com.libreshockwave.player.Player;
import com.libreshockwave.player.PlayerState;
import com.libreshockwave.player.debug.DebugController;
import com.libreshockwave.player.debug.DebugSnapshot;
import com.libreshockwave.player.debug.DebugStateListener;
import com.libreshockwave.player.debug.WatchExpression;
import com.libreshockwave.player.timeout.TimeoutManager;
import com.libreshockwave.vm.TraceListener;
import com.libreshockwave.vm.builtin.movie.MoviePropertyProvider;
import com.libreshockwave.vm.datum.Datum;

import javax.swing.*;
import java.awt.*;
import java.util.*;
import java.util.List;

/**
 * Bytecode debugger window for the editor.
 * Provides step/continue controls, breakpoint management, and state inspection.
 */
public class BytecodeDebuggerWindow extends EditorPanel implements DebugStateListener, TraceListener {

    private DebugController controller;

    // UI Components
    private final DebugToolbar toolbar;
    private final ScriptBrowserPanel scriptBrowser;
    private final BytecodeListPanel bytecodePanel;
    private final StateInspectionTabs stateTabs;
    private final JLabel statusLabel;
    private final JLabel handlerLabel;

    // Handler navigator
    private HandlerNavigator navigator;

    // Current handler info
    private volatile TraceListener.HandlerInfo currentHandlerInfo;
    private final Deque<TraceListener.HandlerInfo> handlerInfoStack = new ArrayDeque<>();

    // Track browse mode vs trace mode
    private boolean browseMode = false;
    private ScriptChunk browseScript = null;
    private ScriptChunk.Handler browseHandler = null;

    private static final String[] MOVIE_PROP_NAMES = {
        "frame", "lastFrame", "tempo", "timer", "ticks",
        "movieName", "platform", "exitLock", "itemDelimiter", "puppetTempo"
    };

    public BytecodeDebuggerWindow(EditorContext context) {
        super("bytecode-debugger", "Bytecode Debugger", context, true, true, true, true);

        JPanel mainContent = new JPanel(new BorderLayout());

        // Create toolbar
        toolbar = new DebugToolbar();
        mainContent.add(toolbar, BorderLayout.NORTH);

        // Main panel
        JPanel mainPanel = new JPanel(new BorderLayout(5, 5));
        mainPanel.setBorder(BorderFactory.createEmptyBorder(5, 5, 5, 5));

        // Status panel
        JPanel statusPanel = new JPanel(new BorderLayout());
        statusLabel = new JLabel("Status: Running");
        statusLabel.setFont(statusLabel.getFont().deriveFont(Font.BOLD));
        statusPanel.add(statusLabel, BorderLayout.NORTH);

        handlerLabel = new JLabel("Handler: -");
        handlerLabel.setFont(new Font(Font.MONOSPACED, Font.PLAIN, 11));
        statusPanel.add(handlerLabel, BorderLayout.SOUTH);

        mainPanel.add(statusPanel, BorderLayout.NORTH);

        // Split pane
        JSplitPane mainSplit = new JSplitPane(JSplitPane.VERTICAL_SPLIT);
        mainSplit.setResizeWeight(0.5);

        // Top: Bytecode panel with script browser
        JPanel topPanel = new JPanel(new BorderLayout());

        scriptBrowser = new ScriptBrowserPanel();
        scriptBrowser.setListener(new ScriptBrowserPanel.ScriptBrowserListener() {
            @Override
            public void onScriptSelected(ScriptItem script) {
                // Handler will be selected automatically
            }

            @Override
            public void onHandlerSelected(HandlerItem handler) {
                browseMode = true;
                browseScript = handler.getScript();
                browseHandler = handler.getHandler();
                bytecodePanel.setCurrentScriptId(browseScript.id().value());
                bytecodePanel.loadHandlerBytecode(browseScript, browseHandler);
                handlerLabel.setText("Handler: " + browseScript.getHandlerName(browseHandler) +
                    " (" + browseScript.getDisplayName() + ")");
            }
        });

        scriptBrowser.setViewHandlerDetailsAction(() -> {
            HandlerItem selected = scriptBrowser.getSelectedHandler();
            if (selected != null) {
                HandlerDetailsDialog.show(BytecodeDebuggerWindow.this, selected.getScript(), selected.getHandler(),
                    scriptBrowser.getDirectorFile());
            }
        });

        topPanel.add(scriptBrowser, BorderLayout.NORTH);

        bytecodePanel = new BytecodeListPanel();
        bytecodePanel.setListener(new BytecodeListPanel.BytecodeListListener() {
            @Override
            public void onBreakpointToggleRequested(int offset) {
                bytecodePanel.refreshBreakpointMarkers();
            }

            @Override
            public void onNavigateToHandler(String handlerName) {
                navigateToHandler(handlerName);
            }

            @Override
            public void onShowHandlerDetails(String handlerName) {
                HandlerDetailsDialog.show(BytecodeDebuggerWindow.this, scriptBrowser.getAllScripts(),
                    scriptBrowser.getDirectorFile(), handlerName);
            }
        });

        topPanel.add(bytecodePanel, BorderLayout.CENTER);
        mainSplit.setTopComponent(topPanel);

        // Bottom: State inspection tabs
        stateTabs = new StateInspectionTabs();
        stateTabs.setDatumClickListener((datum, title) ->
            DatumDetailsDialog.show(BytecodeDebuggerWindow.this, datum, title));

        // Configure watches panel
        stateTabs.getWatchesPanel().setListener(new WatchesPanel.WatchesPanelListener() {
            @Override
            public void onAddWatch(String expression) {
                if (controller != null) {
                    controller.addWatchExpression(expression);
                    refreshWatches();
                }
            }

            @Override
            public void onRemoveWatch(String watchId) {
                if (controller != null) {
                    controller.removeWatchExpression(watchId);
                    refreshWatches();
                }
            }

            @Override
            public void onEditWatch(String watchId, String newExpression) {
                if (controller != null) {
                    controller.updateWatchExpression(watchId, newExpression);
                    refreshWatches();
                }
            }

            @Override
            public void onClearWatches() {
                if (controller != null) {
                    controller.clearWatchExpressions();
                    refreshWatches();
                }
            }
        });

        // Tab change listener: capture snapshot when Objects tab is selected
        stateTabs.addChangeListener(e -> {
            Player player = context.getPlayer();
            if (stateTabs.isObjectsTabSelected() && player != null
                    && player.getState() == PlayerState.PLAYING) {
                player.pause();
                captureObjectsSnapshot();
            }
        });

        mainSplit.setBottomComponent(stateTabs);

        mainPanel.add(mainSplit, BorderLayout.CENTER);
        mainContent.add(mainPanel, BorderLayout.CENTER);

        setContentPane(mainContent);
        setSize(600, 700);
    }

    private void refreshWatches() {
        if (controller != null) {
            List<WatchExpression> watches = controller.evaluateWatchExpressions();
            stateTabs.setWatches(watches);
        }
    }

    private void captureObjectsSnapshot() {
        Player player = context.getPlayer();
        if (player == null) return;

        TimeoutManager tm = player.getTimeoutManager();
        List<String> names = tm.getTimeoutNames();
        List<TimeoutTableModel.TimeoutSnapshot> timeoutSnapshots = new ArrayList<>();
        for (String name : names) {
            Datum periodDatum = tm.getTimeoutProp(name, "period");
            Datum handlerDatum = tm.getTimeoutProp(name, "handler");
            Datum target = tm.getTimeoutProp(name, "target");
            Datum persistentDatum = tm.getTimeoutProp(name, "persistent");
            int period = periodDatum instanceof Datum.Int i ? i.value() : 0;
            String handler = handlerDatum instanceof Datum.Symbol s ? s.name() : handlerDatum.toStr();
            boolean persistent = persistentDatum.isTruthy();
            timeoutSnapshots.add(new TimeoutTableModel.TimeoutSnapshot(name, period, handler, target, persistent));
        }

        Map<String, Datum> globals = player.getVM().getGlobals();

        MoviePropertyProvider movieProps = player.getMovieProperties();
        List<Map.Entry<String, Datum>> moviePropEntries = new ArrayList<>();
        for (String prop : MOVIE_PROP_NAMES) {
            Datum value = movieProps.getMovieProp(prop);
            moviePropEntries.add(Map.entry(prop, value));
        }

        stateTabs.setObjects(timeoutSnapshots, globals, moviePropEntries);
    }

    private void navigateToHandler(String handlerName) {
        if (navigator == null) {
            navigator = new HandlerNavigator(scriptBrowser.getAllScripts());
        }

        HandlerNavigator.HandlerLocation location = navigator.findHandler(handlerName);
        if (location.found()) {
            browseMode = true;
            browseScript = location.script();
            browseHandler = location.handler();
            bytecodePanel.setCurrentScriptId(location.scriptId());

            scriptBrowser.selectScriptSilently(location.script());
            scriptBrowser.selectHandlerSilently(location.script(), location.handler());
            bytecodePanel.loadHandlerBytecode(location.script(), location.handler());
            handlerLabel.setText("Handler: " + location.handlerName() +
                " (" + location.script().getDisplayName() + ")");
        } else {
            statusLabel.setText("Handler '" + handlerName + "' not found");
        }
    }

    // --- EditorPanel lifecycle ---

    @Override
    protected void onFileOpened(DirectorFile file) {
        controller = context.getDebugController();
        if (controller != null) {
            toolbar.setController(controller);
            bytecodePanel.setController(controller);
            controller.addListener(this);
            controller.setDelegateListener(this);
        }

        Player player = context.getPlayer();
        bytecodePanel.clear();
        scriptBrowser.setDirectorFile(file, player != null ? player.getCastLibManager() : null);
        navigator = file != null ? new HandlerNavigator(scriptBrowser.getAllScripts()) : null;
        bytecodePanel.setNavigator(navigator);

        // Register keyboard shortcuts
        DebugKeyboardHandler.registerShortcuts(getRootPane(), controller);
    }

    @Override
    protected void onCastsLoaded() {
        // Refresh script list when external casts finish loading
        scriptBrowser.refreshScriptList();
        navigator = new HandlerNavigator(scriptBrowser.getAllScripts());
        bytecodePanel.setNavigator(navigator);
    }

    @Override
    protected void onFileClosed() {
        bytecodePanel.clear();
        scriptBrowser.setDirectorFile(null, null);
        browseMode = false;
        browseScript = null;
        browseHandler = null;
        navigator = null;
        statusLabel.setText("Status: Running");
        handlerLabel.setText("Handler: -");
        scriptBrowser.clearFilters();
        controller = null;
    }

    // --- DebugStateListener ---

    @Override
    public void onPaused(DebugSnapshot snapshot) {
        SwingUtilities.invokeLater(() -> {
            statusLabel.setText("Status: PAUSED at offset " + snapshot.instructionOffset());
            handlerLabel.setText("Handler: " + snapshot.handlerName() + " (" + snapshot.scriptName() + ")");

            bytecodePanel.setCurrentScriptId(snapshot.scriptId());

            if (navigator != null) {
                HandlerNavigator.HandlerLocation location =
                    navigator.findHandlerInScript(snapshot.scriptId(), snapshot.handlerName());
                if (location.found()) {
                    browseMode = false;
                    bytecodePanel.loadHandlerBytecode(location.script(), location.handler());
                    scriptBrowser.selectScriptSilently(location.script());
                    scriptBrowser.selectHandlerSilently(location.script(), location.handler());
                }
            }

            bytecodePanel.highlightCurrentInstruction(snapshot.instructionIndex());

            stateTabs.setStack(snapshot.stack());
            stateTabs.setLocals(snapshot.locals());
            stateTabs.setGlobals(snapshot.globals());

            if (snapshot.watchResults() != null) {
                stateTabs.setWatches(snapshot.watchResults());
            } else {
                refreshWatches();
            }

            captureObjectsSnapshot();
            toolbar.setStepButtonsEnabled(true);
        });
    }

    @Override
    public void onResumed() {
        SwingUtilities.invokeLater(() -> {
            statusLabel.setText("Status: Running");
            toolbar.setStepButtonsEnabled(false);
            bytecodePanel.getBytecodeList().clearSelection();
        });
    }

    @Override
    public void onBreakpointsChanged() {
        SwingUtilities.invokeLater(() -> bytecodePanel.refreshBreakpointMarkers());
    }

    @Override
    public void onWatchExpressionsChanged() {
        refreshWatches();
    }

    // --- TraceListener ---

    @Override
    public void onHandlerEnter(HandlerInfo info) {
        currentHandlerInfo = info;
        handlerInfoStack.push(info);
        bytecodePanel.setCurrentScriptId(info.scriptId());
    }

    @Override
    public void onHandlerExit(HandlerInfo info, Datum returnValue) {
        if (!handlerInfoStack.isEmpty()) {
            handlerInfoStack.pop();
        }
        currentHandlerInfo = handlerInfoStack.isEmpty() ? null : handlerInfoStack.peek();
    }

    @Override
    public void onInstruction(InstructionInfo info) {
        // Only update UI on pause/breakpoint (see onPaused), not during live execution
    }
}
