#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QMainWindow>
#include <QMap>
#include <QString>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "model/debugger_model.hpp"
#include "model/symbol_index.hpp"

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
/// Persists window layout, last movie source, and replay session via QSettings.
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
    void onBreakpointToggled(int script_id, const std::string& handler_name, int offset);
    void onClearBreakpoints();
    void onResetLayout();
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
    void onScriptSelected(int cast_lib_number, int script_id);
    void onHandlerSelected(int cast_lib_number, int script_id,
                           const std::string& handler_name);

    // Code view
    void onCodeHandlerChanged(const std::string& handler_name);
    /// Jump to the declaration of a word the user right-clicked in the code
    /// view (method, property, or global in another script).
    void onGoToDeclaration(const DeclarationTarget& target);

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
    void loadHandlerCode(int cast_lib_number, int script_id,
                         const std::string& handler_name);
    void refreshBreakpoints();
    /// Rebuild the movie-wide declaration index from a snapshot and push it
    /// to the code view (right-click "Go to declaration" + method coloring).
    void refreshMovieIndex(const MovieTreeSnapshot& snapshot);

    // Persistence
    void saveSettings();
    void restoreSettings();
    void addRecentSession(const QString& path, bool make_last_session = true);
    void updateRecentSessionsMenu();
    void updateParamsMenu();

    struct RecordedInputEvent {
        qint64 time_ms{0};
        int type{0};
        int stage_x{0};
        int stage_y{0};
        int director_key_code{0};
        QString key_text;
        bool shift{false};
        bool ctrl{false};
        bool alt{false};
        bool right_button{false};
    };

    struct RecordingFile {
        QString movie;
        QMap<QString, QString> external_params;
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
    void recordStageInput(int type, int stage_x, int stage_y, int key_code,
                          const std::string& key_text, bool shift, bool ctrl,
                          bool alt, bool right_button);
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
                           const std::string& base_path,
                           bool resume_playback = false);

    DebuggerContext* context_;

    // Panels
    StageWidget* stage_widget_;
    MovieTreePanel* movie_tree_panel_;
    CodeViewPanel* code_view_panel_;
    VariablesPanel* variables_panel_;
    CallStackPanel* call_stack_panel_;
    WatchPanel* watch_panel_;
    DebugWindowPanel* debug_window_panel_;

    // Docks
    QDockWidget* left_dock_;
    QDockWidget* call_stack_dock_;
    QDockWidget* variables_dock_;
    QDockWidget* watch_dock_;
    QDockWidget* bottom_dock_;
    QDockWidget* debug_dock_;

    // Toolbar
    QAction* play_action_;
    QAction* record_action_;
    QAction* pause_action_;
    QAction* stop_action_;
    QAction* step_into_action_;
    QAction* step_over_action_;
    QAction* step_out_action_;
    QAction* continue_action_;
    QAction* toggle_breakpoint_action_;
    QAction* clear_breakpoints_action_;
    QLabel* status_label_;

    // Menus
    QMenu* recent_sessions_menu_;
    QMenu* params_menu_;

    // State
    bool is_paused_{false};
    bool settings_ready_{false};
    int current_cast_lib_number_{0};
    int current_script_id_{0};
    std::string current_handler_name_;
    /// Movie-wide declaration index (methods, properties, globals) used by
    /// the code view's right-click "Go to declaration".
    SymbolIndex symbol_index_;
    QStringList recent_sessions_;
    QString last_movie_;
    QString last_session_;
    QMap<QString, QString> external_params_;

    /// Dock layout captured at startup; View > Reset Layout restores it.
    QByteArray default_layout_state_;

    // Play & Record / replay state. Recordings contain stage input events and
    // the movie/parameter metadata needed to reopen the same session.
    bool recording_{false};
    QString recording_file_path_;
    QFile recording_file_;
    QTimer* recording_flush_timer_{nullptr};
    QElapsedTimer recording_clock_;
    qint64 last_recorded_mouse_move_ms_{-1};
    int last_recorded_mouse_x_{0};
    int last_recorded_mouse_y_{0};
    bool has_recorded_mouse_move_{false};
    QTimer* replay_timer_{nullptr};
    QElapsedTimer frame_dump_clock_;
    QElapsedTimer frame_stats_clock_;
    int frame_stats_count_{0};
    bool replaying_{false};
    QElapsedTimer replay_clock_;
    qint64 replay_clock_base_ms_{0};
    std::vector<RecordedInputEvent> replay_events_;
    std::size_t replay_index_{0};
    std::optional<RecordingFile> pending_replay_;
    bool replay_waiting_for_navigation_{false};
    bool play_requested_{false};
};

} // namespace libreshockwave::debugger
