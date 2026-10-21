#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include "model/debugger_model.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace libreshockwave {
namespace moviesource {
class MovieSource;
}
namespace player {
class Player;
namespace render::pipeline {
struct FrameSnapshot;
} // namespace render::pipeline
namespace debug {
class DebugController;
} // namespace debug
namespace net {
class QueuedNetProvider;
} // namespace net
} // namespace player
} // namespace libreshockwave

namespace libreshockwave::debugger {

class DebugStateBridge;

/// Manages a Player + DebugController running on a dedicated std::thread.
/// The worker thread runs a tick loop; step/continue commands are called
/// directly on the thread-safe DebugController from the main thread.
///
/// Network I/O bridges (mirroring the WASM worker):
///  - QueuedNetProvider lives on the worker (VM) thread; pending fetch
///    requests are copied out to a thread-safe queue drained by a main-thread
///    QTimer + QNetworkAccessManager pump. Completed results are fed back
///    through onFetchComplete/onFetchError by the worker each loop iteration.
///  - gotoNetPage/gotoNetMovie become movie navigations; the worker completes
///    them and emits movieNavigationRequested() so the window can load the
///    new movie bytes as a fresh session.
class DebuggerContext : public QObject {
    Q_OBJECT

public:
    explicit DebuggerContext(QObject* parent = nullptr);
    ~DebuggerContext() override;

    /// Load a Director movie from a file path. Returns true on success.
    bool loadMovie(const std::string& path);

    /// Load a Director movie from raw data bytes.  The optional path is used
    /// as the movie's base path for resolving relative assets.
    bool loadMovieFromData(std::vector<std::uint8_t> data,
                           const std::string& base_path = "");

    /// Accessors.
    [[nodiscard]] libreshockwave::player::Player* player() const;
    [[nodiscard]] libreshockwave::player::debug::DebugController* debugController() const;
    [[nodiscard]] DebugStateBridge* stateBridge() const;
    [[nodiscard]] const std::string& moviePath() const;
    [[nodiscard]] bool hasMovie() const;
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool networkReady() const;

    /// Store external parameters for the current movie and for future fresh
    /// player sessions after Stop.
    void setExternalParams(std::vector<std::pair<std::string, std::string>> params);

    /// Enable or disable authored debug output (put/alert/debug builtins).
    /// Applies to the current session and to future sessions.
    void setDebugEnabled(bool enabled);

    /// True while authored debug output is enabled.
    [[nodiscard]] bool debugEnabled() const;

    /// Thread-safe input queue for keyboard/mouse events from StageWidget.
    struct InputEvent;
    void enqueueInput(InputEvent event);

    /// A pending HTTP fetch copied out of QueuedNetProvider for the main
    /// thread's QNetworkAccessManager pump (worker → main).
    struct NetFetchRequest {
        int task_id{0};
        std::string url;
        std::string method;
        std::string post_data;
        std::vector<std::string> fallbacks;
    };
    /// A completed (or failed) HTTP fetch fed back into QueuedNetProvider
    /// (main → worker). errorStatus == 0 means success.
    struct NetFetchResult {
        int task_id{0};
        std::vector<std::uint8_t> data;
        int error_status{0};
    };

public slots:
    void play();
    void pausePlayback();
    void stop();

    /// Step/continue — called directly on DebugController (thread-safe).
    void stepInto();
    void stepOver();
    void stepOut();
    void continueExecution();

    bool toggleBreakpoint(int script_id, const std::string& handler_name, int offset);
    void clearAllBreakpoints();
    void addWatch(const std::string& expression);
    void removeWatch(const std::string& id);

    /// Main-thread pump: issues HTTP requests for any queued fetch requests.
    void pumpNetRequests();

signals:
    void movieLoaded();
    void errorOccurred(const QString& message);
    /// Emitted on the QObject/UI thread with the newest rendered frame.
    /// Snapshot capture, compositing, and pixel conversion are all detached
    /// from the VM loop; the UI receives at most one latest frame at a time.
    void frameRendered(const QImage& image);
    /// Emitted after the first play() preparation completes. This gives the
    /// debugger a stable execution-time origin for recording and replay.
    void playbackStarted();
    /// Emitted from the worker thread when the movie navigates to a new URL
    /// (gotoNetPage/gotoNetMovie).  The window must fetch the bytes and load
    /// them as a new session.
    void movieNavigationRequested(const QString& url);

    /// Emitted from the worker thread when an external cast (CCT/CST)
    /// finishes loading at runtime.  Carries a movie-tree snapshot captured
    /// on the VM thread; the window rebuilds the script list from it.
    void castLoaded(const MovieTreeSnapshot& snapshot);

    /// Emitted from the worker thread for each debug print line: the
    /// tag is "PUT", "ALERT", "DEBUG", or "TRACE".  Delivered to the GUI
    /// thread via queued connection.
    void vmOutput(const QString& tag, const QString& text);

private:
    void shutdownWorker();
    void runLoop();
    void runRenderLoop();
    void submitFrameSnapshot(libreshockwave::player::render::pipeline::FrameSnapshot snapshot);
    void dispatchLatestFrame();
    void wakeWorker();

    void issueFetch(NetFetchRequest request);
    void startFetchAttempt(NetFetchRequest request, QList<QUrl> urls);
    void deliverFetchResult(int task_id, std::vector<std::uint8_t> data);
    void deliverFetchError(int task_id, int status);

    /// Recreate the player/provider from the retained movie bytes after a
    /// full Stop teardown.
    [[nodiscard]] bool restoreMovieSession();

    /// Mirror of WasmBridge::isAlreadyLoadedCastRequest: a cast-like request
    /// whose file name matches an already loaded/fetched castLib is satisfied
    /// locally and must not be fetched again.
    [[nodiscard]] bool isAlreadyLoadedCastRequest(std::string_view url) const;

    std::unique_ptr<libreshockwave::player::Player> player_;
    std::shared_ptr<libreshockwave::player::debug::DebugController> debug_controller_;
    DebugStateBridge* state_bridge_{nullptr};

    /// QueuedNetProvider is owned by the worker (VM) thread — all access to it
    /// happens in runLoop().
    std::unique_ptr<libreshockwave::player::net::QueuedNetProvider> queued_net_;
    std::optional<MovieTreeSnapshot> last_cast_snapshot_;
    QNetworkAccessManager* net_access_{nullptr};
    QTimer* net_pump_timer_{nullptr};
    QTimer* frame_pump_timer_{nullptr};
    std::unordered_set<QNetworkReply*> active_replies_;

    // Network queues (thread-safe).  Worker → main: pending fetch requests;
    // main → worker: completed fetch results.
    mutable std::mutex net_in_mutex_;
    std::vector<NetFetchResult> net_in_queue_;
    mutable std::mutex net_out_mutex_;
    std::vector<NetFetchRequest> net_out_queue_;

    std::thread worker_thread_;
    std::thread render_thread_;
    std::atomic<bool> quit_worker_{false};
    std::atomic<bool> playing_{false};
    std::atomic<bool> network_ready_{false};

    // Rendering is deliberately fed immutable snapshots.  The VM thread
    // never waits for software compositing or for the GUI to paint a frame;
    // the renderer keeps only the newest snapshot so a slow window cannot
    // build a queue of stale frames.
    mutable std::mutex render_mutex_;
    std::condition_variable render_condition_;
    std::shared_ptr<const libreshockwave::player::render::pipeline::FrameSnapshot>
        pending_frame_snapshot_;
    std::uint64_t pending_frame_generation_{0};
    bool quit_render_{false};
    std::atomic<std::uint64_t> render_generation_{0};

    mutable std::mutex frame_mutex_;
    std::shared_ptr<QImage> latest_frame_;

    // Wake the VM loop as soon as input or an asynchronous network result
    // arrives, while retaining the movie's own frame deadline.
    mutable std::mutex worker_wait_mutex_;
    std::condition_variable worker_condition_;
    std::atomic<std::uint64_t> worker_wake_generation_{0};

    // Input queue (thread-safe)
    mutable std::mutex input_mutex_;
    std::vector<InputEvent> input_queue_;

    // The source movie is retained without retaining a live Player session.
    // Stop tears the session down; Play reconstructs it from these bytes.
    std::vector<std::uint8_t> movie_data_;
    std::string movie_path_;
    std::vector<std::pair<std::string, std::string>> external_params_;
    std::atomic<bool> debug_enabled_{true};
};

/// A raw input event enqueued from the StageWidget (main thread) and
/// consumed by the worker thread.
struct DebuggerContext::InputEvent {
    enum Type { MouseMove, MouseDown, MouseUp, KeyDown, KeyUp };
    Type type{KeyDown};
    int stage_x{0};
    int stage_y{0};
    int director_key_code{0};
    std::string key_text;
    bool shift{false};
    bool ctrl{false};
    bool alt{false};
    bool right_button{false};
};

} // namespace libreshockwave::debugger
