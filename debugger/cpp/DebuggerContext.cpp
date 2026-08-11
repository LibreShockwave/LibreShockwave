#include "DebuggerContext.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include "DebugStateBridge.hpp"
#include "ui/MovieTreePanel.hpp"
#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/player/Player.hpp"
#include "libreshockwave/player/cast/CastLib.hpp"
#include "libreshockwave/player/cast/CastLibManager.hpp"
#include "libreshockwave/player/debug/DebugController.hpp"
#include "libreshockwave/player/InputHandler.hpp"
#include "libreshockwave/player/net/QueuedNetProvider.hpp"
#include "libreshockwave/util/FileUtil.hpp"

namespace libreshockwave::debugger {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::vector<std::uint8_t> readFileBytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open file");
    }
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(end));
    if (!data.empty()) {
        input.read(reinterpret_cast<char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
    }
    return data;
}

static std::chrono::steady_clock::duration frameDuration(int tempo) {
    const double framesPerSecond = tempo > 0 ? static_cast<double>(tempo) : 15.0;
    const auto duration = std::chrono::duration<double>(1.0 / framesPerSecond);
    return std::max(
        std::chrono::steady_clock::duration{1},
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration));
}

static std::string toLowerAscii(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char ch : value) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

static bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    return toLowerAscii(lhs) == toLowerAscii(rhs);
}

static std::optional<bitmap::Bitmap> decodeEmbeddedJpeg(const std::vector<std::uint8_t>& data) {
    const auto* raw = reinterpret_cast<const uchar*>(data.data());
    QImage image = QImage::fromData(raw, static_cast<int>(data.size()), "JPEG");
    if (image.isNull()) {
        return std::nullopt;
    }

    image = image.convertToFormat(QImage::Format_ARGB32);
    bitmap::Bitmap bitmap(image.width(), image.height(), 32);
    for (int y = 0; y < image.height(); ++y) {
        const auto* scanline = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            bitmap.setPixel(x, y, static_cast<std::uint32_t>(scanline[x]));
        }
    }
    bitmap.setNativeAlpha(true);
    return bitmap;
}

// ---------------------------------------------------------------------------
// DebuggerContext
// ---------------------------------------------------------------------------

DebuggerContext::DebuggerContext(QObject* parent)
    : QObject(parent) {
    DirectorFile::setJpegDecoder(decodeEmbeddedJpeg);
    stateBridge_ = new DebugStateBridge(this);
    netAccess_ = new QNetworkAccessManager(this);
    netPumpTimer_ = new QTimer(this);
    // QueuedNetProvider is drained on demand. A fixed 15 ms timer added
    // avoidable network latency on a 60/75/120 Hz movie and was especially
    // visible while the old VM loop was rendering.
    netPumpTimer_->setInterval(0);
    connect(netPumpTimer_, &QTimer::timeout, this, &DebuggerContext::pumpNetRequests);

    framePumpTimer_ = new QTimer(this);
    // The frame mailbox keeps only the newest rendered image, so polling it
    // every 5 ms just wakes the GUI repeatedly when no new frame is ready.
    // A normal display cadence is frequent enough and avoids competing with
    // input and recording work on the GUI thread.
    framePumpTimer_->setInterval(16);
    framePumpTimer_->setTimerType(Qt::CoarseTimer);
    connect(framePumpTimer_, &QTimer::timeout, this, &DebuggerContext::dispatchLatestFrame);
    framePumpTimer_->start();

    // This thread never touches Player.  It only renders immutable snapshots
    // submitted by the VM loop, so compositing cannot delay networking,
    // input, or the movie's frame deadline.
    renderThread_ = std::thread(&DebuggerContext::runRenderLoop, this);
}

DebuggerContext::~DebuggerContext() {
    shutdownWorker();

    {
        std::lock_guard<std::mutex> lock(renderMutex_);
        quitRender_ = true;
        pendingFrameSnapshot_.reset();
    }
    renderCondition_.notify_one();
    if (renderThread_.joinable()) {
        renderThread_.join();
    }
}

bool DebuggerContext::loadMovie(const std::string& path) {
    std::vector<std::uint8_t> data;
    try {
        data = readFileBytes(path);
    } catch (const std::exception& ex) {
        emit errorOccurred(
            QStringLiteral("Unable to open file: %1").arg(QString::fromUtf8(ex.what())));
        return false;
    }
    return loadMovieFromData(std::move(data), path);
}

bool DebuggerContext::loadMovieFromData(std::vector<std::uint8_t> data,
                                        const std::string& basePath) {
    std::shared_ptr<DirectorFile> directorFile;
    try {
        directorFile = DirectorFile::load(data);
    } catch (const std::exception& ex) {
        emit errorOccurred(
            QStringLiteral("Failed to parse movie: %1").arg(QString::fromUtf8(ex.what())));
        return false;
    }

    if (directorFile == nullptr) {
        emit errorOccurred(QStringLiteral("Failed to parse movie: unknown format"));
        return false;
    }

    directorFile->setBasePath(basePath);

    // Shut down any previous session
    shutdownWorker();

    // Discard stale fetch results/requests from the previous session
    {
        std::lock_guard<std::mutex> lock(netInMutex_);
        netInQueue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(netOutMutex_);
        netOutQueue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(renderMutex_);
        renderGeneration_.fetch_add(1, std::memory_order_acq_rel);
        pendingFrameSnapshot_.reset();
    }
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        latestFrame_.reset();
    }
    lastCastSnapshot_.reset();
    networkReady_ = false;
    movieData_ = data;
    moviePath_ = basePath;

    // QueuedNetProvider is owned by the worker thread.  The plain Player
    // constructor registers the native SocketMultiuserBridge (the equivalent
    // of the WASM WebSocket multiuser bridge); setNetProvider then swaps the
    // active fetch provider to the queued one.
    queuedNet_ = std::make_unique<libreshockwave::player::net::QueuedNetProvider>(basePath);
    player_ = std::make_unique<libreshockwave::player::Player>(directorFile);
    player_->setNetProvider(queuedNet_.get());
    player_->setExternalParams(externalParams_);
    // Authored debug output follows the Debug Window toggle: put/alert/debug
    // builtins only produce output while the user keeps debug mode enabled.
    player_->setDebugEnabled(debugEnabled_.load());

    // Route authored debug output and VM trace lines to the Debug Window
    // panel.  Both handlers run on the VM thread and hand off to the GUI
    // thread through the queued vmOutput signal.
    auto& vm = player_->vm();
    vm.builtinContext().outputHandler =
        [this](std::string_view tag, std::string_view text) {
            emit vmOutput(QString::fromUtf8(tag.data(), static_cast<int>(tag.size())),
                          QString::fromUtf8(text.data(), static_cast<int>(text.size())));
        };
    vm.setTraceOutputHandler([this](std::string_view line) {
        emit vmOutput(QStringLiteral("TRACE"),
                      QString::fromUtf8(line.data(), static_cast<int>(line.size())));
    });
    // Runtime CCT/CST loads mutate the cast libs on the VM thread.  Capture
    // a movie-tree snapshot there and hand it to the window through a queued
    // signal, so the script list stays in sync without the UI ever reading
    // live cast state concurrently with the tick loop.
    player_->setCastLoadedListener([this] {
        if (player_ == nullptr) {
            return;
        }
        const auto snapshot = MovieTreePanel::buildSnapshot(*player_);
        if (lastCastSnapshot_.has_value() && *lastCastSnapshot_ == snapshot) {
            return;
        }
        lastCastSnapshot_ = snapshot;
        emit castLoaded(snapshot);
    });
    player_->setErrorListener([this](std::string_view message, std::string_view detail) {
        QString text = QString::fromUtf8(message.data(), static_cast<int>(message.size()));
        if (!detail.empty()) {
            text += QStringLiteral(": ");
            text += QString::fromUtf8(detail.data(), static_cast<int>(detail.size()));
        }
        emit errorOccurred(text);
    });

    // Create DebugController (default = blocking mode with condition_variable)
    debugController_ = std::make_shared<libreshockwave::player::debug::DebugController>();
    // Keep the controller attached for debugger commands, but do not trace
    // every startup instruction during ordinary playback.  Habbo's startup
    // deobfuscation is particularly expensive under instruction tracing.
    debugController_->setTracingEnabled(false);
    player_->setDebugController(debugController_);

    // Register the state bridge so the UI receives pause/resume callbacks
    debugController_->addListener(stateBridge_);

    // Network I/O bridges — mirror the WASM wiring (WasmBridge.cpp):
    // gotoNetPage/gotoNetMovie become movie navigations completed by the
    // worker and surfaced to the window for a full movie reload.
    player_->movieProperties().setGotoNetPageHandler(
        [this](const std::string& url, const std::string& /*target*/) {
            (void)queuedNet_->beginMovieNavigation(url);
        });
    player_->movieProperties().setGotoNetMovieHandler(
        [this](const std::string& url) {
            return queuedNet_->beginMovieNavigation(url);
        });
    // Fetches complete with the raw bytes on the VM thread, like the WASM
    // worker's fetchCompleteCallback.
    queuedNet_->setFetchCompleteCallback(
        [this](const std::string& url, const std::vector<std::uint8_t>& fetchData) {
            if (player_ != nullptr) {
                player_->onNetFetchComplete(url, fetchData);
            }
        });
    // Cast requests that are already loaded/fetched must not be re-fetched.
    queuedNet_->setSatisfiedFetchPredicate(
        [this](std::string_view url) {
            return isAlreadyLoadedCastRequest(url);
        });

    playing_ = false;
    quitWorker_ = false;

    emit movieLoaded();
    return true;
}

libreshockwave::player::Player* DebuggerContext::player() const { return player_.get(); }
libreshockwave::player::debug::DebugController* DebuggerContext::debugController() const {
    return debugController_.get();
}
DebugStateBridge* DebuggerContext::stateBridge() const { return stateBridge_; }
const std::string& DebuggerContext::moviePath() const { return moviePath_; }
bool DebuggerContext::hasMovie() const {
    return player_ != nullptr || !movieData_.empty();
}
bool DebuggerContext::isPlaying() const { return playing_.load(); }
bool DebuggerContext::networkReady() const { return networkReady_.load(std::memory_order_acquire); }

void DebuggerContext::setExternalParams(
    std::vector<std::pair<std::string, std::string>> params) {
    externalParams_ = std::move(params);
    if (player_ != nullptr) {
        player_->setExternalParams(externalParams_);
    }
}

void DebuggerContext::setDebugEnabled(bool enabled) {
    debugEnabled_.store(enabled, std::memory_order_relaxed);
    if (player_ != nullptr) {
        player_->setDebugEnabled(enabled);
    }
}

bool DebuggerContext::debugEnabled() const {
    return debugEnabled_.load(std::memory_order_relaxed);
}

bool DebuggerContext::restoreMovieSession() {
    if (player_ != nullptr) {
        return true;
    }
    if (movieData_.empty()) {
        return false;
    }
    return loadMovieFromData(movieData_, moviePath_);
}

void DebuggerContext::enqueueInput(InputEvent event) {
    std::lock_guard<std::mutex> lock(inputMutex_);

    // Mouse moves are high-frequency and only the newest position matters
    // before the next VM iteration. Coalesce adjacent moves so a busy GUI
    // cannot build an unbounded input backlog while the movie is ticking.
    if (event.type == InputEvent::MouseMove && !inputQueue_.empty() &&
        inputQueue_.back().type == InputEvent::MouseMove) {
        inputQueue_.back() = std::move(event);
        wakeWorker();
        return;
    }

    inputQueue_.push_back(std::move(event));
    wakeWorker();
}

void DebuggerContext::play() {
    if (playing_.load()) {
        return;
    }

    if (player_ == nullptr && !restoreMovieSession()) {
        return;
    }
    if (player_ == nullptr) {
        return;
    }

    quitWorker_ = false;
    playing_ = true;
    wakeWorker();

    // Spawn the tick-loop worker.  All Player calls, including the initial
    // prepareMovie lifecycle, stay on that VM thread.

    workerThread_ = std::thread(&DebuggerContext::runLoop, this);
}

void DebuggerContext::pausePlayback() {
    if (debugController_ != nullptr) {
        debugController_->setTracingEnabled(true);
        debugController_->pause();
    }
    wakeWorker();
}

void DebuggerContext::stop() {
    // Stop is a full session reset.  The selected movie bytes remain so the
    // next Play creates a new Player, VM, cast manager, and net provider.
    shutdownWorker();
}

void DebuggerContext::stepInto() {
    if (debugController_ != nullptr) {
        debugController_->setTracingEnabled(true);
        debugController_->stepInto();
    }
    wakeWorker();
}
void DebuggerContext::stepOver() {
    if (debugController_ != nullptr) {
        debugController_->setTracingEnabled(true);
        debugController_->stepOver();
    }
    wakeWorker();
}
void DebuggerContext::stepOut() {
    if (debugController_ != nullptr) {
        debugController_->setTracingEnabled(true);
        debugController_->stepOut();
    }
    wakeWorker();
}
void DebuggerContext::continueExecution() {
    if (debugController_ != nullptr) debugController_->continueExecution();
    wakeWorker();
}

bool DebuggerContext::toggleBreakpoint(int scriptId,
                                       const std::string& handlerName,
                                       int offset) {
    if (debugController_ == nullptr) return false;
    debugController_->setTracingEnabled(true);
    return debugController_->toggleBreakpoint(scriptId, handlerName, offset);
}

void DebuggerContext::clearAllBreakpoints() {
    if (debugController_ != nullptr) debugController_->clearAllBreakpoints();
}

void DebuggerContext::addWatch(const std::string& expression) {
    if (debugController_ != nullptr) {
        debugController_->setTracingEnabled(true);
        (void)debugController_->addWatchExpression(expression);
    }
}

void DebuggerContext::removeWatch(const std::string& id) {
    if (debugController_ != nullptr) {
        (void)debugController_->removeWatchExpression(id);
    }
}

void DebuggerContext::shutdownWorker() {
    // Stop the HTTP pump and cancel any in-flight replies so stale results
    // can never be delivered to a future session's QueuedNetProvider (task
    // ids restart from 1 per provider).
    if (netPumpTimer_ != nullptr) {
        netPumpTimer_->stop();
    }
    for (auto* reply : std::exchange(activeReplies_, {})) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (netAccess_ != nullptr) {
        netAccess_->clearConnectionCache();
    }

    if (player_ != nullptr && debugController_ != nullptr) {
        debugController_->removeListener(stateBridge_);
        debugController_->clearAllBreakpoints();
        debugController_->reset();
    }
    quitWorker_ = true;
    playing_ = false;
    wakeWorker();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(netInMutex_);
        netInQueue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(netOutMutex_);
        netOutQueue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(inputMutex_);
        inputQueue_.clear();
    }

    if (player_ != nullptr) {
        player_->stop();
        player_->shutdown();
        player_.reset();
    }
    debugController_.reset();
    queuedNet_.reset();
    lastCastSnapshot_.reset();
    networkReady_ = false;
}

// ---------------------------------------------------------------------------
// Worker thread loop
// ---------------------------------------------------------------------------

void DebuggerContext::runLoop() {
    using namespace std::chrono_literals;
    using Clock = std::chrono::steady_clock;

    bool movieStarted = false;
    bool firstFrame = false;
    Clock::time_point nextTickAt{};

    const auto deliverPendingFetchResults = [this] {
        std::vector<NetFetchResult> results;
        {
            std::lock_guard<std::mutex> lock(netInMutex_);
            results.swap(netInQueue_);
        }
        for (auto& result : results) {
            if (result.errorStatus == 0) {
                queuedNet_->onFetchComplete(result.taskId, std::move(result.data));
            } else {
                queuedNet_->onFetchError(result.taskId, result.errorStatus);
            }
        }
    };

    const auto queuePendingFetchRequests = [this] {
        if (queuedNet_->pendingRequestCount() <= 0) {
            return;
        }

        const auto& pending = queuedNet_->pendingRequests();
        {
            std::lock_guard<std::mutex> lock(netOutMutex_);
            for (const auto& request : pending) {
                netOutQueue_.push_back(NetFetchRequest{
                    request.taskId,
                    request.url,
                    request.method,
                    request.postData.value_or(""),
                    request.fallbacks});
            }
        }
        queuedNet_->drainPendingRequests();

        // The QNetworkAccessManager lives on the GUI thread, but the pump is
        // scheduled only when work exists. This mirrors the WASM worker's
        // host-queue polling without a continuously firing GUI timer.
        QMetaObject::invokeMethod(this, [this] {
            if (netPumpTimer_ != nullptr) {
                netPumpTimer_->start();
            }
        }, Qt::QueuedConnection);
    };

    // The shell movie's cast 2 contains the bootstrap scripts that set up
    // the external cast/text flow. Let its fetch complete before the first
    // prepareMovie pass, but keep the wait bounded so a failed CDN request
    // cannot prevent the debugger from starting.
    (void)player_->preloadAllCasts();
    const auto bootstrapCast = player_->castLibManager().getCastLib(2);
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (!quitWorker_.load(std::memory_order_relaxed) &&
           bootstrapCast && bootstrapCast->isExternal() && !bootstrapCast->isLoaded() &&
           std::chrono::steady_clock::now() < deadline) {
        deliverPendingFetchResults();
        queuePendingFetchRequests();
        const auto observedWake = workerWakeGeneration_.load(std::memory_order_acquire);
        std::unique_lock<std::mutex> waitLock(workerWaitMutex_);
        workerCondition_.wait_for(waitLock, 50ms, [this, observedWake] {
            return quitWorker_.load(std::memory_order_relaxed) ||
                   workerWakeGeneration_.load(std::memory_order_acquire) != observedWake;
        });
    }

    const auto submitCurrentFrame = [this] {
        if (player_ == nullptr) {
            return;
        }

        try {
            // FrameSnapshot owns immutable/shared bitmap data. Only this
            // snapshot capture remains on the VM thread; full compositing
            // and pixel conversion happen in runRenderLoop().
            submitFrameSnapshot(player_->frameSnapshot());
        } catch (const std::exception&) {
            // Transient snapshot failure (e.g. movie mid-transition) — keep
            // the last good frame and let networking/ticking continue.
        }
    };

    while (!quitWorker_.load(std::memory_order_relaxed)) {
        // 1. Deliver completed network fetches (main thread → VM thread).
        //    onFetchComplete invokes fetchCompleteCallback_ which feeds the
        //    bytes to Player::onNetFetchComplete on this (VM) thread.
        deliverPendingFetchResults();

        // 2. Complete pending movie navigations and surface them to the
        //    window, which reloads the movie (mirrors the WASM worker's
        //    navigation poll).
        if (queuedNet_->pendingMovieNavigationTaskCount() > 0) {
            for (const auto& nav : queuedNet_->pendingMovieNavigationRequests()) {
                queuedNet_->onMovieNavigationComplete(nav.taskId);
                emit movieNavigationRequested(QString::fromStdString(nav.url));
            }
        }

        // 3. Copy pending fetch requests out of the provider for the
        //    main-thread HTTP pump.
        queuePendingFetchRequests();

        // 4. Drain queued input events
        {
            std::lock_guard<std::mutex> lock(inputMutex_);
            std::vector<InputEvent> queued;
            queued.swap(inputQueue_);
            for (std::size_t index = 0; index < queued.size(); ++index) {
                const auto& event = queued[index];
                auto& handler = player_->inputHandler();
                switch (event.type) {
                    case InputEvent::MouseMove:
                        handler.onMouseMove(event.stageX, event.stageY);
                        break;
                    case InputEvent::MouseDown:
                        handler.onMouseDown(event.stageX, event.stageY, event.rightButton);
                        break;
                    case InputEvent::MouseUp:
                        handler.onMouseUp(event.stageX, event.stageY, event.rightButton);
                        break;
                    case InputEvent::KeyDown:
                        handler.onKeyDown(event.directorKeyCode, event.keyText,
                                          event.shift, event.ctrl, event.alt);
                        break;
                    case InputEvent::KeyUp:
                        handler.onKeyUp(event.directorKeyCode, event.keyText,
                                        event.shift, event.ctrl, event.alt);
                        break;
                }
            }
        }
        (void)player_->inputHandler().processInputEvents();

        if (!movieStarted) {
            player_->play();
            movieStarted = true;
            networkReady_.store(player_->networkReady(), std::memory_order_release);
            emit playbackStarted();
            // play() prepares the movie's first frame. Keep that frame visible
            // for one complete movie frame before the first tick advances the
            // score to frame 2.
            firstFrame = true;
            nextTickAt = Clock::now() + frameDuration(player_->tempo());
        }

        if (firstFrame) {
            submitCurrentFrame();
            firstFrame = false;
        }

        // 5. Run one frame tick at the movie's deadline. Rendering is not part
        // of this deadline: it consumes the latest immutable snapshot on a
        // separate thread. If a breakpoint is hit, DebugController blocks
        // this VM thread on its own condition_variable until the main thread
        // calls step/continue, while GUI and network event loops remain free.
        if (player_->state() == libreshockwave::player::PlayerState::Playing &&
            Clock::now() >= nextTickAt) {
            (void)player_->tick();

            networkReady_.store(player_->networkReady(), std::memory_order_release);
            if (!quitWorker_.load(std::memory_order_relaxed)) {
                submitCurrentFrame();
            }

            const auto delay = frameDuration(player_->tempo());
            nextTickAt += delay;
            // A slow VM frame must not cause a burst of catch-up ticks. The
            // next tick is scheduled from the current time, preserving the
            // movie cadence while giving network/input work a chance to run.
            const auto now = Clock::now();
            if (nextTickAt <= now) {
                nextTickAt = now + delay;
            }
            continue;
        }

        networkReady_.store(player_->networkReady(), std::memory_order_release);

        const auto observedWake = workerWakeGeneration_.load(std::memory_order_acquire);
        std::unique_lock<std::mutex> waitLock(workerWaitMutex_);
        if (player_->state() == libreshockwave::player::PlayerState::Playing) {
            workerCondition_.wait_until(waitLock, nextTickAt, [this, observedWake] {
                return quitWorker_.load(std::memory_order_relaxed) ||
                       workerWakeGeneration_.load(std::memory_order_acquire) != observedWake;
            });
        } else {
            workerCondition_.wait(waitLock, [this, observedWake] {
                return quitWorker_.load(std::memory_order_relaxed) ||
                       workerWakeGeneration_.load(std::memory_order_acquire) != observedWake;
            });
        }
    }

    playing_.store(false);
}

void DebuggerContext::wakeWorker() {
    workerWakeGeneration_.fetch_add(1, std::memory_order_release);
    workerCondition_.notify_one();
}

void DebuggerContext::submitFrameSnapshot(
    libreshockwave::player::render::pipeline::FrameSnapshot snapshot) {
    auto sharedSnapshot = std::make_shared<const libreshockwave::player::render::pipeline::FrameSnapshot>(
        std::move(snapshot));
    {
        std::lock_guard<std::mutex> lock(renderMutex_);
        pendingFrameGeneration_ = renderGeneration_.load(std::memory_order_acquire);
        pendingFrameSnapshot_ = std::move(sharedSnapshot);
    }
    // Replacing the pending snapshot is intentional: presentation must never
    // make the VM or network client wait behind stale frames.
    renderCondition_.notify_one();
}

void DebuggerContext::runRenderLoop() {
    bool firstVisibleFrame = false;
    int leadingBlankFrames = 0;

    while (true) {
        std::shared_ptr<const libreshockwave::player::render::pipeline::FrameSnapshot> snapshot;
        std::uint64_t generation = 0;
        {
            std::unique_lock<std::mutex> lock(renderMutex_);
            renderCondition_.wait(lock, [this] {
                return quitRender_ || pendingFrameSnapshot_ != nullptr;
            });
            if (quitRender_) {
                return;
            }
            snapshot = std::move(pendingFrameSnapshot_);
            generation = pendingFrameGeneration_;
        }

        try {
            const auto frame = snapshot->renderFrame();
            const auto& pixels = frame.pixels();
            if (!firstVisibleFrame) {
                const bool hasVisiblePixels = std::any_of(
                    pixels.begin(), pixels.end(), [](std::uint32_t pixel) {
                        return (pixel & 0x00FFFFFFU) != 0;
                    });
                // Preserve the debugger's short blank lead-in suppression for
                // the bootstrap movie without making it part of VM timing.
                if (!hasVisiblePixels && leadingBlankFrames++ < 5) {
                    continue;
                }
                firstVisibleFrame = true;
            }

            QImage image(frame.width(), frame.height(), QImage::Format_ARGB32);
            if (!image.isNull() && !pixels.empty()) {
                const auto rowBytes = static_cast<std::size_t>(frame.width()) * sizeof(std::uint32_t);
                for (int y = 0; y < frame.height(); ++y) {
                    std::memcpy(image.scanLine(y),
                                pixels.data() + static_cast<std::size_t>(y) * frame.width(),
                                rowBytes);
                }
            }

            // A movie reload invalidates snapshots already being composed.
            // Do not let an old render overwrite the new session's mailbox.
            if (generation != renderGeneration_.load(std::memory_order_acquire)) {
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(frameMutex_);
                latestFrame_ = std::make_shared<QImage>(std::move(image));
            }
        } catch (const std::exception&) {
            // Keep the latest successfully rendered frame. Snapshot/render
            // failures must not stop networking or VM playback.
        }
    }
}

void DebuggerContext::dispatchLatestFrame() {
    std::shared_ptr<QImage> image;
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        image = std::move(latestFrame_);
    }
    if (image != nullptr && !image->isNull()) {
        emit frameRendered(*image);
    }
}

// ---------------------------------------------------------------------------
// HTTP pump (main thread)
// ---------------------------------------------------------------------------

void DebuggerContext::pumpNetRequests() {
    std::vector<NetFetchRequest> requests;
    {
        std::lock_guard<std::mutex> lock(netOutMutex_);
        requests.swap(netOutQueue_);
    }
    for (auto& request : requests) {
        issueFetch(std::move(request));
    }
    netPumpTimer_->stop();
}

void DebuggerContext::issueFetch(NetFetchRequest request) {
    QList<QUrl> urls;
    const auto addUrl = [&urls](const std::string& value) {
        if (!value.empty()) {
            urls.append(QUrl(QString::fromStdString(value)));
        }
    };
    addUrl(request.url);
    for (const auto& fallback : request.fallbacks) {
        addUrl(fallback);
    }
    if (urls.isEmpty()) {
        deliverFetchError(request.taskId, 404);
        return;
    }
    startFetchAttempt(std::move(request), urls);
}

void DebuggerContext::startFetchAttempt(NetFetchRequest request, QList<QUrl> urls) {
    if (urls.isEmpty()) {
        deliverFetchError(request.taskId, 404);
        return;
    }

    const QUrl url = urls.takeFirst();
    QNetworkRequest netRequest(url);
    netRequest.setTransferTimeout(20000);

    QNetworkReply* reply = nullptr;
    if (request.method == "POST" && !request.postData.empty()) {
        netRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                             QStringLiteral("application/x-www-form-urlencoded"));
        reply = netAccess_->post(
            netRequest, QByteArray(request.postData.data(),
                                   static_cast<int>(request.postData.size())));
    } else {
        reply = netAccess_->get(netRequest);
    }

    activeReplies_.insert(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, request, urls, reply] {
                activeReplies_.erase(reply);
                reply->deleteLater();

                if (reply->error() == QNetworkReply::NoError) {
                    const QByteArray data = reply->readAll();
                    std::vector<std::uint8_t> bytes(data.cbegin(), data.cend());
                    deliverFetchResult(request.taskId, std::move(bytes));
                } else if (!urls.isEmpty()) {
                    // Try the next fallback URL
                    startFetchAttempt(request, urls);
                } else {
                    deliverFetchError(request.taskId, 404);
                }
            });
}

void DebuggerContext::deliverFetchResult(int taskId, std::vector<std::uint8_t> data) {
    std::lock_guard<std::mutex> lock(netInMutex_);
    netInQueue_.push_back(NetFetchResult{taskId, std::move(data), 0});
    wakeWorker();
}

void DebuggerContext::deliverFetchError(int taskId, int status) {
    std::lock_guard<std::mutex> lock(netInMutex_);
    netInQueue_.push_back(NetFetchResult{taskId, {}, status});
    wakeWorker();
}

bool DebuggerContext::isAlreadyLoadedCastRequest(std::string_view url) const {
    if (player_ == nullptr) {
        return false;
    }

    const std::string fileName = libreshockwave::util::getFileName(url);
    const std::string lowerFileName = toLowerAscii(fileName);
    const bool castLikeRequest = lowerFileName.ends_with(".cct") ||
                                 lowerFileName.ends_with(".cst") ||
                                 fileName.find('.') == std::string::npos;
    if (!castLikeRequest) {
        return false;
    }

    const std::string baseName = libreshockwave::util::getFileNameWithoutExtension(fileName);
    if (baseName.empty()) {
        return false;
    }

    for (const auto& [number, castLib] : player_->castLibManager().castLibs()) {
        (void)number;
        if (castLib == nullptr) {
            continue;
        }

        const bool nameMatches = !castLib->name().empty() &&
                                 equalsIgnoreCase(castLib->name(), baseName);
        const std::string castFileBaseName = libreshockwave::util::getFileNameWithoutExtension(
            libreshockwave::util::getFileName(castLib->fileName()));
        const bool fileMatches = !castFileBaseName.empty() &&
                                 equalsIgnoreCase(castFileBaseName, baseName);
        if (!nameMatches && !fileMatches) {
            continue;
        }

        if (!castLib->isExternal() && castLib->isLoaded()) {
            return true;
        }
        if (castLib->isExternal() && castLib->isFetched()) {
            return true;
        }
    }
    return false;
}

} // namespace libreshockwave::debugger
