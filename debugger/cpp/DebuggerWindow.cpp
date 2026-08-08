#include "DebuggerWindow.hpp"

#include <QApplication>
#include <QCryptographicHash>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

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
#include "ui/CallStackPanel.hpp"
#include "ui/CodeViewPanel.hpp"
#include "ui/MovieTreePanel.hpp"
#include "ui/StageWidget.hpp"
#include "ui/VariablesPanel.hpp"
#include "ui/WatchPanel.hpp"

namespace libreshockwave::debugger {

// -----------------------------------------------------------------------
// Keys for QSettings persistence
// -----------------------------------------------------------------------

static const char* kSettingGeometry       = "debugger/geometry";
static const char* kSettingState          = "debugger/state";
static const char* kSettingLastMovie      = "debugger/lastMovie";
static const char* kSettingRecentMovies   = "debugger/recentMovies";
static const char* kSettingLastDir        = "debugger/lastDir";
static const char* kSettingLastUrl        = "debugger/lastUrl";
static const char* kSettingExternalParams = "debugger/externalParams";
static const char* kSettingBreakpoints    = "debugger/breakpoints";
static const int  kMaxRecentMovies        = 8;

// Breakpoint entries live under "debugger/breakpoints/<hash>" with the movie
// path/URL hashed into the sub-key: QSettings keys may not contain '/', and
// movie paths and URLs usually do.  Each entry is "scriptId\thandlerName\toffset\tenabled".
static QString breakpointSettingKey(const QString& movieIdentity) {
    const QByteArray hash =
        QCryptographicHash::hash(movieIdentity.toUtf8(), QCryptographicHash::Md5);
    return QString::fromLatin1(kSettingBreakpoints) + QLatin1Char('/') +
           QString::fromLatin1(hash.toHex());
}

// -----------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------

DebuggerWindow::DebuggerWindow(QWidget* parent)
    : QMainWindow(parent) {

    setWindowTitle(QStringLiteral("LibreShockwave Debugger"));
    setMinimumSize(1024, 640);
    resize(1280, 800);

    context_ = new DebuggerContext(this);

    setupCentralWidget();
    setupDockWidgets();
    setupMenuBar();
    setupToolBar();
    setupShortcuts();
    connectSignals();

    restoreSettings();
    updateToolbarState();

    // Resume the last session: if a movie (file or URL) was open last time,
    // load it and start playing, so the debugger begins "running" like the
    // WASM version instead of showing "No movie loaded".
    QSettings settings;
    const auto lastMovie = settings.value(QString::fromLatin1(kSettingLastMovie)).toString();
    // A command-line movie is already opened by main().  Do not also restore
    // the previous URL here: the two asynchronous sessions can finish in
    // either order and leave the debugger displaying the wrong movie/frame.
    const bool hasCommandLineMovie = QCoreApplication::arguments().size() > 1;
    if (!hasCommandLineMovie && !lastMovie.isEmpty() &&
        (isUrl(lastMovie) || QFileInfo::exists(lastMovie))) {
        statusBar()->showMessage(QStringLiteral("Loading last movie..."));
        openMovie(lastMovie, externalParams_);
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
    if (path.isEmpty()) return false;

    // URLs are fetched asynchronously; the shared finish path loads and plays.
    if (isUrl(path)) {
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

    // Apply external params
    if (!params.isEmpty()) {
        auto playerParams = ExternalParamsDialog::toPlayerParams(params);
        if (context_->player() != nullptr) {
            context_->player()->setExternalParams(std::move(playerParams));
        }
        externalParams_ = params;
    }

    setWindowTitle(QStringLiteral("LibreShockwave Debugger — %1").arg(
        QFileInfo(path).fileName()));
    movieTreePanel_->populate(context_->player());
    restoreBreakpointsForCurrentMovie();
    addRecentMovie(path);
    refreshBreakpoints();

    // Movies start running immediately, like the WASM debugger.
    context_->play();
    statusLabel_->setText(QStringLiteral(" ● Running"));
    statusLabel_->setStyleSheet(
        QStringLiteral("color:#4da86d; font-weight:bold; padding:0 8px;"));
    updateToolbarState();

    statusBar()->showMessage(
        QStringLiteral("Loaded: %1").arg(QFileInfo(path).fileName()));
    return true;
}

// -----------------------------------------------------------------------
// Persistence
// -----------------------------------------------------------------------

void DebuggerWindow::saveSettings() {
    QSettings settings;
    settings.setValue(QString::fromLatin1(kSettingGeometry), saveGeometry());
    settings.setValue(QString::fromLatin1(kSettingState), saveState());
    settings.setValue(QString::fromLatin1(kSettingRecentMovies), recentMovies_);

    // Save external params as a serialized list
    QStringList paramList;
    for (auto it = externalParams_.cbegin(); it != externalParams_.cend(); ++it) {
        paramList.append(it.key() + QStringLiteral("=") + it.value());
    }
    settings.setValue(QString::fromLatin1(kSettingExternalParams), paramList);
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

    // Recent movies
    recentMovies_ = settings.value(QString::fromLatin1(kSettingRecentMovies)).toStringList();
    recentMovies_.removeDuplicates();
    while (recentMovies_.size() > kMaxRecentMovies) {
        recentMovies_.removeLast();
    }
    updateRecentMoviesMenu();

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

void DebuggerWindow::addRecentMovie(const QString& path) {
    recentMovies_.removeAll(path);
    recentMovies_.prepend(path);
    while (recentMovies_.size() > kMaxRecentMovies) {
        recentMovies_.removeLast();
    }
    updateRecentMoviesMenu();

    // Persist last movie path
    QSettings settings;
    settings.setValue(QString::fromLatin1(kSettingLastMovie), path);
    settings.setValue(QString::fromLatin1(kSettingRecentMovies), recentMovies_);
}

void DebuggerWindow::updateRecentMoviesMenu() {
    recentMoviesMenu_->clear();
    if (recentMovies_.isEmpty()) {
        recentMoviesMenu_->addAction(QStringLiteral("(none)"))->setEnabled(false);
        return;
    }
    for (const auto& path : recentMovies_) {
        // Files are labelled by their file name; URLs by their path segment.
        QString label = QFileInfo(path).fileName();
        if (label.isEmpty() || isUrl(path)) {
            label = QUrl(path).host() + QUrl(path).path();
        }
        if (label.isEmpty()) {
            label = path;
        }
        auto* action = recentMoviesMenu_->addAction(
            label + QStringLiteral("  —  ") + path);
        action->setData(path);
        connect(action, &QAction::triggered, this, &DebuggerWindow::onOpenRecent);
    }
    recentMoviesMenu_->addSeparator();
    auto* clearAction = recentMoviesMenu_->addAction(QStringLiteral("Clear Recent"));
    connect(clearAction, &QAction::triggered, this, [this] {
        recentMovies_.clear();
        updateRecentMoviesMenu();
    });
}

// -----------------------------------------------------------------------
// Close event
// -----------------------------------------------------------------------

void DebuggerWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    context_->stop();
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

    recentMoviesMenu_ = fileMenu->addMenu(QStringLiteral("&Recent Movies"));
    updateRecentMoviesMenu();

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

void DebuggerWindow::setupDockWidgets() {
    // Left dock: Movie tree
    leftDock_ = new QDockWidget(QStringLiteral("Movies & Scripts"), this);
    leftDock_->setObjectName(QStringLiteral("movieTreeDock"));
    movieTreePanel_ = new MovieTreePanel(leftDock_);
    leftDock_->setWidget(movieTreePanel_);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock_);

    // Right dock: Variables + Call Stack + Watches
    rightDock_ = new QDockWidget(QStringLiteral("Inspector"), this);
    rightDock_->setObjectName(QStringLiteral("inspectorDock"));
    auto* rightWidget = new QWidget(rightDock_);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    callStackPanel_ = new CallStackPanel(rightWidget);
    callStackPanel_->setMaximumHeight(150);
    rightLayout->addWidget(callStackPanel_);

    variablesPanel_ = new VariablesPanel(rightWidget);
    rightLayout->addWidget(variablesPanel_, 1);

    watchPanel_ = new WatchPanel(rightWidget);
    watchPanel_->setMaximumHeight(220);
    rightLayout->addWidget(watchPanel_);

    rightDock_->setWidget(rightWidget);
    addDockWidget(Qt::RightDockWidgetArea, rightDock_);

    // Bottom dock: Code view
    bottomDock_ = new QDockWidget(QStringLiteral("Code"), this);
    bottomDock_->setObjectName(QStringLiteral("codeDock"));
    codeViewPanel_ = new CodeViewPanel(bottomDock_);
    bottomDock_->setWidget(codeViewPanel_);
    addDockWidget(Qt::BottomDockWidgetArea, bottomDock_);
}

void DebuggerWindow::setupCentralWidget() {
    stageWidget_ = new StageWidget(this);
    setCentralWidget(stageWidget_);

    stageWidget_->setInputCallback(
        [this](int type, int stageX, int stageY,
               int keyCode, const std::string& keyText,
               bool shift, bool ctrl, bool alt, bool rightButton) {
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
        });
}

void DebuggerWindow::setupShortcuts() {
    // Shortcuts handled via menu QAction shortcuts
}

void DebuggerWindow::connectSignals() {
    connect(context_, &DebuggerContext::movieLoaded, this, [this]() {
        updateToolbarState();
    });
    connect(context_, &DebuggerContext::frameRendered,
            this, &DebuggerWindow::onFrameRendered);
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
        QStringLiteral("Director Files (*.dir *.dcr *.dxr *.cct *.cst);;All Files (*)"));
    if (path.isEmpty()) return;
    settings.setValue(QString::fromLatin1(kSettingLastDir), QFileInfo(path).absolutePath());
    openMovie(path, externalParams_);
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
    loadMovieFromUrl(url);
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
                                       const std::string& basePath) {
    const auto byteCount = data.size();
    const bool ok = context_->loadMovieFromData(std::move(data), basePath);
    if (!ok) {
        statusBar()->showMessage(QStringLiteral("Failed to load movie"), 5000);
        return;
    }

    stageWidget_->prepareForMovie();

    // Apply external params
    if (!externalParams_.isEmpty() && context_->player() != nullptr) {
        context_->player()->setExternalParams(
            ExternalParamsDialog::toPlayerParams(externalParams_));
    }

    setWindowTitle(QStringLiteral("LibreShockwave Debugger — %1").arg(label));
    movieTreePanel_->populate(context_->player());
    restoreBreakpointsForCurrentMovie();
    addRecentMovie(label);
    refreshBreakpoints();

    // Movies start running immediately, like the WASM debugger.
    context_->play();
    statusLabel_->setText(QStringLiteral(" ● Running"));
    statusLabel_->setStyleSheet(
        QStringLiteral("color:#4da86d; font-weight:bold; padding:0 8px;"));
    updateToolbarState();

    statusBar()->showMessage(
        QStringLiteral("Loaded: %1 (%2 bytes)").arg(label).arg(byteCount), 5000);
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

    // If a movie is already loaded, re-apply params
    if (context_->hasMovie() && context_->player() != nullptr) {
        context_->player()->setExternalParams(
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

    // Apply to loaded movie if any
    if (context_->hasMovie() && context_->player() != nullptr) {
        context_->player()->setExternalParams(
            ExternalParamsDialog::toPlayerParams(externalParams_));
    }

    statusBar()->showMessage(
        QStringLiteral("Default preset loaded (%1 parameters)")
            .arg(externalParams_.size()), 4000);
}

void DebuggerWindow::onClearParameters() {
    externalParams_.clear();
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
    context_->play();
    statusLabel_->setText(QStringLiteral(" ● Running"));
    statusLabel_->setStyleSheet(
        QStringLiteral("color:#4da86d; font-weight:bold; padding:0 8px;"));
    statusBar()->showMessage(QStringLiteral("Running..."));
    updateToolbarState();
}

void DebuggerWindow::onPause() {
    context_->pausePlayback();
}

void DebuggerWindow::onStop() {
    context_->stop();
    codeViewPanel_->clear();
    variablesPanel_->clearAll();
    callStackPanel_->clearAll();
    watchPanel_->clearAll();
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

void DebuggerWindow::onFrameRendered(const QImage& image) {
    // The frame was captured on the worker (VM) thread; the image is a
    // detached copy so painting here never races with the tick loop.
    stageWidget_->setFrameImage(image);
}

void DebuggerWindow::onErrorOccurred(const QString& message) {
    statusBar()->showMessage(message, 5000);
    QMessageBox::warning(this, QStringLiteral("Error"), message);
}

void DebuggerWindow::onMovieNavigationRequested(const QString& url) {
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
        finishLoadedMovie(url, std::move(bytes), url.toStdString());
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
    const bool playing = context_->isPlaying();

    playAction_->setEnabled(hasMovie && !playing && !isPaused_);
    pauseAction_->setEnabled(hasMovie && playing && !isPaused_);
    stopAction_->setEnabled(hasMovie && (playing || isPaused_));
    continueAction_->setEnabled(hasMovie && isPaused_);
    stepIntoAction_->setEnabled(hasMovie && isPaused_);
    stepOverAction_->setEnabled(hasMovie && isPaused_);
    stepOutAction_->setEnabled(hasMovie && isPaused_);
    toggleBreakpointAction_->setEnabled(hasMovie);
    clearBreakpointsAction_->setEnabled(hasMovie);
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
