#include "libreshockwave/player/player.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/cast/cast_member.hpp"
#include "libreshockwave/chunks/cast_member_chunk.hpp"
#include "libreshockwave/chunks/script_chunk.hpp"
#include "libreshockwave/chunks/script_names_chunk.hpp"
#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/lingo/vm/dispatch/image_method_dispatcher.hpp"
#include "libreshockwave/lingo/vm/util/ancestor_chain_walker.hpp"
#include "libreshockwave/lingo/xtra/multiuser_xtra.hpp"
#include "libreshockwave/lingo/xtra/xml_parser_xtra.hpp"
#include "libreshockwave/player/external_cast_load_handler.hpp"
#include "libreshockwave/player/player_event.hpp"
#include "libreshockwave/player/debug/debug_controller_api.hpp"
#include "libreshockwave/player/debug/lifecycle_diagnostics.hpp"
#include "libreshockwave/player/event/event_dispatcher.hpp"
#include "libreshockwave/player/render/output/simple_text_renderer.hpp"
#include "libreshockwave/player/score/score_navigator.hpp"
#include "libreshockwave/util/file_util.hpp"

namespace libreshockwave::player {
namespace {

constexpr int DEFAULT_TEMPO = 15;

class TickDeadlineGuard {
public:
    explicit TickDeadlineGuard(lingo::vm::LingoVM& vm)
        : vm_(vm),
          previous_deadline_(vm.tickDeadline()) {
        if (previous_deadline_ == 0) {
            vm_.armTickDeadline();
        }
    }

    ~TickDeadlineGuard() {
        vm_.setTickDeadline(previous_deadline_);
    }

private:
    lingo::vm::LingoVM& vm_;
    std::int64_t previous_deadline_{0};
};

struct ResolvedScriptTarget {
    std::shared_ptr<chunks::ScriptChunk> script;
    std::shared_ptr<const chunks::ScriptNamesChunk> script_names;
    std::shared_ptr<const DirectorFile> script_owner;
    chunks::ScriptChunkType script_type{chunks::ScriptChunkType::Unknown};
};

struct HandlerLookupKey {
    const chunks::ScriptChunk* script{nullptr};
    const chunks::ScriptNamesChunk* script_names{nullptr};
    std::string handler_name;

    bool operator==(const HandlerLookupKey& other) const {
        return script == other.script &&
               script_names == other.script_names &&
               handler_name == other.handler_name;
    }
};

struct HandlerLookupKeyHash {
    std::size_t operator()(const HandlerLookupKey& key) const {
        const auto script_hash = std::hash<const void*>{}(key.script);
        const auto names_hash = std::hash<const void*>{}(key.script_names);
        const auto name_hash = std::hash<std::string>{}(key.handler_name);
        return script_hash ^ (names_hash << 1U) ^ (name_hash << 2U);
    }
};

struct ScriptHandlerFinderKey {
    int cast_lib{0};
    int member_num{0};
    std::string handler_name;

    bool operator==(const ScriptHandlerFinderKey& other) const {
        return cast_lib == other.cast_lib &&
               member_num == other.member_num &&
               handler_name == other.handler_name;
    }
};

struct ScriptHandlerFinderKeyHash {
    std::size_t operator()(const ScriptHandlerFinderKey& key) const {
        const auto cast_hash = std::hash<int>{}(key.cast_lib);
        const auto member_hash = std::hash<int>{}(key.member_num);
        const auto name_hash = std::hash<std::string>{}(key.handler_name);
        return cast_hash ^ (member_hash << 1U) ^ (name_hash << 2U);
    }
};

std::string lowerAscii(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

int configuredTempo(const std::shared_ptr<DirectorFile>& file) {
    if (file == nullptr) {
        return DEFAULT_TEMPO;
    }
    const int tempo = file->tempo();
    return tempo > 0 ? tempo : DEFAULT_TEMPO;
}

std::int64_t currentTimeMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string trimWhitespace(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

bool isLingoIdentifier(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    const auto first = static_cast<unsigned char>(value.front());
    if (!std::isalpha(first) && value.front() != '_') {
        return false;
    }
    for (char ch : value.substr(1)) {
        const auto raw = static_cast<unsigned char>(ch);
        if (!std::isalnum(raw) && ch != '_') {
            return false;
        }
    }
    return true;
}

std::optional<std::string> legacyEventHandlerName(const lingo::Datum& value) {
    if (const auto* symbol = value.asSymbol()) {
        if (symbol->name.empty()) {
            return std::nullopt;
        }
        return symbol->name;
    }
    if (!value.isString()) {
        return std::nullopt;
    }

    std::string handler_name = trimWhitespace(value.stringValue());
    if (!handler_name.empty() && handler_name.front() == '#') {
        handler_name.erase(handler_name.begin());
    }
    return isLingoIdentifier(handler_name) ? std::optional<std::string>(std::move(handler_name)) : std::nullopt;
}

std::string_view legacyEventScriptProperty(PlayerEvent event) {
    switch (event) {
        case PlayerEvent::MouseDown:
            return "mouseDownScript";
        case PlayerEvent::MouseUp:
            return "mouseUpScript";
        case PlayerEvent::KeyDown:
            return "keyDownScript";
        case PlayerEvent::KeyUp:
            return "keyUpScript";
        default:
            return {};
    }
}

} // namespace

class Player::PlayerTraceListener final : public lingo::vm::TraceListener {
public:
    explicit PlayerTraceListener(Player& player)
        : player_(player) {}

    [[nodiscard]] bool needsHandlerTrace() const override {
        return debug::LifecycleDiagnostics::isEnabled() ||
               (player_.debug_controller_ != nullptr && player_.debug_controller_->tracingEnabled());
    }

    void onHandlerEnter(const HandlerInfo& info) override {
        debug::LifecycleDiagnostics::logHandlerEnter(info);
        if (player_.debug_controller_) {
            player_.debug_controller_->onHandlerEnter(info);
        }
    }

    void onHandlerExit(const HandlerInfo& info, const lingo::Datum& return_value) override {
        debug::LifecycleDiagnostics::logHandlerExit(info, return_value);
        if (player_.debug_controller_) {
            player_.debug_controller_->onHandlerExit(info, return_value);
        }
    }

    [[nodiscard]] bool needsInstructionTrace() const override {
        return player_.debug_controller_ != nullptr && player_.debug_controller_->needsInstructionTrace();
    }

    [[nodiscard]] bool needsFullInstructionInfo(int offset) const override {
        return player_.debug_controller_ == nullptr ||
               player_.debug_controller_->needsFullInstructionInfo(offset);
    }

    [[nodiscard]] bool needsVariableTrace() const override {
        return player_.debug_controller_ != nullptr && player_.debug_controller_->tracingEnabled();
    }

    void onInstruction(const InstructionInfo& info) override {
        if (player_.debug_controller_) {
            player_.debug_controller_->onInstruction(info);
        }
    }

    void onVariableSet(std::string_view type, std::string_view name, const lingo::Datum& value) override {
        if (player_.debug_controller_) {
            player_.debug_controller_->onVariableSet(type, name, value);
        }
    }

    void onError(std::string_view message, std::string_view error) override {
        debug::LifecycleDiagnostics::logError(message, error);
        if (player_.debug_controller_) {
            player_.debug_controller_->onError(message, error);
        }
        player_.handleTraceError(message, error);
    }

    void onDebugMessage(std::string_view message) override {
        if (player_.debug_controller_) {
            player_.debug_controller_->onDebugMessage(message);
        }
    }

private:
    Player& player_;
};

Player::Player(std::shared_ptr<DirectorFile> file)
    : Player(std::move(file), true) {}

Player::Player(std::shared_ptr<DirectorFile> file, bool register_socket_multiuser_xtra)
    : file_(std::move(file)),
      frame_context_(file_.get()),
      stage_renderer_(file_.get()),
      net_manager_(),
      xtra_manager_(),
      movie_properties_(file_.get()),
      sprite_properties_(&stage_renderer_.spriteRegistry()),
      cast_lib_manager_(file_, [this](int cast_lib_number, const std::string& file_name) {
          loadCastFromNetCache(cast_lib_number, file_name);
      }),
      timeout_manager_(),
      sound_manager_(file_.get()),
      bitmap_cache_(),
      sprite_baker_(&bitmap_cache_),
      default_text_renderer_(std::make_unique<render::output::SimpleTextRenderer>()),
      frame_render_pipeline_(&stage_renderer_, &sprite_baker_),
      input_state_(),
      bitmap_resolver_(file_, &cast_lib_manager_, &frame_context_),
      cursor_manager_(&input_state_, &stage_renderer_.spriteRegistry()),
      input_handler_(&input_state_, &stage_renderer_, &frame_context_.eventDispatcher(), &cast_lib_manager_),
      vm_(file_.get()),
      tempo_(configuredTempo(file_)) {
    xtra_manager_.registerXtra(std::make_unique<lingo::xtra::XmlParserXtra>());
    if (register_socket_multiuser_xtra) {
        socket_multiuser_bridge_ = std::make_unique<xtra::SocketMultiuserBridge>();
        socket_multiuser_bridge_->setIncomingDataReadyPredicate([this] {
            return externalCastsReady();
        });
        registerMultiuserXtra(*socket_multiuser_bridge_);
    }
    player_trace_listener_ = std::make_shared<PlayerTraceListener>(*this);
    vm_.setTraceListener(player_trace_listener_);
    wireComponents();
    setTextRenderer(default_text_renderer_.get());
}

Player::Player(std::shared_ptr<DirectorFile> file, net::NetProvider* net_provider)
    : Player(std::move(file), false) {
    setNetProvider(net_provider);
}

Player::Player(std::shared_ptr<DirectorFile> file,
               net::NetProvider* net_provider,
               CastDataRequestCallback cast_data_request_callback)
    : Player(std::move(file), false) {
    cast_data_request_callback_ = std::move(cast_data_request_callback);
    setNetProvider(net_provider);
}

Player::~Player() {
    if (debug_controller_) {
        debug_controller_->reset();
    }
    joinVmWorker();
    vm_.setTraceListener(nullptr);
    lingo::vm::dispatch::ImageMethodDispatcher::clearImageMutationCallback(this);
}

std::shared_ptr<DirectorFile> Player::file() const { return file_; }
frame::FrameContext& Player::frameContext() { return frame_context_; }
score::ScoreNavigator& Player::navigator() { return frame_context_.navigator(); }
behavior::BehaviorManager& Player::behaviorManager() { return frame_context_.behaviorManager(); }
event::EventDispatcher& Player::eventDispatcher() { return frame_context_.eventDispatcher(); }
render::pipeline::StageRenderer& Player::stageRenderer() { return stage_renderer_; }
net::NetManager& Player::netManager() { return net_manager_; }
lingo::xtra::XtraManager& Player::xtraManager() { return xtra_manager_; }
MovieProperties& Player::movieProperties() { return movie_properties_; }
SpriteProperties& Player::spriteProperties() { return sprite_properties_; }
cast::CastLibManager& Player::castLibManager() { return cast_lib_manager_; }
timeout::TimeoutManager& Player::timeoutManager() { return timeout_manager_; }
audio::SoundManager& Player::soundManager() { return sound_manager_; }
render::pipeline::BitmapCache& Player::bitmapCache() { return bitmap_cache_; }
render::pipeline::SpriteBaker& Player::spriteBaker() { return sprite_baker_; }
render::pipeline::FrameRenderPipeline& Player::frameRenderPipeline() { return frame_render_pipeline_; }
input::InputState& Player::inputState() { return input_state_; }
BitmapResolver& Player::bitmapResolver() { return bitmap_resolver_; }
CursorManager& Player::cursorManager() { return cursor_manager_; }
InputHandler& Player::inputHandler() { return input_handler_; }
lingo::vm::LingoVM& Player::vm() { return vm_; }
lingo::builtin::BuiltinRegistry& Player::builtinRegistry() { return vm_.builtinRegistry(); }
lingo::builtin::BuiltinContext& Player::builtinContext() { return vm_.builtinContext(); }

void Player::receiveUpdate(const lingo::Datum& target) {
    if (target.isVoid()) {
        return;
    }
    if (std::find(updating_objects_.begin(), updating_objects_.end(), target) == updating_objects_.end()) {
        updating_objects_.push_back(target);
    }
}

void Player::removeUpdate(const lingo::Datum& target) {
    updating_objects_.erase(std::remove(updating_objects_.begin(), updating_objects_.end(), target),
                           updating_objects_.end());
}

PlayerState Player::state() const { return state_; }
int Player::currentFrame() const { return frame_context_.currentFrame(); }
int Player::effectiveFrame() const { return frame_context_.effectiveFrame(); }
int Player::frameCount() const { return frame_context_.frameCount(); }

int Player::getFrameForLabel(std::string_view label) const {
    return frame_context_.navigator().getFrameForLabel(label);
}

std::set<std::string> Player::getFrameLabels() const {
    return frame_context_.navigator().getFrameLabels();
}

int Player::tempo() const {
    const int puppet_tempo = movie_properties_.puppetTempo();
    if (puppet_tempo > 0) {
        return puppet_tempo;
    }
    if (file_ != nullptr) {
        const int score_tempo = file_->getScoreTempo(currentFrame() - 1);
        if (score_tempo > 0) {
            return score_tempo;
        }
    }
    return tempo_;
}

int Player::baseTempo() const { return tempo_; }

void Player::setTempo(int tempo) {
    tempo_ = tempo > 0 ? tempo : DEFAULT_TEMPO;
    input_state_.setCaretBlinkRate(tempo_);
}

void Player::setEventListener(std::function<void(const PlayerEventInfo&)> listener) {
    event_listener_ = std::move(listener);
}

void Player::setCastLoadedListener(std::function<void()> listener) {
    cast_loaded_listener_ = std::move(listener);
}

void Player::setExternalCastLoadListener(std::function<void(const ExternalCastLoadEvent&)> listener) {
    external_cast_load_listener_ = std::move(listener);
}

void Player::setErrorListener(ErrorListener listener) {
    error_listener_ = std::move(listener);
}

void Player::addExternalCastLoadHandler(ExternalCastLoadHandler* handler) {
    if (handler != nullptr) {
        external_cast_load_handlers_.push_back(handler);
    }
}

void Player::setDebugEnabled(bool enabled) {
    debug_enabled_ = enabled;
    vm_.builtinContext().debug_playback_enabled = enabled;
    frame_context_.setDebugEnabled(enabled);
    if (enabled) {
        dumpScriptInfo();
    }
}

void Player::setSlowHandlerWarningThresholdMs(std::int64_t milliseconds) {
    vm_.setSlowHandlerWarningThresholdMs(milliseconds);
}

void Player::setDebugController(std::shared_ptr<debug::DebugControllerApi> controller) {
    debug_controller_ = std::move(controller);
}

std::shared_ptr<debug::DebugControllerApi> Player::getDebugController() const {
    return debug_controller_;
}

void Player::setNetProvider(net::NetProvider* provider) {
    override_net_provider_ = provider;
    refreshBuiltinNetProvider();
}

void Player::setAudioBackend(audio::AudioBackend* backend) {
    sound_manager_.setBackend(backend);
}

void Player::setTextRenderer(render::output::TextRenderer* renderer) {
    sprite_baker_.setTextRenderer(renderer);
    cast_lib_manager_.setTextRenderer(renderer);
}

void Player::registerMultiuserXtra(lingo::xtra::MultiuserNetBridge& bridge) {
    xtra_manager_.registerXtra(std::make_unique<lingo::xtra::MultiuserXtra>(
        &bridge,
        [this](const lingo::Datum& target,
               const std::string& handler_name,
               const std::vector<lingo::Datum>& args) {
            try {
                if (target.type() == lingo::DatumType::ScriptInstanceRef &&
                    vm_.builtinContext().call_target_handler) {
                    (void)vm_.builtinContext().call_target_handler(target, handler_name, args);
                    return;
                }
                if (const auto* script_ref = target.asScriptRef();
                    script_ref != nullptr && vm_.builtinContext().call_target_handler) {
                    (void)vm_.builtinContext().call_target_handler(
                        lingo::Datum::scriptInstance("script", script_ref->member_ref),
                        handler_name,
                        args);
                    return;
                }
                (void)vm_.callHandler(handler_name, args);
            } catch (...) {
                // Match Java Player callback behavior: Xtra callbacks do not break frame polling.
            }
        },
        [this] {
            return externalCastsReady();
        }));
}

void Player::setExternalParams(std::vector<std::pair<std::string, std::string>> params) {
    external_params_ = std::move(params);
    vm_.builtinContext().external_params = external_params_;
}

const std::vector<std::pair<std::string, std::string>>& Player::externalParams() const {
    return external_params_;
}

void Player::setInitialBuiltinVariable(std::string variable_name, lingo::Datum default_value) {
    if (variable_name.empty()) {
        return;
    }
    for (auto& entry : initial_builtin_variables_) {
        if (entry.first == variable_name) {
            entry.second = default_value.deepCopy();
            return;
        }
    }
    initial_builtin_variables_.emplace_back(std::move(variable_name), default_value.deepCopy());
}

void Player::setInitialBuiltinVariables(std::vector<std::pair<std::string, lingo::Datum>> values) {
    initial_builtin_variables_.clear();
    for (auto& [name, value] : values) {
        setInitialBuiltinVariable(std::move(name), std::move(value));
    }
}

bool Player::debugEnabled() const { return debug_enabled_; }

void Player::dumpScriptInfo() const {
    dumpScriptInfo(std::cout);
}

void Player::dumpScriptInfo(std::ostream& out) const {
    if (file_ == nullptr) {
        out << "[Player] No file loaded\n";
        return;
    }

    out << "[Player] === Script Summary ===\n";
    out << "[Player] Total scripts: " << file_->scripts().size() << '\n';

    int movie_scripts = 0;
    int behaviors = 0;
    int parents = 0;
    int unknown = 0;

    for (const auto& script : file_->scripts()) {
        if (!script) {
            ++unknown;
            continue;
        }

        switch (script->scriptType()) {
            case chunks::ScriptChunkType::MovieScript:
                ++movie_scripts;
                break;
            case chunks::ScriptChunkType::Behavior:
                ++behaviors;
                break;
            case chunks::ScriptChunkType::Parent:
                ++parents;
                break;
            case chunks::ScriptChunkType::Score:
            case chunks::ScriptChunkType::Unknown:
                ++unknown;
                break;
        }

        if (script->resolvedScriptType() == chunks::ScriptChunkType::MovieScript) {
            auto names = file_->getScriptNamesForScript(script);
            out << "[Player] Movie script #" << script->id().value() << " handlers:\n";
            for (const auto& handler : script->handlers()) {
                out << "[Player]   - " << script->getHandlerName(handler, names.get()) << '\n';
            }
        }
    }

    out << "[Player] Movie scripts: " << movie_scripts << '\n';
    out << "[Player] Behaviors: " << behaviors << '\n';
    out << "[Player] Parent scripts: " << parents << '\n';
    out << "[Player] Unknown type: " << unknown << '\n';
    out << "[Player] ======================\n";
}

std::vector<lingo::vm::LingoVM::CallStackFrame> Player::getLingoCallStack() const {
    return vm_.callStack();
}

std::string Player::formatLingoCallStack() const {
    return vm_.formatCallStack();
}

std::string Player::getRecentScriptErrorMessage(std::int64_t max_age_ms) const {
    return recentScriptErrorIsFresh(max_age_ms) ? last_script_error_message_ : std::string();
}

std::string Player::getRecentScriptErrorStack(std::int64_t max_age_ms) const {
    return recentScriptErrorIsFresh(max_age_ms) ? last_script_error_stack_ : std::string();
}

bool Player::isVmRunning() const {
    return vm_running_.load();
}

void Player::play() {
    const bool was_stopped = state_ == PlayerState::Stopped;
    state_ = PlayerState::Playing;
    if (was_stopped) {
        legacy_timeout_armed_ = true;
        prepareMovieFoundation();
    }
}

void Player::playAsync(std::function<void()> on_ready) {
    if (state_ != PlayerState::Stopped) {
        state_ = PlayerState::Playing;
        if (on_ready) {
            on_ready();
        }
        return;
    }

    launchVmWorker([this] {
        prepareMovieFoundation();
        state_ = PlayerState::Playing;
    }, std::move(on_ready));
}

void Player::pause() {
    if (state_ == PlayerState::Playing) {
        state_ = PlayerState::Paused;
    }
}

void Player::resume() {
    if (state_ == PlayerState::Paused) {
        state_ = PlayerState::Playing;
    }
}

void Player::stop() {
    if (state_ == PlayerState::Stopped) {
        return;
    }

    if (timeout_system_event_dispatcher_) {
        timeout_system_event_dispatcher_("stopMovie");
    }
    eventDispatcher().dispatchToMovieScripts(PlayerEvent::StopMovie);
    frame_context_.reset();
    stage_renderer_.reset();
    timeout_manager_.clear();
    sound_manager_.stopAll();
    legacy_timeout_armed_ = true;
    state_ = PlayerState::Stopped;
}

void Player::shutdown() {
    stop();
    net_manager_.shutdown();
    if (debug_controller_) {
        debug_controller_->reset();
    }
    joinVmWorker();
    lingo::vm::dispatch::ImageMethodDispatcher::clearImageMutationCallback(this);
}

void Player::goToFrame(int frame) {
    frame_context_.goToFrame(frame);
}

void Player::goToLabel(std::string_view label) {
    frame_context_.goToLabel(label);
}

void Player::stepFrame() {
    if (state_ == PlayerState::Stopped) {
        prepareMovieFoundation();
        state_ = PlayerState::Paused;
    }

    executeFrameCycle(false);
}

void Player::stepFrameAsync(std::function<void()> on_complete) {
    launchVmWorker([this] {
        if (state_ == PlayerState::Stopped) {
            prepareMovieFoundation();
            state_ = PlayerState::Paused;
        }

        do {
            executeFrameCycle(false);
        } while (debug_controller_ && debug_controller_->isAwaitingStepContinuation());
    }, std::move(on_complete));
}

bool Player::tick() {
    if (state_ != PlayerState::Playing) {
        return state_ == PlayerState::Paused;
    }

    executeFrameCycle(true);
    return true;
}

void Player::tickAsync(std::function<void()> on_complete) {
    if (state_ != PlayerState::Playing) {
        return;
    }

    launchVmWorker([this] {
        do {
            executeFrameCycle(true);
        } while (debug_controller_ && debug_controller_->isAwaitingStepContinuation());
    }, std::move(on_complete));
}

bool Player::fireTestError(std::string_view error_message) {
    try {
        const bool handled = vm_.fireAlertHook("Script Error", error_message);
        vm_.flushDeferredTasks();
        return handled;
    } catch (...) {
        vm_.flushDeferredTasks();
        throw;
    }
}

void Player::executeFrameCycle(bool process_updates) {
    TickDeadlineGuard deadline_guard(vm_);
    refreshDebugControllerGlobals();
    (void)input_handler_.processInputEvents();
    input_state_.incrementCaretBlink();
    (void)frame_context_.executeFrame();

    // If the debug controller paused execution during this frame (breakpoint
    // hit, manual pause, or step completed), don't advance the frame or run
    // any more handlers.  This is essential for cooperative (non-blocking)
    // pause in WASM where we can't block the VM thread.
    if (debug_controller_ && debug_controller_->isPaused()) {
        vm_.flushDeferredTasks();
        return;
    }

    if (timeout_processor_) {
        timeout_processor_();
    }
    xtra_manager_.tickAll();
    sound_manager_.updateFades();
    sound_manager_.updateQueues();
    eventDispatcher().dispatchToMovieScripts(PlayerEvent::Idle);
    if (process_updates) {
        processUpdatingObjects();
    }
    vm_.flushDeferredTasks();
    (void)frame_context_.advanceFrame();
}

void Player::onNetFetchComplete(std::string_view url, const std::vector<std::uint8_t>& data) {
    if (url.empty()) {
        return;
    }

    const std::string url_value(url);
    cast_lib_manager_.cacheExternalData(url_value, data);

    std::string lower_url(url_value);
    std::transform(lower_url.begin(), lower_url.end(), lower_url.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    const auto query_index = lower_url.find('?');
    if (query_index != std::string::npos && query_index > 0) {
        lower_url = lower_url.substr(0, query_index);
    }
    if (!lower_url.ends_with(".cct") && !lower_url.ends_with(".cst")) {
        return;
    }

    handleExternalCastFetch(url_value, data);
}

int Player::preloadAllCasts() {
    int count = 0;
    std::set<std::string> requested_files;
    for (const auto& [_, cast_lib] : cast_lib_manager_.castLibs()) {
        if (!cast_lib || !cast_lib->isExternal() || cast_lib->isLoaded() || cast_lib->isFetching()) {
            continue;
        }
        const std::string file_name = util::getFileName(cast_lib->fileName());
        if (file_name.empty()) {
            continue;
        }
        cast_lib->markFetching();
        // Director movies commonly bind a large number of placeholder cast
        // slots to the same empty.cct/empty.cst file.  They are one network
        // resource, not one request per slot; loading each duplicate causes
        // the VM to spend startup processing the same cast repeatedly and
        // can starve the movie's own initialization and login requests.
        if (requested_files.insert(lowerAscii(file_name)).second) {
            (void)activeNetProvider().preloadNetThing(file_name);
        }
        ++count;
    }
    return count;
}

bool Player::externalCastsReady() const {
    for (const auto& [_, cast_lib] : cast_lib_manager_.castLibs()) {
        // A Failed cast (fetched data that did not parse) is settled empty
        // and must not keep the gate closed the way an in-flight cast does.
        if (cast_lib && cast_lib->isExternal() && !cast_lib->isSettled() && !cast_lib->fileName().empty()) {
            return false;
        }
    }
    return !activeNetProvider().hasPendingCastLoads();
}

bool Player::networkReady() const {
    const bool casts_ready = externalCastsReady();
    bool has_unloaded_external_cast = false;
    for (const auto& [_, cast_lib] : cast_lib_manager_.castLibs()) {
        if (cast_lib && cast_lib->isExternal() && !cast_lib->isSettled() && !cast_lib->fileName().empty()) {
            has_unloaded_external_cast = true;
            break;
        }
    }
    if (!casts_ready || !has_unloaded_external_cast) {
        external_cast_load_observed_ = true;
    }
    // The multiuser handshake only gates readiness after the movie has asked
    // for a text connection. Before that request (for example while a replay
    // is still on the navigator) there is nothing to wait for, and blocking
    // here would stop the replay from delivering the click that enters a room.
    if (socket_multiuser_bridge_ != nullptr &&
        socket_multiuser_bridge_->hasRequestedTextConnection() &&
        !socket_multiuser_bridge_->hasCompletedHandshake()) {
        return false;
    }
    if (socket_multiuser_bridge_ == nullptr) {
        return casts_ready;
    }

    const bool ready = casts_ready && external_cast_load_observed_;
    return ready;
}

void Player::onSynchronousExternalCastLoad(int cast_lib_number) {
    if (cast_lib_number <= 0) {
        return;
    }

    bitmap_cache_.clear();
    vm_.invalidateHandlerCache();
    stage_renderer_.spriteRegistry().bumpRevision();
    frame_context_.rebindBehaviorsForLoadedCast(cast_lib_number);
    notifyExternalCastLoaded(cast_lib_number);
}

bool Player::loadExternalCastFromCachedData(int cast_lib_number, const std::vector<std::uint8_t>& data) {
    return loadExternalCastFromCachedData(cast_lib_number, data, {});
}

bool Player::loadExternalCastFromCachedData(int cast_lib_number,
                                            const std::vector<std::uint8_t>& data,
                                            std::function<void()> after_load) {
    if (cast_lib_number <= 0 || data.empty()) {
        return false;
    }
    TickDeadlineGuard deadline_guard(vm_);
    return applyExternalCastDataNow(cast_lib_number, data, std::move(after_load));
}

bool Player::applyExternalCastDataNow(int cast_lib_number,
                                      const std::vector<std::uint8_t>& data,
                                      std::function<void()> after_load) {
    if (!cast_lib_manager_.setExternalCastData(cast_lib_number, data)) {
        return false;
    }
    onSynchronousExternalCastLoad(cast_lib_number);
    if (after_load) {
        after_load();
    }
    return true;
}

void Player::notifyExternalCastLoaded(int cast_lib_number) {
    auto cast_lib = cast_lib_manager_.getCastLib(cast_lib_number);
    if (!cast_lib) {
        return;
    }

    const std::string file_name = cast_lib->fileName();
    debug::LifecycleDiagnostics::logExternalCastLoaded(cast_lib_number, file_name);
    ExternalCastLoadEvent event{cast_lib_number, file_name};
    for (auto* handler : external_cast_load_handlers_) {
        if (handler != nullptr) {
            handler->onExternalCastLoaded(*this, cast_lib_number, file_name);
        }
    }
    if (external_cast_load_listener_) {
        external_cast_load_listener_(event);
    }
    if (cast_loaded_listener_) {
        cast_loaded_listener_();
    }
    // Runtime scripts can create additional external cast slots while an
    // earlier cast is being applied. Pick those slots up on the same load
    // boundary instead of waiting for an unrelated caller to preload them.
    (void)preloadAllCasts();
}

void Player::handleTraceError(std::string_view message, std::string_view error_detail) {
    last_script_error_time_ms_ = currentTimeMillis();
    last_script_error_message_ = std::string(message);
    last_script_error_stack_ = vm_.hasActiveCallStack() ? vm_.formatCallStack() : std::string();
    if (error_listener_) {
        error_listener_(last_script_error_message_, error_detail);
    }
}

bool Player::recentScriptErrorIsFresh(std::int64_t max_age_ms) const {
    if (max_age_ms < 0) {
        max_age_ms = 0;
    }
    if (last_script_error_time_ms_ <= 0) {
        return false;
    }
    return currentTimeMillis() - last_script_error_time_ms_ <= max_age_ms;
}

render::pipeline::FrameSnapshot Player::frameSnapshot() {
    const int frame = currentFrame();
    auto snapshot = frame_render_pipeline_.renderFrame(
        frame,
        stage_renderer_.stageWidth(),
        stage_renderer_.stageHeight(),
        stage_renderer_.backgroundColor(),
        stage_renderer_.renderableStageImage(),
        "Frame " + std::to_string(frame) + " | " + std::string(name(state_)));
    // Captured here so hosts composing on other threads read a consistent
    // caret/selection state frozen at snapshot time.
    snapshot.editable_overlay = input_handler_.editableFieldOverlay();
    return snapshot;
}

lingo::Datum Player::currentStageImageDatum() {
    const int frame = currentFrame();
    auto snapshot = frame_render_pipeline_.renderFrame(
        frame,
        stage_renderer_.stageWidth(),
        stage_renderer_.stageHeight(),
        stage_renderer_.backgroundColor(),
        stage_renderer_.renderableStageImage(),
        "Stage image " + std::to_string(frame));
    auto composed = snapshot.renderFrame();
    return lingo::Datum::imageRef(stage_renderer_.updateStageImageSnapshot(composed));
}

void Player::wireComponents() {
    frame_context_.setSpriteRegistry(&stage_renderer_.spriteRegistry());
    frame_context_.setEventListener([this](const frame::FrameEvent& event) {
        if (event_listener_) {
            event_listener_(PlayerEventInfo{event.event, event.frame, 0});
        }
        if (event.event == PlayerEvent::EnterFrame) {
            stage_renderer_.onFrameEnter(event.frame);
        }
    });
    frame_context_.setScriptNameResolver([this](const std::shared_ptr<chunks::ScriptChunk>& script) {
        if (!script) {
            return std::string();
        }
        if (auto* owner = const_cast<DirectorFile*>(script->file())) {
            return owner->getScriptName(script);
        }
        return file_ != nullptr ? file_->getScriptName(script) : std::string();
    });
    behaviorManager().setFrameScriptResolver([this](const score::ScoreBehaviorRef& behavior_ref)
        -> std::shared_ptr<chunks::ScriptChunk> {
        const int cast_lib = behavior_ref.castLib() > 0 ? behavior_ref.castLib() : 1;
        const int member = behavior_ref.castMember();
        if (member <= 0) {
            return nullptr;
        }
        auto library = cast_lib_manager_.getCastLib(cast_lib);
        if (library && library->isLoaded()) {
            if (auto script = library->getScript(member)) {
                return script;
            }
        }
        return nullptr;
    });
    cast_lib_manager_.setMemberSlotRetiredCallback([this](int cast_lib, int member_num) {
        (void)stage_renderer_.spriteRegistry().clearDynamicMemberBindings(cast_lib, member_num);
    });
    lingo::vm::dispatch::ImageMethodDispatcher::setImageMutationCallback(this, [this] {
        stage_renderer_.spriteRegistry().bumpRevision();
    });

    if (file_ != nullptr && !file_->basePath().empty()) {
        net_manager_.setBasePath(file_->basePath());
    }
    net_manager_.setCompletionCallback([this](const std::string& url, const std::vector<std::uint8_t>& data) {
        handleExternalCastFetch(url, data);
    });

    movie_properties_.setInputState(&input_state_);
    movie_properties_.setEffectiveFrameSupplier([this] {
        return effectiveFrame();
    });
    movie_properties_.setFrameCountSupplier([this] {
        return frameCount();
    });
    movie_properties_.setTempoSupplier([this] {
        return tempo();
    });
    movie_properties_.setTempoSetter([this](int value) {
        setTempo(value);
    });
    movie_properties_.setCastLibCountSupplier([this] {
        return cast_lib_manager_.getCastLibCount();
    });
    movie_properties_.setStageBackgroundColorSupplier([this] {
        return stage_renderer_.backgroundColor();
    });
    movie_properties_.setStageBackgroundColorSetter([this](int rgb) {
        stage_renderer_.setBackgroundColor(rgb);
    });
    movie_properties_.setStageImageSupplier([this] {
        return currentStageImageDatum();
    });
    movie_properties_.setGoToFrameHandler([this](int frame) {
        goToFrame(frame);
    });
    movie_properties_.setGoToLabelHandler([this](const std::string& label) {
        goToLabel(label);
    });
    movie_properties_.setFrameForLabelResolver([this](const std::string& label) {
        return std::max(navigator().getFrameForLabel(label), 0);
    });
    movie_properties_.setMarkerFrameResolver([this](int offset) {
        return navigator().getMarkerFrame(currentFrame(), offset);
    });
    movie_properties_.setGotoNetPageHandler([this](const std::string& url, const std::string& target) {
        (void)target;
        (void)activeNetProvider().preloadNetThing(url);
    });
    movie_properties_.setGotoNetMovieHandler([this](const std::string& url) {
        return activeNetProvider().preloadNetThing(url);
    });

    sprite_properties_.setMemberInfoResolver([this](int cast_lib,
                                                   int member_num) -> std::optional<SpriteProperties::MemberInfo> {
        auto member = cast_lib_manager_.resolveMember(cast_lib, member_num);
        if (!member) {
            return std::nullopt;
        }
        SpriteProperties::MemberInfo info;
        info.width = member->width();
        info.height = member->height();
        info.reg_x = member->regX();
        info.reg_y = member->regY();
        info.bitmap = member->isBitmap();
        info.bitmap_width = member->width();
        info.bitmap_height = member->height();
        info.bitmap_reg_x = member->regX();
        info.bitmap_reg_y = member->regY();
        info.has_image = member->isBitmap();
        return info;
    });
    sprite_properties_.setMemberNameResolver(
        [this](const std::string& member_name) -> std::optional<lingo::Datum::CastMemberRef> {
            auto member = cast_lib_manager_.findCastMemberByName(member_name);
            if (!member) {
                return std::nullopt;
            }
            return lingo::Datum::CastMemberRef::of(
                id::CastLibId(member->castLib()),
                id::MemberId(member->memberNum())
            );
        });

    stage_renderer_.setCastMemberResolver([this](int cast_lib, int member_num)
        -> std::shared_ptr<const ::libreshockwave::cast::CastMember> {
        return cast_lib_manager_.resolveMember(cast_lib, member_num);
    });

    sprite_baker_.setBitmapDecodeProvider([this](const chunks::CastMemberChunk& member,
                                                const bitmap::Palette* palette) {
        return bitmap_resolver_.decodeBitmapForProvider(member, palette);
    });
    sprite_baker_.setLiveBitmapProvider([this](const render::pipeline::RenderSprite& sprite)
        -> std::shared_ptr<const bitmap::Bitmap> {
        const auto discard_decode_placeholder = [this, &sprite](
            const std::shared_ptr<const ::libreshockwave::cast::CastMember>& member) {
            if (member == nullptr ||
                (!member->hasRuntimeBitmapDecodePlaceholder() &&
                 !member->shouldRedecodeAuthoredRuntimeBitmap())) {
                return false;
            }
            if (const auto cast_member = sprite.castMember()) {
                sprite_baker_.bitmapCache().invalidateMember(*cast_member);
            }
            return true;
        };
        if (auto dynamic_member = sprite.dynamicMember()) {
            if (discard_decode_placeholder(dynamic_member)) {
                return nullptr;
            }
            return dynamic_member->runtimeBitmap();
        }
        if (auto member = sprite.castMember()) {
            if (auto runtime_member = cast_lib_manager_.findRuntimeMember(
                    std::const_pointer_cast<chunks::CastMemberChunk>(member))) {
                if (discard_decode_placeholder(runtime_member)) {
                    return nullptr;
                }
                return runtime_member->runtimeBitmap();
            }
        }
        return nullptr;
    });
    sprite_baker_.setPaletteVersionProvider([this](const render::pipeline::RenderSprite& sprite)
        -> std::optional<int> {
        if (auto member = sprite.castMember()) {
            if (auto runtime_member = cast_lib_manager_.findRuntimeMember(
                    std::const_pointer_cast<chunks::CastMemberChunk>(member))) {
                return runtime_member->paletteVersion();
            }
        }
        return std::nullopt;
    });

    sound_manager_.setAudioResolver([this](const lingo::Datum::CastMemberRef& ref)
        -> std::optional<std::vector<std::uint8_t>> {
        auto cast_lib = cast_lib_manager_.getCastLib(ref.cast_lib);
        if (!cast_lib) {
            return std::nullopt;
        }
        auto source_file = cast_lib->sourceFile();
        if (!source_file) {
            return std::nullopt;
        }
        auto member = cast_lib->findMemberByNumber(ref.memberNum());
        if (!member) {
            return std::nullopt;
        }
        auto sound = audio::SoundManager::findSoundForMember(*source_file, member);
        return sound ? audio::SoundManager::convertSoundToPlayable(*sound) : std::nullopt;
    });

    cursor_manager_.setSpriteProvider([this] {
        auto sprites = stage_renderer_.lastBakedSprites();
        if (sprites.empty()) {
            sprites = stage_renderer_.getSpritesForFrame(currentFrame());
        }
        return sprites;
    });
    cursor_manager_.setMemberInfoResolver([this](int cast_lib,
                                                int member_num) -> std::optional<CursorManager::MemberInfo> {
        auto member = cast_lib_manager_.resolveMember(cast_lib, member_num);
        if (!member) {
            return std::nullopt;
        }
        CursorManager::MemberInfo info;
        info.member_type = member->memberType();
        info.editable = member->isText();
        info.reg_x = member->regX();
        info.reg_y = member->regY();
        return info;
    });
    cursor_manager_.setBitmapResolver([this](int cast_lib, int member_num) {
        return bitmap_resolver_.decodeBitmap(cast_lib_manager_.getCastMember(cast_lib, member_num));
    });
    cursor_manager_.setInteractivePredicate([this](int channel) {
        return eventDispatcher().isSpriteMouseInteractive(channel);
    });
    cursor_manager_.setGlobalCursorSupplier([this] {
        return movie_properties_.getMovieProp("cursor");
    });
    movie_properties_.setXtraNamesSupplier([this] {
        return xtra_manager_.registeredXtraNames();
    });
    movie_properties_.setSoundEnabledSupplier([this] {
        return sound_manager_.isEnabled() ? 1 : 0;
    });
    movie_properties_.setSoundEnabledSetter([this](int enabled) {
        sound_manager_.setEnabled(enabled != 0);
    });
    movie_properties_.setSoundLevelSupplier([this] {
        return sound_manager_.getSoundLevel();
    });
    movie_properties_.setSoundLevelSetter([this](int level) {
        sound_manager_.setSoundLevel(level);
    });
    movie_properties_.setSoundKeepDeviceSupplier([this] {
        return sound_manager_.soundKeepDevice() ? 1 : 0;
    });
    movie_properties_.setSoundKeepDeviceSetter([this](int keep_device) {
        sound_manager_.setSoundKeepDevice(keep_device != 0);
    });
    movie_properties_.setSoundMixMediaSupplier([this] {
        return sound_manager_.soundMixMedia() ? 1 : 0;
    });
    movie_properties_.setSoundMixMediaSetter([this](int mix_media) {
        sound_manager_.setSoundMixMedia(mix_media != 0);
    });

    input_handler_.setCurrentFrameSupplier([this] {
        return currentFrame();
    });
    input_handler_.setEventDispatcherSupplier([this] {
        return &eventDispatcher();
    });
    input_handler_.setSpriteLocationSetter([this](int channel, int loc_h, int loc_v) {
        return sprite_properties_.setSpriteProp(channel, "loc", lingo::Datum::intPoint(loc_h, loc_v));
    });

    auto& context = vm_.builtinContext();
    context.movie_properties = &movie_properties_;
    refreshBuiltinNetProvider();
    context.sound_manager = &sound_manager_;
    context.sprite_properties = &sprite_properties_;
    context.timeout_manager = &timeout_manager_;
    context.external_params = external_params_;
    context.debug_playback_enabled = debug_enabled_;
    cast_lib_manager_.installBuiltinCallbacks(context);
    context.xtra_registered_resolver = [this](const std::string& name) {
        return xtra_manager_.isXtraRegistered(name);
    };
    context.xtra_instance_creator = [this](const std::string& name, const std::vector<lingo::Datum>& args) {
        return xtra_manager_.createInstance(name, args);
    };
    context.xtra_handler = [this](const lingo::Datum::XtraInstance& instance,
                                 const std::string& handler_name,
                                 const std::vector<lingo::Datum>& args) {
        return xtra_manager_.callHandler(instance, handler_name, args);
    };
    context.xtra_property_getter = [this](const lingo::Datum::XtraInstance& instance,
                                        const std::string& property_name) {
        return xtra_manager_.getProperty(instance, property_name);
    };
    context.xtra_property_setter = [this](const lingo::Datum::XtraInstance& instance,
                                        const std::string& property_name,
                                        const lingo::Datum& value) {
        xtra_manager_.setProperty(instance, property_name, value);
    };
    context.sprite_method_handler = [this](int channel,
                                         const std::string& method_name,
                                         const std::vector<lingo::Datum>& args) {
        return sprite_properties_.callSpriteMethod(channel, method_name, args);
    };
    context.image_palette_resolver = [this](const lingo::Datum& palette_ref)
        -> std::optional<lingo::builtin::BuiltinContext::ResolvedPalette> {
        const auto* member = palette_ref.asCastMemberRef();
        if (member == nullptr || member->memberNum() <= 0) {
            return std::nullopt;
        }

        const int cast_lib = member->cast_lib > 0 ? member->cast_lib : 1;
        auto palette = bitmap_resolver_.resolvePaletteByMember(cast_lib, member->memberNum());
        if (palette == nullptr) {
            return std::nullopt;
        }
        return lingo::builtin::BuiltinContext::ResolvedPalette{
            palette,
            lingo::Datum::CastMemberRef::of(id::CastLibId(cast_lib), id::MemberId(member->memberNum())),
            std::nullopt
        };
    };

    auto handler_lookup_cache =
        std::make_shared<std::unordered_map<HandlerLookupKey, lingo::vm::HandlerRef, HandlerLookupKeyHash>>();
    auto script_handler_finder_cache =
        std::make_shared<std::unordered_map<
            ScriptHandlerFinderKey,
            std::optional<lingo::builtin::BuiltinContext::ScriptHandlerLocation>,
            ScriptHandlerFinderKeyHash>>();
    auto script_ref_target_cache = std::make_shared<std::unordered_map<std::uint64_t, ResolvedScriptTarget>>();
    auto script_instance_target_cache = std::make_shared<std::unordered_map<std::uint64_t, ResolvedScriptTarget>>();

    auto find_handler_in_script = [this, handler_lookup_cache](const std::shared_ptr<chunks::ScriptChunk>& script,
                                                          const std::shared_ptr<const chunks::ScriptNamesChunk>& script_names,
                                                          const std::shared_ptr<const DirectorFile>& script_owner,
                                                          chunks::ScriptChunkType script_type,
                                                          std::string_view handler_name)
        -> std::optional<lingo::vm::HandlerRef> {
        if (!script) {
            return std::nullopt;
        }
        const HandlerLookupKey key{script.get(), script_names.get(), lowerAscii(handler_name)};
        if (const auto cached = handler_lookup_cache->find(key); cached != handler_lookup_cache->end()) {
            return cached->second;
        }
        if (script_names) {
            if (auto handler = script->findHandlerPtr(handler_name, script_names.get())) {
                auto ref = lingo::vm::HandlerRef{script.get(), handler, script, script_owner, script_names, script_type};
                handler_lookup_cache->emplace(std::move(key), ref);
                return ref;
            }
        }
        if (auto handler = vm_.findHandler(*script, handler_name)) {
            handler->script_owner = script;
            handler->script_type = script_type;
            if (script_owner) {
                handler->file_owner = script_owner;
            }
            if (script_names) {
                handler->script_names_owner = script_names;
            }
            auto ref = *handler;
            handler_lookup_cache->emplace(std::move(key), ref);
            return ref;
        }
        return std::nullopt;
    };

    auto resolve_script_ref = [this, script_ref_target_cache](int cast_lib, int member_num)
        -> std::optional<ResolvedScriptTarget> {
        cast_lib = cast_lib > 0 ? cast_lib : 1;
        if (member_num <= 0) {
            return std::nullopt;
        }

        const auto cache_key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cast_lib)) << 32U) |
                              static_cast<std::uint32_t>(member_num);
        if (const auto cached = script_ref_target_cache->find(cache_key); cached != script_ref_target_cache->end()) {
            return cached->second;
        }

        if (auto cast_lib_ref = cast_lib_manager_.getCastLib(cast_lib)) {
            if (auto resolved_script = cast_lib_ref->getScript(member_num)) {
                const auto source = cast_lib_ref->sourceFile();
                auto resolved = ResolvedScriptTarget{
                    resolved_script,
                    source ? source->getScriptNamesForScript(resolved_script) : cast_lib_ref->scriptNames(),
                    source,
                    cast_lib_ref->scriptTypeForScript(resolved_script)
                };
                script_ref_target_cache->emplace(cache_key, resolved);
                return resolved;
            }
        }

        if (file_ != nullptr) {
            if (auto member = file_->getCastMemberByNumber(cast_lib, member_num); member && member->isScript()) {
                if (auto resolved_script = file_->getScriptForCastMember(member, file_->getMappedCastChunk(cast_lib))) {
                    const auto script_type = resolved_script->resolvedScriptType();
                    auto resolved = ResolvedScriptTarget{
                        resolved_script,
                        file_->getScriptNamesForScript(resolved_script),
                        file_,
                        script_type
                    };
                    script_ref_target_cache->emplace(cache_key, resolved);
                    return resolved;
                }
            }
        }

        return std::nullopt;
    };

    auto canonical_script_ref_candidates = [this](int cast_lib, int member_num)
        -> std::vector<std::pair<int, int>> {
        cast_lib = cast_lib > 0 ? cast_lib : 1;
        std::vector<std::pair<int, int>> candidates;
        if (member_num <= 0) {
            return candidates;
        }

        const auto member_name = cast_lib_manager_.getMemberProp(cast_lib, member_num, "name").stringValue();
        if (!member_name.empty() && member_name != "VOID") {
            if (const auto* canonical = cast_lib_manager_.getMemberByName(0, member_name).asCastMemberRef()) {
                const int canonical_cast_lib = canonical->cast_lib > 0 ? canonical->cast_lib : 1;
                const int canonical_member = canonical->memberNum();
                if (canonical_member > 0 && (canonical_cast_lib != cast_lib || canonical_member != member_num)) {
                    candidates.emplace_back(canonical_cast_lib, canonical_member);
                }
            }
        }

        candidates.emplace_back(cast_lib, member_num);
        return candidates;
    };

    auto resolve_script_instance = [resolve_script_ref, script_instance_target_cache](
        const lingo::Datum::ScriptInstanceRef& instance)
        -> std::optional<ResolvedScriptTarget> {
        if (const auto cached = script_instance_target_cache->find(instance.identityId());
            cached != script_instance_target_cache->end()) {
            return cached->second;
        }
        const auto& script_ref = instance.scriptRef();
        if (!script_ref.has_value()) {
            return std::nullopt;
        }
        auto resolved = resolve_script_ref(script_ref->cast_lib, script_ref->memberNum());
        if (resolved.has_value()) {
            script_instance_target_cache->emplace(instance.identityId(), *resolved);
        }
        return resolved;
    };

    auto find_event_handler = [this, find_handler_in_script, resolve_script_ref, resolve_script_instance, canonical_script_ref_candidates](
        const event::EventTarget& target,
        std::string_view handler_name)
        -> std::optional<lingo::vm::HandlerRef> {
        if (target.behavior) {
            const auto& behavior_ref = target.behavior->behaviorRef();
            if (auto resolved = resolve_script_ref(behavior_ref.castLib(), behavior_ref.castMember());
                resolved && resolved->script == target.script) {
                if (auto handler = find_handler_in_script(resolved->script,
                                                       resolved->script_names,
                                                       resolved->script_owner,
                                                       resolved->script_type,
                                                       handler_name)) {
                    return handler;
                }
            }
        }

        if (auto handler = find_handler_in_script(target.script,
                                               target.script_names,
                                               target.script_owner,
                                               target.script_type,
                                               handler_name)) {
            return handler;
        }

        if (!target.script_instance || target.script_instance->type() != lingo::DatumType::ScriptInstanceRef) {
            return std::nullopt;
        }

        const auto* current = &target.script_instance->scriptInstanceValue();
        for (int depth = 0; current != nullptr && depth < lingo::vm::util::MAX_ANCESTOR_DEPTH; ++depth) {
            if (const auto& script_ref = current->scriptRef(); script_ref.has_value()) {
                for (const auto& [candidate_cast_lib, candidate_member] :
                     canonical_script_ref_candidates(script_ref->cast_lib, script_ref->memberNum())) {
                    if (auto resolved = resolve_script_ref(candidate_cast_lib, candidate_member)) {
                        if (auto handler = find_handler_in_script(resolved->script,
                                                               resolved->script_names,
                                                               resolved->script_owner,
                                                               resolved->script_type,
                                                               handler_name)) {
                            return handler;
                        }
                    }
                }
            } else if (auto resolved = resolve_script_instance(*current)) {
                if (auto handler = find_handler_in_script(resolved->script,
                                                       resolved->script_names,
                                                       resolved->script_owner,
                                                       resolved->script_type,
                                                       handler_name)) {
                    return handler;
                }
            }

            auto ancestor = current->ancestor();
            current = ancestor ? ancestor.get() : nullptr;
        }

        return std::nullopt;
    };

    context.script_handler_finder = [find_handler_in_script, resolve_script_ref, canonical_script_ref_candidates, script_handler_finder_cache](
        int cast_lib,
        int member_num,
        const std::string& handler_name)
        -> std::optional<lingo::builtin::BuiltinContext::ScriptHandlerLocation> {
        const ScriptHandlerFinderKey key{cast_lib > 0 ? cast_lib : 1, member_num, lowerAscii(handler_name)};
        if (const auto cached = script_handler_finder_cache->find(key); cached != script_handler_finder_cache->end()) {
            return cached->second;
        }

        std::optional<lingo::builtin::BuiltinContext::ScriptHandlerLocation> result;
        for (const auto& [candidate_cast_lib, candidate_member] : canonical_script_ref_candidates(cast_lib, member_num)) {
            const auto resolved = resolve_script_ref(candidate_cast_lib, candidate_member);
            if (!resolved.has_value()) {
                continue;
            }
            const auto handler = find_handler_in_script(resolved->script,
                                                     resolved->script_names,
                                                     resolved->script_owner,
                                                     resolved->script_type,
                                                     handler_name);
            if (!handler || handler->script == nullptr || handler->handler == nullptr) {
                continue;
            }
            result = lingo::builtin::BuiltinContext::ScriptHandlerLocation{
                handler->script,
                handler->handler,
                handler->script_owner,
                handler->file_owner,
                handler->script_names_owner,
                handler->script_type
            };
            break;
        }
        script_handler_finder_cache->emplace(std::move(key), result);
        return result;
    };

    auto execute_target = [this, find_event_handler](const event::EventTarget& target,
                                                  std::string_view handler_name,
                                                  const std::vector<lingo::Datum>& args)
        -> std::optional<std::pair<lingo::Datum, bool>> {
        auto handler = find_event_handler(target, handler_name);
        if (!handler || handler->script == nullptr) {
            return std::nullopt;
        }
        lingo::Datum receiver = lingo::Datum::voidValue();
        if (target.behavior) {
            receiver = target.behavior->toDatum();
        } else if (target.script_instance) {
            receiver = *target.script_instance;
        }

        bool passed = false;
        vm_.resetEventStopped();
        vm_.setPassCallback([this, &passed] {
            passed = true;
            eventDispatcher().pass();
        });
        lingo::Datum result = lingo::Datum::voidValue();
        try {
            result = vm_.executeHandler(*handler, args, receiver);
        } catch (...) {
            vm_.clearPassCallback();
            vm_.resetEventStopped();
            throw;
        }
        vm_.clearPassCallback();
        if (vm_.eventStopped()) {
            eventDispatcher().stopEvent();
            vm_.resetEventStopped();
        }
        return std::make_pair(result, passed);
    };

    auto invoke_target = [execute_target](const event::EventTarget& target,
                                        std::string_view handler_name,
                                        const std::vector<lingo::Datum>& args) {
        auto result = execute_target(target, handler_name, args);
        if (!result.has_value()) {
            return event::HandlerResult{false, false};
        }
        return event::HandlerResult{true, result->second};
    };

    context.call_target_handler = [this, execute_target](const lingo::Datum& target,
                                                      const std::string& handler_name,
                                                      const std::vector<lingo::Datum>& args) {
        if (target.type() == lingo::DatumType::ScriptInstanceRef) {
            event::EventTarget event_target;
            event_target.kind = event::EventTargetKind::ScriptInstance;
            event_target.script_instance = target;
            const auto result = execute_target(event_target, handler_name, args);
            return result.has_value() ? result->first : lingo::Datum::voidValue();
        }

        int channel = 0;
        if (const auto* sprite = target.asSpriteRef()) {
            channel = sprite->channel;
        } else if (target.isInt() || target.isFloat()) {
            channel = target.intValue();
        }
        if (channel <= 0) {
            return lingo::Datum::voidValue();
        }

        const auto script_instances = sprite_properties_.getScriptInstanceList(channel);
        if (!script_instances.has_value()) {
            return sprite_properties_.callSpriteMethod(channel, handler_name, args);
        }

        lingo::Datum last_result = lingo::Datum::voidValue();
        for (const auto& script_instance : *script_instances) {
            if (script_instance.type() != lingo::DatumType::ScriptInstanceRef) {
                continue;
            }
            event::EventTarget event_target;
            event_target.kind = event::EventTargetKind::ScriptInstance;
            event_target.channel = channel;
            event_target.script_instance = script_instance;
            const auto result = execute_target(event_target, handler_name, args);
            if (result.has_value()) {
                last_result = result->first;
            }
        }
        return last_result;
    };
    context.send_all_sprites_handler = [this](const std::string& handler_name,
                                           const std::vector<lingo::Datum>& args) {
        std::vector<int> channels;
        channels.reserve(stage_renderer_.spriteRegistry().getAll().size());
        for (const auto& [channel, sprite] : stage_renderer_.spriteRegistry().getAll()) {
            if (sprite != nullptr) {
                channels.push_back(channel);
            }
        }
        std::ranges::sort(channels);

        for (const int channel : channels) {
            eventDispatcher().dispatchSpriteEvent(channel, handler_name, args);
        }
        return lingo::Datum::voidValue();
    };
    context.alert_hook_handler = [this, execute_target](const std::string& alert_type, const std::string& text) {
        const auto& hook = movie_properties_.alertHook();
        if (hook.type() != lingo::DatumType::ScriptInstanceRef) {
            return false;
        }

        event::EventTarget event_target;
        event_target.kind = event::EventTargetKind::ScriptInstance;
        event_target.script_instance = hook;
        try {
            const auto result = execute_target(
                event_target,
                "alertHook",
                {lingo::Datum::of(alert_type), lingo::Datum::of(text)});
            return result.has_value() && result->first.boolValue();
        } catch (...) {
            return false;
        }
    };

    auto collect_movie_script_targets = [this] {
        std::vector<event::EventDispatcher::MovieScriptTarget> targets;
        if (file_ != nullptr) {
            targets.reserve(file_->scripts().size());
            for (const auto& script : file_->scripts()) {
                const auto script_type = script ? script->resolvedScriptType() : chunks::ScriptChunkType::Unknown;
                if (!script || script_type != chunks::ScriptChunkType::MovieScript) {
                    continue;
                }
                targets.push_back(event::EventDispatcher::MovieScriptTarget{
                    script,
                    file_->getScriptNamesForScript(script),
                    file_,
                    script_type
                });
            }
        }

        for (const auto& [_, cast_lib] : cast_lib_manager_.castLibs()) {
            if (!cast_lib || !cast_lib->isExternal() || !cast_lib->isLoaded()) {
                continue;
            }
            const auto source = cast_lib->sourceFile();
            const auto default_names = cast_lib->scriptNames();
            for (const auto& script : cast_lib->allScripts()) {
                const auto script_type = cast_lib->scriptTypeForScript(script);
                if (!script || script_type != chunks::ScriptChunkType::MovieScript) {
                    continue;
                }
                targets.push_back(event::EventDispatcher::MovieScriptTarget{
                    script,
                    source ? source->getScriptNamesForScript(script) : default_names,
                    source,
                    script_type
                });
            }
        }
        return targets;
    };

    auto collect_global_handler_targets = [this] {
        std::vector<event::EventDispatcher::MovieScriptTarget> targets;
        if (file_ != nullptr) {
            targets.reserve(file_->scripts().size());
            for (const auto& script : file_->scripts()) {
                const auto script_type = script ? script->resolvedScriptType() : chunks::ScriptChunkType::Unknown;
                if (!script || !lingo::vm::LingoVM::isGlobalHandlerScriptType(script_type)) {
                    continue;
                }
                targets.push_back(event::EventDispatcher::MovieScriptTarget{
                    script,
                    file_->getScriptNamesForScript(script),
                    file_,
                    script_type
                });
            }
        }

        for (const auto& [_, cast_lib] : cast_lib_manager_.castLibs()) {
            if (!cast_lib || !cast_lib->isExternal() || !cast_lib->isLoaded()) {
                continue;
            }
            const auto source = cast_lib->sourceFile();
            const auto default_names = cast_lib->scriptNames();
            for (const auto& script : cast_lib->allScripts()) {
                const auto script_type = cast_lib->scriptTypeForScript(script);
                if (!script || !lingo::vm::LingoVM::isGlobalHandlerScriptType(script_type)) {
                    continue;
                }
                targets.push_back(event::EventDispatcher::MovieScriptTarget{
                    script,
                    source ? source->getScriptNamesForScript(script) : default_names,
                    source,
                    script_type
                });
            }
        }
        return targets;
    };

    vm_.setGlobalHandlerFinder([this, collect_global_handler_targets](std::string_view handler_name)
        -> std::optional<lingo::vm::HandlerRef> {
        for (const auto& target : collect_global_handler_targets()) {
            if (!target.script) {
                continue;
            }
            if (target.script_names) {
                if (auto handler = target.script->findHandlerPtr(handler_name, target.script_names.get())) {
                    return lingo::vm::HandlerRef{
                        target.script.get(),
                        handler,
                        target.script,
                        target.script_owner,
                        target.script_names,
                        target.script_type
                    };
                }
            }
            if (auto handler = vm_.findHandler(*target.script, handler_name)) {
                handler->script_owner = target.script;
                handler->script_type = target.script_type;
                if (target.script_owner) {
                    handler->file_owner = target.script_owner;
                }
                if (target.script_names) {
                    handler->script_names_owner = target.script_names;
                }
                return handler;
            }
        }
        return std::nullopt;
    });

    auto legacy_timeout_handler_name = [this]() -> std::optional<std::string> {
        const lingo::Datum script = movie_properties_.getMovieProp("timeoutScript");
        if (script.isVoid() || script.isNull() || (script.isString() && script.stringValue().empty())) {
            return std::string("timeOut");
        }
        return legacyEventHandlerName(script);
    };

    auto dispatch_legacy_movie_timeout = [this, invoke_target, collect_movie_script_targets, legacy_timeout_handler_name] {
        const int timeout_length = movie_properties_.getMovieProp("timeoutLength").intValue();
        if (timeout_length <= 0) {
            legacy_timeout_armed_ = true;
            return;
        }

        const int elapsed_ticks = movie_properties_.getMovieProp("timeoutLapsed").intValue();
        if (elapsed_ticks < timeout_length) {
            legacy_timeout_armed_ = true;
            return;
        }
        if (!legacy_timeout_armed_) {
            return;
        }

        const auto handler_name = legacy_timeout_handler_name();
        if (!handler_name.has_value() || handler_name->empty()) {
            return;
        }

        bool handled = false;
        eventDispatcher().resetEventStopped();
        for (const auto& movie : collect_movie_script_targets()) {
            if (!movie.script || movie.script_type != chunks::ScriptChunkType::MovieScript) {
                continue;
            }

            event::EventTarget target;
            target.kind = event::EventTargetKind::MovieScript;
            target.script = movie.script;
            target.script_names = movie.script_names;
            target.script_owner = movie.script_owner;
            target.script_type = movie.script_type;
            const auto result = invoke_target(target, *handler_name, {});
            if (!result.handled) {
                continue;
            }

            handled = true;
            if (!result.passed || eventDispatcher().isEventStopped()) {
                break;
            }
        }
        if (!handled) {
            (void)vm_.callHandler(*handler_name, {});
        }
        legacy_timeout_armed_ = false;
    };

    input_handler_.setLegacyEventScriptDispatcher([this, invoke_target, collect_movie_script_targets](PlayerEvent event) {
        const std::string_view prop_name = legacyEventScriptProperty(event);
        if (prop_name.empty()) {
            return InputHandler::LegacyEventScriptResult{};
        }

        const auto handler_name = legacyEventHandlerName(movie_properties_.getMovieProp(prop_name));
        if (!handler_name.has_value()) {
            return InputHandler::LegacyEventScriptResult{};
        }

        InputHandler::LegacyEventScriptResult aggregate;
        eventDispatcher().resetEventStopped();
        for (const auto& movie : collect_movie_script_targets()) {
            if (!movie.script || movie.script_type != chunks::ScriptChunkType::MovieScript) {
                continue;
            }

            event::EventTarget target;
            target.kind = event::EventTargetKind::MovieScript;
            target.script = movie.script;
            target.script_names = movie.script_names;
            target.script_owner = movie.script_owner;
            target.script_type = movie.script_type;
            const auto result = invoke_target(target, *handler_name, {});
            if (!result.handled) {
                continue;
            }

            aggregate.handled = true;
            aggregate.passed = result.passed;
            if (!result.passed || eventDispatcher().isEventStopped()) {
                break;
            }
        }
        return aggregate;
    });

    eventDispatcher().setScriptNamesResolver([this](const std::shared_ptr<chunks::ScriptChunk>& script) {
        if (script) {
            if (auto* owner = const_cast<DirectorFile*>(script->file())) {
                return owner->getScriptNamesForScript(script);
            }
        }
        return file_ != nullptr ? file_->getScriptNamesForScript(script) : nullptr;
    });
    eventDispatcher().setMovieScriptSupplier(collect_movie_script_targets);
    eventDispatcher().setRespondsPredicate([find_event_handler](const event::EventTarget& target,
                                                              std::string_view handler_name) {
        return find_event_handler(target, handler_name).has_value();
    });
    eventDispatcher().setHandlerInvoker([invoke_target](const event::EventTarget& target,
                                                       std::string_view handler_name,
                                                       const std::vector<lingo::Datum>& args) {
        return invoke_target(target, handler_name, args);
    });
    timeout_system_event_dispatcher_ = [this, invoke_target](std::string_view handler_name) {
        timeout_manager_.dispatchSystemEvent(handler_name, [invoke_target](const lingo::Datum& target,
                                                                       std::string_view system_handler) {
            event::EventTarget event_target;
            event_target.kind = event::EventTargetKind::ScriptInstance;
            event_target.script_instance = target;
            (void)invoke_target(event_target, system_handler, {});
        });
    };
    timeout_processor_ = [this, invoke_target, dispatch_legacy_movie_timeout] {
        timeout_manager_.processTimeouts([this, invoke_target](const timeout::TimeoutEntry& entry) {
            vm_.resetErrorState();
            lingo::Datum resolved_target = entry.target;
            if (resolved_target.type() != lingo::DatumType::ScriptInstanceRef && !resolved_target.isVoid()) {
                const auto resolved = vm_.callHandler("getobject", {resolved_target});
                if (resolved.type() == lingo::DatumType::ScriptInstanceRef) {
                    resolved_target = resolved;
                }
                vm_.resetErrorState();
            }

            const std::vector<lingo::Datum> args{lingo::Datum::timeoutRef(entry.name)};
            if (resolved_target.type() == lingo::DatumType::ScriptInstanceRef) {
                event::EventTarget target;
                target.kind = event::EventTargetKind::ScriptInstance;
                target.script_instance = resolved_target;
                const auto result = invoke_target(target, entry.handler, args);
                if (result.handled) {
                    return;
                }
            }

            (void)vm_.callHandler(entry.handler, args);
        });
        dispatch_legacy_movie_timeout();
    };
    frame_context_.setTimeoutEventDispatcher(timeout_system_event_dispatcher_);
    frame_context_.setActorListDispatcher([this, invoke_target](std::string_view handler_name) {
        const auto& actor_list = movie_properties_.actorList();
        if (!actor_list.isList()) {
            return;
        }

        std::vector<lingo::Datum> actors;
        for (const auto& item : actor_list.listValue().items()) {
            if (item.type() == lingo::DatumType::ScriptInstanceRef) {
                actors.push_back(item);
            }
        }

        for (const auto& actor : actors) {
            event::EventTarget target;
            target.kind = event::EventTargetKind::ScriptInstance;
            target.script_instance = actor;
            (void)invoke_target(target, handler_name, {actor});
        }
    });
}

void Player::prepareMovieFoundation() {
    TickDeadlineGuard deadline_guard(vm_);
    applyInitialBuiltinVariables();
    (void)preloadAllCasts();
    cast_lib_manager_.preloadCasts(2);
    if (timeout_system_event_dispatcher_) {
        timeout_system_event_dispatcher_("prepareMovie");
    }
    eventDispatcher().dispatchToMovieScripts(PlayerEvent::PrepareMovie);
    cast_lib_manager_.preloadCasts(1);
    frame_context_.initializeFirstFrame();
    frame_context_.dispatchBeginSpriteEvents();
    if (timeout_system_event_dispatcher_) {
        timeout_system_event_dispatcher_("prepareFrame");
    }
    eventDispatcher().dispatchGlobalEvent(PlayerEvent::PrepareFrame);
    eventDispatcher().dispatchToMovieScripts(PlayerEvent::StartMovie);
    if (timeout_system_event_dispatcher_) {
        timeout_system_event_dispatcher_("startMovie");
    }
    eventDispatcher().dispatchGlobalEvent(PlayerEvent::EnterFrame);
    if (timeout_system_event_dispatcher_) {
        timeout_system_event_dispatcher_("exitFrame");
    }
    eventDispatcher().dispatchSpriteAndMovieEvent(handlerName(PlayerEvent::ExitFrame));
    cast_lib_manager_.preloadCasts(1);
}

void Player::applyInitialBuiltinVariables() {
    for (const auto& [name, value] : initial_builtin_variables_) {
        if (vm_.globals().find(name) == vm_.globals().end()) {
            vm_.setGlobal(name, value.deepCopy());
        }
    }
}

void Player::processUpdatingObjects() {
    if (updating_objects_.empty() || !vm_.builtinContext().call_target_handler) {
        return;
    }

    const auto snapshot = updating_objects_;
    for (const auto& target : snapshot) {
        if (target.type() != lingo::DatumType::ScriptInstanceRef) {
            continue;
        }
        try {
            (void)vm_.builtinContext().call_target_handler(target, "update", {});
        } catch (...) {
            // Java's update-provider dispatch suppresses per-target handler failures.
        }
    }
}

void Player::refreshDebugControllerGlobals() {
    // Keeping the controller attached lets the debugger enable tracing later,
    // but ordinary playback does not need a full copy of the VM globals every
    // frame.  Movies can add globals while running, so doing this unconditionally
    // turns a constant per-frame cost into progressively more work over time.
    if (!debug_controller_ || !debug_controller_->tracingEnabled()) {
        return;
    }

    std::map<std::string, lingo::Datum> globals;
    for (const auto& [name, value] : vm_.globals()) {
        globals.emplace(name, value);
    }
    debug_controller_->setGlobalsSnapshot(std::move(globals));
}

net::NetProvider& Player::activeNetProvider() {
    return override_net_provider_ != nullptr ? *override_net_provider_ : static_cast<net::NetProvider&>(net_manager_);
}

const net::NetProvider& Player::activeNetProvider() const {
    return override_net_provider_ != nullptr ? *override_net_provider_ : static_cast<const net::NetProvider&>(net_manager_);
}

void Player::refreshBuiltinNetProvider() {
    vm_.builtinContext().net_manager = &activeNetProvider();
}

void Player::launchVmWorker(std::function<void()> work, std::function<void()> callback) {
    if (vm_running_.exchange(true)) {
        return;
    }

    joinVmWorker();
    std::lock_guard<std::mutex> lock(vm_thread_mutex_);
    vm_thread_ = std::thread([this, work = std::move(work), callback = std::move(callback)]() mutable {
        try {
            if (work) {
                work();
            }
        } catch (const std::exception& error) {
            handleTraceError("Async VM error", error.what());
        } catch (...) {
            handleTraceError("Async VM error", "unknown exception");
        }

        vm_running_.store(false);
        if (callback) {
            try {
                callback();
            } catch (const std::exception& error) {
                handleTraceError("Async VM callback error", error.what());
            } catch (...) {
                handleTraceError("Async VM callback error", "unknown exception");
            }
        }
    });
}

void Player::joinVmWorker() {
    std::thread thread_to_join;
    {
        std::lock_guard<std::mutex> lock(vm_thread_mutex_);
        if (!vm_thread_.joinable()) {
            return;
        }
        if (vm_thread_.get_id() == std::this_thread::get_id()) {
            vm_thread_.detach();
            return;
        }
        thread_to_join = std::move(vm_thread_);
    }

    thread_to_join.join();
}

void Player::loadCastFromNetCache(int cast_lib_number, const std::string& file_name) {
    if (file_name.empty()) {
        return;
    }

    if (auto data = net_manager_.getCachedData(file_name); data.has_value()) {
        (void)loadExternalCastFromCachedData(cast_lib_number, *data);
        return;
    }

    const auto base_name = util::getFileNameWithoutExtension(util::getFileName(file_name));
    if (!base_name.empty()) {
        if (auto cached = cast_lib_manager_.getCachedExternalData(base_name); cached.has_value()) {
            (void)loadExternalCastFromCachedData(cast_lib_number, *cached);
            return;
        }
    }

    if (cast_data_request_callback_) {
        cast_data_request_callback_(cast_lib_number, file_name);
        return;
    }
    (void)activeNetProvider().preloadNetThing(file_name);
}

void Player::handleExternalCastFetch(const std::string& url, const std::vector<std::uint8_t>& data) {
    cast_lib_manager_.cacheExternalData(url, data);
    try {
        auto cast_numbers = cast_lib_manager_.getRequestedExternalCastSlots(url);
        for (const int cast_lib_number : cast_lib_manager_.getMatchingCastLibNumbersByUrl(url)) {
            if (std::find(cast_numbers.begin(), cast_numbers.end(), cast_lib_number) == cast_numbers.end()) {
                cast_numbers.push_back(cast_lib_number);
            }
        }

        for (const int cast_lib_number : cast_numbers) {
            (void)loadExternalCastFromCachedData(cast_lib_number, data);
        }
    } catch (...) {
    }
}

} // namespace libreshockwave::player
