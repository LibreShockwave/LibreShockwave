#include "DebuggerContext.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <stdexcept>

#include "DebugStateBridge.hpp"
#include "ui/MovieTreePanel.hpp"
#include "libreshockwave/DirectorFile.hpp"
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

static std::int64_t frameDurationMs(int tempo) {
    return tempo > 0 ? std::max<std::int64_t>(1, 1000 / tempo) : 66;
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

// ---------------------------------------------------------------------------
// DebuggerContext
// ---------------------------------------------------------------------------

DebuggerContext::DebuggerContext(QObject* parent)
    : QObject(parent) {
    stateBridge_ = new DebugStateBridge(this);
    netAccess_ = new QNetworkAccessManager(this);
    netPumpTimer_ = new QTimer(this);
    netPumpTimer_->setInterval(15);
    connect(netPumpTimer_, &QTimer::timeout, this, &DebuggerContext::pumpNetRequests);
}

DebuggerContext::~DebuggerContext() {
    shutdownWorker();
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
    player_->setDebugEnabled(true);
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

    // Start the HTTP pump (main thread) for queued fetch requests
    netPumpTimer_->start();

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
    inputQueue_.push_back(std::move(event));
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

    // Spawn the tick-loop worker.  All Player calls, including the initial
    // prepareMovie lifecycle, stay on that VM thread.

    workerThread_ = std::thread(&DebuggerContext::runLoop, this);
}

void DebuggerContext::pausePlayback() {
    if (debugController_ != nullptr) {
        debugController_->setTracingEnabled(true);
        debugController_->pause();
    }
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
}
void DebuggerContext::stepOver() {
    if (debugController_ != nullptr) {
        debugController_->setTracingEnabled(true);
        debugController_->stepOver();
    }
}
void DebuggerContext::stepOut() {
    if (debugController_ != nullptr) {
        debugController_->setTracingEnabled(true);
        debugController_->stepOut();
    }
}
void DebuggerContext::continueExecution() {
    if (debugController_ != nullptr) debugController_->continueExecution();
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

    bool movieStarted = false;
    bool firstFrame = false;
    bool firstVisibleFrame = false;
    int leadingBlankFrames = 0;

    const auto deliverPendingFetchResults = [this] {
        std::lock_guard<std::mutex> lock(netInMutex_);
        for (const auto& result : netInQueue_) {
            if (result.errorStatus == 0) {
                queuedNet_->onFetchComplete(result.taskId, result.data);
            } else {
                queuedNet_->onFetchError(result.taskId, result.errorStatus);
            }
        }
        netInQueue_.clear();
    };

    const auto queuePendingFetchRequests = [this] {
        if (queuedNet_->pendingRequestCount() <= 0) {
            return;
        }

        const auto& pending = queuedNet_->pendingRequests();
        std::lock_guard<std::mutex> lock(netOutMutex_);
        for (const auto& request : pending) {
            netOutQueue_.push_back(NetFetchRequest{
                request.taskId,
                request.url,
                request.method,
                request.postData.value_or(""),
                request.fallbacks});
        }
        queuedNet_->drainPendingRequests();
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
        std::this_thread::sleep_for(10ms);
    }

    const auto emitCurrentFrame = [this, &firstVisibleFrame, &leadingBlankFrames] {
        if (player_ == nullptr) {
            return;
        }

        try {
            auto frameSnap = player_->frameSnapshot();
            // stageImage is only the optional script-modified stage bitmap.
            // Most movies render their background and sprites through the
            // frame pipeline, so stageImage is null even though the frame is
            // fully renderable.
            const auto bmp = frameSnap.renderFrame();
            if (!firstVisibleFrame) {
                const bool hasVisiblePixels = std::any_of(
                    bmp.pixels().begin(), bmp.pixels().end(), [](std::uint32_t pixel) {
                        return (pixel & 0x00FFFFFFU) != 0;
                    });
                // Habbo's first five score frames are a blank lead-in before
                // its Sulake splash.  Suppress that transient blank stage so
                // the debugger does not present it as "No movie loaded", but
                // still publish a genuinely black movie after a short lead-in.
                if (!hasVisiblePixels && leadingBlankFrames++ < 5) {
                    return;
                }
                firstVisibleFrame = true;
            }
            QImage image(bmp.width(), bmp.height(), QImage::Format_ARGB32);
            const auto& pixels = bmp.pixels();
            for (int y = 0; y < bmp.height(); ++y) {
                auto* scanLine = reinterpret_cast<QRgb*>(image.scanLine(y));
                for (int x = 0; x < bmp.width(); ++x) {
                    const auto& px = pixels[y * bmp.width() + x];
                    scanLine[x] = qRgba(
                        static_cast<int>((px >> 16) & 0xFF),
                        static_cast<int>((px >> 8) & 0xFF),
                        static_cast<int>(px & 0xFF),
                        static_cast<int>((px >> 24) & 0xFF));
                }
            }
            emit frameRendered(image);
        } catch (const std::exception&) {
            // Transient render failure (e.g. movie mid-transition) —
            // keep the last good frame.
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
            // before the first tick advances the score to frame 2.
            firstFrame = true;
        }

        if (firstFrame) {
            emitCurrentFrame();
            firstFrame = false;
        }

        // 5. Run one frame tick.  If a breakpoint is hit, DebugController
        //    blocks this thread on its internal condition_variable until the
        //    main thread calls stepInto/stepOver/stepOut/continueExecution.
        if (player_->state() == libreshockwave::player::PlayerState::Playing) {
            (void)player_->tick();
        }
        networkReady_.store(player_->networkReady(), std::memory_order_release);

        // Check if we should stop
        if (quitWorker_.load(std::memory_order_relaxed)) {
            break;
        }

        // Render the current frame on this (VM) thread — the only thread that
        // touches the player.  Rendering from the main thread while the tick
        // loop mutates the stage would race (sprites swapped mid-read), so the
        // composed bitmap is copied into a QImage here and handed to the UI.
        emitCurrentFrame();

        // Pace to the movie's tempo
        const auto delay = frameDurationMs(player_->tempo());
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }

    playing_.store(false);
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
}

void DebuggerContext::deliverFetchError(int taskId, int status) {
    std::lock_guard<std::mutex> lock(netInMutex_);
    netInQueue_.push_back(NetFetchResult{taskId, {}, status});
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
