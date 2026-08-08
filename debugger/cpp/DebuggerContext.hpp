#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include "model/DebuggerModel.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace libreshockwave {
class DirectorFile;
namespace player {
class Player;
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
                           const std::string& basePath = "");

    /// Accessors.
    [[nodiscard]] libreshockwave::player::Player* player() const;
    [[nodiscard]] libreshockwave::player::debug::DebugController* debugController() const;
    [[nodiscard]] DebugStateBridge* stateBridge() const;
    [[nodiscard]] const std::string& moviePath() const;
    [[nodiscard]] bool hasMovie() const;
    [[nodiscard]] bool isPlaying() const;

    /// Thread-safe input queue for keyboard/mouse events from StageWidget.
    struct InputEvent;
    void enqueueInput(InputEvent event);

    /// A pending HTTP fetch copied out of QueuedNetProvider for the main
    /// thread's QNetworkAccessManager pump (worker → main).
    struct NetFetchRequest {
        int taskId{0};
        std::string url;
        std::string method;
        std::string postData;
        std::vector<std::string> fallbacks;
    };
    /// A completed (or failed) HTTP fetch fed back into QueuedNetProvider
    /// (main → worker). errorStatus == 0 means success.
    struct NetFetchResult {
        int taskId{0};
        std::vector<std::uint8_t> data;
        int errorStatus{0};
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

    bool toggleBreakpoint(int scriptId, const std::string& handlerName, int offset);
    void clearAllBreakpoints();
    void addWatch(const std::string& expression);
    void removeWatch(const std::string& id);

    /// Main-thread pump: issues HTTP requests for any queued fetch requests.
    void pumpNetRequests();

signals:
    void movieLoaded();
    void errorOccurred(const QString& message);
    /// Emitted from the worker thread with the newly rendered frame.
    /// The bitmap is captured on the worker (the only thread touching the
    /// player), so the UI never races with the tick loop.
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

private:
    void shutdownWorker();
    void runLoop();

    void issueFetch(NetFetchRequest request);
    void startFetchAttempt(NetFetchRequest request, QList<QUrl> urls);
    void deliverFetchResult(int taskId, std::vector<std::uint8_t> data);
    void deliverFetchError(int taskId, int status);

    /// Mirror of WasmBridge::isAlreadyLoadedCastRequest: a cast-like request
    /// whose file name matches an already loaded/fetched castLib is satisfied
    /// locally and must not be fetched again.
    [[nodiscard]] bool isAlreadyLoadedCastRequest(std::string_view url) const;

    std::unique_ptr<libreshockwave::player::Player> player_;
    std::shared_ptr<libreshockwave::player::debug::DebugController> debugController_;
    DebugStateBridge* stateBridge_{nullptr};

    /// QueuedNetProvider is owned by the worker (VM) thread — all access to it
    /// happens in runLoop().
    std::unique_ptr<libreshockwave::player::net::QueuedNetProvider> queuedNet_;
    std::optional<MovieTreeSnapshot> lastCastSnapshot_;
    QNetworkAccessManager* netAccess_{nullptr};
    QTimer* netPumpTimer_{nullptr};
    std::unordered_set<QNetworkReply*> activeReplies_;

    // Network queues (thread-safe).  Worker → main: pending fetch requests;
    // main → worker: completed fetch results.
    mutable std::mutex netInMutex_;
    std::vector<NetFetchResult> netInQueue_;
    mutable std::mutex netOutMutex_;
    std::vector<NetFetchRequest> netOutQueue_;

    std::thread workerThread_;
    std::atomic<bool> quitWorker_{false};
    std::atomic<bool> playing_{false};

    // Input queue (thread-safe)
    mutable std::mutex inputMutex_;
    std::vector<InputEvent> inputQueue_;

    std::string moviePath_;
};

/// A raw input event enqueued from the StageWidget (main thread) and
/// consumed by the worker thread.
struct DebuggerContext::InputEvent {
    enum Type { MouseMove, MouseDown, MouseUp, KeyDown, KeyUp };
    Type type{KeyDown};
    int stageX{0};
    int stageY{0};
    int directorKeyCode{0};
    std::string keyText;
    bool shift{false};
    bool ctrl{false};
    bool alt{false};
    bool rightButton{false};
};

} // namespace libreshockwave::debugger
