#pragma once

#include <QMainWindow>
#include <QMap>
#include <QString>

#include <cstdint>
#include <string>
#include <vector>

#include "model/DebuggerModel.hpp"

class QAction;
class QDockWidget;
class QLabel;
class QMenu;

namespace libreshockwave::debugger {

class CodeViewPanel;
class CallStackPanel;
class DebuggerContext;
class MovieTreePanel;
class StageWidget;
class VariablesPanel;
class WatchPanel;

/// Main debugger window with dock panels, toolbar, and menu bar.
/// Persists window layout, last movie, and external params via QSettings.
class DebuggerWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit DebuggerWindow(QWidget* parent = nullptr);
    ~DebuggerWindow() override;

    /// Open a movie with the given external params. Returns true on success.
    bool openMovie(const QString& path,
                   const QMap<QString, QString>& params = {});

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onOpenMovie();
    void onOpenUrl();
    void onOpenRecent();
    void onEditParameters();
    void onLoadDefaultPreset();
    void onClearParameters();
    void onPlay();
    void onPause();
    void onStop();
    void onStepInto();
    void onStepOver();
    void onStepOut();
    void onContinue();
    void onToggleBreakpoint();
    void onBreakpointToggled(int scriptId, const std::string& handlerName, int offset);
    void onClearBreakpoints();
    void onMovieNavigationRequested(const QString& url);

    // Debug state callbacks
    void onPaused(const SnapshotData& data);
    void onResumed();
    void onBreakpointsChanged();
    void onFrameRendered(const QImage& image);
    void onErrorOccurred(const QString& message);

    // Movie tree
    void onScriptSelected(int castLibNumber, int scriptId);
    void onHandlerSelected(int scriptId, const std::string& handlerName);

    // Code view
    void onCodeHandlerChanged(const std::string& handlerName);

    // Watches
    void onWatchAdded(const std::string& expression);
    void onWatchRemoved(const std::string& id);

private:
    void setupMenuBar();
    void setupToolBar();
    void setupDockWidgets();
    void setupCentralWidget();
    void setupShortcuts();
    void connectSignals();
    void updateToolbarState();
    void loadHandlerCode(int scriptId, const std::string& handlerName);
    void refreshBreakpoints();

    // Persistence
    void saveSettings();
    void restoreSettings();
    void addRecentMovie(const QString& path);
    void updateRecentMoviesMenu();
    void updateParamsMenu();

    // Movie loading helpers (files and URLs share one finish path)
    /// True if the given value is an http(s) URL (as opposed to a file path).
    static bool isUrl(const QString& value);
    /// Fetch a movie from a URL asynchronously and hand the bytes to
    /// finishLoadedMovie(). Used by Open URL, URL recents, and auto-load.
    void loadMovieFromUrl(const QString& url);
    /// Common tail for every successful load: install the bytes, apply the
    /// external params, update the UI, record the recent entry, and start
    /// playback (movies run immediately, like the WASM debugger).
    void finishLoadedMovie(const QString& label,
                           std::vector<std::uint8_t> data,
                           const std::string& basePath);

    DebuggerContext* context_;

    // Panels
    StageWidget* stageWidget_;
    MovieTreePanel* movieTreePanel_;
    CodeViewPanel* codeViewPanel_;
    VariablesPanel* variablesPanel_;
    CallStackPanel* callStackPanel_;
    WatchPanel* watchPanel_;

    // Docks
    QDockWidget* leftDock_;
    QDockWidget* rightDock_;
    QDockWidget* bottomDock_;

    // Toolbar
    QAction* playAction_;
    QAction* pauseAction_;
    QAction* stopAction_;
    QAction* stepIntoAction_;
    QAction* stepOverAction_;
    QAction* stepOutAction_;
    QAction* continueAction_;
    QAction* toggleBreakpointAction_;
    QAction* clearBreakpointsAction_;
    QLabel* statusLabel_;

    // Menus
    QMenu* recentMoviesMenu_;
    QMenu* paramsMenu_;

    // State
    bool isPaused_{false};
    int currentScriptId_{0};
    std::string currentHandlerName_;
    QStringList recentMovies_;
    QMap<QString, QString> externalParams_;
};

} // namespace libreshockwave::debugger
