#include "DebuggerWindow.hpp"

#include <QApplication>
#include <QCryptographicHash>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QUrl>

#include <algorithm>
#include <iostream>

#include "DebuggerContext.hpp"
#include "DebugStateBridge.hpp"
#include "ExternalParamsDialog.hpp"
#include "format/DatumFormat.hpp"
#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/lingo/decompiler/LingoDecompiler.hpp"
#include "libreshockwave/lingo/vm/trace/InstructionAnnotator.hpp"
#include "libreshockwave/player/Player.hpp"
#include "libreshockwave/player/cast/CastLib.hpp"
#include "libreshockwave/player/cast/CastLibManager.hpp"
#include "libreshockwave/player/debug/BreakpointManager.hpp"
#include "libreshockwave/player/debug/DebugController.hpp"
#include "libreshockwave/player/render/pipeline/FrameSnapshot.hpp"
#include "StartupSession.hpp"
#include "ui/CallStackPanel.hpp"
#include "ui/CodeViewPanel.hpp"
#include "ui/DebugWindowPanel.hpp"
#include "ui/MovieTreePanel.hpp"
#include "ui/StageWidget.hpp"
#include "ui/VariablesPanel.hpp"
#include "ui/WatchPanel.hpp"

namespace libreshockwave::debugger {

// -----------------------------------------------------------------------
// Keys for QSettings persistence
// -----------------------------------------------------------------------

static const char* kSettingGeometry       = "debugger/geometry";
// Versioned: the Inspector dock was split into three docks (call stack,
// variables, watches); restoreState() must never be fed the pre-split bytes.
static const char* kSettingState          = "debugger/state2";
static const char* kSettingLastSession    = "debugger/lastSession";
static const char* kSettingRecentSessions = "debugger/recentSessions";
// Legacy settings retained so existing debugger installations are upgraded.
static const char* kSettingLastMovie      = "debugger/lastMovie";
static const char* kSettingRecentMovies   = "debugger/recentMovies";
static const char* kSettingLastDir        = "debugger/lastDir";
static const char* kSettingLastRecordingDir = "debugger/lastRecordingDir";
static const char* kSettingLastUrl        = "debugger/lastUrl";
static const char* kSettingExternalParams = "debugger/externalParams";
static const char* kSettingBreakpoints    = "debugger/breakpoints";
static const int  kMaxRecentSessions      = 8;
static constexpr qint64 kRecordingFlushIntervalMs = 1000;
static constexpr qint64 kRecordingMouseMoveIntervalMs = 50;

// Breakpoint entries live under "debugger/breakpoints/<hash>" with the movie
// path/URL hashed into the sub-key: QSettings keys may not contain '/', and
// movie paths and URLs usually do.  Each entry is "scriptId\thandlerName\toffset\tenabled".
static QString breakpointSettingKey(const QString& movieIdentity) {
    const QByteArray hash =
        QCryptographicHash::hash(movieIdentity.toUtf8(), QCryptographicHash::Md5);
    return QString::fromLatin1(kSettingBreakpoints) + QLatin1Char('/') +
           QString::fromLatin1(hash.toHex());
}

static QString recordingEventTypeName(int type) {
    using InputEvent = DebuggerContext::InputEvent;
    switch (static_cast<InputEvent::Type>(type)) {
        case InputEvent::MouseMove: return QStringLiteral("mouseMove");
        case InputEvent::MouseDown: return QStringLiteral("mouseDown");
        case InputEvent::MouseUp: return QStringLiteral("mouseUp");
        case InputEvent::KeyDown: return QStringLiteral("keyDown");
        case InputEvent::KeyUp: return QStringLiteral("keyUp");
    }
    return {};
}

static bool recordingEventTypeFromName(const QString& name, int& type) {
    using InputEvent = DebuggerContext::InputEvent;
    if (name == QStringLiteral("mouseMove")) type = InputEvent::MouseMove;
    else if (name == QStringLiteral("mouseDown")) type = InputEvent::MouseDown;
    else if (name == QStringLiteral("mouseUp")) type = InputEvent::MouseUp;
    else if (name == QStringLiteral("keyDown")) type = InputEvent::KeyDown;
    else if (name == QStringLiteral("keyUp")) type = InputEvent::KeyUp;
    else return false;
    return true;
}

// -----------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------

DebuggerWindow::DebuggerWindow(QWidget* parent)
    : QMainWindow(parent) {

    ExternalParamsDialog::ensureDefaultPresetFile();
    setWindowTitle(QStringLiteral("LibreShockwave Debugger"));
    setMinimumSize(1024, 640);
    resize(1280, 800);

    context_ = new DebuggerContext(this);
    recordingFlushTimer_ = new QTimer(this);
    recordingFlushTimer_->setInterval(static_cast<int>(kRecordingFlushIntervalMs));
    recordingFlushTimer_->setTimerType(Qt::CoarseTimer);
    connect(recordingFlushTimer_, &QTimer::timeout,
            this, &DebuggerWindow::onRecordingFlushTimer);

    replayTimer_ = new QTimer(this);
    replayTimer_->setInterval(16);
    replayTimer_->setTimerType(Qt::CoarseTimer);
    connect(replayTimer_, &QTimer::timeout,
            this, &DebuggerWindow::onReplayTimer);

    setupCentralWidget();
    setupDockWidgets();
    setupMenuBar();
    setupToolBar();
    setupShortcuts();
    connectSignals();

    restoreSettings();
    updateToolbarState();

    // Restore the last selected movie without starting playback. The user
    // explicitly controls execution with Play.
    QSettings settings;
    lastMovie_ = settings.value(QString::fromLatin1(kSettingLastMovie)).toString();
    lastSession_ = settings.value(QString::fromLatin1(kSettingLastSession)).toString();
    const auto canRestore = [this](const QString& path) {
        return !path.isEmpty() && (isUrl(path) || QFileInfo::exists(path));
    };
    const auto startup_movie = select_startup_movie_source(
        canRestore(lastMovie_) ? lastMovie_ : QString(), lastSession_);
    settingsReady_ = true;
    // A command-line movie is already opened by main().  Do not also restore
    // the previous URL here: the two asynchronous sessions can finish in
    // either order and leave the debugger displaying the wrong movie/frame.
    // Options such as --play are not movies. Only suppress the persisted
    // movie when the command line actually contains a positional path.
    const auto arguments = QCoreApplication::arguments();
    const bool hasCommandLineMovie = std::any_of(
        arguments.cbegin() + (arguments.isEmpty() ? 0 : 1), arguments.cend(),
        [](const QString& argument) { return !argument.startsWith(QLatin1Char('-')); });
    if (!hasCommandLineMovie && canRestore(startup_movie)) {
        statusBar()->showMessage(QStringLiteral("Loading last movie..."));
        openMovie(startup_movie, externalParams_);
    } else {
        statusBar()->showMessage(
            QStringLiteral("Ready — Open a movie to begin (Ctrl+O)"));
    }
}

DebuggerWindow::~DebuggerWindow() {
    saveSettings();
}

// -----------------------------------------------------------------------
// Movie loading
// -----------------------------------------------------------------------

bool DebuggerWindow::openMovie(const QString& path, const QMap<QString, QString>& params) {
    playRequested_ = false;
    if (QFileInfo(path).suffix().compare(QStringLiteral("lswdebug"),
                                         Qt::CaseInsensitive) == 0) {
        return openRecording(path);
    }
    if (recording_) {
        context_->stop();
        finishRecording();
    }
    cancelReplay();
    pendingReplay_.reset();
    return openMovieInternal(path, params);
}

bool DebuggerWindow::openMovieInternal(const QString& path,
                                       const QMap<QString, QString>& params) {
    if (path.isEmpty()) return false;

    // URLs are fetched asynchronously; the shared finish path only loads.
    if (isUrl(path)) {
        // Set this before the asynchronous URL fetch. A recording's
        // parameters must replace the parameters restored from the previous
        // debugger session, even though the Player does not exist until the
        // fetch ends.
        externalParams_ = params;
        const bool isRecordingSession = pendingReplay_.has_value();
        lastMovie_ = path;
        addRecentSession(path, !isRecordingSession);
        loadMovieFromUrl(path);
        return true;
    }

    statusBar()->showMessage(QStringLiteral("Loading %1...").arg(path));

    const bool ok = context_->loadMovie(path.toStdString());
    if (!ok) {
        statusBar()->showMessage(QStringLiteral("Failed to load movie"), 5000);
        return false;
    }

    stageWidget_->prepareForMovie();
    isPaused_ = false;

    // Store parameters in the context so a later fresh Play session receives
    // the same values as this initial load.
    externalParams_ = params;
    context_->setExternalParams(ExternalParamsDialog::toPlayerParams(params));

    setWindowTitle(QStringLiteral("LibreShockwave Debugger — %1").arg(
        QFileInfo(path).fileName()));
    movieTreePanel_->populate(context_->player());
    restoreBreakpointsForCurrentMovie();
    const bool isRecordingSession = pendingReplay_.has_value();
    lastMovie_ = path;
    addRecentSession(path, !isRecordingSession);
    refreshBreakpoints();

    statusLabel_->setText(QStringLiteral(" ● Ready"));
    statusLabel_->setStyleSheet(
        QStringLiteral("color:#8a9488; font-weight:bold; padding:0 8px;"));
    updateToolbarState();

    statusBar()->showMessage(
        QStringLiteral("Loaded: %1 — press Play to start").arg(QFileInfo(path).fileName()));
    if (!replaying_ && playRequested_) {
        playRequested_ = false;
        onPlay();
    }
    return true;
}

void DebuggerWindow::startPlayback() {
    playRequested_ = true;
    if (!context_->hasMovie()) {
        return;
    }

    // Do not restart a replay that is already active.
    if (replaying_ && !replayWaitingForNavigation_) {
        playRequested_ = false;
        return;
    }

    playRequested_ = false;
    onPlay();
}

// -----------------------------------------------------------------------
// Persistence
// -----------------------------------------------------------------------

void DebuggerWindow::saveSettings() {
    if (!settingsReady_) {
        return;
    }

    QSettings settings;
    settings.setValue(QString::fromLatin1(kSettingGeometry), saveGeometry());
    settings.setValue(QString::fromLatin1(kSettingState), saveState());
    settings.setValue(QString::fromLatin1(kSettingLastMovie), lastMovie_);
    settings.setValue(QString::fromLatin1(kSettingLastSession), lastSession_);
    settings.setValue(QString::fromLatin1(kSettingRecentSessions), recentSessions_);

    // Save external params as a serialized list
    QStringList paramList;
    for (auto it = externalParams_.cbegin(); it != externalParams_.cend(); ++it) {
        paramList.append(it.key() + QStringLiteral("=") + it.value());
    }
    settings.setValue(QString::fromLatin1(kSettingExternalParams), paramList);
}

void DebuggerWindow::onResetLayout() {
    // Re-apply the layout captured at startup: default positions, splits,
    // sizes, and visibility of every dock.
    restoreState(defaultLayoutState_);
}

void DebuggerWindow::restoreSettings() {
    QSettings settings;

    // Window geometry
    if (settings.contains(QString::fromLatin1(kSettingGeometry))) {
        restoreGeometry(settings.value(QString::fromLatin1(kSettingGeometry)).toByteArray());
    }
    if (settings.contains(QString::fromLatin1(kSettingState))) {
        restoreState(settings.value(QString::fromLatin1(kSettingState)).toByteArray());
    }

    // Recent movie sources and recording sessions.
    if (settings.contains(QString::fromLatin1(kSettingRecentSessions))) {
        recentSessions_ =
            settings.value(QString::fromLatin1(kSettingRecentSessions)).toStringList();
    } else {
        recentSessions_ = settings.value(QString::fromLatin1(kSettingRecentMovies)).toStringList();
        const auto recordingDirectory =
            settings.value(QString::fromLatin1(kSettingLastRecordingDir)).toString();
        if (!recordingDirectory.isEmpty()) {
            const auto recordingFiles = QDir(recordingDirectory).entryInfoList(
                {QStringLiteral("*.lswdebug")}, QDir::Files, QDir::Time);
            for (const auto& fileInfo : recordingFiles) {
                if (recentSessions_.size() >= kMaxRecentSessions) {
                    break;
                }
                const auto path = fileInfo.absoluteFilePath();
                if (!recentSessions_.contains(path)) {
                    recentSessions_.append(path);
                }
            }
        }
    }
    recentSessions_.removeDuplicates();
    while (recentSessions_.size() > kMaxRecentSessions) {
        recentSessions_.removeLast();
    }
    lastMovie_ = settings.value(QString::fromLatin1(kSettingLastMovie)).toString();
    lastSession_ = settings.value(QString::fromLatin1(kSettingLastSession)).toString();
    updateRecentSessionsMenu();

    // External params
    const auto paramList = settings.value(QString::fromLatin1(kSettingExternalParams)).toStringList();
    for (const auto& entry : paramList) {
        const int eq = entry.indexOf(QLatin1Char('='));
        if (eq > 0) {
            externalParams_.insert(entry.left(eq), entry.mid(eq + 1));
        }
    }
    updateParamsMenu();
}

QString DebuggerWindow::currentMovieKey() const {
    return context_ == nullptr ? QString()
                               : QString::fromStdString(context_->moviePath());
}

void DebuggerWindow::restoreBreakpointsForCurrentMovie() {
    auto* dc = context_->debugController();
    if (dc == nullptr) return;

    QSettings settings;
    const QString movieKey = currentMovieKey();
    const QStringList entries =
        settings.value(breakpointSettingKey(movieKey)).toStringList();
    if (entries.isEmpty()) return;

    bool hasEnabledBreakpoint = false;
    auto& manager = dc->breakpointManager();
    for (const auto& entry : entries) {
        const auto parts = entry.split(QLatin1Char('\t'));
        if (parts.size() != 4) continue;
        bool okScriptId = false;
        bool okOffset = false;
        const int scriptId = parts[0].toInt(&okScriptId);
        const int offset = parts[2].toInt(&okOffset);
        if (!okScriptId || !okOffset) continue;

        const bool enabled = parts[3] == QLatin1String("1");
        const auto breakpoint =
            manager.addBreakpoint(scriptId, parts[1].toStdString(), offset);
        if (!enabled) {
            manager.setBreakpoint(breakpoint.withEnabled(false));
        } else {
            hasEnabledBreakpoint = true;
        }
    }

    if (hasEnabledBreakpoint) {
        // Breakpoints only fire while instruction tracing is on — the same
        // requirement toggleBreakpoint() enforces.
        dc->setTracingEnabled(true);
    }
}

void DebuggerWindow::persistBreakpoints() {
    auto* dc = context_->debugController();
    if (dc == nullptr) return;

    QStringList entries;
    for (const auto& bp : dc->breakpointManager().getAllBreakpoints()) {
        const QStringList fields{
            QString::number(bp.scriptId),
            QString::fromStdString(bp.handlerName),
            QString::number(bp.offset),
            QString::number(bp.enabled ? 1 : 0)};
        entries.append(fields.join(QLatin1Char('\t')));
    }

    QSettings settings;
    settings.setValue(breakpointSettingKey(currentMovieKey()), entries);
}

void DebuggerWindow::addRecentSession(const QString& path, bool makeLastSession) {
    recentSessions_.removeAll(path);
    recentSessions_.prepend(path);
    while (recentSessions_.size() > kMaxRecentSessions) {
        recentSessions_.removeLast();
    }
    if (makeLastSession) {
        lastSession_ = path;
    }
    updateRecentSessionsMenu();

    // Persist both the movie source and the target used to reopen a session.
    QSettings settings;
    settings.setValue(QString::fromLatin1(kSettingLastMovie), lastMovie_);
    settings.setValue(QString::fromLatin1(kSettingLastSession), lastSession_);
    settings.setValue(QString::fromLatin1(kSettingRecentSessions), recentSessions_);
}

void DebuggerWindow::updateRecentSessionsMenu() {
    recentSessionsMenu_->clear();
    if (recentSessions_.isEmpty()) {
        recentSessionsMenu_->addAction(QStringLiteral("(none)"))->setEnabled(false);
        return;
    }
    for (const auto& path : recentSessions_) {
        // Files are labelled by their file name; URLs by their path segment.
        QString label = QFileInfo(path).fileName();
        if (label.isEmpty() || isUrl(path)) {
            label = QUrl(path).host() + QUrl(path).path();
        }
        if (label.isEmpty()) {
            label = path;
        }
        auto* action = recentSessionsMenu_->addAction(
            label + QStringLiteral("  —  ") + path);
        action->setData(path);
        connect(action, &QAction::triggered, this, &DebuggerWindow::onOpenRecent);
    }
    recentSessionsMenu_->addSeparator();
    auto* clearAction = recentSessionsMenu_->addAction(QStringLiteral("Clear Recent"));
    connect(clearAction, &QAction::triggered, this, [this] {
        recentSessions_.clear();
        updateRecentSessionsMenu();
    });
}

// -----------------------------------------------------------------------
// Close event
// -----------------------------------------------------------------------

void DebuggerWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    cancelReplay();
    context_->stop();
    finishRecording();
    event->accept();
}

// -----------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------

void DebuggerWindow::setupMenuBar() {
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));

    auto* openAction = fileMenu->addAction(QStringLiteral("&Open Movie..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &DebuggerWindow::onOpenMovie);

    auto* openUrlAction = fileMenu->addAction(QStringLiteral("Open &URL..."));
    openUrlAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(openUrlAction, &QAction::triggered, this, &DebuggerWindow::onOpenUrl);

    auto* openRecordingAction = fileMenu->addAction(
        QStringLiteral("Open Debug &Recording..."));
    openRecordingAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    connect(openRecordingAction, &QAction::triggered,
            this, &DebuggerWindow::onOpenRecording);

    recentSessionsMenu_ = fileMenu->addMenu(QStringLiteral("&Recent Sessions"));
    updateRecentSessionsMenu();

    fileMenu->addSeparator();

    auto* quitAction = fileMenu->addAction(QStringLiteral("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    // ---- Parameters menu ----
    paramsMenu_ = menuBar()->addMenu(QStringLiteral("&Parameters"));

    auto* editParamsAction = paramsMenu_->addAction(
        QStringLiteral("&Edit Parameters..."));
    editParamsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    connect(editParamsAction, &QAction::triggered,
            this, &DebuggerWindow::onEditParameters);

    auto* defaultPresetAction = paramsMenu_->addAction(
        QStringLiteral("&Default Preset"));
    connect(defaultPresetAction, &QAction::triggered,
            this, &DebuggerWindow::onLoadDefaultPreset);

    auto* clearParamsAction = paramsMenu_->addAction(
        QStringLiteral("&Clear All Parameters"));
    connect(clearParamsAction, &QAction::triggered,
            this, &DebuggerWindow::onClearParameters);

    paramsMenu_->addSeparator();
    // Current params shown below (updated dynamically)

    // ---- Debug menu ----
    auto* debugMenu = menuBar()->addMenu(QStringLiteral("&Debug"));

    continueAction_ = debugMenu->addAction(QStringLiteral("&Continue"));
    continueAction_->setShortcut(QKeySequence(Qt::Key_F5));
    connect(continueAction_, &QAction::triggered, this, &DebuggerWindow::onContinue);

    pauseAction_ = debugMenu->addAction(QStringLiteral("&Pause"));
    pauseAction_->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(pauseAction_, &QAction::triggered, this, &DebuggerWindow::onPause);

    debugMenu->addSeparator();

    stepIntoAction_ = debugMenu->addAction(QStringLiteral("Step &Into"));
    stepIntoAction_->setShortcut(QKeySequence(Qt::Key_F11));
    connect(stepIntoAction_, &QAction::triggered, this, &DebuggerWindow::onStepInto);

    stepOverAction_ = debugMenu->addAction(QStringLiteral("Step &Over"));
    stepOverAction_->setShortcut(QKeySequence(Qt::Key_F10));
    connect(stepOverAction_, &QAction::triggered, this, &DebuggerWindow::onStepOver);

    stepOutAction_ = debugMenu->addAction(QStringLiteral("Step O&ut"));
    stepOutAction_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F11));
    connect(stepOutAction_, &QAction::triggered, this, &DebuggerWindow::onStepOut);

    debugMenu->addSeparator();

    toggleBreakpointAction_ = debugMenu->addAction(
        QStringLiteral("Toggle &Breakpoint"));
    toggleBreakpointAction_->setShortcut(QKeySequence(Qt::Key_F9));
    connect(toggleBreakpointAction_, &QAction::triggered,
            this, &DebuggerWindow::onToggleBreakpoint);

    clearBreakpointsAction_ = debugMenu->addAction(
        QStringLiteral("&Clear All Breakpoints"));
    connect(clearBreakpointsAction_, &QAction::triggered,
            this, &DebuggerWindow::onClearBreakpoints);

    // ---- View menu ----
    auto* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(leftDock_->toggleViewAction());
    viewMenu->addAction(callStackDock_->toggleViewAction());
    viewMenu->addAction(variablesDock_->toggleViewAction());
    viewMenu->addAction(watchDock_->toggleViewAction());
    viewMenu->addAction(bottomDock_->toggleViewAction());
    viewMenu->addAction(debugDock_->toggleViewAction());

    viewMenu->addSeparator();
    auto* resetLayoutAction = viewMenu->addAction(QStringLiteral("&Reset Layout"));
    connect(resetLayoutAction, &QAction::triggered,
            this, &DebuggerWindow::onResetLayout);
}

void DebuggerWindow::setupToolBar() {
    auto* toolbar = addToolBar(QStringLiteral("Debug"));
    toolbar->setObjectName(QStringLiteral("debugToolbar"));
    toolbar->setMovable(false);

    auto* openAction = toolbar->addAction(QStringLiteral("📂 Open..."));
    connect(openAction, &QAction::triggered, this, &DebuggerWindow::onOpenMovie);

    toolbar->addSeparator();

    playAction_ = toolbar->addAction(QStringLiteral("▶ Play"));
    connect(playAction_, &QAction::triggered, this, &DebuggerWindow::onPlay);

    recordAction_ = toolbar->addAction(QStringLiteral("▶⏺ Play & Record"));
    connect(recordAction_, &QAction::triggered,
            this, &DebuggerWindow::onPlayAndRecord);

    pauseAction_ = toolbar->addAction(QStringLiteral("⏸ Pause"));
    connect(pauseAction_, &QAction::triggered, this, &DebuggerWindow::onPause);

    stopAction_ = toolbar->addAction(QStringLiteral("⏹ Stop"));
    connect(stopAction_, &QAction::triggered, this, &DebuggerWindow::onStop);

    toolbar->addSeparator();

    continueAction_ = toolbar->addAction(QStringLiteral("▶ Continue"));
    connect(continueAction_, &QAction::triggered, this, &DebuggerWindow::onContinue);

    stepIntoAction_ = toolbar->addAction(QStringLiteral("↓ Into"));
    connect(stepIntoAction_, &QAction::triggered, this, &DebuggerWindow::onStepInto);

    stepOverAction_ = toolbar->addAction(QStringLiteral("→ Over"));
    connect(stepOverAction_, &QAction::triggered, this, &DebuggerWindow::onStepOver);

    stepOutAction_ = toolbar->addAction(QStringLiteral("↑ Out"));
    connect(stepOutAction_, &QAction::triggered, this, &DebuggerWindow::onStepOut);

    toolbar->addSeparator();

    auto* clearBpAction = toolbar->addAction(QStringLiteral("Clear BPs"));
    connect(clearBpAction, &QAction::triggered, this, &DebuggerWindow::onClearBreakpoints);

    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    statusLabel_ = new QLabel(QStringLiteral(" ● Stopped"), this);
    statusLabel_->setStyleSheet(
        QStringLiteral("color:#8a9488; font-weight:bold; padding:0 8px;"));
    toolbar->addWidget(statusLabel_);
}

// Right-clicking a dock's title bar or panel body shows "Hide Panel".
// QDockWidget and the panel widgets don't consume QContextMenuEvent, so one
// CustomContextMenu policy per dock covers everything (except text fields,
// which correctly show Qt's own edit menu).
static void addHidePanelMenu(QDockWidget* dock) {
    dock->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(dock, &QWidget::customContextMenuRequested, dock,
                     [dock](const QPoint& pos) {
                         QMenu menu(dock);
                         menu.addAction(QStringLiteral("Hide Panel"), dock,
                                        [dock] { dock->hide(); });
                         menu.exec(dock->mapToGlobal(pos));
                     });
}

void DebuggerWindow::setupDockWidgets() {
    // Left dock: Movie tree
    leftDock_ = new QDockWidget(QStringLiteral("Movies & Scripts"), this);
    leftDock_->setObjectName(QStringLiteral("movieTreeDock"));
    movieTreePanel_ = new MovieTreePanel(leftDock_);
    leftDock_->setWidget(movieTreePanel_);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock_);

    // Right docks: Call Stack / Variables / Watches stacked vertically.
    // Add order matters for saveState()/restoreState(): a split host must be
    // added before the dock split out of it.
    callStackDock_ = new QDockWidget(QStringLiteral("Call Stack"), this);
    callStackDock_->setObjectName(QStringLiteral("callStackDock"));
    callStackPanel_ = new CallStackPanel(callStackDock_);
    callStackDock_->setWidget(callStackPanel_);
    addDockWidget(Qt::RightDockWidgetArea, callStackDock_);

    variablesDock_ = new QDockWidget(QStringLiteral("Variables"), this);
    variablesDock_->setObjectName(QStringLiteral("variablesDock"));
    variablesPanel_ = new VariablesPanel(variablesDock_);
    variablesDock_->setWidget(variablesPanel_);
    addDockWidget(Qt::RightDockWidgetArea, variablesDock_);
    splitDockWidget(callStackDock_, variablesDock_, Qt::Vertical);

    watchDock_ = new QDockWidget(QStringLiteral("Watches"), this);
    watchDock_->setObjectName(QStringLiteral("watchDock"));
    watchPanel_ = new WatchPanel(watchDock_);
    watchDock_->setWidget(watchPanel_);
    addDockWidget(Qt::RightDockWidgetArea, watchDock_);
    splitDockWidget(variablesDock_, watchDock_, Qt::Vertical);

    // Initial sizes match the old stacked Inspector (call stack ~150px,
    // watches ~220px; variables takes the remainder).
    resizeDocks({callStackDock_, watchDock_}, {150, 220}, Qt::Vertical);

    // Bottom dock: Code view
    bottomDock_ = new QDockWidget(QStringLiteral("Code"), this);
    bottomDock_->setObjectName(QStringLiteral("codeDock"));
    codeViewPanel_ = new CodeViewPanel(bottomDock_);
    bottomDock_->setWidget(codeViewPanel_);
    addDockWidget(Qt::BottomDockWidgetArea, bottomDock_);

    // Bottom-right dock: Debug window, beside the code view
    debugDock_ = new QDockWidget(QStringLiteral("Debug Window"), this);
    debugDock_->setObjectName(QStringLiteral("debugWindowDock"));
    debugWindowPanel_ = new DebugWindowPanel(debugDock_);
    debugDock_->setWidget(debugWindowPanel_);
    addDockWidget(Qt::BottomDockWidgetArea, debugDock_);
    splitDockWidget(bottomDock_, debugDock_, Qt::Horizontal);

    // Per-dock "Hide Panel" context menu.
    for (QDockWidget* dock : {leftDock_, callStackDock_, variablesDock_,
                              watchDock_, bottomDock_, debugDock_}) {
        addHidePanelMenu(dock);
    }

    // Snapshot of the default layout for View > Reset Layout. Captured here
    // (before restoreSettings()) so it is independent of any saved state.
    defaultLayoutState_ = saveState();
}

void DebuggerWindow::setupCentralWidget() {
    stageWidget_ = new StageWidget(this);
    setCentralWidget(stageWidget_);

    stageWidget_->setInputCallback(
        [this](int type, int stageX, int stageY,
               int keyCode, const std::string& keyText,
               bool shift, bool ctrl, bool alt, bool rightButton) {
            recordStageInput(type, stageX, stageY, keyCode, keyText,
                             shift, ctrl, alt, rightButton);
        });
}

void DebuggerWindow::setupShortcuts() {
    // Shortcuts handled via menu QAction shortcuts
}

void DebuggerWindow::connectSignals() {
    connect(context_, &DebuggerContext::movieLoaded, this, [this]() {
        if (context_->player() != nullptr) {
            movieTreePanel_->populate(context_->player());
            restoreBreakpointsForCurrentMovie();
            refreshBreakpoints();
        }
        // Start each movie with a fresh debug log; keep the toggle state.
        debugWindowPanel_->setDebugEnabled(context_->debugEnabled());
        debugWindowPanel_->clearLog();
        updateToolbarState();
    });
    connect(debugWindowPanel_, &DebugWindowPanel::debugEnabledChanged,
            context_, &DebuggerContext::setDebugEnabled);
    connect(context_, &DebuggerContext::vmOutput,
            debugWindowPanel_, &DebugWindowPanel::appendLogLine);
    connect(context_, &DebuggerContext::frameRendered,
            this, &DebuggerWindow::onFrameRendered);
    connect(context_, &DebuggerContext::playbackStarted,
            this, &DebuggerWindow::onPlaybackStarted);
    connect(context_, &DebuggerContext::errorOccurred,
            this, &DebuggerWindow::onErrorOccurred);

    auto* bridge = context_->stateBridge();
    connect(bridge, &DebugStateBridge::paused, this, &DebuggerWindow::onPaused);
    connect(bridge, &DebugStateBridge::resumed, this, &DebuggerWindow::onResumed);
    connect(bridge, &DebugStateBridge::breakpointsChanged,
            this, &DebuggerWindow::onBreakpointsChanged);

    connect(movieTreePanel_, &MovieTreePanel::scriptSelected,
            this, &DebuggerWindow::onScriptSelected);
    connect(movieTreePanel_, &MovieTreePanel::handlerSelected,
            this, &DebuggerWindow::onHandlerSelected);
    connect(codeViewPanel_, &CodeViewPanel::handlerChanged,
            this, &DebuggerWindow::onCodeHandlerChanged);
    connect(codeViewPanel_, &CodeViewPanel::breakpointToggled,
            this, &DebuggerWindow::onBreakpointToggled);
    connect(context_, &DebuggerContext::movieNavigationRequested,
            this, &DebuggerWindow::onMovieNavigationRequested);
    // Runtime CCT/CST loads arrive as a snapshot from the VM thread; rebuild
    // the script list from it so newly loaded scripts appear.
    connect(context_, &DebuggerContext::castLoaded, this,
            [this](const MovieTreeSnapshot& snapshot) {
                movieTreePanel_->populateFromSnapshot(snapshot);
            });
    connect(watchPanel_, &WatchPanel::watchAdded,
            this, &DebuggerWindow::onWatchAdded);
    connect(watchPanel_, &WatchPanel::watchRemoved,
            this, &DebuggerWindow::onWatchRemoved);

    // Persist the dock layout immediately on user-driven changes
    // (hide/show, float/dock, move) so a crash or kill does not lose them.
    // Splitter drag-resizes are still captured by saveSettings() on close.
    for (QDockWidget* dock : {leftDock_, callStackDock_, variablesDock_,
                              watchDock_, bottomDock_, debugDock_}) {
        connect(dock, &QDockWidget::visibilityChanged,
                this, &DebuggerWindow::saveSettings);
        connect(dock, &QDockWidget::topLevelChanged,
                this, &DebuggerWindow::saveSettings);
        connect(dock, &QDockWidget::dockLocationChanged,
                this, &DebuggerWindow::saveSettings);
    }
}

// -----------------------------------------------------------------------
// Actions — File
// -----------------------------------------------------------------------

void DebuggerWindow::onOpenMovie() {
    // Remember the last directory the user browsed so re-opening is quick.
    QSettings settings;
    const auto lastDir = settings.value(QString::fromLatin1(kSettingLastDir)).toString();
    const auto path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open Director Movie"), lastDir,
        QStringLiteral("Director Files (*.dir *.dcr *.dxr *.cct *.cst);;"
                       "Debugger Recordings (*.lswdebug);;All Files (*)"));
    if (path.isEmpty()) return;
    settings.setValue(QString::fromLatin1(kSettingLastDir), QFileInfo(path).absolutePath());
    if (QFileInfo(path).suffix().compare(QStringLiteral("lswdebug"),
                                         Qt::CaseInsensitive) == 0) {
        openRecording(path);
    } else {
        openMovie(path, externalParams_);
    }
}

void DebuggerWindow::onOpenUrl() {
    // Remember the last URL entered so the next open starts pre-filled.
    QSettings settings;
    const auto lastUrl = settings.value(QString::fromLatin1(kSettingLastUrl)).toString();
    const auto url = QInputDialog::getText(
        this, QStringLiteral("Open Movie URL"),
        QStringLiteral("Enter the URL of a Director movie (.dir/.dcr):"),
        QLineEdit::Normal, lastUrl);
    if (url.isEmpty()) return;
    settings.setValue(QString::fromLatin1(kSettingLastUrl), url);
    openMovie(url, externalParams_);
}

void DebuggerWindow::onOpenRecording() {
    QSettings settings;
    const auto lastDir = settings.value(QString::fromLatin1(kSettingLastDir)).toString();
    const auto path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open Debug Recording"), lastDir,
        QStringLiteral("Debugger Recordings (*.lswdebug);;All Files (*)"));
    if (path.isEmpty()) return;
    settings.setValue(QString::fromLatin1(kSettingLastDir), QFileInfo(path).absolutePath());
    openRecording(path);
}

bool DebuggerWindow::readRecording(const QString& path,
                                   RecordingFile& recording,
                                   QString& error) const {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Unable to open recording: %1").arg(file.errorString());
        return false;
    }

    const auto data = file.readAll();

    const auto parseHeader = [&recording, &error](const QJsonObject& root) {
        recording.movie = root.value(QStringLiteral("movie")).toString();
        if (recording.movie.isEmpty()) {
            error = QStringLiteral("Recording does not contain a movie path or URL");
            return false;
        }

        const auto params = root.value(QStringLiteral("externalParams"));
        if (!params.isUndefined() && !params.isObject()) {
            error = QStringLiteral("Recording externalParams must be an object");
            return false;
        }
        if (params.isObject()) {
            const auto paramsObject = params.toObject();
            for (auto it = paramsObject.begin(); it != paramsObject.end(); ++it) {
                if (!it.value().isString()) {
                    error = QStringLiteral(
                        "Recording contains a non-string external parameter");
                    return false;
                }
                recording.externalParams.insert(it.key(), it.value().toString());
            }
        }
        return true;
    };

    const auto parseEvent = [&error](const QJsonObject& object,
                                     RecordedInputEvent& event) {
        if (!recordingEventTypeFromName(
                object.value(QStringLiteral("type")).toString(), event.type)) {
            error = QStringLiteral("Recording contains an unknown input event type");
            return false;
        }
        event.timeMs = static_cast<qint64>(object.value(QStringLiteral("timeMs"))
                                               .toDouble(-1));
        if (event.timeMs < 0) {
            error = QStringLiteral("Recording contains an invalid event time");
            return false;
        }
        event.stageX = object.value(QStringLiteral("stageX")).toInt();
        event.stageY = object.value(QStringLiteral("stageY")).toInt();
        event.directorKeyCode = object.value(QStringLiteral("keyCode")).toInt();
        event.keyText = object.value(QStringLiteral("keyText")).toString();
        event.shift = object.value(QStringLiteral("shift")).toBool();
        event.ctrl = object.value(QStringLiteral("ctrl")).toBool();
        event.alt = object.value(QStringLiteral("alt")).toBool();
        event.rightButton = object.value(QStringLiteral("rightButton")).toBool();
        return true;
    };

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(data, &parseError);
    const bool wholeDocumentIsObject =
        parseError.error == QJsonParseError::NoError && document.isObject();
    if (wholeDocumentIsObject &&
        document.object().value(QStringLiteral("version")).toInt() == 1) {
        const auto root = document.object();
        if (root.value(QStringLiteral("format")).toString() !=
                QStringLiteral("LibreShockwave Debugger Recording")) {
            error = QStringLiteral("Unsupported debugger recording format");
            return false;
        }
        if (!parseHeader(root)) {
            return false;
        }

        const auto events = root.value(QStringLiteral("events"));
        if (!events.isArray()) {
            error = QStringLiteral("Recording does not contain an events array");
            return false;
        }
        for (const auto& value : events.toArray()) {
            if (!value.isObject()) {
                error = QStringLiteral("Recording contains an invalid event");
                return false;
            }
            RecordedInputEvent event;
            if (!parseEvent(value.toObject(), event)) {
                return false;
            }
            recording.events.push_back(std::move(event));
        }
        return true;
    }

    // Version 2 is newline-delimited JSON. Ignore only an incomplete final
    // line: it can be left behind if the debugger exits during a write, while
    // all preceding flushed events remain usable.
    const auto lines = data.split('\n');
    if (lines.isEmpty()) {
        error = QStringLiteral("Invalid recording JSON");
        return false;
    }
    QJsonParseError headerParseError;
    const auto headerDocument = QJsonDocument::fromJson(
        lines.front().trimmed(), &headerParseError);
    if (headerParseError.error != QJsonParseError::NoError ||
        !headerDocument.isObject()) {
        error = QStringLiteral("Invalid recording JSON: %1")
                    .arg(parseError.errorString());
        return false;
    }
    const auto header = headerDocument.object();
    if (header.value(QStringLiteral("format")).toString() !=
            QStringLiteral("LibreShockwave Debugger Recording") ||
        header.value(QStringLiteral("version")).toInt() != 2) {
        error = QStringLiteral("Unsupported debugger recording format");
        return false;
    }
    if (!parseHeader(header)) {
        return false;
    }

    for (int index = 1; index < lines.size(); ++index) {
        const auto line = lines[index].trimmed();
        if (line.isEmpty()) {
            continue;
        }
        QJsonParseError eventParseError;
        const auto eventDocument = QJsonDocument::fromJson(line, &eventParseError);
        if (eventParseError.error != QJsonParseError::NoError ||
            !eventDocument.isObject()) {
            if (index == lines.size() - 1 && !data.endsWith('\n')) {
                break;
            }
            error = QStringLiteral("Invalid recording event: %1")
                        .arg(eventParseError.errorString());
            return false;
        }
        RecordedInputEvent event;
        if (!parseEvent(eventDocument.object(), event)) {
            return false;
        }
        recording.events.push_back(std::move(event));
    }
    return true;
}

bool DebuggerWindow::openRecordingFile(QString& error) {
    if (recordingFilePath_.isEmpty()) {
        error = QStringLiteral("No recording file was selected");
        return false;
    }

    recordingFile_.setFileName(recordingFilePath_);
    if (!recordingFile_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = QStringLiteral("Unable to save recording: %1")
                    .arg(recordingFile_.errorString());
        return false;
    }

    // Version 2 is newline-delimited JSON. The header and every event are
    // complete records, so a recording can be opened while it is still
    // running and survives a debugger crash without a final rewrite.
    QJsonObject header;
    header.insert(QStringLiteral("format"),
                  QStringLiteral("LibreShockwave Debugger Recording"));
    header.insert(QStringLiteral("version"), 2);
    header.insert(QStringLiteral("movie"),
                  QString::fromStdString(context_->moviePath()));
    QJsonObject params;
    for (auto it = externalParams_.cbegin(); it != externalParams_.cend(); ++it) {
        params.insert(it.key(), it.value());
    }
    header.insert(QStringLiteral("externalParams"), params);

    auto bytes = QJsonDocument(header).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    if (recordingFile_.write(bytes) != bytes.size() || !recordingFile_.flush()) {
        error = QStringLiteral("Unable to save recording: %1")
                    .arg(recordingFile_.errorString());
        closeRecordingFile();
        return false;
    }
    return true;
}

bool DebuggerWindow::writeRecordingEvent(const RecordedInputEvent& event,
                                         QString& error) {
    const auto type = recordingEventTypeName(event.type);
    if (type.isEmpty()) {
        error = QStringLiteral("Cannot save an unknown input event type");
        return false;
    }

    QJsonObject object;
    object.insert(QStringLiteral("timeMs"), event.timeMs);
    object.insert(QStringLiteral("type"), type);
    object.insert(QStringLiteral("stageX"), event.stageX);
    object.insert(QStringLiteral("stageY"), event.stageY);
    object.insert(QStringLiteral("keyCode"), event.directorKeyCode);
    object.insert(QStringLiteral("keyText"), event.keyText);
    object.insert(QStringLiteral("shift"), event.shift);
    object.insert(QStringLiteral("ctrl"), event.ctrl);
    object.insert(QStringLiteral("alt"), event.alt);
    object.insert(QStringLiteral("rightButton"), event.rightButton);

    auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    if (recordingFile_.write(bytes) != bytes.size()) {
        error = QStringLiteral("Unable to save recording: %1")
                    .arg(recordingFile_.errorString());
        return false;
    }
    return true;
}

bool DebuggerWindow::flushRecordingFile(QString& error) {
    if (!recordingFile_.isOpen() || recordingFile_.flush()) {
        return true;
    }
    error = QStringLiteral("Unable to flush recording: %1")
                .arg(recordingFile_.errorString());
    return false;
}

void DebuggerWindow::closeRecordingFile() {
    if (recordingFlushTimer_ != nullptr) {
        recordingFlushTimer_->stop();
    }
    if (recordingFile_.isOpen()) {
        recordingFile_.flush();
        recordingFile_.close();
    }
}

bool DebuggerWindow::openRecording(const QString& path) {
    RecordingFile recording;
    QString error;
    if (!readRecording(path, recording, error)) {
        QMessageBox::warning(this, QStringLiteral("Open Debug Recording"), error);
        return false;
    }

    if (recording_) {
        context_->stop();
        finishRecording();
    }
    playRequested_ = false;
    cancelReplay();
    pendingReplay_ = std::move(recording);
    if (!openMovieInternal(pendingReplay_->movie, pendingReplay_->externalParams)) {
        pendingReplay_.reset();
        return false;
    }
    lastSession_ = path;
    addRecentSession(path);
    return true;
}

void DebuggerWindow::startPendingReplayIfReady() {
    if (!pendingReplay_.has_value() || !context_->hasMovie()) {
        return;
    }

    replayEvents_ = std::move(pendingReplay_->events);
    pendingReplay_.reset();
    replayIndex_ = 0;
    replaying_ = true;
    playRequested_ = false;
    replayClockBaseMs_ = 0;
    replayWaitingForNavigation_ = false;
    replayClock_.invalidate();
    context_->play();
    statusLabel_->setText(QStringLiteral(" ◉ Replaying"));
    statusLabel_->setStyleSheet(
        QStringLiteral("color:#c8873d; font-weight:bold; padding:0 8px;"));
    statusBar()->showMessage(QStringLiteral("Replaying debugger recording..."));
    updateToolbarState();
}

void DebuggerWindow::recordStageInput(int type, int stageX, int stageY,
                                      int keyCode, const std::string& keyText,
                                      bool shift, bool ctrl, bool alt,
                                      bool rightButton) {
    // Do not let physical input perturb an automated replay.
    if (replaying_) {
        return;
    }

    if (recording_) {
        const qint64 eventTimeMs = recordingClock_.isValid()
            ? recordingClock_.elapsed()
            : 0;
        const auto inputType = static_cast<DebuggerContext::InputEvent::Type>(type);
        bool shouldRecord = true;
        if (inputType == DebuggerContext::InputEvent::MouseMove) {
            // Stage mouseMove can arrive hundreds of times per second. The
            // movie only needs a useful motion sample, while clicks and keys
            // remain lossless and carry their own coordinates.
            shouldRecord = !hasRecordedMouseMove_ ||
                           (eventTimeMs - lastRecordedMouseMoveMs_ >=
                            kRecordingMouseMoveIntervalMs &&
                            (stageX != lastRecordedMouseX_ ||
                             stageY != lastRecordedMouseY_));
        }

        if (shouldRecord) {
            RecordedInputEvent event;
            event.timeMs = eventTimeMs;
            event.type = type;
            event.stageX = stageX;
            event.stageY = stageY;
            event.directorKeyCode = keyCode;
            event.keyText = QString::fromStdString(keyText);
            event.shift = shift;
            event.ctrl = ctrl;
            event.alt = alt;
            event.rightButton = rightButton;

            QString error;
            if (!writeRecordingEvent(event, error)) {
                recording_ = false;
                recordingClock_.invalidate();
                closeRecordingFile();
                QMessageBox::warning(this, QStringLiteral("Save Debug Recording"), error);
            } else if (inputType == DebuggerContext::InputEvent::MouseMove) {
                lastRecordedMouseMoveMs_ = eventTimeMs;
                lastRecordedMouseX_ = stageX;
                lastRecordedMouseY_ = stageY;
                hasRecordedMouseMove_ = true;
            }
        }
    }

    DebuggerContext::InputEvent event;
    event.type = static_cast<DebuggerContext::InputEvent::Type>(type);
    event.stageX = stageX;
    event.stageY = stageY;
    event.directorKeyCode = keyCode;
    event.keyText = keyText;
    event.shift = shift;
    event.ctrl = ctrl;
    event.alt = alt;
    event.rightButton = rightButton;
    context_->enqueueInput(std::move(event));
}

void DebuggerWindow::finishRecording() {
    if (!recording_ && !recordingFile_.isOpen()) {
        return;
    }
    recording_ = false;
    recordingClock_.invalidate();

    QString error;
    if (!flushRecordingFile(error)) {
        QMessageBox::warning(this, QStringLiteral("Save Debug Recording"), error);
    } else {
        QSettings settings;
        settings.setValue(QString::fromLatin1(kSettingLastRecordingDir),
                          QFileInfo(recordingFilePath_).absolutePath());
    }
    closeRecordingFile();
}

void DebuggerWindow::cancelReplay() {
    if (replayTimer_ != nullptr) {
        replayTimer_->stop();
    }
    replaying_ = false;
    replayClock_.invalidate();
    replayClockBaseMs_ = 0;
    replayEvents_.clear();
    replayIndex_ = 0;
    replayWaitingForNavigation_ = false;
}

void DebuggerWindow::pauseReplayForNavigation() {
    if (!replaying_ || replayWaitingForNavigation_) {
        return;
    }
    if (replayClock_.isValid()) {
        replayClockBaseMs_ += replayClock_.elapsed();
        replayClock_.invalidate();
    }
    replayWaitingForNavigation_ = true;
    replayTimer_->stop();
}

void DebuggerWindow::resumeReplayAfterNavigation() {
    if (!replaying_ || !replayWaitingForNavigation_) {
        return;
    }
    if (context_->networkReady()) {
        replayClock_.start();
    } else {
        replayClock_.invalidate();
    }
    replayWaitingForNavigation_ = false;
    replayTimer_->start();
}

bool DebuggerWindow::isUrl(const QString& value) {
    return value.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
           value.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
}

void DebuggerWindow::loadMovieFromUrl(const QString& url) {
    statusBar()->showMessage(QStringLiteral("Downloading %1...").arg(url));

    auto* manager = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LibreShockwave Debugger/0.1"));
    request.setTransferTimeout(20000);
    auto* reply = manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, manager, url] {
        reply->deleteLater();
        manager->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            pendingReplay_.reset();
            statusBar()->showMessage(
                QStringLiteral("Failed to download: %1").arg(reply->errorString()), 5000);
            return;
        }

        const auto data = reply->readAll();
        std::vector<std::uint8_t> bytes(data.cbegin(), data.cend());
        finishLoadedMovie(url, std::move(bytes), url.toStdString());
    });
}

void DebuggerWindow::finishLoadedMovie(const QString& label,
                                       std::vector<std::uint8_t> data,
                                       const std::string& basePath,
                                       bool resumePlayback) {
    const auto byteCount = data.size();
    const bool ok = context_->loadMovieFromData(std::move(data), basePath);
    if (!ok) {
        statusBar()->showMessage(QStringLiteral("Failed to load movie"), 5000);
        return;
    }

    stageWidget_->prepareForMovie();
    isPaused_ = false;

    context_->setExternalParams(
        ExternalParamsDialog::toPlayerParams(externalParams_));

    setWindowTitle(QStringLiteral("LibreShockwave Debugger — %1").arg(label));
    movieTreePanel_->populate(context_->player());
    restoreBreakpointsForCurrentMovie();
    const bool isRecordingSession = pendingReplay_.has_value();
    lastMovie_ = label;
    addRecentSession(label, !isRecordingSession);
    refreshBreakpoints();

    statusLabel_->setText(QStringLiteral(" ● Ready"));
    statusLabel_->setStyleSheet(
        QStringLiteral("color:#8a9488; font-weight:bold; padding:0 8px;"));
    updateToolbarState();

    statusBar()->showMessage(
        QStringLiteral("Loaded: %1 (%2 bytes) — press Play to start")
            .arg(label)
            .arg(byteCount),
        5000);
    if (replaying_ && resumePlayback) {
        // Runtime navigation replaces the Player session. Keep the active
        // recording replay attached to the new session and resume its
        // paused recording clock.
        context_->play();
        resumeReplayAfterNavigation();
    } else if (!replaying_ && (resumePlayback || playRequested_)) {
        playRequested_ = false;
        onPlay();
    }
}

void DebuggerWindow::onOpenRecent() {
    auto* action = qobject_cast<QAction*>(sender());
    if (action == nullptr) return;
    const auto path = action->data().toString();
    if (!path.isEmpty()) {
        openMovie(path, externalParams_);
    }
}

// -----------------------------------------------------------------------
// Parameters menu
// -----------------------------------------------------------------------

void DebuggerWindow::onEditParameters() {
    ExternalParamsDialog dialog(externalParams_, this);
    if (dialog.exec() != QDialog::Accepted) return;

    externalParams_ = dialog.params();
    saveSettings();
    updateParamsMenu();

    // Keep the context copy in sync so fresh Play sessions use these params.
    if (context_->hasMovie()) {
        context_->setExternalParams(
            ExternalParamsDialog::toPlayerParams(externalParams_));
        statusBar()->showMessage(
            QStringLiteral("Parameters updated (%1 entries) — reload movie to apply")
                .arg(externalParams_.size()), 4000);
    } else {
        statusBar()->showMessage(
            QStringLiteral("Parameters saved (%1 entries)")
                .arg(externalParams_.size()), 3000);
    }
}

void DebuggerWindow::onLoadDefaultPreset() {
    // Apply the preset directly — no dialog needed
    ExternalParamsDialog dialog(externalParams_, this);
    dialog.loadDefaultPreset();
    externalParams_ = dialog.params();
    saveSettings();
    updateParamsMenu();

    // Apply to the current session and retain for a fresh Play session.
    if (context_->hasMovie()) {
        context_->setExternalParams(
            ExternalParamsDialog::toPlayerParams(externalParams_));
    }

    statusBar()->showMessage(
        QStringLiteral("Default preset loaded (%1 parameters)")
            .arg(externalParams_.size()), 4000);
}

void DebuggerWindow::onClearParameters() {
    externalParams_.clear();
    if (context_->hasMovie()) {
        context_->setExternalParams({});
    }
    saveSettings();
    updateParamsMenu();
    statusBar()->showMessage(QStringLiteral("Parameters cleared"), 3000);
}

void DebuggerWindow::updateParamsMenu() {
    if (paramsMenu_ == nullptr) return;

    // Remove old dynamic items (everything after the separator)
    const auto actions = paramsMenu_->actions();
    // Find the separator and remove everything after it
    int sepIdx = -1;
    for (int i = 0; i < actions.size(); ++i) {
        if (actions[i]->isSeparator()) {
            sepIdx = i;
            break;
        }
    }
    if (sepIdx >= 0) {
        for (int i = actions.size() - 1; i > sepIdx; --i) {
            paramsMenu_->removeAction(actions[i]);
        }
    }

    if (externalParams_.isEmpty()) {
        auto* emptyAction = paramsMenu_->addAction(
            QStringLiteral("(no parameters set)"));
        emptyAction->setEnabled(false);
        // Update status bar hint
        if (!context_->hasMovie()) {
            statusBar()->showMessage(
                QStringLiteral("Tip: Use Parameters → Edit to set connection params, then open a movie"),
                8000);
        }
    } else {
        for (auto it = externalParams_.cbegin(); it != externalParams_.cend(); ++it) {
            auto* item = paramsMenu_->addAction(
                QStringLiteral("%1 = %2").arg(it.key(), it.value()));
            item->setEnabled(false);
        }
    }
}

// -----------------------------------------------------------------------
// Actions — Playback
// -----------------------------------------------------------------------

void DebuggerWindow::onPlay() {
    if (!context_->hasMovie()) return;
    if (pendingReplay_.has_value()) {
        startPendingReplayIfReady();
        return;
    }
    context_->play();
    statusLabel_->setText(QStringLiteral(" ● Running"));
    statusLabel_->setStyleSheet(
        QStringLiteral("color:#4da86d; font-weight:bold; padding:0 8px;"));
    statusBar()->showMessage(QStringLiteral("Running..."));
    updateToolbarState();
}

void DebuggerWindow::onPlayAndRecord() {
    if (!context_->hasMovie() || context_->isPlaying() || isPaused_ ||
        pendingReplay_.has_value()) {
        return;
    }

    const auto suggestedName = QStringLiteral("debug-recording.lswdebug");
    QSettings settings;
    const auto lastRecordingDir =
        settings.value(QString::fromLatin1(kSettingLastRecordingDir)).toString();
    const auto suggestedPath = lastRecordingDir.isEmpty()
        ? suggestedName
        : QDir(lastRecordingDir).filePath(suggestedName);
    auto path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save Debug Recording"), suggestedPath,
        QStringLiteral("Debugger Recordings (*.lswdebug);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).suffix().compare(QStringLiteral("lswdebug"),
                                         Qt::CaseInsensitive) != 0) {
        path += QStringLiteral(".lswdebug");
    }

    recordingFilePath_ = path;
    QString error;
    if (!openRecordingFile(error)) {
        QMessageBox::warning(this, QStringLiteral("Save Debug Recording"), error);
        recordingFilePath_.clear();
        return;
    }

    lastRecordedMouseMoveMs_ = -1;
    lastRecordedMouseX_ = 0;
    lastRecordedMouseY_ = 0;
    hasRecordedMouseMove_ = false;
    recordingClock_.invalidate();
    recording_ = true;
    recordingFlushTimer_->start();
    context_->play();

    statusLabel_->setText(QStringLiteral(" ⏺ Recording"));
    statusLabel_->setStyleSheet(
        QStringLiteral("color:#c34e4e; font-weight:bold; padding:0 8px;"));
    statusBar()->showMessage(QStringLiteral("Recording stage input..."));
    updateToolbarState();
}

void DebuggerWindow::onPause() {
    context_->pausePlayback();
}

void DebuggerWindow::onStop() {
    cancelReplay();
    context_->stop();
    finishRecording();
    codeViewPanel_->clear();
    variablesPanel_->clearAll();
    callStackPanel_->clearAll();
    watchPanel_->clearAll();
    movieTreePanel_->clearAll();
    stageWidget_->clearFrame();
    isPaused_ = false;
    statusLabel_->setText(QStringLiteral(" ● Stopped"));
    statusLabel_->setStyleSheet(
        QStringLiteral("color:#8a9488; font-weight:bold; padding:0 8px;"));
    statusBar()->showMessage(QStringLiteral("Stopped"));
    updateToolbarState();
}

void DebuggerWindow::onStepInto()  { context_->stepInto(); }
void DebuggerWindow::onStepOver()  { context_->stepOver(); }
void DebuggerWindow::onStepOut()   { context_->stepOut(); }
void DebuggerWindow::onContinue()  { context_->continueExecution(); }

void DebuggerWindow::onToggleBreakpoint() {
    if (context_->debugController() == nullptr) return;

    // F9 while paused: toggle at the current instruction (matches WASM F9).
    if (isPaused_) {
        auto snap = context_->debugController()->currentSnapshot();
        if (!snap.has_value()) return;
        context_->toggleBreakpoint(snap->scriptId, snap->handlerName,
                                   snap->instructionOffset);
        refreshBreakpoints();
        persistBreakpoints();
        return;
    }

    // While running: toggle at the last code line clicked in the gutter
    // (matches the WASM harness's click-to-toggle-anytime behavior).
    if (currentHandlerName_.empty()) {
        statusBar()->showMessage(
            QStringLiteral("Select a handler first to toggle breakpoints"), 4000);
        return;
    }
    const int offset = codeViewPanel_->lastClickedOffset();
    if (offset < 0) {
        statusBar()->showMessage(
            QStringLiteral("Click a code line's gutter column to toggle a breakpoint"),
            4000);
        return;
    }
    context_->toggleBreakpoint(currentScriptId_, currentHandlerName_, offset);
    refreshBreakpoints();
    persistBreakpoints();
}

void DebuggerWindow::onBreakpointToggled(int scriptId,
                                         const std::string& handlerName,
                                         int offset) {
    if (context_->toggleBreakpoint(scriptId, handlerName, offset)) {
        refreshBreakpoints();
        persistBreakpoints();
    }
}

void DebuggerWindow::onClearBreakpoints() {
    context_->clearAllBreakpoints();
    refreshBreakpoints();
    persistBreakpoints();
}

// -----------------------------------------------------------------------
// Debug state callbacks
// -----------------------------------------------------------------------

void DebuggerWindow::onPaused(const SnapshotData& data) {
    isPaused_ = true;
    statusLabel_->setText(QStringLiteral(" ◉ Paused"));
    statusLabel_->setStyleSheet(
        QStringLiteral("color:#c9a53b; font-weight:bold; padding:0 8px;"));
    statusBar()->showMessage(
        QStringLiteral("Paused at %1::%2 offset %3")
            .arg(QString::fromStdString(data.scriptName),
                 QString::fromStdString(data.handlerName))
            .arg(data.instructionOffset));

    currentScriptId_ = data.scriptId;
    currentHandlerName_ = data.handlerName;
    codeViewPanel_->setCurrentInstruction(data.instructionOffset);
    variablesPanel_->updateFromSnapshot(data);
    callStackPanel_->updateFromSnapshot(data);
    watchPanel_->updateFromSnapshot(data);
    updateToolbarState();
    loadHandlerCode(data.scriptId, data.handlerName);
}

void DebuggerWindow::onResumed() {
    isPaused_ = false;
    statusLabel_->setText(QStringLiteral(" ● Running"));
    statusLabel_->setStyleSheet(
        QStringLiteral("color:#4da86d; font-weight:bold; padding:0 8px;"));
    statusBar()->showMessage(QStringLiteral("Running..."));
    variablesPanel_->clearAll();
    callStackPanel_->clearAll();
    updateToolbarState();
}

void DebuggerWindow::onBreakpointsChanged() {
    refreshBreakpoints();
}

void DebuggerWindow::onRecordingFlushTimer() {
    if (!recording_ || !recordingFile_.isOpen()) {
        recordingFlushTimer_->stop();
        return;
    }

    QString error;
    if (!flushRecordingFile(error)) {
        recording_ = false;
        recordingClock_.invalidate();
        closeRecordingFile();
        QMessageBox::warning(this, QStringLiteral("Save Debug Recording"), error);
    }
}

void DebuggerWindow::onFrameRendered(const QImage& image) {
    // The image is a detached copy produced from an immutable snapshot, so
    // painting here never races with the VM or renderer threads.
    stageWidget_->setFrameImage(image);

    if (!qEnvironmentVariable("LIBRESHOCKWAVE_DEBUGGER_FRAME_STATS").isEmpty()) {
        if (!frameStatsClock_.isValid()) {
            frameStatsClock_.start();
        }
        ++frameStatsCount_;
        const qint64 elapsed = frameStatsClock_.elapsed();
        if (elapsed >= 1000) {
            std::cerr << "debugger presentation: "
                      << (1000.0 * frameStatsCount_) / static_cast<double>(elapsed)
                      << " fps\n";
            frameStatsClock_.restart();
            frameStatsCount_ = 0;
        }
    }

    const auto dumpPath = qEnvironmentVariable(
        "LIBRESHOCKWAVE_DEBUGGER_FRAME_DUMP");
    // Frame dumps are diagnostic output, not part of presentation. Writing a
    // PNG for every movie frame can itself destroy the frame budget, so keep
    // the requested rolling snapshot at most once per second.
    if (!dumpPath.isEmpty() &&
        (!frameDumpClock_.isValid() || frameDumpClock_.elapsed() >= 1000)) {
        frameDumpClock_.restart();
        QSaveFile dumpFile(dumpPath);
        if (dumpFile.open(QIODevice::WriteOnly) &&
            image.save(&dumpFile, "PNG")) {
            dumpFile.commit();
        }
    }
}

void DebuggerWindow::onPlaybackStarted() {
    if (recording_ && !recordingClock_.isValid()) {
        recordingClock_.start();
    }
    if (replaying_ && !replayWaitingForNavigation_) {
        // A movie navigation replaces the Player session while a recording
        // is replaying. Preserve the original recording clock and only
        // restart the timer if the replacement session stopped it.
        if (!replayClock_.isValid() && context_->networkReady()) {
            replayClock_.start();
        }
        if (!replayTimer_->isActive()) {
            replayTimer_->start();
        }
    }
}

void DebuggerWindow::onReplayTimer() {
    if (!replaying_) {
        replayTimer_->stop();
        return;
    }

    if (replayWaitingForNavigation_) {
        return;
    }

    if (!replayClock_.isValid()) {
        if (!context_->networkReady()) {
            return;
        }
        replayClock_.start();
    } else if (!context_->networkReady()) {
        // A replay's timestamps are relative to usable movie time, not to
        // wall-clock time spent loading external casts or completing the
        // session handshake.  Keep the pending event at its recorded time so
        // clicks are not delivered to an empty/loading score.
        replayClockBaseMs_ += replayClock_.elapsed();
        replayClock_.invalidate();
        return;
    }

    const qint64 elapsed = replayClockBaseMs_ + replayClock_.elapsed();
    while (replayIndex_ < replayEvents_.size() &&
           replayEvents_[replayIndex_].timeMs <= elapsed) {
        const auto& recorded = replayEvents_[replayIndex_++];
        DebuggerContext::InputEvent event;
        event.type = static_cast<DebuggerContext::InputEvent::Type>(recorded.type);
        event.stageX = recorded.stageX;
        event.stageY = recorded.stageY;
        event.directorKeyCode = recorded.directorKeyCode;
        event.keyText = recorded.keyText.toStdString();
        event.shift = recorded.shift;
        event.ctrl = recorded.ctrl;
        event.alt = recorded.alt;
        event.rightButton = recorded.rightButton;
        context_->enqueueInput(std::move(event));
    }

    if (replayIndex_ >= replayEvents_.size()) {
        replayTimer_->stop();
        replaying_ = false;
        statusLabel_->setText(QStringLiteral(" ● Running"));
        statusLabel_->setStyleSheet(
            QStringLiteral("color:#4da86d; font-weight:bold; padding:0 8px;"));
        statusBar()->showMessage(QStringLiteral("Debugger recording replay complete"), 4000);
        updateToolbarState();
    }
}

void DebuggerWindow::onErrorOccurred(const QString& message) {
    statusBar()->showMessage(message, 5000);
    QMessageBox::warning(this, QStringLiteral("Error"), message);
}

void DebuggerWindow::onMovieNavigationRequested(const QString& url) {
    pauseReplayForNavigation();
    statusBar()->showMessage(QStringLiteral("Navigating to %1...").arg(url));

    auto* manager = new QNetworkAccessManager(this);
    auto* reply = manager->get(QNetworkRequest(QUrl(url)));

    connect(reply, &QNetworkReply::finished, this, [this, reply, manager, url] {
        reply->deleteLater();
        manager->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            statusBar()->showMessage(
                QStringLiteral("Navigation failed: %1").arg(reply->errorString()), 5000);
            return;
        }

        const auto data = reply->readAll();
        std::vector<std::uint8_t> bytes(data.cbegin(), data.cend());
        finishLoadedMovie(url, std::move(bytes), url.toStdString(), true);
    });
}

// -----------------------------------------------------------------------
// Movie tree + code view
// -----------------------------------------------------------------------

void DebuggerWindow::onScriptSelected(int /*castLibNumber*/, int scriptId) {
    currentScriptId_ = scriptId;
    loadHandlerCode(scriptId, "");
}

void DebuggerWindow::onHandlerSelected(int scriptId, const std::string& handlerName) {
    currentScriptId_ = scriptId;
    currentHandlerName_ = handlerName;
    loadHandlerCode(scriptId, handlerName);
}

void DebuggerWindow::onCodeHandlerChanged(const std::string& handlerName) {
    currentHandlerName_ = handlerName;
    loadHandlerCode(currentScriptId_, handlerName);
}

// -----------------------------------------------------------------------
// Watches
// -----------------------------------------------------------------------

void DebuggerWindow::onWatchAdded(const std::string& expression) {
    context_->addWatch(expression);
    if (auto* dc = context_->debugController()) {
        auto watches = dc->watchExpressions();
        if (!watches.empty()) {
            const auto& last = watches.back();
            watchPanel_->addWatchRow(last.id, last.expression);
        }
    }
}

void DebuggerWindow::onWatchRemoved(const std::string& id) {
    context_->removeWatch(id);
}

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

void DebuggerWindow::updateToolbarState() {
    const bool hasMovie = context_->hasMovie();
    const bool hasPlayer = context_->player() != nullptr;
    const bool playing = context_->isPlaying();
    const bool hasPendingReplay = pendingReplay_.has_value();

    playAction_->setEnabled(hasMovie && !playing && !isPaused_);
    recordAction_->setEnabled(
        hasMovie && !playing && !isPaused_ && !replaying_ && !hasPendingReplay);
    pauseAction_->setEnabled(hasMovie && playing && !isPaused_);
    stopAction_->setEnabled(hasMovie && (playing || isPaused_));
    continueAction_->setEnabled(hasMovie && isPaused_);
    stepIntoAction_->setEnabled(hasMovie && isPaused_);
    stepOverAction_->setEnabled(hasMovie && isPaused_);
    stepOutAction_->setEnabled(hasMovie && isPaused_);
    toggleBreakpointAction_->setEnabled(hasPlayer);
    clearBreakpointsAction_->setEnabled(hasPlayer);
}

void DebuggerWindow::loadHandlerCode(int scriptId, const std::string& handlerName) {
    auto* player = context_->player();
    if (player == nullptr) return;

    std::shared_ptr<libreshockwave::chunks::ScriptChunk> foundScript;
    const libreshockwave::chunks::ScriptChunk::Handler* foundHandler = nullptr;
    const libreshockwave::chunks::ScriptNamesChunk* foundNames = nullptr;

    for (const auto& [number, castLib] : player->castLibManager().castLibs()) {
        if (castLib == nullptr) continue;
        for (const auto& script : castLib->allScripts()) {
            if (script == nullptr) continue;
            if (script->id().value() != scriptId) continue;
            foundScript = script;
            foundNames = castLib->scriptNames().get();
            if (!handlerName.empty()) {
                foundHandler = script->findHandlerPtr(handlerName, foundNames);
            }
            break;
        }
        if (foundScript != nullptr) break;
    }

    if (foundScript == nullptr) {
        codeViewPanel_->clear();
        return;
    }

    std::vector<InstructionData> instructions;
    std::vector<DecompiledLineData> decompiledLines;
    std::vector<std::string> handlerNames;

    for (const auto& handler : foundScript->handlers()) {
        handlerNames.push_back(foundScript->resolveName(handler.nameId));
    }

    if (foundHandler != nullptr) {
        for (const auto& instr : foundHandler->instructions) {
            InstructionData id;
            id.offset = instr.offset;
            id.index = foundHandler->getInstructionIndex(instr.offset);
            id.opcode = std::string(libreshockwave::lingo::mnemonic(instr.opcode));
            id.argument = instr.argument;
            id.annotation = libreshockwave::lingo::vm::trace::InstructionAnnotator::annotate(
                *foundScript, foundHandler, instr, foundNames, true);
            instructions.push_back(std::move(id));
        }

        libreshockwave::lingo::decompiler::LingoDecompiler decompiler;
        auto decompiled = decompiler.decompileHandlerWithMapping(
            *foundHandler, *foundScript, foundNames);
        for (const auto& line : decompiled.lines) {
            DecompiledLineData dld;
            dld.text = line.text;
            dld.bytecodeOffset = line.bytecodeOffset;
            decompiledLines.push_back(std::move(dld));
        }
    }

    codeViewPanel_->setHandlerCode(
        scriptId, foundScript->displayName(), handlerName,
        instructions, decompiledLines, handlerNames);
    refreshBreakpoints();
}

void DebuggerWindow::refreshBreakpoints() {
    auto* dc = context_->debugController();
    if (dc == nullptr) return;

    std::set<int> offsets;
    for (const auto& bp : dc->breakpointManager().getAllBreakpoints()) {
        if (bp.scriptId == currentScriptId_ &&
            bp.handlerName == currentHandlerName_) {
            offsets.insert(bp.offset);
        }
    }
    codeViewPanel_->setBreakpointOffsets(offsets);
}

} // namespace libreshockwave::debugger
