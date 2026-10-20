#include "debugger_context.hpp"

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

#include "debug_state_bridge.hpp"
#include "ui/movie_tree_panel.hpp"
#include "libreshockwave/director_file.hpp"
#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/player/player.hpp"
#include "libreshockwave/player/cast/cast_lib.hpp"
#include "libreshockwave/player/cast/cast_lib_manager.hpp"
#include "libreshockwave/player/debug/debug_controller.hpp"
#include "libreshockwave/player/input_handler.hpp"
#include "libreshockwave/player/net/queued_net_provider.hpp"
#include "libreshockwave/util/file_util.hpp"

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
    const double frames_per_second = tempo > 0 ? static_cast<double>(tempo) : 15.0;
    const auto duration = std::chrono::duration<double>(1.0 / frames_per_second);
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
    state_bridge_ = new DebugStateBridge(this);
    net_access_ = new QNetworkAccessManager(this);
    net_pump_timer_ = new QTimer(this);
    // QueuedNetProvider is drained on demand. A fixed 15 ms timer added
    // avoidable network latency on a 60/75/120 Hz movie and was especially
    // visible while the old VM loop was rendering.
    net_pump_timer_->setInterval(0);
    connect(net_pump_timer_, &QTimer::timeout, this, &DebuggerContext::pumpNetRequests);

    frame_pump_timer_ = new QTimer(this);
    // The frame mailbox keeps only the newest rendered image, so polling it
    // every 5 ms just wakes the GUI repeatedly when no new frame is ready.
    // A normal display cadence is frequent enough and avoids competing with
    // input and recording work on the GUI thread.
    frame_pump_timer_->setInterval(16);
    frame_pump_timer_->setTimerType(Qt::CoarseTimer);
    connect(frame_pump_timer_, &QTimer::timeout, this, &DebuggerContext::dispatchLatestFrame);
    frame_pump_timer_->start();

    // This thread never touches Player.  It only renders immutable snapshots
    // submitted by the VM loop, so compositing cannot delay networking,
    // input, or the movie's frame deadline.
    render_thread_ = std::thread(&DebuggerContext::runRenderLoop, this);
}

DebuggerContext::~DebuggerContext() {
    shutdownWorker();

    {
        std::lock_guard<std::mutex> lock(render_mutex_);
        quit_render_ = true;
        pending_frame_snapshot_.reset();
    }
    render_condition_.notify_one();
    if (render_thread_.joinable()) {
        render_thread_.join();
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
                                        const std::string& base_path) {
    std::shared_ptr<DirectorFile> director_file;
    try {
        director_file = DirectorFile::load(data);
    } catch (const std::exception& ex) {
        emit errorOccurred(
            QStringLiteral("Failed to parse movie: %1").arg(QString::fromUtf8(ex.what())));
        return false;
    }

    if (director_file == nullptr) {
        emit errorOccurred(QStringLiteral("Failed to parse movie: unknown format"));
        return false;
    }

    director_file->setBasePath(base_path);

    // Shut down any previous session
    shutdownWorker();

    // Discard stale fetch results/requests from the previous session
    {
        std::lock_guard<std::mutex> lock(net_in_mutex_);
        net_in_queue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(net_out_mutex_);
        net_out_queue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(render_mutex_);
        render_generation_.fetch_add(1, std::memory_order_acq_rel);
        pending_frame_snapshot_.reset();
    }
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        latest_frame_.reset();
    }
    last_cast_snapshot_.reset();
    network_ready_ = false;
    movie_data_ = data;
    movie_path_ = base_path;

    // QueuedNetProvider is owned by the worker thread.  The plain Player
    // constructor registers the native SocketMultiuserBridge (the equivalent
    // of the WASM WebSocket multiuser bridge); setNetProvider then swaps the
    // active fetch provider to the queued one.
    queued_net_ = std::make_unique<libreshockwave::player::net::QueuedNetProvider>(base_path);
    player_ = std::make_unique<libreshockwave::player::Player>(director_file);
    player_->setNetProvider(queued_net_.get());
    player_->setExternalParams(external_params_);
    // Authored debug output follows the Debug Window toggle: put/alert/debug
    // builtins only produce output while the user keeps debug mode enabled.
    player_->setDebugEnabled(debug_enabled_.load());

    // Route authored debug output and VM trace lines to the Debug Window
    // panel.  Both handlers run on the VM thread and hand off to the GUI
    // thread through the queued vmOutput signal.
    auto& vm = player_->vm();
    vm.builtinContext().output_handler =
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
        if (last_cast_snapshot_.has_value() && *last_cast_snapshot_ == snapshot) {
            return;
        }
        last_cast_snapshot_ = snapshot;
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
    debug_controller_ = std::make_shared<libreshockwave::player::debug::DebugController>();
    // Keep the controller attached for debugger commands, but do not trace
    // every startup instruction during ordinary playback.  Habbo's startup
    // deobfuscation is particularly expensive under instruction tracing.
    debug_controller_->setTracingEnabled(false);
    player_->setDebugController(debug_controller_);

    // Register the state bridge so the UI receives pause/resume callbacks
    debug_controller_->addListener(state_bridge_);

    // Network I/O bridges — mirror the WASM wiring (wasm_bridge.cpp):
    // gotoNetPage/gotoNetMovie become movie navigations completed by the
    // worker and surfaced to the window for a full movie reload.
    player_->movieProperties().setGotoNetPageHandler(
        [this](const std::string& url, const std::string& /*target*/) {
            (void)queued_net_->beginMovieNavigation(url);
        });
    player_->movieProperties().setGotoNetMovieHandler(
        [this](const std::string& url) {
            return queued_net_->beginMovieNavigation(url);
        });
    // Fetches complete with the raw bytes on the VM thread, like the WASM
    // worker's fetchCompleteCallback.
    queued_net_->setFetchCompleteCallback(
        [this](const std::string& url, const std::vector<std::uint8_t>& fetch_data) {
            if (player_ != nullptr) {
                player_->onNetFetchComplete(url, fetch_data);
            }
        });
    // Cast requests that are already loaded/fetched must not be re-fetched.
    queued_net_->setSatisfiedFetchPredicate(
        [this](std::string_view url) {
            return isAlreadyLoadedCastRequest(url);
        });

    playing_ = false;
    quit_worker_ = false;

    emit movieLoaded();
    return true;
}

libreshockwave::player::Player* DebuggerContext::player() const { return player_.get(); }
libreshockwave::player::debug::DebugController* DebuggerContext::debugController() const {
    return debug_controller_.get();
}
DebugStateBridge* DebuggerContext::stateBridge() const { return state_bridge_; }
const std::string& DebuggerContext::moviePath() const { return movie_path_; }
bool DebuggerContext::hasMovie() const {
    return player_ != nullptr || !movie_data_.empty();
}
bool DebuggerContext::isPlaying() const { return playing_.load(); }
bool DebuggerContext::networkReady() const { return network_ready_.load(std::memory_order_acquire); }

void DebuggerContext::setExternalParams(
    std::vector<std::pair<std::string, std::string>> params) {
    external_params_ = std::move(params);
    if (player_ != nullptr) {
        player_->setExternalParams(external_params_);
    }
}

void DebuggerContext::setDebugEnabled(bool enabled) {
    debug_enabled_.store(enabled, std::memory_order_relaxed);
    if (player_ != nullptr) {
        player_->setDebugEnabled(enabled);
    }
}

bool DebuggerContext::debugEnabled() const {
    return debug_enabled_.load(std::memory_order_relaxed);
}

bool DebuggerContext::restoreMovieSession() {
    if (player_ != nullptr) {
        return true;
    }
    if (movie_data_.empty()) {
        return false;
    }
    return loadMovieFromData(movie_data_, movie_path_);
}

void DebuggerContext::enqueueInput(InputEvent event) {
    std::lock_guard<std::mutex> lock(input_mutex_);

    // Mouse moves are high-frequency and only the newest position matters
    // before the next VM iteration. Coalesce adjacent moves so a busy GUI
    // cannot build an unbounded input backlog while the movie is ticking.
    if (event.type == InputEvent::MouseMove && !input_queue_.empty() &&
        input_queue_.back().type == InputEvent::MouseMove) {
        input_queue_.back() = std::move(event);
        wakeWorker();
        return;
    }

    input_queue_.push_back(std::move(event));
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

    quit_worker_ = false;
    playing_ = true;
    wakeWorker();

    // Spawn the tick-loop worker.  All Player calls, including the initial
    // prepareMovie lifecycle, stay on that VM thread.

    worker_thread_ = std::thread(&DebuggerContext::runLoop, this);
}

void DebuggerContext::pausePlayback() {
    if (debug_controller_ != nullptr) {
        debug_controller_->setTracingEnabled(true);
        debug_controller_->pause();
    }
    wakeWorker();
}

void DebuggerContext::stop() {
    // Stop is a full session reset.  The selected movie bytes remain so the
    // next Play creates a new Player, VM, cast manager, and net provider.
    shutdownWorker();
}

void DebuggerContext::stepInto() {
    if (debug_controller_ != nullptr) {
        debug_controller_->setTracingEnabled(true);
        debug_controller_->stepInto();
    }
    wakeWorker();
}
void DebuggerContext::stepOver() {
    if (debug_controller_ != nullptr) {
        debug_controller_->setTracingEnabled(true);
        debug_controller_->stepOver();
    }
    wakeWorker();
}
void DebuggerContext::stepOut() {
    if (debug_controller_ != nullptr) {
        debug_controller_->setTracingEnabled(true);
        debug_controller_->stepOut();
    }
    wakeWorker();
}
void DebuggerContext::continueExecution() {
    if (debug_controller_ != nullptr) debug_controller_->continueExecution();
    wakeWorker();
}

bool DebuggerContext::toggleBreakpoint(int script_id,
                                       const std::string& handler_name,
                                       int offset) {
    if (debug_controller_ == nullptr) return false;
    debug_controller_->setTracingEnabled(true);
    return debug_controller_->toggleBreakpoint(script_id, handler_name, offset);
}

void DebuggerContext::clearAllBreakpoints() {
    if (debug_controller_ != nullptr) debug_controller_->clearAllBreakpoints();
}

void DebuggerContext::addWatch(const std::string& expression) {
    if (debug_controller_ != nullptr) {
        debug_controller_->setTracingEnabled(true);
        (void)debug_controller_->addWatchExpression(expression);
    }
}

void DebuggerContext::removeWatch(const std::string& id) {
    if (debug_controller_ != nullptr) {
        (void)debug_controller_->removeWatchExpression(id);
    }
}

void DebuggerContext::shutdownWorker() {
    // Stop the HTTP pump and cancel any in-flight replies so stale results
    // can never be delivered to a future session's QueuedNetProvider (task
    // ids restart from 1 per provider).
    if (net_pump_timer_ != nullptr) {
        net_pump_timer_->stop();
    }
    for (auto* reply : std::exchange(active_replies_, {})) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (net_access_ != nullptr) {
        net_access_->clearConnectionCache();
    }

    if (player_ != nullptr && debug_controller_ != nullptr) {
        debug_controller_->removeListener(state_bridge_);
        debug_controller_->clearAllBreakpoints();
        debug_controller_->reset();
    }
    quit_worker_ = true;
    playing_ = false;
    wakeWorker();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(net_in_mutex_);
        net_in_queue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(net_out_mutex_);
        net_out_queue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        input_queue_.clear();
    }

    if (player_ != nullptr) {
        player_->stop();
        player_->shutdown();
        player_.reset();
    }
    debug_controller_.reset();
    queued_net_.reset();
    last_cast_snapshot_.reset();
    network_ready_ = false;
}

// ---------------------------------------------------------------------------
// Worker thread loop
// ---------------------------------------------------------------------------

void DebuggerContext::runLoop() {
    using namespace std::chrono_literals;
    using Clock = std::chrono::steady_clock;

    bool movie_started = false;
    bool first_frame = false;
    Clock::time_point next_tick_at{};

    const auto deliver_pending_fetch_results = [this] {
        std::vector<NetFetchResult> results;
        {
            std::lock_guard<std::mutex> lock(net_in_mutex_);
            results.swap(net_in_queue_);
        }
        for (auto& result : results) {
            if (result.error_status == 0) {
                queued_net_->onFetchComplete(result.task_id, std::move(result.data));
            } else {
                queued_net_->onFetchError(result.task_id, result.error_status);
            }
        }
    };

    const auto queue_pending_fetch_requests = [this] {
        if (queued_net_->pendingRequestCount() <= 0) {
            return;
        }

        const auto& pending = queued_net_->pendingRequests();
        {
            std::lock_guard<std::mutex> lock(net_out_mutex_);
            for (const auto& request : pending) {
                net_out_queue_.push_back(NetFetchRequest{
                    request.task_id,
                    request.url,
                    request.method,
                    request.post_data.value_or(""),
                    request.fallbacks});
            }
        }
        queued_net_->drainPendingRequests();

        // The QNetworkAccessManager lives on the GUI thread, but the pump is
        // scheduled only when work exists. This mirrors the WASM worker's
        // host-queue polling without a continuously firing GUI timer.
        QMetaObject::invokeMethod(this, [this] {
            if (net_pump_timer_ != nullptr) {
                net_pump_timer_->start();
            }
        }, Qt::QueuedConnection);
    };

    // The shell movie's cast 2 contains the bootstrap scripts that set up
    // the external cast/text flow. Let its fetch complete before the first
    // prepareMovie pass, but keep the wait bounded so a failed CDN request
    // cannot prevent the debugger from starting.
    (void)player_->preloadAllCasts();
    const auto bootstrap_cast = player_->castLibManager().getCastLib(2);
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (!quit_worker_.load(std::memory_order_relaxed) &&
           bootstrap_cast && bootstrap_cast->isExternal() && !bootstrap_cast->isLoaded() &&
           std::chrono::steady_clock::now() < deadline) {
        deliver_pending_fetch_results();
        queue_pending_fetch_requests();
        const auto observed_wake = worker_wake_generation_.load(std::memory_order_acquire);
        std::unique_lock<std::mutex> wait_lock(worker_wait_mutex_);
        worker_condition_.wait_for(wait_lock, 50ms, [this, observed_wake] {
            return quit_worker_.load(std::memory_order_relaxed) ||
                   worker_wake_generation_.load(std::memory_order_acquire) != observed_wake;
        });
    }

    const auto submit_current_frame = [this] {
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

    while (!quit_worker_.load(std::memory_order_relaxed)) {
        // 1. Deliver completed network fetches (main thread → VM thread).
        //    onFetchComplete invokes fetchCompleteCallback_ which feeds the
        //    bytes to Player::onNetFetchComplete on this (VM) thread.
        deliver_pending_fetch_results();

        // 2. Complete pending movie navigations and surface them to the
        //    window, which reloads the movie (mirrors the WASM worker's
        //    navigation poll).
        if (queued_net_->pendingMovieNavigationTaskCount() > 0) {
            for (const auto& nav : queued_net_->pendingMovieNavigationRequests()) {
                queued_net_->onMovieNavigationComplete(nav.task_id);
                emit movieNavigationRequested(QString::fromStdString(nav.url));
            }
        }

        // 3. Copy pending fetch requests out of the provider for the
        //    main-thread HTTP pump.
        queue_pending_fetch_requests();

        // 4. Drain queued input events
        {
            std::lock_guard<std::mutex> lock(input_mutex_);
            std::vector<InputEvent> queued;
            queued.swap(input_queue_);
            for (std::size_t index = 0; index < queued.size(); ++index) {
                const auto& event = queued[index];
                auto& handler = player_->inputHandler();
                switch (event.type) {
                    case InputEvent::MouseMove:
                        handler.onMouseMove(event.stage_x, event.stage_y);
                        break;
                    case InputEvent::MouseDown:
                        handler.onMouseDown(event.stage_x, event.stage_y, event.right_button);
                        break;
                    case InputEvent::MouseUp:
                        handler.onMouseUp(event.stage_x, event.stage_y, event.right_button);
                        break;
                    case InputEvent::KeyDown:
                        handler.onKeyDown(event.director_key_code, event.key_text,
                                          event.shift, event.ctrl, event.alt);
                        break;
                    case InputEvent::KeyUp:
                        handler.onKeyUp(event.director_key_code, event.key_text,
                                        event.shift, event.ctrl, event.alt);
                        break;
                }
            }
        }
        (void)player_->inputHandler().processInputEvents();

        if (!movie_started) {
            player_->play();
            movie_started = true;
            network_ready_.store(player_->networkReady(), std::memory_order_release);
            emit playbackStarted();
            // play() prepares the movie's first frame. Keep that frame visible
            // for one complete movie frame before the first tick advances the
            // score to frame 2.
            first_frame = true;
            next_tick_at = Clock::now() + frameDuration(player_->tempo());
        }

        if (first_frame) {
            submit_current_frame();
            first_frame = false;
        }

        // 5. Run one frame tick at the movie's deadline. Rendering is not part
        // of this deadline: it consumes the latest immutable snapshot on a
        // separate thread. If a breakpoint is hit, DebugController blocks
        // this VM thread on its own condition_variable until the main thread
        // calls step/continue, while GUI and network event loops remain free.
        if (player_->state() == libreshockwave::player::PlayerState::Playing &&
            Clock::now() >= next_tick_at) {
            try {
                (void)player_->tick();
            } catch (const std::exception& error) {
                emit errorOccurred(
                    QStringLiteral("Tick error: %1").arg(QString::fromUtf8(error.what())));
            } catch (...) {
                emit errorOccurred(QStringLiteral("Tick error: unknown exception"));
            }

            network_ready_.store(player_->networkReady(), std::memory_order_release);
            if (!quit_worker_.load(std::memory_order_relaxed)) {
                submit_current_frame();
            }

            const auto delay = frameDuration(player_->tempo());
            next_tick_at += delay;
            // A slow VM frame must not cause a burst of catch-up ticks. The
            // next tick is scheduled from the current time, preserving the
            // movie cadence while giving network/input work a chance to run.
            const auto now = Clock::now();
            if (next_tick_at <= now) {
                next_tick_at = now + delay;
            }
            continue;
        }

        network_ready_.store(player_->networkReady(), std::memory_order_release);

        const auto observed_wake = worker_wake_generation_.load(std::memory_order_acquire);
        std::unique_lock<std::mutex> wait_lock(worker_wait_mutex_);
        if (player_->state() == libreshockwave::player::PlayerState::Playing) {
            worker_condition_.wait_until(wait_lock, next_tick_at, [this, observed_wake] {
                return quit_worker_.load(std::memory_order_relaxed) ||
                       worker_wake_generation_.load(std::memory_order_acquire) != observed_wake;
            });
        } else {
            worker_condition_.wait(wait_lock, [this, observed_wake] {
                return quit_worker_.load(std::memory_order_relaxed) ||
                       worker_wake_generation_.load(std::memory_order_acquire) != observed_wake;
            });
        }
    }

    playing_.store(false);
}

void DebuggerContext::wakeWorker() {
    worker_wake_generation_.fetch_add(1, std::memory_order_release);
    worker_condition_.notify_one();
}

void DebuggerContext::submitFrameSnapshot(
    libreshockwave::player::render::pipeline::FrameSnapshot snapshot) {
    auto shared_snapshot = std::make_shared<const libreshockwave::player::render::pipeline::FrameSnapshot>(
        std::move(snapshot));
    {
        std::lock_guard<std::mutex> lock(render_mutex_);
        pending_frame_generation_ = render_generation_.load(std::memory_order_acquire);
        pending_frame_snapshot_ = std::move(shared_snapshot);
    }
    // Replacing the pending snapshot is intentional: presentation must never
    // make the VM or network client wait behind stale frames.
    render_condition_.notify_one();
}

void DebuggerContext::runRenderLoop() {
    bool first_visible_frame = false;
    int leading_blank_frames = 0;

    while (true) {
        std::shared_ptr<const libreshockwave::player::render::pipeline::FrameSnapshot> snapshot;
        std::uint64_t generation = 0;
        {
            std::unique_lock<std::mutex> lock(render_mutex_);
            render_condition_.wait(lock, [this] {
                return quit_render_ || pending_frame_snapshot_ != nullptr;
            });
            if (quit_render_) {
                return;
            }
            snapshot = std::move(pending_frame_snapshot_);
            generation = pending_frame_generation_;
        }

        try {
            const auto frame = snapshot->renderPresentableFrame();
            const auto& pixels = frame.pixels();
            if (!first_visible_frame) {
                const bool has_visible_pixels = std::any_of(
                    pixels.begin(), pixels.end(), [](std::uint32_t pixel) {
                        return (pixel & 0x00FFFFFFU) != 0;
                    });
                // Preserve the debugger's short blank lead-in suppression for
                // the bootstrap movie without making it part of VM timing.
                if (!has_visible_pixels && leading_blank_frames++ < 5) {
                    continue;
                }
                first_visible_frame = true;
            }

            QImage image(frame.width(), frame.height(), QImage::Format_ARGB32);
            if (!image.isNull() && !pixels.empty()) {
                const auto row_bytes = static_cast<std::size_t>(frame.width()) * sizeof(std::uint32_t);
                for (int y = 0; y < frame.height(); ++y) {
                    std::memcpy(image.scanLine(y),
                                pixels.data() + static_cast<std::size_t>(y) * frame.width(),
                                row_bytes);
                }
            }

            // A movie reload invalidates snapshots already being composed.
            // Do not let an old render overwrite the new session's mailbox.
            if (generation != render_generation_.load(std::memory_order_acquire)) {
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(frame_mutex_);
                latest_frame_ = std::make_shared<QImage>(std::move(image));
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
        std::lock_guard<std::mutex> lock(frame_mutex_);
        image = std::move(latest_frame_);
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
        std::lock_guard<std::mutex> lock(net_out_mutex_);
        requests.swap(net_out_queue_);
    }
    for (auto& request : requests) {
        issueFetch(std::move(request));
    }
    net_pump_timer_->stop();
}

void DebuggerContext::issueFetch(NetFetchRequest request) {
    QList<QUrl> urls;
    const auto add_url = [&urls](const std::string& value) {
        if (!value.empty()) {
            urls.append(QUrl(QString::fromStdString(value)));
        }
    };
    add_url(request.url);
    for (const auto& fallback : request.fallbacks) {
        add_url(fallback);
    }
    if (urls.isEmpty()) {
        deliverFetchError(request.task_id, 404);
        return;
    }
    startFetchAttempt(std::move(request), urls);
}

void DebuggerContext::startFetchAttempt(NetFetchRequest request, QList<QUrl> urls) {
    if (urls.isEmpty()) {
        deliverFetchError(request.task_id, 404);
        return;
    }

    const QUrl url = urls.takeFirst();
    QNetworkRequest net_request(url);
    net_request.setTransferTimeout(20000);

    QNetworkReply* reply = nullptr;
    if (request.method == "POST" && !request.post_data.empty()) {
        net_request.setHeader(QNetworkRequest::ContentTypeHeader,
                             QStringLiteral("application/x-www-form-urlencoded"));
        reply = net_access_->post(
            net_request, QByteArray(request.post_data.data(),
                                   static_cast<int>(request.post_data.size())));
    } else {
        reply = net_access_->get(net_request);
    }

    active_replies_.insert(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, request, urls, reply] {
                active_replies_.erase(reply);
                reply->deleteLater();

                if (reply->error() == QNetworkReply::NoError) {
                    const QByteArray data = reply->readAll();
                    std::vector<std::uint8_t> bytes(data.cbegin(), data.cend());
                    deliverFetchResult(request.task_id, std::move(bytes));
                } else if (!urls.isEmpty()) {
                    // Try the next fallback URL
                    startFetchAttempt(request, urls);
                } else {
                    deliverFetchError(request.task_id, 404);
                }
            });
}

void DebuggerContext::deliverFetchResult(int task_id, std::vector<std::uint8_t> data) {
    std::lock_guard<std::mutex> lock(net_in_mutex_);
    net_in_queue_.push_back(NetFetchResult{task_id, std::move(data), 0});
    wakeWorker();
}

void DebuggerContext::deliverFetchError(int task_id, int status) {
    std::lock_guard<std::mutex> lock(net_in_mutex_);
    net_in_queue_.push_back(NetFetchResult{task_id, {}, status});
    wakeWorker();
}

bool DebuggerContext::isAlreadyLoadedCastRequest(std::string_view url) const {
    if (player_ == nullptr) {
        return false;
    }

    const std::string file_name = libreshockwave::util::getFileName(url);
    const std::string lower_file_name = toLowerAscii(file_name);
    const bool cast_like_request = lower_file_name.ends_with(".cct") ||
                                 lower_file_name.ends_with(".cst") ||
                                 file_name.find('.') == std::string::npos;
    if (!cast_like_request) {
        return false;
    }

    const std::string base_name = libreshockwave::util::getFileNameWithoutExtension(file_name);
    if (base_name.empty()) {
        return false;
    }

    for (const auto& [number, cast_lib] : player_->castLibManager().castLibs()) {
        (void)number;
        if (cast_lib == nullptr) {
            continue;
        }

        const bool name_matches = !cast_lib->name().empty() &&
                                 equalsIgnoreCase(cast_lib->name(), base_name);
        const std::string cast_file_base_name = libreshockwave::util::getFileNameWithoutExtension(
            libreshockwave::util::getFileName(cast_lib->fileName()));
        const bool file_matches = !cast_file_base_name.empty() &&
                                 equalsIgnoreCase(cast_file_base_name, base_name);
        if (!name_matches && !file_matches) {
            continue;
        }

        if (!cast_lib->isExternal() && cast_lib->isLoaded()) {
            return true;
        }
        if (cast_lib->isExternal() && cast_lib->isFetched()) {
            return true;
        }
    }
    return false;
}

} // namespace libreshockwave::debugger
