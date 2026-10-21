#include "debugger_window.hpp"

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

#include "debugger_context.hpp"
#include "libreshockwave/format/script_format_utils.hpp"
#include "debug_state_bridge.hpp"
#include "external_params_dialog.hpp"
#include "format/datum_format.hpp"
#include "libreshockwave/chunks/script_chunk.hpp"
#include "libreshockwave/lingo/decompiler/lingo_decompiler.hpp"
#include "libreshockwave/lingo/vm/trace/instruction_annotator.hpp"
#include "libreshockwave/moviesource/director_record_converter.hpp"
#include "libreshockwave/player/player.hpp"
#include "libreshockwave/player/cast/cast_lib.hpp"
#include "libreshockwave/player/cast/cast_lib_manager.hpp"
#include "libreshockwave/player/debug/breakpoint_manager.hpp"
#include "libreshockwave/player/debug/debug_controller.hpp"
#include "libreshockwave/player/render/pipeline/frame_snapshot.hpp"
#include "startup_session.hpp"
#include "ui/call_stack_panel.hpp"
#include "ui/code_view_panel.hpp"
#include "ui/debug_window_panel.hpp"
#include "ui/movie_tree_panel.hpp"
#include "ui/stage_widget.hpp"
#include "ui/variables_panel.hpp"
#include "ui/watch_panel.hpp"

namespace libreshockwave::debugger {

// -----------------------------------------------------------------------
// Keys for QSettings persistence
// -----------------------------------------------------------------------

static const char* k_setting_geometry       = "debugger/geometry";
// Versioned: the Inspector dock was split into three docks (call stack,
// variables, watches); restoreState() must never be fed the pre-split bytes.
static const char* k_setting_state          = "debugger/state2";
static const char* k_setting_last_session    = "debugger/lastSession";
static const char* k_setting_recent_sessions = "debugger/recentSessions";
// Legacy settings retained so existing debugger installations are upgraded.
static const char* k_setting_last_movie      = "debugger/lastMovie";
static const char* k_setting_recent_movies   = "debugger/recentMovies";
static const char* k_setting_last_dir        = "debugger/lastDir";
static const char* k_setting_last_recording_dir = "debugger/lastRecordingDir";
static const char* k_setting_last_url        = "debugger/lastUrl";
static const char* k_setting_external_params = "debugger/externalParams";
static const char* k_setting_breakpoints    = "debugger/breakpoints";
static const int  k_max_recent_sessions      = 8;
static constexpr qint64 k_recording_flush_interval_ms = 1000;
static constexpr qint64 k_recording_mouse_move_interval_ms = 50;

// Breakpoint entries live under "debugger/breakpoints/<hash>" with the movie
// path/URL hashed into the sub-key: QSettings keys may not contain '/', and
// movie paths and URLs usually do.  Each entry is "scriptId\thandlerName\toffset\tenabled".
static QString breakpointSettingKey(const QString& movie_identity) {
    const QByteArray hash =
        QCryptographicHash::hash(movie_identity.toUtf8(), QCryptographicHash::Md5);
    return QString::fromLatin1(k_setting_breakpoints) + QLatin1Char('/') +
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
    recording_flush_timer_ = new QTimer(this);
    recording_flush_timer_->setInterval(static_cast<int>(k_recording_flush_interval_ms));
    recording_flush_timer_->setTimerType(Qt::CoarseTimer);
    connect(recording_flush_timer_, &QTimer::timeout,
            this, &DebuggerWindow::onRecordingFlushTimer);

    replay_timer_ = new QTimer(this);
    replay_timer_->setInterval(16);
    replay_timer_->setTimerType(Qt::CoarseTimer);
    connect(replay_timer_, &QTimer::timeout,
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
    last_movie_ = settings.value(QString::fromLatin1(k_setting_last_movie)).toString();
    last_session_ = settings.value(QString::fromLatin1(k_setting_last_session)).toString();
    const auto can_restore = [this](const QString& path) {
        return !path.isEmpty() && (isUrl(path) || QFileInfo::exists(path));
    };
    const auto startup_movie = selectStartupMovieSource(
        can_restore(last_movie_) ? last_movie_ : QString(), last_session_);
    settings_ready_ = true;
    // A command-line movie is already opened by main().  Do not also restore
    // the previous URL here: the two asynchronous sessions can finish in
    // either order and leave the debugger displaying the wrong movie/frame.
    // Options such as --play are not movies. Only suppress the persisted
    // movie when the command line actually contains a positional path.
    const auto arguments = QCoreApplication::arguments();
    const bool has_command_line_movie = std::any_of(
        arguments.cbegin() + (arguments.isEmpty() ? 0 : 1), arguments.cend(),
        [](const QString& argument) { return !argument.startsWith(QLatin1Char('-')); });
    if (!has_command_line_movie && can_restore(startup_movie)) {
        statusBar()->showMessage(QStringLiteral("Loading last movie..."));
        openMovie(startup_movie, external_params_);
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
    play_requested_ = false;
    if (QFileInfo(path).suffix().compare(QStringLiteral("lswdebug"),
                                         Qt::CaseInsensitive) == 0) {
        return openRecording(path);
    }
    if (recording_) {
        context_->stop();
        finishRecording();
    }
    cancelReplay();
    pending_replay_.reset();
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
        external_params_ = params;
        const bool is_recording_session = pending_replay_.has_value();
        last_movie_ = path;
        addRecentSession(path, !is_recording_session);
        loadMovieFromUrl(path);
        return true;
    }

    statusBar()->showMessage(QStringLiteral("Loading %1...").arg(path));

    const bool ok = context_->loadMovie(path.toStdString());
    if (!ok) {
        statusBar()->showMessage(QStringLiteral("Failed to load movie"), 5000);
        return false;
    }

    stage_widget_->prepareForMovie();
    is_paused_ = false;

    // Store parameters in the context so a later fresh Play session receives
    // the same values as this initial load.
    external_params_ = params;
    context_->setExternalParams(ExternalParamsDialog::toPlayerParams(params));

    setWindowTitle(QStringLiteral("LibreShockwave Debugger — %1").arg(
        QFileInfo(path).fileName()));
    movie_tree_panel_->populate(context_->player());
    refreshMovieIndex(movie_tree_panel_->lastSnapshot());
    restoreBreakpointsForCurrentMovie();
    const bool is_recording_session = pending_replay_.has_value();
    last_movie_ = path;
    addRecentSession(path, !is_recording_session);
    refreshBreakpoints();

    status_label_->setText(QStringLiteral(" ● Ready"));
    status_label_->setStyleSheet(
        QStringLiteral("color:#8a9488; font-weight:bold; padding:0 8px;"));
    updateToolbarState();

    statusBar()->showMessage(
        QStringLiteral("Loaded: %1 — press Play to start").arg(QFileInfo(path).fileName()));
    if (!replaying_ && play_requested_) {
        play_requested_ = false;
        onPlay();
    }
    return true;
}

void DebuggerWindow::startPlayback() {
    play_requested_ = true;
    if (!context_->hasMovie()) {
        return;
    }

    // Do not restart a replay that is already active.
    if (replaying_ && !replay_waiting_for_navigation_) {
        play_requested_ = false;
        return;
    }

    play_requested_ = false;
    onPlay();
}

// -----------------------------------------------------------------------
// Persistence
// -----------------------------------------------------------------------

void DebuggerWindow::saveSettings() {
    if (!settings_ready_) {
        return;
    }

    QSettings settings;
    settings.setValue(QString::fromLatin1(k_setting_geometry), saveGeometry());
    settings.setValue(QString::fromLatin1(k_setting_state), saveState());
    settings.setValue(QString::fromLatin1(k_setting_last_movie), last_movie_);
    settings.setValue(QString::fromLatin1(k_setting_last_session), last_session_);
    settings.setValue(QString::fromLatin1(k_setting_recent_sessions), recent_sessions_);

    // Save external params as a serialized list
    QStringList param_list;
    for (auto it = external_params_.cbegin(); it != external_params_.cend(); ++it) {
        param_list.append(it.key() + QStringLiteral("=") + it.value());
    }
    settings.setValue(QString::fromLatin1(k_setting_external_params), param_list);
}

void DebuggerWindow::onResetLayout() {
    // Re-apply the layout captured at startup: default positions, splits,
    // sizes, and visibility of every dock.
    restoreState(default_layout_state_);
}

void DebuggerWindow::restoreSettings() {
    QSettings settings;

    // Window geometry
    if (settings.contains(QString::fromLatin1(k_setting_geometry))) {
        restoreGeometry(settings.value(QString::fromLatin1(k_setting_geometry)).toByteArray());
    }
    if (settings.contains(QString::fromLatin1(k_setting_state))) {
        restoreState(settings.value(QString::fromLatin1(k_setting_state)).toByteArray());
    }

    // Recent movie sources and recording sessions.
    if (settings.contains(QString::fromLatin1(k_setting_recent_sessions))) {
        recent_sessions_ =
            settings.value(QString::fromLatin1(k_setting_recent_sessions)).toStringList();
    } else {
        recent_sessions_ = settings.value(QString::fromLatin1(k_setting_recent_movies)).toStringList();
        const auto recording_directory =
            settings.value(QString::fromLatin1(k_setting_last_recording_dir)).toString();
        if (!recording_directory.isEmpty()) {
            const auto recording_files = QDir(recording_directory).entryInfoList(
                {QStringLiteral("*.lswdebug")}, QDir::Files, QDir::Time);
            for (const auto& file_info : recording_files) {
                if (recent_sessions_.size() >= k_max_recent_sessions) {
                    break;
                }
                const auto path = file_info.absoluteFilePath();
                if (!recent_sessions_.contains(path)) {
                    recent_sessions_.append(path);
                }
            }
        }
    }
    recent_sessions_.removeDuplicates();
    while (recent_sessions_.size() > k_max_recent_sessions) {
        recent_sessions_.removeLast();
    }
    last_movie_ = settings.value(QString::fromLatin1(k_setting_last_movie)).toString();
    last_session_ = settings.value(QString::fromLatin1(k_setting_last_session)).toString();
    updateRecentSessionsMenu();

    // External params
    const auto param_list = settings.value(QString::fromLatin1(k_setting_external_params)).toStringList();
    for (const auto& entry : param_list) {
        const int eq = entry.indexOf(QLatin1Char('='));
        if (eq > 0) {
            external_params_.insert(entry.left(eq), entry.mid(eq + 1));
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
    const QString movie_key = currentMovieKey();
    const QStringList entries =
        settings.value(breakpointSettingKey(movie_key)).toStringList();
    if (entries.isEmpty()) return;

    bool has_enabled_breakpoint = false;
    auto& manager = dc->breakpointManager();
    for (const auto& entry : entries) {
        const auto parts = entry.split(QLatin1Char('\t'));
        if (parts.size() != 4) continue;
        bool ok_script_id = false;
        bool ok_offset = false;
        const int script_id = parts[0].toInt(&ok_script_id);
        const int offset = parts[2].toInt(&ok_offset);
        if (!ok_script_id || !ok_offset) continue;

        const bool enabled = parts[3] == QLatin1String("1");
        const auto breakpoint =
            manager.addBreakpoint(script_id, parts[1].toStdString(), offset);
        if (!enabled) {
            manager.setBreakpoint(breakpoint.withEnabled(false));
        } else {
            has_enabled_breakpoint = true;
        }
    }

    if (has_enabled_breakpoint) {
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
            QString::number(bp.script_id),
            QString::fromStdString(bp.handler_name),
            QString::number(bp.offset),
            QString::number(bp.enabled ? 1 : 0)};
        entries.append(fields.join(QLatin1Char('\t')));
    }

    QSettings settings;
    settings.setValue(breakpointSettingKey(currentMovieKey()), entries);
}

void DebuggerWindow::addRecentSession(const QString& path, bool make_last_session) {
    recent_sessions_.removeAll(path);
    recent_sessions_.prepend(path);
    while (recent_sessions_.size() > k_max_recent_sessions) {
        recent_sessions_.removeLast();
    }
    if (make_last_session) {
        last_session_ = path;
    }
    updateRecentSessionsMenu();

    // Persist both the movie source and the target used to reopen a session.
    QSettings settings;
    settings.setValue(QString::fromLatin1(k_setting_last_movie), last_movie_);
    settings.setValue(QString::fromLatin1(k_setting_last_session), last_session_);
    settings.setValue(QString::fromLatin1(k_setting_recent_sessions), recent_sessions_);
}

void DebuggerWindow::updateRecentSessionsMenu() {
    recent_sessions_menu_->clear();
    if (recent_sessions_.isEmpty()) {
        recent_sessions_menu_->addAction(QStringLiteral("(none)"))->setEnabled(false);
        return;
    }
    for (const auto& path : recent_sessions_) {
        // Files are labelled by their file name; URLs by their path segment.
        QString label = QFileInfo(path).fileName();
        if (label.isEmpty() || isUrl(path)) {
            label = QUrl(path).host() + QUrl(path).path();
        }
        if (label.isEmpty()) {
            label = path;
        }
        auto* action = recent_sessions_menu_->addAction(
            label + QStringLiteral("  —  ") + path);
        action->setData(path);
        connect(action, &QAction::triggered, this, &DebuggerWindow::onOpenRecent);
    }
    recent_sessions_menu_->addSeparator();
    auto* clear_action = recent_sessions_menu_->addAction(QStringLiteral("Clear Recent"));
    connect(clear_action, &QAction::triggered, this, [this] {
        recent_sessions_.clear();
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
    auto* file_menu = menuBar()->addMenu(QStringLiteral("&File"));

    auto* open_action = file_menu->addAction(QStringLiteral("&Open Movie..."));
    open_action->setShortcut(QKeySequence::Open);
    connect(open_action, &QAction::triggered, this, &DebuggerWindow::onOpenMovie);

    auto* open_url_action = file_menu->addAction(QStringLiteral("Open &URL..."));
    open_url_action->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(open_url_action, &QAction::triggered, this, &DebuggerWindow::onOpenUrl);

    auto* open_recording_action = file_menu->addAction(
        QStringLiteral("Open Debug &Recording..."));
    open_recording_action->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    connect(open_recording_action, &QAction::triggered,
            this, &DebuggerWindow::onOpenRecording);

    recent_sessions_menu_ = file_menu->addMenu(QStringLiteral("&Recent Sessions"));
    updateRecentSessionsMenu();

    file_menu->addSeparator();

    auto* quit_action = file_menu->addAction(QStringLiteral("&Quit"));
    quit_action->setShortcut(QKeySequence::Quit);
    connect(quit_action, &QAction::triggered, this, &QWidget::close);

    // ---- Parameters menu ----
    params_menu_ = menuBar()->addMenu(QStringLiteral("&Parameters"));

    auto* edit_params_action = params_menu_->addAction(
        QStringLiteral("&Edit Parameters..."));
    edit_params_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    connect(edit_params_action, &QAction::triggered,
            this, &DebuggerWindow::onEditParameters);

    auto* default_preset_action = params_menu_->addAction(
        QStringLiteral("&Default Preset"));
    connect(default_preset_action, &QAction::triggered,
            this, &DebuggerWindow::onLoadDefaultPreset);

    auto* clear_params_action = params_menu_->addAction(
        QStringLiteral("&Clear All Parameters"));
    connect(clear_params_action, &QAction::triggered,
            this, &DebuggerWindow::onClearParameters);

    params_menu_->addSeparator();
    // Current params shown below (updated dynamically)

    // ---- Debug menu ----
    auto* debug_menu = menuBar()->addMenu(QStringLiteral("&Debug"));

    continue_action_ = debug_menu->addAction(QStringLiteral("&Continue"));
    continue_action_->setShortcut(QKeySequence(Qt::Key_F5));
    connect(continue_action_, &QAction::triggered, this, &DebuggerWindow::onContinue);

    pause_action_ = debug_menu->addAction(QStringLiteral("&Pause"));
    pause_action_->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(pause_action_, &QAction::triggered, this, &DebuggerWindow::onPause);

    debug_menu->addSeparator();

    step_into_action_ = debug_menu->addAction(QStringLiteral("Step &Into"));
    step_into_action_->setShortcut(QKeySequence(Qt::Key_F11));
    connect(step_into_action_, &QAction::triggered, this, &DebuggerWindow::onStepInto);

    step_over_action_ = debug_menu->addAction(QStringLiteral("Step &Over"));
    step_over_action_->setShortcut(QKeySequence(Qt::Key_F10));
    connect(step_over_action_, &QAction::triggered, this, &DebuggerWindow::onStepOver);

    step_out_action_ = debug_menu->addAction(QStringLiteral("Step O&ut"));
    step_out_action_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F11));
    connect(step_out_action_, &QAction::triggered, this, &DebuggerWindow::onStepOut);

    debug_menu->addSeparator();

    toggle_breakpoint_action_ = debug_menu->addAction(
        QStringLiteral("Toggle &Breakpoint"));
    toggle_breakpoint_action_->setShortcut(QKeySequence(Qt::Key_F9));
    connect(toggle_breakpoint_action_, &QAction::triggered,
            this, &DebuggerWindow::onToggleBreakpoint);

    clear_breakpoints_action_ = debug_menu->addAction(
        QStringLiteral("&Clear All Breakpoints"));
    connect(clear_breakpoints_action_, &QAction::triggered,
            this, &DebuggerWindow::onClearBreakpoints);

    // ---- View menu ----
    auto* view_menu = menuBar()->addMenu(QStringLiteral("&View"));
    view_menu->addAction(left_dock_->toggleViewAction());
    view_menu->addAction(call_stack_dock_->toggleViewAction());
    view_menu->addAction(variables_dock_->toggleViewAction());
    view_menu->addAction(watch_dock_->toggleViewAction());
    view_menu->addAction(bottom_dock_->toggleViewAction());
    view_menu->addAction(debug_dock_->toggleViewAction());

    view_menu->addSeparator();
    auto* reset_layout_action = view_menu->addAction(QStringLiteral("&Reset Layout"));
    connect(reset_layout_action, &QAction::triggered,
            this, &DebuggerWindow::onResetLayout);
}

void DebuggerWindow::setupToolBar() {
    auto* toolbar = addToolBar(QStringLiteral("Debug"));
    toolbar->setObjectName(QStringLiteral("debugToolbar"));
    toolbar->setMovable(false);

    auto* open_action = toolbar->addAction(QStringLiteral("📂 Open..."));
    connect(open_action, &QAction::triggered, this, &DebuggerWindow::onOpenMovie);

    toolbar->addSeparator();

    play_action_ = toolbar->addAction(QStringLiteral("▶ Play"));
    connect(play_action_, &QAction::triggered, this, &DebuggerWindow::onPlay);

    record_action_ = toolbar->addAction(QStringLiteral("▶⏺ Play & Record"));
    connect(record_action_, &QAction::triggered,
            this, &DebuggerWindow::onPlayAndRecord);

    pause_action_ = toolbar->addAction(QStringLiteral("⏸ Pause"));
    connect(pause_action_, &QAction::triggered, this, &DebuggerWindow::onPause);

    stop_action_ = toolbar->addAction(QStringLiteral("⏹ Stop"));
    connect(stop_action_, &QAction::triggered, this, &DebuggerWindow::onStop);

    toolbar->addSeparator();

    continue_action_ = toolbar->addAction(QStringLiteral("▶ Continue"));
    connect(continue_action_, &QAction::triggered, this, &DebuggerWindow::onContinue);

    step_into_action_ = toolbar->addAction(QStringLiteral("↓ Into"));
    connect(step_into_action_, &QAction::triggered, this, &DebuggerWindow::onStepInto);

    step_over_action_ = toolbar->addAction(QStringLiteral("→ Over"));
    connect(step_over_action_, &QAction::triggered, this, &DebuggerWindow::onStepOver);

    step_out_action_ = toolbar->addAction(QStringLiteral("↑ Out"));
    connect(step_out_action_, &QAction::triggered, this, &DebuggerWindow::onStepOut);

    toolbar->addSeparator();

    auto* clear_bp_action = toolbar->addAction(QStringLiteral("Clear BPs"));
    connect(clear_bp_action, &QAction::triggered, this, &DebuggerWindow::onClearBreakpoints);

    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    status_label_ = new QLabel(QStringLiteral(" ● Stopped"), this);
    status_label_->setStyleSheet(
        QStringLiteral("color:#8a9488; font-weight:bold; padding:0 8px;"));
    toolbar->addWidget(status_label_);
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
    left_dock_ = new QDockWidget(QStringLiteral("Movies & Scripts"), this);
    left_dock_->setObjectName(QStringLiteral("movieTreeDock"));
    movie_tree_panel_ = new MovieTreePanel(left_dock_);
    left_dock_->setWidget(movie_tree_panel_);
    addDockWidget(Qt::LeftDockWidgetArea, left_dock_);

    // Right docks: Call Stack / Variables / Watches stacked vertically.
    // Add order matters for saveState()/restoreState(): a split host must be
    // added before the dock split out of it.
    call_stack_dock_ = new QDockWidget(QStringLiteral("Call Stack"), this);
    call_stack_dock_->setObjectName(QStringLiteral("callStackDock"));
    call_stack_panel_ = new CallStackPanel(call_stack_dock_);
    call_stack_dock_->setWidget(call_stack_panel_);
    addDockWidget(Qt::RightDockWidgetArea, call_stack_dock_);

    variables_dock_ = new QDockWidget(QStringLiteral("Variables"), this);
    variables_dock_->setObjectName(QStringLiteral("variablesDock"));
    variables_panel_ = new VariablesPanel(variables_dock_);
    variables_dock_->setWidget(variables_panel_);
    addDockWidget(Qt::RightDockWidgetArea, variables_dock_);
    splitDockWidget(call_stack_dock_, variables_dock_, Qt::Vertical);

    watch_dock_ = new QDockWidget(QStringLiteral("Watches"), this);
    watch_dock_->setObjectName(QStringLiteral("watchDock"));
    watch_panel_ = new WatchPanel(watch_dock_);
    watch_dock_->setWidget(watch_panel_);
    addDockWidget(Qt::RightDockWidgetArea, watch_dock_);
    splitDockWidget(variables_dock_, watch_dock_, Qt::Vertical);

    // Initial sizes match the old stacked Inspector (call stack ~150px,
    // watches ~220px; variables takes the remainder).
    resizeDocks({call_stack_dock_, watch_dock_}, {150, 220}, Qt::Vertical);

    // Bottom dock: Code view
    bottom_dock_ = new QDockWidget(QStringLiteral("Code"), this);
    bottom_dock_->setObjectName(QStringLiteral("codeDock"));
    code_view_panel_ = new CodeViewPanel(bottom_dock_);
    bottom_dock_->setWidget(code_view_panel_);
    addDockWidget(Qt::BottomDockWidgetArea, bottom_dock_);

    // Bottom-right dock: Debug window, beside the code view
    debug_dock_ = new QDockWidget(QStringLiteral("Debug Window"), this);
    debug_dock_->setObjectName(QStringLiteral("debugWindowDock"));
    debug_window_panel_ = new DebugWindowPanel(debug_dock_);
    debug_dock_->setWidget(debug_window_panel_);
    addDockWidget(Qt::BottomDockWidgetArea, debug_dock_);
    splitDockWidget(bottom_dock_, debug_dock_, Qt::Horizontal);

    // Per-dock "Hide Panel" context menu.
    for (QDockWidget* dock : {left_dock_, call_stack_dock_, variables_dock_,
                              watch_dock_, bottom_dock_, debug_dock_}) {
        addHidePanelMenu(dock);
    }

    // Snapshot of the default layout for View > Reset Layout. Captured here
    // (before restoreSettings()) so it is independent of any saved state.
    default_layout_state_ = saveState();
}

void DebuggerWindow::setupCentralWidget() {
    stage_widget_ = new StageWidget(this);
    setCentralWidget(stage_widget_);

    stage_widget_->setInputCallback(
        [this](int type, int stage_x, int stage_y,
               int key_code, const std::string& key_text,
               bool shift, bool ctrl, bool alt, bool right_button) {
            recordStageInput(type, stage_x, stage_y, key_code, key_text,
                             shift, ctrl, alt, right_button);
        });
}

void DebuggerWindow::setupShortcuts() {
    // Shortcuts handled via menu QAction shortcuts
}

void DebuggerWindow::connectSignals() {
    connect(context_, &DebuggerContext::movieLoaded, this, [this]() {
        if (context_->player() != nullptr) {
            movie_tree_panel_->populate(context_->player());
            refreshMovieIndex(movie_tree_panel_->lastSnapshot());
            restoreBreakpointsForCurrentMovie();
            refreshBreakpoints();
        }
        // Start each movie with a fresh debug log; keep the toggle state.
        debug_window_panel_->setDebugEnabled(context_->debugEnabled());
        debug_window_panel_->clearLog();
        updateToolbarState();
    });
    connect(debug_window_panel_, &DebugWindowPanel::debugEnabledChanged,
            context_, &DebuggerContext::setDebugEnabled);
    connect(context_, &DebuggerContext::vmOutput,
            debug_window_panel_, &DebugWindowPanel::appendLogLine);
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

    connect(movie_tree_panel_, &MovieTreePanel::scriptSelected,
            this, &DebuggerWindow::onScriptSelected);
    connect(movie_tree_panel_, &MovieTreePanel::handlerSelected,
            this, &DebuggerWindow::onHandlerSelected);
    connect(code_view_panel_, &CodeViewPanel::handlerChanged,
            this, &DebuggerWindow::onCodeHandlerChanged);
    connect(code_view_panel_, &CodeViewPanel::breakpointToggled,
            this, &DebuggerWindow::onBreakpointToggled);
    connect(code_view_panel_, &CodeViewPanel::goToDeclarationRequested,
            this, &DebuggerWindow::onGoToDeclaration);
    connect(context_, &DebuggerContext::movieNavigationRequested,
            this, &DebuggerWindow::onMovieNavigationRequested);
    // Runtime CCT/CST loads arrive as a snapshot from the VM thread; rebuild
    // the script list from it so newly loaded scripts appear.
    connect(context_, &DebuggerContext::castLoaded, this,
            [this](const MovieTreeSnapshot& snapshot) {
                movie_tree_panel_->populateFromSnapshot(snapshot);
                refreshMovieIndex(snapshot);
            });
    connect(watch_panel_, &WatchPanel::watchAdded,
            this, &DebuggerWindow::onWatchAdded);
    connect(watch_panel_, &WatchPanel::watchRemoved,
            this, &DebuggerWindow::onWatchRemoved);

    // Persist the dock layout immediately on user-driven changes
    // (hide/show, float/dock, move) so a crash or kill does not lose them.
    // Splitter drag-resizes are still captured by saveSettings() on close.
    for (QDockWidget* dock : {left_dock_, call_stack_dock_, variables_dock_,
                              watch_dock_, bottom_dock_, debug_dock_}) {
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
    const auto last_dir = settings.value(QString::fromLatin1(k_setting_last_dir)).toString();
    const auto path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open Director Movie"), last_dir,
        QStringLiteral("Director Files (*.dir *.dcr *.dxr *.cct *.cst);;"
                       "Debugger Recordings (*.lswdebug);;All Files (*)"));
    if (path.isEmpty()) return;
    settings.setValue(QString::fromLatin1(k_setting_last_dir), QFileInfo(path).absolutePath());
    if (QFileInfo(path).suffix().compare(QStringLiteral("lswdebug"),
                                         Qt::CaseInsensitive) == 0) {
        openRecording(path);
    } else {
        openMovie(path, external_params_);
    }
}

void DebuggerWindow::onOpenUrl() {
    // Remember the last URL entered so the next open starts pre-filled.
    QSettings settings;
    const auto last_url = settings.value(QString::fromLatin1(k_setting_last_url)).toString();
    const auto url = QInputDialog::getText(
        this, QStringLiteral("Open Movie URL"),
        QStringLiteral("Enter the URL of a Director movie (.dir/.dcr):"),
        QLineEdit::Normal, last_url);
    if (url.isEmpty()) return;
    settings.setValue(QString::fromLatin1(k_setting_last_url), url);
    openMovie(url, external_params_);
}

void DebuggerWindow::onOpenRecording() {
    QSettings settings;
    const auto last_dir = settings.value(QString::fromLatin1(k_setting_last_dir)).toString();
    const auto path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open Debug Recording"), last_dir,
        QStringLiteral("Debugger Recordings (*.lswdebug);;All Files (*)"));
    if (path.isEmpty()) return;
    settings.setValue(QString::fromLatin1(k_setting_last_dir), QFileInfo(path).absolutePath());
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

    const auto parse_header = [&recording, &error](const QJsonObject& root) {
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
            const auto params_object = params.toObject();
            for (auto it = params_object.begin(); it != params_object.end(); ++it) {
                if (!it.value().isString()) {
                    error = QStringLiteral(
                        "Recording contains a non-string external parameter");
                    return false;
                }
                recording.external_params.insert(it.key(), it.value().toString());
            }
        }
        return true;
    };

    const auto parse_event = [&error](const QJsonObject& object,
                                     RecordedInputEvent& event) {
        if (!recordingEventTypeFromName(
                object.value(QStringLiteral("type")).toString(), event.type)) {
            error = QStringLiteral("Recording contains an unknown input event type");
            return false;
        }
        event.time_ms = static_cast<qint64>(object.value(QStringLiteral("timeMs"))
                                               .toDouble(-1));
        if (event.time_ms < 0) {
            error = QStringLiteral("Recording contains an invalid event time");
            return false;
        }
        event.stage_x = object.value(QStringLiteral("stageX")).toInt();
        event.stage_y = object.value(QStringLiteral("stageY")).toInt();
        event.director_key_code = object.value(QStringLiteral("keyCode")).toInt();
        event.key_text = object.value(QStringLiteral("keyText")).toString();
        event.shift = object.value(QStringLiteral("shift")).toBool();
        event.ctrl = object.value(QStringLiteral("ctrl")).toBool();
        event.alt = object.value(QStringLiteral("alt")).toBool();
        event.right_button = object.value(QStringLiteral("rightButton")).toBool();
        return true;
    };

    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(data, &parse_error);
    const bool whole_document_is_object =
        parse_error.error == QJsonParseError::NoError && document.isObject();
    if (whole_document_is_object &&
        document.object().value(QStringLiteral("version")).toInt() == 1) {
        const auto root = document.object();
        if (root.value(QStringLiteral("format")).toString() !=
                QStringLiteral("LibreShockwave Debugger Recording")) {
            error = QStringLiteral("Unsupported debugger recording format");
            return false;
        }
        if (!parse_header(root)) {
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
            if (!parse_event(value.toObject(), event)) {
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
    QJsonParseError header_parse_error;
    const auto header_document = QJsonDocument::fromJson(
        lines.front().trimmed(), &header_parse_error);
    if (header_parse_error.error != QJsonParseError::NoError ||
        !header_document.isObject()) {
        error = QStringLiteral("Invalid recording JSON: %1")
                    .arg(parse_error.errorString());
        return false;
    }
    const auto header = header_document.object();
    if (header.value(QStringLiteral("format")).toString() !=
            QStringLiteral("LibreShockwave Debugger Recording") ||
        header.value(QStringLiteral("version")).toInt() != 2) {
        error = QStringLiteral("Unsupported debugger recording format");
        return false;
    }
    if (!parse_header(header)) {
        return false;
    }

    for (int index = 1; index < lines.size(); ++index) {
        const auto line = lines[index].trimmed();
        if (line.isEmpty()) {
            continue;
        }
        QJsonParseError event_parse_error;
        const auto event_document = QJsonDocument::fromJson(line, &event_parse_error);
        if (event_parse_error.error != QJsonParseError::NoError ||
            !event_document.isObject()) {
            if (index == lines.size() - 1 && !data.endsWith('\n')) {
                break;
            }
            error = QStringLiteral("Invalid recording event: %1")
                        .arg(event_parse_error.errorString());
            return false;
        }
        RecordedInputEvent event;
        if (!parse_event(event_document.object(), event)) {
            return false;
        }
        recording.events.push_back(std::move(event));
    }
    return true;
}

bool DebuggerWindow::openRecordingFile(QString& error) {
    if (recording_file_path_.isEmpty()) {
        error = QStringLiteral("No recording file was selected");
        return false;
    }

    recording_file_.setFileName(recording_file_path_);
    if (!recording_file_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = QStringLiteral("Unable to save recording: %1")
                    .arg(recording_file_.errorString());
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
    for (auto it = external_params_.cbegin(); it != external_params_.cend(); ++it) {
        params.insert(it.key(), it.value());
    }
    header.insert(QStringLiteral("externalParams"), params);

    auto bytes = QJsonDocument(header).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    if (recording_file_.write(bytes) != bytes.size() || !recording_file_.flush()) {
        error = QStringLiteral("Unable to save recording: %1")
                    .arg(recording_file_.errorString());
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
    object.insert(QStringLiteral("timeMs"), event.time_ms);
    object.insert(QStringLiteral("type"), type);
    object.insert(QStringLiteral("stageX"), event.stage_x);
    object.insert(QStringLiteral("stageY"), event.stage_y);
    object.insert(QStringLiteral("keyCode"), event.director_key_code);
    object.insert(QStringLiteral("keyText"), event.key_text);
    object.insert(QStringLiteral("shift"), event.shift);
    object.insert(QStringLiteral("ctrl"), event.ctrl);
    object.insert(QStringLiteral("alt"), event.alt);
    object.insert(QStringLiteral("rightButton"), event.right_button);

    auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    if (recording_file_.write(bytes) != bytes.size()) {
        error = QStringLiteral("Unable to save recording: %1")
                    .arg(recording_file_.errorString());
        return false;
    }
    return true;
}

bool DebuggerWindow::flushRecordingFile(QString& error) {
    if (!recording_file_.isOpen() || recording_file_.flush()) {
        return true;
    }
    error = QStringLiteral("Unable to flush recording: %1")
                .arg(recording_file_.errorString());
    return false;
}

void DebuggerWindow::closeRecordingFile() {
    if (recording_flush_timer_ != nullptr) {
        recording_flush_timer_->stop();
    }
    if (recording_file_.isOpen()) {
        recording_file_.flush();
        recording_file_.close();
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
    play_requested_ = false;
    cancelReplay();
    pending_replay_ = std::move(recording);
    if (!openMovieInternal(pending_replay_->movie, pending_replay_->external_params)) {
        pending_replay_.reset();
        return false;
    }
    last_session_ = path;
    addRecentSession(path);
    return true;
}

void DebuggerWindow::startPendingReplayIfReady() {
    if (!pending_replay_.has_value() || !context_->hasMovie()) {
        return;
    }

    replay_events_ = std::move(pending_replay_->events);
    pending_replay_.reset();
    replay_index_ = 0;
    replaying_ = true;
    play_requested_ = false;
    replay_clock_base_ms_ = 0;
    replay_waiting_for_navigation_ = false;
    replay_clock_.invalidate();
    context_->play();
    status_label_->setText(QStringLiteral(" ◉ Replaying"));
    status_label_->setStyleSheet(
        QStringLiteral("color:#c8873d; font-weight:bold; padding:0 8px;"));
    statusBar()->showMessage(QStringLiteral("Replaying debugger recording..."));
    updateToolbarState();
}

void DebuggerWindow::recordStageInput(int type, int stage_x, int stage_y,
                                      int key_code, const std::string& key_text,
                                      bool shift, bool ctrl, bool alt,
                                      bool right_button) {
    // Do not let physical input perturb an automated replay.
    if (replaying_) {
        return;
    }

    if (recording_) {
        const qint64 event_time_ms = recording_clock_.isValid()
            ? recording_clock_.elapsed()
            : 0;
        const auto input_type = static_cast<DebuggerContext::InputEvent::Type>(type);
        bool should_record = true;
        if (input_type == DebuggerContext::InputEvent::MouseMove) {
            // Stage mouseMove can arrive hundreds of times per second. The
            // movie only needs a useful motion sample, while clicks and keys
            // remain lossless and carry their own coordinates.
            should_record = !has_recorded_mouse_move_ ||
                           (event_time_ms - last_recorded_mouse_move_ms_ >=
                            k_recording_mouse_move_interval_ms &&
                            (stage_x != last_recorded_mouse_x_ ||
                             stage_y != last_recorded_mouse_y_));
        }

        if (should_record) {
            RecordedInputEvent event;
            event.time_ms = event_time_ms;
            event.type = type;
            event.stage_x = stage_x;
            event.stage_y = stage_y;
            event.director_key_code = key_code;
            event.key_text = QString::fromStdString(key_text);
            event.shift = shift;
            event.ctrl = ctrl;
            event.alt = alt;
            event.right_button = right_button;

            QString error;
            if (!writeRecordingEvent(event, error)) {
                recording_ = false;
                recording_clock_.invalidate();
                closeRecordingFile();
                QMessageBox::warning(this, QStringLiteral("Save Debug Recording"), error);
            } else if (input_type == DebuggerContext::InputEvent::MouseMove) {
                last_recorded_mouse_move_ms_ = event_time_ms;
                last_recorded_mouse_x_ = stage_x;
                last_recorded_mouse_y_ = stage_y;
                has_recorded_mouse_move_ = true;
            }
        }
    }

    DebuggerContext::InputEvent event;
    event.type = static_cast<DebuggerContext::InputEvent::Type>(type);
    event.stage_x = stage_x;
    event.stage_y = stage_y;
    event.director_key_code = key_code;
    event.key_text = key_text;
    event.shift = shift;
    event.ctrl = ctrl;
    event.alt = alt;
    event.right_button = right_button;
    context_->enqueueInput(std::move(event));
}

void DebuggerWindow::finishRecording() {
    if (!recording_ && !recording_file_.isOpen()) {
        return;
    }
    recording_ = false;
    recording_clock_.invalidate();

    QString error;
    if (!flushRecordingFile(error)) {
        QMessageBox::warning(this, QStringLiteral("Save Debug Recording"), error);
    } else {
        QSettings settings;
        settings.setValue(QString::fromLatin1(k_setting_last_recording_dir),
                          QFileInfo(recording_file_path_).absolutePath());
    }
    closeRecordingFile();
}

void DebuggerWindow::cancelReplay() {
    if (replay_timer_ != nullptr) {
        replay_timer_->stop();
    }
    replaying_ = false;
    replay_clock_.invalidate();
    replay_clock_base_ms_ = 0;
    replay_events_.clear();
    replay_index_ = 0;
    replay_waiting_for_navigation_ = false;
}

void DebuggerWindow::pauseReplayForNavigation() {
    if (!replaying_ || replay_waiting_for_navigation_) {
        return;
    }
    if (replay_clock_.isValid()) {
        replay_clock_base_ms_ += replay_clock_.elapsed();
        replay_clock_.invalidate();
    }
    replay_waiting_for_navigation_ = true;
    replay_timer_->stop();
}

void DebuggerWindow::resumeReplayAfterNavigation() {
    if (!replaying_ || !replay_waiting_for_navigation_) {
        return;
    }
    if (context_->networkReady()) {
        replay_clock_.start();
    } else {
        replay_clock_.invalidate();
    }
    replay_waiting_for_navigation_ = false;
    replay_timer_->start();
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
            pending_replay_.reset();
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
                                       const std::string& base_path,
                                       bool resume_playback) {
    const auto byte_count = data.size();
    const bool ok = context_->loadMovieFromData(std::move(data), base_path);
    if (!ok) {
        statusBar()->showMessage(QStringLiteral("Failed to load movie"), 5000);
        return;
    }

    stage_widget_->prepareForMovie();
    is_paused_ = false;

    context_->setExternalParams(
        ExternalParamsDialog::toPlayerParams(external_params_));

    setWindowTitle(QStringLiteral("LibreShockwave Debugger — %1").arg(label));
    movie_tree_panel_->populate(context_->player());
    refreshMovieIndex(movie_tree_panel_->lastSnapshot());
    restoreBreakpointsForCurrentMovie();
    const bool is_recording_session = pending_replay_.has_value();
    last_movie_ = label;
    addRecentSession(label, !is_recording_session);
    refreshBreakpoints();

    status_label_->setText(QStringLiteral(" ● Ready"));
    status_label_->setStyleSheet(
        QStringLiteral("color:#8a9488; font-weight:bold; padding:0 8px;"));
    updateToolbarState();

    statusBar()->showMessage(
        QStringLiteral("Loaded: %1 (%2 bytes) — press Play to start")
            .arg(label)
            .arg(byte_count),
        5000);
    if (replaying_ && resume_playback) {
        // Runtime navigation replaces the Player session. Keep the active
        // recording replay attached to the new session and resume its
        // paused recording clock.
        context_->play();
        resumeReplayAfterNavigation();
    } else if (!replaying_ && (resume_playback || play_requested_)) {
        play_requested_ = false;
        onPlay();
    }
}

void DebuggerWindow::onOpenRecent() {
    auto* action = qobject_cast<QAction*>(sender());
    if (action == nullptr) return;
    const auto path = action->data().toString();
    if (!path.isEmpty()) {
        openMovie(path, external_params_);
    }
}

// -----------------------------------------------------------------------
// Parameters menu
// -----------------------------------------------------------------------

void DebuggerWindow::onEditParameters() {
    ExternalParamsDialog dialog(external_params_, this);
    if (dialog.exec() != QDialog::Accepted) return;

    external_params_ = dialog.params();
    saveSettings();
    updateParamsMenu();

    // Keep the context copy in sync so fresh Play sessions use these params.
    if (context_->hasMovie()) {
        context_->setExternalParams(
            ExternalParamsDialog::toPlayerParams(external_params_));
        statusBar()->showMessage(
            QStringLiteral("Parameters updated (%1 entries) — reload movie to apply")
                .arg(external_params_.size()), 4000);
    } else {
        statusBar()->showMessage(
            QStringLiteral("Parameters saved (%1 entries)")
                .arg(external_params_.size()), 3000);
    }
}

void DebuggerWindow::onLoadDefaultPreset() {
    // Apply the preset directly — no dialog needed
    ExternalParamsDialog dialog(external_params_, this);
    dialog.loadDefaultPreset();
    external_params_ = dialog.params();
    saveSettings();
    updateParamsMenu();

    // Apply to the current session and retain for a fresh Play session.
    if (context_->hasMovie()) {
        context_->setExternalParams(
            ExternalParamsDialog::toPlayerParams(external_params_));
    }

    statusBar()->showMessage(
        QStringLiteral("Default preset loaded (%1 parameters)")
            .arg(external_params_.size()), 4000);
}

void DebuggerWindow::onClearParameters() {
    external_params_.clear();
    if (context_->hasMovie()) {
        context_->setExternalParams({});
    }
    saveSettings();
    updateParamsMenu();
    statusBar()->showMessage(QStringLiteral("Parameters cleared"), 3000);
}

void DebuggerWindow::updateParamsMenu() {
    if (params_menu_ == nullptr) return;

    // Remove old dynamic items (everything after the separator)
    const auto actions = params_menu_->actions();
    // Find the separator and remove everything after it
    int sep_idx = -1;
    for (int i = 0; i < actions.size(); ++i) {
        if (actions[i]->isSeparator()) {
            sep_idx = i;
            break;
        }
    }
    if (sep_idx >= 0) {
        for (int i = actions.size() - 1; i > sep_idx; --i) {
            params_menu_->removeAction(actions[i]);
        }
    }

    if (external_params_.isEmpty()) {
        auto* empty_action = params_menu_->addAction(
            QStringLiteral("(no parameters set)"));
        empty_action->setEnabled(false);
        // Update status bar hint
        if (!context_->hasMovie()) {
            statusBar()->showMessage(
                QStringLiteral("Tip: Use Parameters → Edit to set connection params, then open a movie"),
                8000);
        }
    } else {
        for (auto it = external_params_.cbegin(); it != external_params_.cend(); ++it) {
            auto* item = params_menu_->addAction(
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
    if (pending_replay_.has_value()) {
        startPendingReplayIfReady();
        return;
    }
    context_->play();
    status_label_->setText(QStringLiteral(" ● Running"));
    status_label_->setStyleSheet(
        QStringLiteral("color:#4da86d; font-weight:bold; padding:0 8px;"));
    statusBar()->showMessage(QStringLiteral("Running..."));
    updateToolbarState();
}

void DebuggerWindow::onPlayAndRecord() {
    if (!context_->hasMovie() || context_->isPlaying() || is_paused_ ||
        pending_replay_.has_value()) {
        return;
    }

    const auto suggested_name = QStringLiteral("debug-recording.lswdebug");
    QSettings settings;
    const auto last_recording_dir =
        settings.value(QString::fromLatin1(k_setting_last_recording_dir)).toString();
    const auto suggested_path = last_recording_dir.isEmpty()
        ? suggested_name
        : QDir(last_recording_dir).filePath(suggested_name);
    auto path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save Debug Recording"), suggested_path,
        QStringLiteral("Debugger Recordings (*.lswdebug);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).suffix().compare(QStringLiteral("lswdebug"),
                                         Qt::CaseInsensitive) != 0) {
        path += QStringLiteral(".lswdebug");
    }

    recording_file_path_ = path;
    QString error;
    if (!openRecordingFile(error)) {
        QMessageBox::warning(this, QStringLiteral("Save Debug Recording"), error);
        recording_file_path_.clear();
        return;
    }

    last_recorded_mouse_move_ms_ = -1;
    last_recorded_mouse_x_ = 0;
    last_recorded_mouse_y_ = 0;
    has_recorded_mouse_move_ = false;
    recording_clock_.invalidate();
    recording_ = true;
    recording_flush_timer_->start();
    context_->play();

    status_label_->setText(QStringLiteral(" ⏺ Recording"));
    status_label_->setStyleSheet(
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
    code_view_panel_->clear();
    variables_panel_->clearAll();
    call_stack_panel_->clearAll();
    watch_panel_->clearAll();
    movie_tree_panel_->clearAll();
    stage_widget_->clearFrame();
    is_paused_ = false;
    status_label_->setText(QStringLiteral(" ● Stopped"));
    status_label_->setStyleSheet(
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
    if (is_paused_) {
        auto snap = context_->debugController()->currentSnapshot();
        if (!snap.has_value()) return;
        context_->toggleBreakpoint(snap->script_id, snap->handler_name,
                                   snap->instruction_offset);
        refreshBreakpoints();
        persistBreakpoints();
        return;
    }

    // While running: toggle at the last code line clicked in the gutter
    // (matches the WASM harness's click-to-toggle-anytime behavior).
    if (current_handler_name_.empty()) {
        statusBar()->showMessage(
            QStringLiteral("Select a handler first to toggle breakpoints"), 4000);
        return;
    }
    const int offset = code_view_panel_->lastClickedOffset();
    if (offset < 0) {
        statusBar()->showMessage(
            QStringLiteral("Click a code line's gutter column to toggle a breakpoint"),
            4000);
        return;
    }
    context_->toggleBreakpoint(current_script_id_, current_handler_name_, offset);
    refreshBreakpoints();
    persistBreakpoints();
}

void DebuggerWindow::onBreakpointToggled(int script_id,
                                         const std::string& handler_name,
                                         int offset) {
    if (context_->toggleBreakpoint(script_id, handler_name, offset)) {
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
    is_paused_ = true;
    status_label_->setText(QStringLiteral(" ◉ Paused"));
    status_label_->setStyleSheet(
        QStringLiteral("color:#c9a53b; font-weight:bold; padding:0 8px;"));
    statusBar()->showMessage(
        QStringLiteral("Paused at %1::%2 offset %3")
            .arg(QString::fromStdString(data.script_name),
                 QString::fromStdString(data.handler_name))
            .arg(data.instruction_offset));

    current_script_id_ = data.script_id;
    current_handler_name_ = data.handler_name;
    code_view_panel_->setCurrentInstruction(data.instruction_offset);
    variables_panel_->updateFromSnapshot(data);
    call_stack_panel_->updateFromSnapshot(data);
    watch_panel_->updateFromSnapshot(data);
    updateToolbarState();
    // The VM snapshot carries no cast-lib number; 0 means "match scriptId in
    // any cast library", preserving the previous first-match behavior.
    loadHandlerCode(0, data.script_id, data.handler_name);
}

void DebuggerWindow::onResumed() {
    is_paused_ = false;
    status_label_->setText(QStringLiteral(" ● Running"));
    status_label_->setStyleSheet(
        QStringLiteral("color:#4da86d; font-weight:bold; padding:0 8px;"));
    statusBar()->showMessage(QStringLiteral("Running..."));
    variables_panel_->clearAll();
    call_stack_panel_->clearAll();
    updateToolbarState();
}

void DebuggerWindow::onBreakpointsChanged() {
    refreshBreakpoints();
}

void DebuggerWindow::onRecordingFlushTimer() {
    if (!recording_ || !recording_file_.isOpen()) {
        recording_flush_timer_->stop();
        return;
    }

    QString error;
    if (!flushRecordingFile(error)) {
        recording_ = false;
        recording_clock_.invalidate();
        closeRecordingFile();
        QMessageBox::warning(this, QStringLiteral("Save Debug Recording"), error);
    }
}

void DebuggerWindow::onFrameRendered(const QImage& image) {
    // The image is a detached copy produced from an immutable snapshot, so
    // painting here never races with the VM or renderer threads.
    stage_widget_->setFrameImage(image);

    if (!qEnvironmentVariable("LIBRESHOCKWAVE_DEBUGGER_FRAME_STATS").isEmpty()) {
        if (!frame_stats_clock_.isValid()) {
            frame_stats_clock_.start();
        }
        ++frame_stats_count_;
        const qint64 elapsed = frame_stats_clock_.elapsed();
        if (elapsed >= 1000) {
            std::cerr << "debugger presentation: "
                      << (1000.0 * frame_stats_count_) / static_cast<double>(elapsed)
                      << " fps\n";
            frame_stats_clock_.restart();
            frame_stats_count_ = 0;
        }
    }

    const auto dump_path = qEnvironmentVariable(
        "LIBRESHOCKWAVE_DEBUGGER_FRAME_DUMP");
    // Frame dumps are diagnostic output, not part of presentation. Writing a
    // PNG for every movie frame can itself destroy the frame budget, so keep
    // the requested rolling snapshot at most once per second.
    if (!dump_path.isEmpty() &&
        (!frame_dump_clock_.isValid() || frame_dump_clock_.elapsed() >= 1000)) {
        frame_dump_clock_.restart();
        QSaveFile dump_file(dump_path);
        if (dump_file.open(QIODevice::WriteOnly) &&
            image.save(&dump_file, "PNG")) {
            dump_file.commit();
        }
    }
}

void DebuggerWindow::onPlaybackStarted() {
    if (recording_ && !recording_clock_.isValid()) {
        recording_clock_.start();
    }
    if (replaying_ && !replay_waiting_for_navigation_) {
        // A movie navigation replaces the Player session while a recording
        // is replaying. Preserve the original recording clock and only
        // restart the timer if the replacement session stopped it.
        if (!replay_clock_.isValid() && context_->networkReady()) {
            replay_clock_.start();
        }
        if (!replay_timer_->isActive()) {
            replay_timer_->start();
        }
    }
}

void DebuggerWindow::onReplayTimer() {
    if (!replaying_) {
        replay_timer_->stop();
        return;
    }

    if (replay_waiting_for_navigation_) {
        return;
    }

    if (!replay_clock_.isValid()) {
        if (!context_->networkReady()) {
            return;
        }
        replay_clock_.start();
    } else if (!context_->networkReady()) {
        // A replay's timestamps are relative to usable movie time, not to
        // wall-clock time spent loading external casts or completing the
        // session handshake.  Keep the pending event at its recorded time so
        // clicks are not delivered to an empty/loading score.
        replay_clock_base_ms_ += replay_clock_.elapsed();
        replay_clock_.invalidate();
        return;
    }

    const qint64 elapsed = replay_clock_base_ms_ + replay_clock_.elapsed();
    while (replay_index_ < replay_events_.size() &&
           replay_events_[replay_index_].time_ms <= elapsed) {
        const auto& recorded = replay_events_[replay_index_++];
        DebuggerContext::InputEvent event;
        event.type = static_cast<DebuggerContext::InputEvent::Type>(recorded.type);
        event.stage_x = recorded.stage_x;
        event.stage_y = recorded.stage_y;
        event.director_key_code = recorded.director_key_code;
        event.key_text = recorded.key_text.toStdString();
        event.shift = recorded.shift;
        event.ctrl = recorded.ctrl;
        event.alt = recorded.alt;
        event.right_button = recorded.right_button;
        context_->enqueueInput(std::move(event));
    }

    if (replay_index_ >= replay_events_.size()) {
        replay_timer_->stop();
        replaying_ = false;
        status_label_->setText(QStringLiteral(" ● Running"));
        status_label_->setStyleSheet(
            QStringLiteral("color:#4da86d; font-weight:bold; padding:0 8px;"));
        statusBar()->showMessage(QStringLiteral("Debugger recording replay complete"), 4000);
        updateToolbarState();
    }
}

void DebuggerWindow::onErrorOccurred(const QString& message) {
    statusBar()->showMessage(message, 5000);
    auto *box = new QMessageBox(QMessageBox::Warning, QStringLiteral("Error"), message, QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowModality(Qt::NonModal);
    box->setModal(false);
    box->show();
}

static bool isDirectorMovieUrl(const QString& url) {
    const QString clean =
        QUrl(url).path().isEmpty()
            ? QString(url).split(QChar('?')).first().split(QChar('#')).first()
            : QUrl(url).path();
    const QString lower = clean.toLower();
    return lower.endsWith(QStringLiteral(".dcr")) ||
           lower.endsWith(QStringLiteral(".dir")) ||
           lower.endsWith(QStringLiteral(".dxr"));
}

void DebuggerWindow::onMovieNavigationRequested(const QString& url) {
    if (!isDirectorMovieUrl(url)) {
        statusBar()->showMessage(
            QStringLiteral("Navigation to %1 (non-movie) — keeping stage ticking").arg(url),
            5000);
        return;
    }
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

void DebuggerWindow::onScriptSelected(int cast_lib_number, int script_id) {
    current_cast_lib_number_ = cast_lib_number;
    current_script_id_ = script_id;
    current_handler_name_.clear();
    loadHandlerCode(cast_lib_number, script_id, "");
}

void DebuggerWindow::onHandlerSelected(int cast_lib_number, int script_id,
                                       const std::string& handler_name) {
    current_cast_lib_number_ = cast_lib_number;
    current_script_id_ = script_id;
    current_handler_name_ = handler_name;
    loadHandlerCode(cast_lib_number, script_id, handler_name);
}

void DebuggerWindow::onCodeHandlerChanged(const std::string& handler_name) {
    current_handler_name_ = handler_name;
    loadHandlerCode(current_cast_lib_number_, current_script_id_, handler_name);
}

void DebuggerWindow::onGoToDeclaration(const DeclarationTarget& target) {
    current_cast_lib_number_ = target.cast_lib_number;
    current_script_id_ = target.script_id;
    current_handler_name_ =
        target.kind == DeclarationKind::Method ? target.handler_name : std::string{};
    loadHandlerCode(target.cast_lib_number, target.script_id, target.handler_name);
    if (target.kind == DeclarationKind::Method && !target.handler_name.empty()) {
        movie_tree_panel_->selectHandler(target.cast_lib_number, target.script_id,
                                       target.handler_name);
    } else {
        movie_tree_panel_->selectScript(target.cast_lib_number, target.script_id);
    }
}

void DebuggerWindow::refreshMovieIndex(const MovieTreeSnapshot& snapshot) {
    symbol_index_ = SymbolIndex(snapshot);
    code_view_panel_->setSymbolIndex(symbol_index_);
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
            watch_panel_->addWatchRow(last.id, last.expression);
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
    const bool has_movie = context_->hasMovie();
    const bool has_player = context_->player() != nullptr;
    const bool playing = context_->isPlaying();
    const bool has_pending_replay = pending_replay_.has_value();

    play_action_->setEnabled(has_movie && !playing && !is_paused_);
    record_action_->setEnabled(
        has_movie && !playing && !is_paused_ && !replaying_ && !has_pending_replay);
    pause_action_->setEnabled(has_movie && playing && !is_paused_);
    stop_action_->setEnabled(has_movie && (playing || is_paused_));
    continue_action_->setEnabled(has_movie && is_paused_);
    step_into_action_->setEnabled(has_movie && is_paused_);
    step_over_action_->setEnabled(has_movie && is_paused_);
    step_out_action_->setEnabled(has_movie && is_paused_);
    toggle_breakpoint_action_->setEnabled(has_player);
    clear_breakpoints_action_->setEnabled(has_player);
}

void DebuggerWindow::loadHandlerCode(int cast_lib_number, int script_id,
                                     const std::string& handler_name) {
    auto* player = context_->player();
    if (player == nullptr) return;

    const auto script_display_name = [](const libreshockwave::chunks::ScriptChunk& script) {
        const auto type_name = libreshockwave::format::getScriptTypeName(script.resolvedScriptType());
        return type_name + " #" + std::to_string(script.id().value());
    };

    std::shared_ptr<libreshockwave::chunks::ScriptChunk> found_script;
    const libreshockwave::chunks::ScriptChunk::Handler* found_handler = nullptr;
    const libreshockwave::chunks::ScriptNamesChunk* found_names = nullptr;

    for (const auto& [number, cast_lib] : player->castLibManager().castLibs()) {
        if (cast_lib == nullptr) continue;
        if (cast_lib_number > 0 && number != cast_lib_number) continue;
        for (const auto& script : cast_lib->allScripts()) {
            if (script == nullptr) continue;
            if (script->id().value() != script_id) continue;
            found_script = script;
            found_names = cast_lib->scriptNames().get();
            if (!handler_name.empty()) {
                found_handler = script->findHandlerPtr(handler_name, found_names);
            }
            break;
        }
        if (found_script != nullptr) break;
    }

    if (found_script == nullptr) {
        code_view_panel_->clear();
        return;
    }

    const auto found_asset = libreshockwave::moviesource::DirectorRecordConverter::toScriptAsset(
        *found_script, found_names,
        static_cast<libreshockwave::moviesource::ScriptType>(found_script->resolvedScriptType()), "");

    std::vector<InstructionData> instructions;
    std::vector<DecompiledLineData> decompiled_lines;
    std::vector<std::string> handler_names;
    std::vector<std::string> arg_names;
    std::vector<std::string> local_names;

    for (const auto& handler : found_script->handlers()) {
        handler_names.push_back(found_script->resolveName(handler.name_id, found_names));
    }

    if (found_handler != nullptr) {
        for (const auto name_id : found_handler->arg_name_ids) {
            arg_names.push_back(found_script->resolveName(name_id, found_names));
        }
        for (const auto name_id : found_handler->local_name_ids) {
            local_names.push_back(found_script->resolveName(name_id, found_names));
        }

        for (const auto& instr : found_handler->instructions) {
            InstructionData id;
            id.offset = instr.offset;
            id.index = found_handler->getInstructionIndex(instr.offset);
            id.opcode = std::string(libreshockwave::lingo::mnemonic(instr.opcode));
            id.argument = instr.argument;
            const auto& asset_handler = found_asset.handlers.front();
            id.annotation = libreshockwave::lingo::vm::trace::InstructionAnnotator::annotate(
                found_asset, &asset_handler,
                libreshockwave::moviesource::ScriptAsset::Instruction{instr.offset, instr.opcode, instr.raw_opcode, instr.argument},
                true);
            instructions.push_back(std::move(id));
        }

        libreshockwave::lingo::decompiler::LingoDecompiler decompiler;
        auto decompiled = decompiler.decompileHandlerWithMapping(
            *found_handler, *found_script, found_names);
        for (const auto& line : decompiled.lines) {
            DecompiledLineData dld;
            dld.text = line.text;
            dld.bytecode_offset = line.bytecode_offset;
            decompiled_lines.push_back(std::move(dld));
        }
    }

    code_view_panel_->setHandlerCode(
        cast_lib_number, script_id, script_display_name(*found_script), handler_name,
        instructions, decompiled_lines, handler_names, arg_names, local_names);
    refreshBreakpoints();
}

void DebuggerWindow::refreshBreakpoints() {
    auto* dc = context_->debugController();
    if (dc == nullptr) return;

    std::set<int> offsets;
    for (const auto& bp : dc->breakpointManager().getAllBreakpoints()) {
        if (bp.script_id == current_script_id_ &&
            bp.handler_name == current_handler_name_) {
            offsets.insert(bp.offset);
        }
    }
    code_view_panel_->setBreakpointOffsets(offsets);
}

} // namespace libreshockwave::debugger
