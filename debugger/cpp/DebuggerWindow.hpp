#pragma once

#include <QElapsedTimer>
#include <QFile>
#include <QMainWindow>
#include <QMap>
#include <QString>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "model/DebuggerModel.hpp"

class QAction;
class QDockWidget;
class QLabel;
class QMenu;
class QTimer;

namespace libreshockwave::debugger {

class CodeViewPanel;
class CallStackPanel;
class DebugWindowPanel;
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

    /// Start playback immediately, or defer it until an asynchronous movie
    /// load finishes. Used by the command-line --play option.
    void startPlayback();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onOpenMovie();
    void onOpenUrl();
    void onOpenRecording();
    void onOpenRecent();
    void onEditParameters();
    void onLoadDefaultPreset();
    void onClearParameters();
    void onPlay();
    void onPlayAndRecord();
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
    void onPlaybackStarted();
    void onRecordingFlushTimer();
    void onReplayTimer();
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

    struct RecordedInputEvent {
        qint64 timeMs{0};
        int type{0};
        int stageX{0};
        int stageY{0};
        int directorKeyCode{0};
        QString keyText;
        bool shift{false};
        bool ctrl{false};
        bool alt{false};
        bool rightButton{false};
    };

    struct RecordingFile {
        QString movie;
        QMap<QString, QString> externalParams;
        std::vector<RecordedInputEvent> events;
    };

    bool openMovieInternal(const QString& path,
                           const QMap<QString, QString>& params);
    bool openRecording(const QString& path);
    bool readRecording(const QString& path, RecordingFile& recording,
                       QString& error) const;
    bool openRecordingFile(QString& error);
    bool writeRecordingEvent(const RecordedInputEvent& event, QString& error);
    bool flushRecordingFile(QString& error);
    void closeRecordingFile();
    void startPendingReplayIfReady();
    void pauseReplayForNavigation();
    void resumeReplayAfterNavigation();
    void recordStageInput(int type, int stageX, int stageY, int keyCode,
                          const std::string& keyText, bool shift, bool ctrl,
                          bool alt, bool rightButton);
    void finishRecording();
    void cancelReplay();

    // Breakpoint persistence.  Breakpoints are stored per movie identity
    // (the movie path/URL) and re-applied whenever that same movie loads —
    // app restart, movie reload, or opening it again — so a session's
    // breakpoints survive across player/controller recreation.
    /// Identity of the currently loaded movie (path or URL).
    QString currentMovieKey() const;
    /// Re-add this movie's stored breakpoints to the fresh controller.
    void restoreBreakpointsForCurrentMovie();
    /// Write the controller's current breakpoints for this movie to QSettings.
    void persistBreakpoints();

    // Movie loading helpers (files and URLs share one finish path)
    /// True if the given value is an http(s) URL (as opposed to a file path).
    static bool isUrl(const QString& value);
    /// Fetch a movie from a URL asynchronously and hand the bytes to
    /// finishLoadedMovie(). Used by Open URL, URL recents, and auto-load.
    void loadMovieFromUrl(const QString& url);
    /// Common tail for every successful load: install the bytes, apply the
    /// external params, update the UI, record the recent entry, and start
    /// in the ready state. Playback starts only from an explicit action.
    void finishLoadedMovie(const QString& label,
                           std::vector<std::uint8_t> data,
                           const std::string& basePath,
                           bool resumePlayback = false);

    DebuggerContext* context_;

    // Panels
    StageWidget* stageWidget_;
    MovieTreePanel* movieTreePanel_;
    CodeViewPanel* codeViewPanel_;
    VariablesPanel* variablesPanel_;
    CallStackPanel* callStackPanel_;
    WatchPanel* watchPanel_;
    DebugWindowPanel* debugWindowPanel_;

    // Docks
    QDockWidget* leftDock_;
    QDockWidget* rightDock_;
    QDockWidget* bottomDock_;
    QDockWidget* debugDock_;

    // Toolbar
    QAction* playAction_;
    QAction* recordAction_;
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

    // Play & Record / replay state. Recordings contain stage input events and
    // the movie/parameter metadata needed to reopen the same session.
    bool recording_{false};
    QString recordingFilePath_;
    QFile recordingFile_;
    QTimer* recordingFlushTimer_{nullptr};
    QElapsedTimer recordingClock_;
    qint64 lastRecordedMouseMoveMs_{-1};
    int lastRecordedMouseX_{0};
    int lastRecordedMouseY_{0};
    bool hasRecordedMouseMove_{false};
    QTimer* replayTimer_{nullptr};
    QElapsedTimer frameDumpClock_;
    QElapsedTimer frameStatsClock_;
    int frameStatsCount_{0};
    bool replaying_{false};
    QElapsedTimer replayClock_;
    qint64 replayClockBaseMs_{0};
    std::vector<RecordedInputEvent> replayEvents_;
    std::size_t replayIndex_{0};
    std::optional<RecordingFile> pendingReplay_;
    bool replayWaitingForNavigation_{false};
    bool playRequested_{false};
};

} // namespace libreshockwave::debugger
