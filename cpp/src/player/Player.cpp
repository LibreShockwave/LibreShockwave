#include "libreshockwave/player/Player.hpp"

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

#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/cast/CastMember.hpp"
#include "libreshockwave/chunks/CastMemberChunk.hpp"
#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/chunks/ScriptNamesChunk.hpp"
#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/lingo/vm/dispatch/ImageMethodDispatcher.hpp"
#include "libreshockwave/lingo/vm/util/AncestorChainWalker.hpp"
#include "libreshockwave/lingo/xtra/MultiuserXtra.hpp"
#include "libreshockwave/lingo/xtra/XmlParserXtra.hpp"
#include "libreshockwave/player/ExternalCastLoadHandler.hpp"
#include "libreshockwave/player/PlayerEvent.hpp"
#include "libreshockwave/player/debug/DebugControllerApi.hpp"
#include "libreshockwave/player/debug/LifecycleDiagnostics.hpp"
#include "libreshockwave/player/event/EventDispatcher.hpp"
#include "libreshockwave/player/render/output/SimpleTextRenderer.hpp"
#include "libreshockwave/player/score/ScoreNavigator.hpp"
#include "libreshockwave/util/FileUtil.hpp"

namespace libreshockwave::player {
namespace {

constexpr int DEFAULT_TEMPO = 15;

class TickDeadlineGuard {
public:
    explicit TickDeadlineGuard(lingo::vm::LingoVM& vm)
        : vm_(vm),
          previousDeadline_(vm.tickDeadline()) {
        if (previousDeadline_ == 0) {
            vm_.armTickDeadline();
        }
    }

    ~TickDeadlineGuard() {
        vm_.setTickDeadline(previousDeadline_);
    }

private:
    lingo::vm::LingoVM& vm_;
    std::int64_t previousDeadline_{0};
};

struct ResolvedScriptTarget {
    std::shared_ptr<chunks::ScriptChunk> script;
    std::shared_ptr<const chunks::ScriptNamesChunk> scriptNames;
    std::shared_ptr<const DirectorFile> scriptOwner;
    chunks::ScriptChunkType scriptType{chunks::ScriptChunkType::Unknown};
};

struct HandlerLookupKey {
    const chunks::ScriptChunk* script{nullptr};
    const chunks::ScriptNamesChunk* scriptNames{nullptr};
    std::string handlerName;

    bool operator==(const HandlerLookupKey& other) const {
        return script == other.script &&
               scriptNames == other.scriptNames &&
               handlerName == other.handlerName;
    }
};

struct HandlerLookupKeyHash {
    std::size_t operator()(const HandlerLookupKey& key) const {
        const auto scriptHash = std::hash<const void*>{}(key.script);
        const auto namesHash = std::hash<const void*>{}(key.scriptNames);
        const auto nameHash = std::hash<std::string>{}(key.handlerName);
        return scriptHash ^ (namesHash << 1U) ^ (nameHash << 2U);
    }
};

struct ScriptHandlerFinderKey {
    int castLib{0};
    int memberNum{0};
    std::string handlerName;

    bool operator==(const ScriptHandlerFinderKey& other) const {
        return castLib == other.castLib &&
               memberNum == other.memberNum &&
               handlerName == other.handlerName;
    }
};

struct ScriptHandlerFinderKeyHash {
    std::size_t operator()(const ScriptHandlerFinderKey& key) const {
        const auto castHash = std::hash<int>{}(key.castLib);
        const auto memberHash = std::hash<int>{}(key.memberNum);
        const auto nameHash = std::hash<std::string>{}(key.handlerName);
        return castHash ^ (memberHash << 1U) ^ (nameHash << 2U);
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

    std::string handlerName = trimWhitespace(value.stringValue());
    if (!handlerName.empty() && handlerName.front() == '#') {
        handlerName.erase(handlerName.begin());
    }
    return isLingoIdentifier(handlerName) ? std::optional<std::string>(std::move(handlerName)) : std::nullopt;
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
        return debug::LifecycleDiagnostics::isEnabled() || player_.debugController_ != nullptr;
    }

    void onHandlerEnter(const HandlerInfo& info) override {
        debug::LifecycleDiagnostics::logHandlerEnter(info);
        if (player_.debugController_) {
            player_.debugController_->onHandlerEnter(info);
        }
    }

    void onHandlerExit(const HandlerInfo& info, const lingo::Datum& returnValue) override {
        debug::LifecycleDiagnostics::logHandlerExit(info, returnValue);
        if (player_.debugController_) {
            player_.debugController_->onHandlerExit(info, returnValue);
        }
    }

    [[nodiscard]] bool needsInstructionTrace() const override {
        return player_.debugController_ != nullptr && player_.debugController_->needsInstructionTrace();
    }

    [[nodiscard]] bool needsVariableTrace() const override {
        return player_.debugController_ != nullptr;
    }

    void onInstruction(const InstructionInfo& info) override {
        if (player_.debugController_) {
            player_.debugController_->onInstruction(info);
        }
    }

    void onVariableSet(std::string_view type, std::string_view name, const lingo::Datum& value) override {
        if (player_.debugController_) {
            player_.debugController_->onVariableSet(type, name, value);
        }
    }

    void onError(std::string_view message, std::string_view error) override {
        debug::LifecycleDiagnostics::logError(message, error);
        if (player_.debugController_) {
            player_.debugController_->onError(message, error);
        }
        player_.handleTraceError(message, error);
    }

    void onDebugMessage(std::string_view message) override {
        if (player_.debugController_) {
            player_.debugController_->onDebugMessage(message);
        }
    }

private:
    Player& player_;
};

Player::Player(std::shared_ptr<DirectorFile> file)
    : Player(std::move(file), true) {}

Player::Player(std::shared_ptr<DirectorFile> file, bool registerSocketMultiuserXtra)
    : file_(std::move(file)),
      frameContext_(file_.get()),
      stageRenderer_(file_.get()),
      netManager_(),
      xtraManager_(),
      movieProperties_(file_.get()),
      spriteProperties_(&stageRenderer_.spriteRegistry()),
      castLibManager_(file_, [this](int castLibNumber, const std::string& fileName) {
          loadCastFromNetCache(castLibNumber, fileName);
      }),
      timeoutManager_(),
      soundManager_(file_.get()),
      bitmapCache_(),
      spriteBaker_(&bitmapCache_),
      defaultTextRenderer_(std::make_unique<render::output::SimpleTextRenderer>()),
      frameRenderPipeline_(&stageRenderer_, &spriteBaker_),
      inputState_(),
      bitmapResolver_(file_, &castLibManager_, &frameContext_),
      cursorManager_(&inputState_, &stageRenderer_.spriteRegistry()),
      inputHandler_(&inputState_, &stageRenderer_, &frameContext_.eventDispatcher(), &castLibManager_),
      vm_(file_.get()),
      tempo_(configuredTempo(file_)) {
    xtraManager_.registerXtra(std::make_unique<lingo::xtra::XmlParserXtra>());
    if (registerSocketMultiuserXtra) {
        socketMultiuserBridge_ = std::make_unique<xtra::SocketMultiuserBridge>();
        registerMultiuserXtra(*socketMultiuserBridge_);
    }
    playerTraceListener_ = std::make_shared<PlayerTraceListener>(*this);
    vm_.setTraceListener(playerTraceListener_);
    wireComponents();
    setTextRenderer(defaultTextRenderer_.get());
}

Player::Player(std::shared_ptr<DirectorFile> file, net::NetProvider* netProvider)
    : Player(std::move(file), false) {
    setNetProvider(netProvider);
}

Player::Player(std::shared_ptr<DirectorFile> file,
               net::NetProvider* netProvider,
               CastDataRequestCallback castDataRequestCallback)
    : Player(std::move(file), false) {
    castDataRequestCallback_ = std::move(castDataRequestCallback);
    setNetProvider(netProvider);
}

Player::~Player() {
    if (debugController_) {
        debugController_->reset();
    }
    joinVmWorker();
    vm_.setTraceListener(nullptr);
    lingo::vm::dispatch::ImageMethodDispatcher::clearImageMutationCallback(this);
}

std::shared_ptr<DirectorFile> Player::file() const { return file_; }
frame::FrameContext& Player::frameContext() { return frameContext_; }
score::ScoreNavigator& Player::navigator() { return frameContext_.navigator(); }
behavior::BehaviorManager& Player::behaviorManager() { return frameContext_.behaviorManager(); }
event::EventDispatcher& Player::eventDispatcher() { return frameContext_.eventDispatcher(); }
render::pipeline::StageRenderer& Player::stageRenderer() { return stageRenderer_; }
net::NetManager& Player::netManager() { return netManager_; }
lingo::xtra::XtraManager& Player::xtraManager() { return xtraManager_; }
MovieProperties& Player::movieProperties() { return movieProperties_; }
SpriteProperties& Player::spriteProperties() { return spriteProperties_; }
cast::CastLibManager& Player::castLibManager() { return castLibManager_; }
timeout::TimeoutManager& Player::timeoutManager() { return timeoutManager_; }
audio::SoundManager& Player::soundManager() { return soundManager_; }
render::pipeline::BitmapCache& Player::bitmapCache() { return bitmapCache_; }
render::pipeline::SpriteBaker& Player::spriteBaker() { return spriteBaker_; }
render::pipeline::FrameRenderPipeline& Player::frameRenderPipeline() { return frameRenderPipeline_; }
input::InputState& Player::inputState() { return inputState_; }
BitmapResolver& Player::bitmapResolver() { return bitmapResolver_; }
CursorManager& Player::cursorManager() { return cursorManager_; }
InputHandler& Player::inputHandler() { return inputHandler_; }
lingo::vm::LingoVM& Player::vm() { return vm_; }
lingo::builtin::BuiltinRegistry& Player::builtinRegistry() { return vm_.builtinRegistry(); }
lingo::builtin::BuiltinContext& Player::builtinContext() { return vm_.builtinContext(); }

void Player::receiveUpdate(const lingo::Datum& target) {
    if (target.isVoid()) {
        return;
    }
    if (std::find(updatingObjects_.begin(), updatingObjects_.end(), target) == updatingObjects_.end()) {
        updatingObjects_.push_back(target);
    }
}

void Player::removeUpdate(const lingo::Datum& target) {
    updatingObjects_.erase(std::remove(updatingObjects_.begin(), updatingObjects_.end(), target),
                           updatingObjects_.end());
}

PlayerState Player::state() const { return state_; }
int Player::currentFrame() const { return frameContext_.currentFrame(); }
int Player::effectiveFrame() const { return frameContext_.effectiveFrame(); }
int Player::frameCount() const { return frameContext_.frameCount(); }

int Player::getFrameForLabel(std::string_view label) const {
    return frameContext_.navigator().getFrameForLabel(label);
}

std::set<std::string> Player::getFrameLabels() const {
    return frameContext_.navigator().getFrameLabels();
}

int Player::tempo() const {
    const int puppetTempo = movieProperties_.puppetTempo();
    if (puppetTempo > 0) {
        return puppetTempo;
    }
    if (file_ != nullptr) {
        const int scoreTempo = file_->getScoreTempo(currentFrame() - 1);
        if (scoreTempo > 0) {
            return scoreTempo;
        }
    }
    return tempo_;
}

int Player::baseTempo() const { return tempo_; }

void Player::setTempo(int tempo) {
    tempo_ = tempo > 0 ? tempo : DEFAULT_TEMPO;
    inputState_.setCaretBlinkRate(tempo_);
}

void Player::setEventListener(std::function<void(const PlayerEventInfo&)> listener) {
    eventListener_ = std::move(listener);
}

void Player::setCastLoadedListener(std::function<void()> listener) {
    castLoadedListener_ = std::move(listener);
}

void Player::setExternalCastLoadListener(std::function<void(const ExternalCastLoadEvent&)> listener) {
    externalCastLoadListener_ = std::move(listener);
}

void Player::setErrorListener(ErrorListener listener) {
    errorListener_ = std::move(listener);
}

void Player::addExternalCastLoadHandler(ExternalCastLoadHandler* handler) {
    if (handler != nullptr) {
        externalCastLoadHandlers_.push_back(handler);
    }
}

void Player::setDebugEnabled(bool enabled) {
    debugEnabled_ = enabled;
    vm_.builtinContext().debugPlaybackEnabled = enabled;
    frameContext_.setDebugEnabled(enabled);
    if (enabled) {
        dumpScriptInfo();
    }
}

void Player::setSlowHandlerWarningThresholdMs(std::int64_t milliseconds) {
    vm_.setSlowHandlerWarningThresholdMs(milliseconds);
}

void Player::setDebugController(std::shared_ptr<debug::DebugControllerApi> controller) {
    debugController_ = std::move(controller);
}

std::shared_ptr<debug::DebugControllerApi> Player::getDebugController() const {
    return debugController_;
}

void Player::setNetProvider(net::NetProvider* provider) {
    overrideNetProvider_ = provider;
    refreshBuiltinNetProvider();
}

void Player::setAudioBackend(audio::AudioBackend* backend) {
    soundManager_.setBackend(backend);
}

void Player::setTextRenderer(render::output::TextRenderer* renderer) {
    spriteBaker_.setTextRenderer(renderer);
    castLibManager_.setTextRenderer(renderer);
}

void Player::registerMultiuserXtra(lingo::xtra::MultiuserNetBridge& bridge) {
    xtraManager_.registerXtra(std::make_unique<lingo::xtra::MultiuserXtra>(
        &bridge,
        [this](const lingo::Datum& target,
               const std::string& handlerName,
               const std::vector<lingo::Datum>& args) {
            try {
                if (target.type() == lingo::DatumType::ScriptInstanceRef &&
                    vm_.builtinContext().callTargetHandler) {
                    (void)vm_.builtinContext().callTargetHandler(target, handlerName, args);
                    return;
                }
                if (const auto* scriptRef = target.asScriptRef();
                    scriptRef != nullptr && vm_.builtinContext().callTargetHandler) {
                    (void)vm_.builtinContext().callTargetHandler(
                        lingo::Datum::scriptInstance("script", scriptRef->memberRef),
                        handlerName,
                        args);
                    return;
                }
                (void)vm_.callHandler(handlerName, args);
            } catch (...) {
                // Match Java Player callback behavior: Xtra callbacks do not break frame polling.
            }
        }));
}

void Player::setExternalParams(std::vector<std::pair<std::string, std::string>> params) {
    externalParams_ = std::move(params);
    vm_.builtinContext().externalParams = externalParams_;
}

const std::vector<std::pair<std::string, std::string>>& Player::externalParams() const {
    return externalParams_;
}

void Player::setInitialBuiltinVariable(std::string variableName, lingo::Datum defaultValue) {
    if (variableName.empty()) {
        return;
    }
    for (auto& entry : initialBuiltinVariables_) {
        if (entry.first == variableName) {
            entry.second = defaultValue.deepCopy();
            return;
        }
    }
    initialBuiltinVariables_.emplace_back(std::move(variableName), defaultValue.deepCopy());
}

void Player::setInitialBuiltinVariables(std::vector<std::pair<std::string, lingo::Datum>> values) {
    initialBuiltinVariables_.clear();
    for (auto& [name, value] : values) {
        setInitialBuiltinVariable(std::move(name), std::move(value));
    }
}

bool Player::debugEnabled() const { return debugEnabled_; }

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

    int movieScripts = 0;
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
                ++movieScripts;
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

    out << "[Player] Movie scripts: " << movieScripts << '\n';
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

std::string Player::getRecentScriptErrorMessage(std::int64_t maxAgeMs) const {
    return recentScriptErrorIsFresh(maxAgeMs) ? lastScriptErrorMessage_ : std::string();
}

std::string Player::getRecentScriptErrorStack(std::int64_t maxAgeMs) const {
    return recentScriptErrorIsFresh(maxAgeMs) ? lastScriptErrorStack_ : std::string();
}

bool Player::isVmRunning() const {
    return vmRunning_.load();
}

void Player::play() {
    const bool wasStopped = state_ == PlayerState::Stopped;
    state_ = PlayerState::Playing;
    if (wasStopped) {
        legacyTimeoutArmed_ = true;
        prepareMovieFoundation();
    }
}

void Player::playAsync(std::function<void()> onReady) {
    if (state_ != PlayerState::Stopped) {
        state_ = PlayerState::Playing;
        if (onReady) {
            onReady();
        }
        return;
    }

    launchVmWorker([this] {
        prepareMovieFoundation();
        state_ = PlayerState::Playing;
    }, std::move(onReady));
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

    if (timeoutSystemEventDispatcher_) {
        timeoutSystemEventDispatcher_("stopMovie");
    }
    eventDispatcher().dispatchToMovieScripts(PlayerEvent::StopMovie);
    frameContext_.reset();
    stageRenderer_.reset();
    timeoutManager_.clear();
    soundManager_.stopAll();
    legacyTimeoutArmed_ = true;
    state_ = PlayerState::Stopped;
}

void Player::shutdown() {
    stop();
    netManager_.shutdown();
    if (debugController_) {
        debugController_->reset();
    }
    joinVmWorker();
    lingo::vm::dispatch::ImageMethodDispatcher::clearImageMutationCallback(this);
}

void Player::goToFrame(int frame) {
    frameContext_.goToFrame(frame);
}

void Player::goToLabel(std::string_view label) {
    frameContext_.goToLabel(label);
}

void Player::stepFrame() {
    if (state_ == PlayerState::Stopped) {
        prepareMovieFoundation();
        state_ = PlayerState::Paused;
    }

    executeFrameCycle(false);
}

void Player::stepFrameAsync(std::function<void()> onComplete) {
    launchVmWorker([this] {
        if (state_ == PlayerState::Stopped) {
            prepareMovieFoundation();
            state_ = PlayerState::Paused;
        }

        do {
            executeFrameCycle(false);
        } while (debugController_ && debugController_->isAwaitingStepContinuation());
    }, std::move(onComplete));
}

bool Player::tick() {
    if (state_ != PlayerState::Playing) {
        return state_ == PlayerState::Paused;
    }

    executeFrameCycle(true);
    return true;
}

void Player::tickAsync(std::function<void()> onComplete) {
    if (state_ != PlayerState::Playing) {
        return;
    }

    launchVmWorker([this] {
        do {
            executeFrameCycle(true);
        } while (debugController_ && debugController_->isAwaitingStepContinuation());
    }, std::move(onComplete));
}

bool Player::fireTestError(std::string_view errorMessage) {
    try {
        const bool handled = vm_.fireAlertHook("Script Error", errorMessage);
        vm_.flushDeferredTasks();
        return handled;
    } catch (...) {
        vm_.flushDeferredTasks();
        throw;
    }
}

void Player::executeFrameCycle(bool processUpdates) {
    TickDeadlineGuard deadlineGuard(vm_);
    refreshDebugControllerGlobals();
    (void)inputHandler_.processInputEvents();
    (void)frameContext_.executeFrame();
    if (timeoutProcessor_) {
        timeoutProcessor_();
    }
    xtraManager_.tickAll();
    eventDispatcher().dispatchToMovieScripts(PlayerEvent::Idle);
    if (processUpdates) {
        processUpdatingObjects();
    }
    vm_.flushDeferredTasks();
    (void)frameContext_.advanceFrame();
}

void Player::onNetFetchComplete(std::string_view url, const std::vector<std::uint8_t>& data) {
    if (url.empty()) {
        return;
    }

    const std::string urlValue(url);
    castLibManager_.cacheExternalData(urlValue, data);

    std::string lowerUrl(urlValue);
    std::transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    const auto queryIndex = lowerUrl.find('?');
    if (queryIndex != std::string::npos && queryIndex > 0) {
        lowerUrl = lowerUrl.substr(0, queryIndex);
    }
    if (!lowerUrl.ends_with(".cct") && !lowerUrl.ends_with(".cst")) {
        return;
    }

    handleExternalCastFetch(urlValue, data);
}

int Player::preloadAllCasts() {
    int count = 0;
    for (const auto& [_, castLib] : castLibManager_.castLibs()) {
        if (!castLib || !castLib->isExternal() || castLib->isLoaded() || castLib->isFetching()) {
            continue;
        }
        const std::string fileName = util::getFileName(castLib->fileName());
        if (fileName.empty()) {
            continue;
        }
        castLib->markFetching();
        (void)activeNetProvider().preloadNetThing(fileName);
        ++count;
    }
    return count;
}

void Player::onSynchronousExternalCastLoad(int castLibNumber) {
    if (castLibNumber <= 0) {
        return;
    }

    bitmapCache_.clear();
    vm_.invalidateHandlerCache();
    stageRenderer_.spriteRegistry().bumpRevision();
    frameContext_.rebindBehaviorsForLoadedCast(castLibNumber);
    notifyExternalCastLoaded(castLibNumber);
}

bool Player::loadExternalCastFromCachedData(int castLibNumber, const std::vector<std::uint8_t>& data) {
    return loadExternalCastFromCachedData(castLibNumber, data, {});
}

bool Player::loadExternalCastFromCachedData(int castLibNumber,
                                            const std::vector<std::uint8_t>& data,
                                            std::function<void()> afterLoad) {
    if (castLibNumber <= 0 || data.empty()) {
        return false;
    }
    TickDeadlineGuard deadlineGuard(vm_);
    return applyExternalCastDataNow(castLibNumber, data, std::move(afterLoad));
}

bool Player::applyExternalCastDataNow(int castLibNumber,
                                      const std::vector<std::uint8_t>& data,
                                      std::function<void()> afterLoad) {
    if (!castLibManager_.setExternalCastData(castLibNumber, data)) {
        return false;
    }
    onSynchronousExternalCastLoad(castLibNumber);
    if (afterLoad) {
        afterLoad();
    }
    return true;
}

void Player::notifyExternalCastLoaded(int castLibNumber) {
    auto castLib = castLibManager_.getCastLib(castLibNumber);
    if (!castLib) {
        return;
    }

    const std::string fileName = castLib->fileName();
    debug::LifecycleDiagnostics::logExternalCastLoaded(castLibNumber, fileName);
    ExternalCastLoadEvent event{castLibNumber, fileName};
    for (auto* handler : externalCastLoadHandlers_) {
        if (handler != nullptr) {
            handler->onExternalCastLoaded(*this, castLibNumber, fileName);
        }
    }
    if (externalCastLoadListener_) {
        externalCastLoadListener_(event);
    }
    if (castLoadedListener_) {
        castLoadedListener_();
    }
}

void Player::handleTraceError(std::string_view message, std::string_view errorDetail) {
    lastScriptErrorTimeMs_ = currentTimeMillis();
    lastScriptErrorMessage_ = std::string(message);
    lastScriptErrorStack_ = vm_.hasActiveCallStack() ? vm_.formatCallStack() : std::string();
    if (errorListener_) {
        errorListener_(lastScriptErrorMessage_, errorDetail);
    }
}

bool Player::recentScriptErrorIsFresh(std::int64_t maxAgeMs) const {
    if (maxAgeMs < 0) {
        maxAgeMs = 0;
    }
    if (lastScriptErrorTimeMs_ <= 0) {
        return false;
    }
    return currentTimeMillis() - lastScriptErrorTimeMs_ <= maxAgeMs;
}

render::pipeline::FrameSnapshot Player::frameSnapshot() {
    const int frame = currentFrame();
    return frameRenderPipeline_.renderFrame(
        frame,
        stageRenderer_.stageWidth(),
        stageRenderer_.stageHeight(),
        stageRenderer_.backgroundColor(),
        stageRenderer_.renderableStageImage(),
        "Frame " + std::to_string(frame) + " | " + std::string(name(state_)));
}

lingo::Datum Player::currentStageImageDatum() {
    const int frame = currentFrame();
    auto snapshot = frameRenderPipeline_.renderFrame(
        frame,
        stageRenderer_.stageWidth(),
        stageRenderer_.stageHeight(),
        stageRenderer_.backgroundColor(),
        stageRenderer_.renderableStageImage(),
        "Stage image " + std::to_string(frame));
    auto composed = snapshot.renderFrame();
    return lingo::Datum::imageRef(stageRenderer_.updateStageImageSnapshot(composed));
}

void Player::wireComponents() {
    frameContext_.setSpriteRegistry(&stageRenderer_.spriteRegistry());
    frameContext_.setEventListener([this](const frame::FrameEvent& event) {
        if (eventListener_) {
            eventListener_(PlayerEventInfo{event.event, event.frame, 0});
        }
        if (event.event == PlayerEvent::EnterFrame) {
            stageRenderer_.onFrameEnter(event.frame);
        }
    });
    frameContext_.setScriptNameResolver([this](const std::shared_ptr<chunks::ScriptChunk>& script) {
        if (!script) {
            return std::string();
        }
        if (auto* owner = const_cast<DirectorFile*>(script->file())) {
            return owner->getScriptName(script);
        }
        return file_ != nullptr ? file_->getScriptName(script) : std::string();
    });
    behaviorManager().setFrameScriptResolver([this](const score::ScoreBehaviorRef& behaviorRef)
        -> std::shared_ptr<chunks::ScriptChunk> {
        const int castLib = behaviorRef.castLib() > 0 ? behaviorRef.castLib() : 1;
        const int member = behaviorRef.castMember();
        if (member <= 0) {
            return nullptr;
        }
        auto library = castLibManager_.getCastLib(castLib);
        if (library && library->isLoaded()) {
            if (auto script = library->getScript(member)) {
                return script;
            }
        }
        return nullptr;
    });
    castLibManager_.setMemberSlotRetiredCallback([this](int castLib, int memberNum) {
        (void)stageRenderer_.spriteRegistry().clearDynamicMemberBindings(castLib, memberNum);
    });
    lingo::vm::dispatch::ImageMethodDispatcher::setImageMutationCallback(this, [this] {
        stageRenderer_.spriteRegistry().bumpRevision();
    });

    if (file_ != nullptr && !file_->basePath().empty()) {
        netManager_.setBasePath(file_->basePath());
    }
    netManager_.setCompletionCallback([this](const std::string& url, const std::vector<std::uint8_t>& data) {
        handleExternalCastFetch(url, data);
    });

    movieProperties_.setInputState(&inputState_);
    movieProperties_.setEffectiveFrameSupplier([this] {
        return effectiveFrame();
    });
    movieProperties_.setFrameCountSupplier([this] {
        return frameCount();
    });
    movieProperties_.setTempoSupplier([this] {
        return tempo();
    });
    movieProperties_.setTempoSetter([this](int value) {
        setTempo(value);
    });
    movieProperties_.setCastLibCountSupplier([this] {
        return castLibManager_.getCastLibCount();
    });
    movieProperties_.setStageBackgroundColorSupplier([this] {
        return stageRenderer_.backgroundColor();
    });
    movieProperties_.setStageBackgroundColorSetter([this](int rgb) {
        stageRenderer_.setBackgroundColor(rgb);
    });
    movieProperties_.setStageImageSupplier([this] {
        return currentStageImageDatum();
    });
    movieProperties_.setGoToFrameHandler([this](int frame) {
        goToFrame(frame);
    });
    movieProperties_.setGoToLabelHandler([this](const std::string& label) {
        goToLabel(label);
    });
    movieProperties_.setFrameForLabelResolver([this](const std::string& label) {
        return std::max(navigator().getFrameForLabel(label), 0);
    });
    movieProperties_.setMarkerFrameResolver([this](int offset) {
        return navigator().getMarkerFrame(currentFrame(), offset);
    });
    movieProperties_.setGotoNetPageHandler([this](const std::string& url, const std::string& target) {
        (void)target;
        (void)activeNetProvider().preloadNetThing(url);
    });
    movieProperties_.setGotoNetMovieHandler([this](const std::string& url) {
        return activeNetProvider().preloadNetThing(url);
    });

    spriteProperties_.setMemberInfoResolver([this](int castLib,
                                                   int memberNum) -> std::optional<SpriteProperties::MemberInfo> {
        auto member = castLibManager_.resolveMember(castLib, memberNum);
        if (!member) {
            return std::nullopt;
        }
        SpriteProperties::MemberInfo info;
        info.width = member->width();
        info.height = member->height();
        info.regX = member->regX();
        info.regY = member->regY();
        info.bitmap = member->isBitmap();
        info.bitmapWidth = member->width();
        info.bitmapHeight = member->height();
        info.bitmapRegX = member->regX();
        info.bitmapRegY = member->regY();
        info.hasImage = member->isBitmap();
        return info;
    });
    spriteProperties_.setMemberNameResolver(
        [this](const std::string& memberName) -> std::optional<lingo::Datum::CastMemberRef> {
            auto member = castLibManager_.findCastMemberByName(memberName);
            if (!member) {
                return std::nullopt;
            }
            return lingo::Datum::CastMemberRef::of(
                id::CastLibId(member->castLib()),
                id::MemberId(member->memberNum())
            );
        });

    stageRenderer_.setCastMemberResolver([this](int castLib, int memberNum)
        -> std::shared_ptr<const ::libreshockwave::cast::CastMember> {
        return castLibManager_.resolveMember(castLib, memberNum);
    });

    spriteBaker_.setBitmapDecodeProvider([this](const chunks::CastMemberChunk& member,
                                                const bitmap::Palette* palette) {
        return bitmapResolver_.decodeBitmapForProvider(member, palette);
    });
    spriteBaker_.setLiveBitmapProvider([this](const render::pipeline::RenderSprite& sprite)
        -> std::shared_ptr<const bitmap::Bitmap> {
        if (auto dynamicMember = sprite.dynamicMember()) {
            return dynamicMember->runtimeBitmap();
        }
        if (auto member = sprite.castMember()) {
            if (auto runtimeMember = castLibManager_.findRuntimeMember(
                    std::const_pointer_cast<chunks::CastMemberChunk>(member))) {
                return runtimeMember->runtimeBitmap();
            }
        }
        return nullptr;
    });
    spriteBaker_.setPaletteVersionProvider([this](const render::pipeline::RenderSprite& sprite)
        -> std::optional<int> {
        if (auto member = sprite.castMember()) {
            if (auto runtimeMember = castLibManager_.findRuntimeMember(
                    std::const_pointer_cast<chunks::CastMemberChunk>(member))) {
                return runtimeMember->paletteVersion();
            }
        }
        return std::nullopt;
    });

    soundManager_.setAudioResolver([this](const lingo::Datum::CastMemberRef& ref)
        -> std::optional<std::vector<std::uint8_t>> {
        auto castLib = castLibManager_.getCastLib(ref.castLib);
        if (!castLib) {
            return std::nullopt;
        }
        auto sourceFile = castLib->sourceFile();
        if (!sourceFile) {
            return std::nullopt;
        }
        auto member = castLib->findMemberByNumber(ref.memberNum());
        if (!member) {
            return std::nullopt;
        }
        auto sound = audio::SoundManager::findSoundForMember(*sourceFile, member);
        return sound ? audio::SoundManager::convertSoundToPlayable(*sound) : std::nullopt;
    });

    cursorManager_.setSpriteProvider([this] {
        auto sprites = stageRenderer_.lastBakedSprites();
        if (sprites.empty()) {
            sprites = stageRenderer_.getSpritesForFrame(currentFrame());
        }
        return sprites;
    });
    cursorManager_.setMemberInfoResolver([this](int castLib,
                                                int memberNum) -> std::optional<CursorManager::MemberInfo> {
        auto member = castLibManager_.resolveMember(castLib, memberNum);
        if (!member) {
            return std::nullopt;
        }
        CursorManager::MemberInfo info;
        info.memberType = member->memberType();
        info.editable = member->isText();
        info.regX = member->regX();
        info.regY = member->regY();
        return info;
    });
    cursorManager_.setBitmapResolver([this](int castLib, int memberNum) {
        return bitmapResolver_.decodeBitmap(castLibManager_.getCastMember(castLib, memberNum));
    });
    cursorManager_.setInteractivePredicate([this](int channel) {
        return eventDispatcher().isSpriteMouseInteractive(channel);
    });
    cursorManager_.setGlobalCursorSupplier([this] {
        return movieProperties_.getMovieProp("cursor");
    });
    movieProperties_.setXtraNamesSupplier([this] {
        return xtraManager_.registeredXtraNames();
    });
    movieProperties_.setSoundEnabledSupplier([this] {
        return soundManager_.isEnabled() ? 1 : 0;
    });
    movieProperties_.setSoundEnabledSetter([this](int enabled) {
        soundManager_.setEnabled(enabled != 0);
    });
    movieProperties_.setSoundLevelSupplier([this] {
        return soundManager_.getSoundLevel();
    });
    movieProperties_.setSoundLevelSetter([this](int level) {
        soundManager_.setSoundLevel(level);
    });
    movieProperties_.setSoundKeepDeviceSupplier([this] {
        return soundManager_.soundKeepDevice() ? 1 : 0;
    });
    movieProperties_.setSoundKeepDeviceSetter([this](int keepDevice) {
        soundManager_.setSoundKeepDevice(keepDevice != 0);
    });
    movieProperties_.setSoundMixMediaSupplier([this] {
        return soundManager_.soundMixMedia() ? 1 : 0;
    });
    movieProperties_.setSoundMixMediaSetter([this](int mixMedia) {
        soundManager_.setSoundMixMedia(mixMedia != 0);
    });

    inputHandler_.setCurrentFrameSupplier([this] {
        return currentFrame();
    });
    inputHandler_.setEventDispatcherSupplier([this] {
        return &eventDispatcher();
    });
    inputHandler_.setSpriteLocationSetter([this](int channel, int locH, int locV) {
        return spriteProperties_.setSpriteProp(channel, "loc", lingo::Datum::intPoint(locH, locV));
    });

    auto& context = vm_.builtinContext();
    context.movieProperties = &movieProperties_;
    refreshBuiltinNetProvider();
    context.soundManager = &soundManager_;
    context.spriteProperties = &spriteProperties_;
    context.timeoutManager = &timeoutManager_;
    context.externalParams = externalParams_;
    context.debugPlaybackEnabled = debugEnabled_;
    castLibManager_.installBuiltinCallbacks(context);
    context.xtraRegisteredResolver = [this](const std::string& name) {
        return xtraManager_.isXtraRegistered(name);
    };
    context.xtraInstanceCreator = [this](const std::string& name, const std::vector<lingo::Datum>& args) {
        return xtraManager_.createInstance(name, args);
    };
    context.xtraHandler = [this](const lingo::Datum::XtraInstance& instance,
                                 const std::string& handlerName,
                                 const std::vector<lingo::Datum>& args) {
        return xtraManager_.callHandler(instance, handlerName, args);
    };
    context.xtraPropertyGetter = [this](const lingo::Datum::XtraInstance& instance,
                                        const std::string& propertyName) {
        return xtraManager_.getProperty(instance, propertyName);
    };
    context.xtraPropertySetter = [this](const lingo::Datum::XtraInstance& instance,
                                        const std::string& propertyName,
                                        const lingo::Datum& value) {
        xtraManager_.setProperty(instance, propertyName, value);
    };
    context.spriteMethodHandler = [this](int channel,
                                         const std::string& methodName,
                                         const std::vector<lingo::Datum>& args) {
        return spriteProperties_.callSpriteMethod(channel, methodName, args);
    };
    context.imagePaletteResolver = [this](const lingo::Datum& paletteRef)
        -> std::optional<lingo::builtin::BuiltinContext::ResolvedPalette> {
        const auto* member = paletteRef.asCastMemberRef();
        if (member == nullptr || member->memberNum() <= 0) {
            return std::nullopt;
        }

        const int castLib = member->castLib > 0 ? member->castLib : 1;
        auto palette = bitmapResolver_.resolvePaletteByMember(castLib, member->memberNum());
        if (palette == nullptr) {
            return std::nullopt;
        }
        return lingo::builtin::BuiltinContext::ResolvedPalette{
            palette,
            lingo::Datum::CastMemberRef::of(id::CastLibId(castLib), id::MemberId(member->memberNum())),
            std::nullopt
        };
    };

    auto handlerLookupCache =
        std::make_shared<std::unordered_map<HandlerLookupKey, lingo::vm::HandlerRef, HandlerLookupKeyHash>>();
    auto scriptHandlerFinderCache =
        std::make_shared<std::unordered_map<
            ScriptHandlerFinderKey,
            std::optional<lingo::builtin::BuiltinContext::ScriptHandlerLocation>,
            ScriptHandlerFinderKeyHash>>();
    auto scriptRefTargetCache = std::make_shared<std::unordered_map<std::uint64_t, ResolvedScriptTarget>>();
    auto scriptInstanceTargetCache = std::make_shared<std::unordered_map<std::uint64_t, ResolvedScriptTarget>>();

    auto findHandlerInScript = [this, handlerLookupCache](const std::shared_ptr<chunks::ScriptChunk>& script,
                                                          const std::shared_ptr<const chunks::ScriptNamesChunk>& scriptNames,
                                                          const std::shared_ptr<const DirectorFile>& scriptOwner,
                                                          chunks::ScriptChunkType scriptType,
                                                          std::string_view handlerName)
        -> std::optional<lingo::vm::HandlerRef> {
        if (!script) {
            return std::nullopt;
        }
        const HandlerLookupKey key{script.get(), scriptNames.get(), lowerAscii(handlerName)};
        if (const auto cached = handlerLookupCache->find(key); cached != handlerLookupCache->end()) {
            return cached->second;
        }
        if (scriptNames) {
            if (auto handler = script->findHandlerPtr(handlerName, scriptNames.get())) {
                auto ref = lingo::vm::HandlerRef{script.get(), handler, script, scriptOwner, scriptNames, scriptType};
                handlerLookupCache->emplace(std::move(key), ref);
                return ref;
            }
        }
        if (auto handler = vm_.findHandler(*script, handlerName)) {
            handler->scriptOwner = script;
            handler->scriptType = scriptType;
            if (scriptOwner) {
                handler->fileOwner = scriptOwner;
            }
            if (scriptNames) {
                handler->scriptNamesOwner = scriptNames;
            }
            auto ref = *handler;
            handlerLookupCache->emplace(std::move(key), ref);
            return ref;
        }
        return std::nullopt;
    };

    auto resolveScriptRef = [this, scriptRefTargetCache](int castLib, int memberNum)
        -> std::optional<ResolvedScriptTarget> {
        castLib = castLib > 0 ? castLib : 1;
        if (memberNum <= 0) {
            return std::nullopt;
        }

        const auto cacheKey = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(castLib)) << 32U) |
                              static_cast<std::uint32_t>(memberNum);
        if (const auto cached = scriptRefTargetCache->find(cacheKey); cached != scriptRefTargetCache->end()) {
            return cached->second;
        }

        if (auto castLibRef = castLibManager_.getCastLib(castLib)) {
            if (auto resolvedScript = castLibRef->getScript(memberNum)) {
                const auto source = castLibRef->sourceFile();
                auto resolved = ResolvedScriptTarget{
                    resolvedScript,
                    source ? source->getScriptNamesForScript(resolvedScript) : castLibRef->scriptNames(),
                    source,
                    castLibRef->scriptTypeForScript(resolvedScript)
                };
                scriptRefTargetCache->emplace(cacheKey, resolved);
                return resolved;
            }
        }

        if (file_ != nullptr) {
            if (auto member = file_->getCastMemberByNumber(castLib, memberNum); member && member->isScript()) {
                if (auto resolvedScript = file_->getScriptForCastMember(member, file_->getMappedCastChunk(castLib))) {
                    const auto scriptType = resolvedScript->resolvedScriptType();
                    auto resolved = ResolvedScriptTarget{
                        resolvedScript,
                        file_->getScriptNamesForScript(resolvedScript),
                        file_,
                        scriptType
                    };
                    scriptRefTargetCache->emplace(cacheKey, resolved);
                    return resolved;
                }
            }
        }

        return std::nullopt;
    };

    auto canonicalScriptRefCandidates = [this](int castLib, int memberNum)
        -> std::vector<std::pair<int, int>> {
        castLib = castLib > 0 ? castLib : 1;
        std::vector<std::pair<int, int>> candidates;
        if (memberNum <= 0) {
            return candidates;
        }

        const auto memberName = castLibManager_.getMemberProp(castLib, memberNum, "name").stringValue();
        if (!memberName.empty() && memberName != "VOID") {
            if (const auto* canonical = castLibManager_.getMemberByName(0, memberName).asCastMemberRef()) {
                const int canonicalCastLib = canonical->castLib > 0 ? canonical->castLib : 1;
                const int canonicalMember = canonical->memberNum();
                if (canonicalMember > 0 && (canonicalCastLib != castLib || canonicalMember != memberNum)) {
                    candidates.emplace_back(canonicalCastLib, canonicalMember);
                }
            }
        }

        candidates.emplace_back(castLib, memberNum);
        return candidates;
    };

    auto resolveScriptInstance = [resolveScriptRef, scriptInstanceTargetCache](
        const lingo::Datum::ScriptInstanceRef& instance)
        -> std::optional<ResolvedScriptTarget> {
        if (const auto cached = scriptInstanceTargetCache->find(instance.identityId());
            cached != scriptInstanceTargetCache->end()) {
            return cached->second;
        }
        const auto& scriptRef = instance.scriptRef();
        if (!scriptRef.has_value()) {
            return std::nullopt;
        }
        auto resolved = resolveScriptRef(scriptRef->castLib, scriptRef->memberNum());
        if (resolved.has_value()) {
            scriptInstanceTargetCache->emplace(instance.identityId(), *resolved);
        }
        return resolved;
    };

    auto findEventHandler = [this, findHandlerInScript, resolveScriptRef, resolveScriptInstance, canonicalScriptRefCandidates](
        const event::EventTarget& target,
        std::string_view handlerName)
        -> std::optional<lingo::vm::HandlerRef> {
        if (target.behavior) {
            const auto& behaviorRef = target.behavior->behaviorRef();
            if (auto resolved = resolveScriptRef(behaviorRef.castLib(), behaviorRef.castMember());
                resolved && resolved->script == target.script) {
                if (auto handler = findHandlerInScript(resolved->script,
                                                       resolved->scriptNames,
                                                       resolved->scriptOwner,
                                                       resolved->scriptType,
                                                       handlerName)) {
                    return handler;
                }
            }
        }

        if (auto handler = findHandlerInScript(target.script,
                                               target.scriptNames,
                                               target.scriptOwner,
                                               target.scriptType,
                                               handlerName)) {
            return handler;
        }

        if (!target.scriptInstance || target.scriptInstance->type() != lingo::DatumType::ScriptInstanceRef) {
            return std::nullopt;
        }

        const auto* current = &target.scriptInstance->scriptInstanceValue();
        for (int depth = 0; current != nullptr && depth < lingo::vm::util::MAX_ANCESTOR_DEPTH; ++depth) {
            if (const auto& scriptRef = current->scriptRef(); scriptRef.has_value()) {
                for (const auto& [candidateCastLib, candidateMember] :
                     canonicalScriptRefCandidates(scriptRef->castLib, scriptRef->memberNum())) {
                    if (auto resolved = resolveScriptRef(candidateCastLib, candidateMember)) {
                        if (auto handler = findHandlerInScript(resolved->script,
                                                               resolved->scriptNames,
                                                               resolved->scriptOwner,
                                                               resolved->scriptType,
                                                               handlerName)) {
                            return handler;
                        }
                    }
                }
            } else if (auto resolved = resolveScriptInstance(*current)) {
                if (auto handler = findHandlerInScript(resolved->script,
                                                       resolved->scriptNames,
                                                       resolved->scriptOwner,
                                                       resolved->scriptType,
                                                       handlerName)) {
                    return handler;
                }
            }

            auto ancestor = current->ancestor();
            current = ancestor ? ancestor.get() : nullptr;
        }

        return std::nullopt;
    };

    context.scriptHandlerFinder = [findHandlerInScript, resolveScriptRef, canonicalScriptRefCandidates, scriptHandlerFinderCache](
        int castLib,
        int memberNum,
        const std::string& handlerName)
        -> std::optional<lingo::builtin::BuiltinContext::ScriptHandlerLocation> {
        const ScriptHandlerFinderKey key{castLib > 0 ? castLib : 1, memberNum, lowerAscii(handlerName)};
        if (const auto cached = scriptHandlerFinderCache->find(key); cached != scriptHandlerFinderCache->end()) {
            return cached->second;
        }

        std::optional<lingo::builtin::BuiltinContext::ScriptHandlerLocation> result;
        for (const auto& [candidateCastLib, candidateMember] : canonicalScriptRefCandidates(castLib, memberNum)) {
            const auto resolved = resolveScriptRef(candidateCastLib, candidateMember);
            if (!resolved.has_value()) {
                continue;
            }
            const auto handler = findHandlerInScript(resolved->script,
                                                     resolved->scriptNames,
                                                     resolved->scriptOwner,
                                                     resolved->scriptType,
                                                     handlerName);
            if (!handler || handler->script == nullptr || handler->handler == nullptr) {
                continue;
            }
            result = lingo::builtin::BuiltinContext::ScriptHandlerLocation{
                handler->script,
                handler->handler,
                handler->scriptOwner,
                handler->fileOwner,
                handler->scriptNamesOwner,
                handler->scriptType
            };
            break;
        }
        scriptHandlerFinderCache->emplace(std::move(key), result);
        return result;
    };

    auto executeTarget = [this, findEventHandler](const event::EventTarget& target,
                                                  std::string_view handlerName,
                                                  const std::vector<lingo::Datum>& args)
        -> std::optional<std::pair<lingo::Datum, bool>> {
        auto handler = findEventHandler(target, handlerName);
        if (!handler || handler->script == nullptr) {
            return std::nullopt;
        }
        lingo::Datum receiver = lingo::Datum::voidValue();
        if (target.behavior) {
            receiver = target.behavior->toDatum();
        } else if (target.scriptInstance) {
            receiver = *target.scriptInstance;
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

    auto invokeTarget = [executeTarget](const event::EventTarget& target,
                                        std::string_view handlerName,
                                        const std::vector<lingo::Datum>& args) {
        auto result = executeTarget(target, handlerName, args);
        if (!result.has_value()) {
            return event::HandlerResult{false, false};
        }
        return event::HandlerResult{true, result->second};
    };

    context.callTargetHandler = [this, executeTarget](const lingo::Datum& target,
                                                      const std::string& handlerName,
                                                      const std::vector<lingo::Datum>& args) {
        if (target.type() == lingo::DatumType::ScriptInstanceRef) {
            event::EventTarget eventTarget;
            eventTarget.kind = event::EventTargetKind::ScriptInstance;
            eventTarget.scriptInstance = target;
            const auto result = executeTarget(eventTarget, handlerName, args);
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

        const auto scriptInstances = spriteProperties_.getScriptInstanceList(channel);
        if (!scriptInstances.has_value()) {
            return spriteProperties_.callSpriteMethod(channel, handlerName, args);
        }

        lingo::Datum lastResult = lingo::Datum::voidValue();
        for (const auto& scriptInstance : *scriptInstances) {
            if (scriptInstance.type() != lingo::DatumType::ScriptInstanceRef) {
                continue;
            }
            event::EventTarget eventTarget;
            eventTarget.kind = event::EventTargetKind::ScriptInstance;
            eventTarget.channel = channel;
            eventTarget.scriptInstance = scriptInstance;
            const auto result = executeTarget(eventTarget, handlerName, args);
            if (result.has_value()) {
                lastResult = result->first;
            }
        }
        return lastResult;
    };
    context.sendAllSpritesHandler = [this](const std::string& handlerName,
                                           const std::vector<lingo::Datum>& args) {
        std::vector<int> channels;
        channels.reserve(stageRenderer_.spriteRegistry().getAll().size());
        for (const auto& [channel, sprite] : stageRenderer_.spriteRegistry().getAll()) {
            if (sprite != nullptr) {
                channels.push_back(channel);
            }
        }
        std::ranges::sort(channels);

        for (const int channel : channels) {
            eventDispatcher().dispatchSpriteEvent(channel, handlerName, args);
        }
        return lingo::Datum::voidValue();
    };
    context.alertHookHandler = [this, executeTarget](const std::string& alertType, const std::string& text) {
        const auto& hook = movieProperties_.alertHook();
        if (hook.type() != lingo::DatumType::ScriptInstanceRef) {
            return false;
        }

        event::EventTarget eventTarget;
        eventTarget.kind = event::EventTargetKind::ScriptInstance;
        eventTarget.scriptInstance = hook;
        try {
            const auto result = executeTarget(
                eventTarget,
                "alertHook",
                {lingo::Datum::of(alertType), lingo::Datum::of(text)});
            return result.has_value() && result->first.boolValue();
        } catch (...) {
            return false;
        }
    };

    auto collectMovieScriptTargets = [this] {
        std::vector<event::EventDispatcher::MovieScriptTarget> targets;
        if (file_ != nullptr) {
            targets.reserve(file_->scripts().size());
            for (const auto& script : file_->scripts()) {
                const auto scriptType = script ? script->resolvedScriptType() : chunks::ScriptChunkType::Unknown;
                if (!script || scriptType != chunks::ScriptChunkType::MovieScript) {
                    continue;
                }
                targets.push_back(event::EventDispatcher::MovieScriptTarget{
                    script,
                    file_->getScriptNamesForScript(script),
                    file_,
                    scriptType
                });
            }
        }

        for (const auto& [_, castLib] : castLibManager_.castLibs()) {
            if (!castLib || !castLib->isExternal() || !castLib->isLoaded()) {
                continue;
            }
            const auto source = castLib->sourceFile();
            const auto defaultNames = castLib->scriptNames();
            for (const auto& script : castLib->allScripts()) {
                const auto scriptType = castLib->scriptTypeForScript(script);
                if (!script || scriptType != chunks::ScriptChunkType::MovieScript) {
                    continue;
                }
                targets.push_back(event::EventDispatcher::MovieScriptTarget{
                    script,
                    source ? source->getScriptNamesForScript(script) : defaultNames,
                    source,
                    scriptType
                });
            }
        }
        return targets;
    };

    auto collectGlobalHandlerTargets = [this] {
        std::vector<event::EventDispatcher::MovieScriptTarget> targets;
        if (file_ != nullptr) {
            targets.reserve(file_->scripts().size());
            for (const auto& script : file_->scripts()) {
                const auto scriptType = script ? script->resolvedScriptType() : chunks::ScriptChunkType::Unknown;
                if (!script || !lingo::vm::LingoVM::isGlobalHandlerScriptType(scriptType)) {
                    continue;
                }
                targets.push_back(event::EventDispatcher::MovieScriptTarget{
                    script,
                    file_->getScriptNamesForScript(script),
                    file_,
                    scriptType
                });
            }
        }

        for (const auto& [_, castLib] : castLibManager_.castLibs()) {
            if (!castLib || !castLib->isExternal() || !castLib->isLoaded()) {
                continue;
            }
            const auto source = castLib->sourceFile();
            const auto defaultNames = castLib->scriptNames();
            for (const auto& script : castLib->allScripts()) {
                const auto scriptType = castLib->scriptTypeForScript(script);
                if (!script || !lingo::vm::LingoVM::isGlobalHandlerScriptType(scriptType)) {
                    continue;
                }
                targets.push_back(event::EventDispatcher::MovieScriptTarget{
                    script,
                    source ? source->getScriptNamesForScript(script) : defaultNames,
                    source,
                    scriptType
                });
            }
        }
        return targets;
    };

    vm_.setGlobalHandlerFinder([this, collectGlobalHandlerTargets](std::string_view handlerName)
        -> std::optional<lingo::vm::HandlerRef> {
        for (const auto& target : collectGlobalHandlerTargets()) {
            if (!target.script) {
                continue;
            }
            if (target.scriptNames) {
                if (auto handler = target.script->findHandlerPtr(handlerName, target.scriptNames.get())) {
                    return lingo::vm::HandlerRef{
                        target.script.get(),
                        handler,
                        target.script,
                        target.scriptOwner,
                        target.scriptNames,
                        target.scriptType
                    };
                }
            }
            if (auto handler = vm_.findHandler(*target.script, handlerName)) {
                handler->scriptOwner = target.script;
                handler->scriptType = target.scriptType;
                if (target.scriptOwner) {
                    handler->fileOwner = target.scriptOwner;
                }
                if (target.scriptNames) {
                    handler->scriptNamesOwner = target.scriptNames;
                }
                return handler;
            }
        }
        return std::nullopt;
    });

    auto legacyTimeoutHandlerName = [this]() -> std::optional<std::string> {
        const lingo::Datum script = movieProperties_.getMovieProp("timeoutScript");
        if (script.isVoid() || script.isNull() || (script.isString() && script.stringValue().empty())) {
            return std::string("timeOut");
        }
        return legacyEventHandlerName(script);
    };

    auto dispatchLegacyMovieTimeout = [this, invokeTarget, collectMovieScriptTargets, legacyTimeoutHandlerName] {
        const int timeoutLength = movieProperties_.getMovieProp("timeoutLength").intValue();
        if (timeoutLength <= 0) {
            legacyTimeoutArmed_ = true;
            return;
        }

        const int elapsedTicks = movieProperties_.getMovieProp("timeoutLapsed").intValue();
        if (elapsedTicks < timeoutLength) {
            legacyTimeoutArmed_ = true;
            return;
        }
        if (!legacyTimeoutArmed_) {
            return;
        }

        const auto handlerName = legacyTimeoutHandlerName();
        if (!handlerName.has_value() || handlerName->empty()) {
            return;
        }

        bool handled = false;
        eventDispatcher().resetEventStopped();
        for (const auto& movie : collectMovieScriptTargets()) {
            if (!movie.script || movie.scriptType != chunks::ScriptChunkType::MovieScript) {
                continue;
            }

            event::EventTarget target;
            target.kind = event::EventTargetKind::MovieScript;
            target.script = movie.script;
            target.scriptNames = movie.scriptNames;
            target.scriptOwner = movie.scriptOwner;
            target.scriptType = movie.scriptType;
            const auto result = invokeTarget(target, *handlerName, {});
            if (!result.handled) {
                continue;
            }

            handled = true;
            if (!result.passed || eventDispatcher().isEventStopped()) {
                break;
            }
        }
        if (!handled) {
            (void)vm_.callHandler(*handlerName, {});
        }
        legacyTimeoutArmed_ = false;
    };

    inputHandler_.setLegacyEventScriptDispatcher([this, invokeTarget, collectMovieScriptTargets](PlayerEvent event) {
        const std::string_view propName = legacyEventScriptProperty(event);
        if (propName.empty()) {
            return InputHandler::LegacyEventScriptResult{};
        }

        const auto handlerName = legacyEventHandlerName(movieProperties_.getMovieProp(propName));
        if (!handlerName.has_value()) {
            return InputHandler::LegacyEventScriptResult{};
        }

        InputHandler::LegacyEventScriptResult aggregate;
        eventDispatcher().resetEventStopped();
        for (const auto& movie : collectMovieScriptTargets()) {
            if (!movie.script || movie.scriptType != chunks::ScriptChunkType::MovieScript) {
                continue;
            }

            event::EventTarget target;
            target.kind = event::EventTargetKind::MovieScript;
            target.script = movie.script;
            target.scriptNames = movie.scriptNames;
            target.scriptOwner = movie.scriptOwner;
            target.scriptType = movie.scriptType;
            const auto result = invokeTarget(target, *handlerName, {});
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
    eventDispatcher().setMovieScriptSupplier(collectMovieScriptTargets);
    eventDispatcher().setRespondsPredicate([findEventHandler](const event::EventTarget& target,
                                                              std::string_view handlerName) {
        return findEventHandler(target, handlerName).has_value();
    });
    eventDispatcher().setHandlerInvoker([invokeTarget](const event::EventTarget& target,
                                                       std::string_view handlerName,
                                                       const std::vector<lingo::Datum>& args) {
        return invokeTarget(target, handlerName, args);
    });
    timeoutSystemEventDispatcher_ = [this, invokeTarget](std::string_view handlerName) {
        timeoutManager_.dispatchSystemEvent(handlerName, [invokeTarget](const lingo::Datum& target,
                                                                       std::string_view systemHandler) {
            event::EventTarget eventTarget;
            eventTarget.kind = event::EventTargetKind::ScriptInstance;
            eventTarget.scriptInstance = target;
            (void)invokeTarget(eventTarget, systemHandler, {});
        });
    };
    timeoutProcessor_ = [this, invokeTarget, dispatchLegacyMovieTimeout] {
        timeoutManager_.processTimeouts([this, invokeTarget](const timeout::TimeoutEntry& entry) {
            vm_.resetErrorState();
            lingo::Datum resolvedTarget = entry.target;
            if (resolvedTarget.type() != lingo::DatumType::ScriptInstanceRef && !resolvedTarget.isVoid()) {
                const auto resolved = vm_.callHandler("getobject", {resolvedTarget});
                if (resolved.type() == lingo::DatumType::ScriptInstanceRef) {
                    resolvedTarget = resolved;
                }
                vm_.resetErrorState();
            }

            const std::vector<lingo::Datum> args{lingo::Datum::timeoutRef(entry.name)};
            if (resolvedTarget.type() == lingo::DatumType::ScriptInstanceRef) {
                event::EventTarget target;
                target.kind = event::EventTargetKind::ScriptInstance;
                target.scriptInstance = resolvedTarget;
                const auto result = invokeTarget(target, entry.handler, args);
                if (result.handled) {
                    return;
                }
            }

            (void)vm_.callHandler(entry.handler, args);
        });
        dispatchLegacyMovieTimeout();
    };
    frameContext_.setTimeoutEventDispatcher(timeoutSystemEventDispatcher_);
    frameContext_.setActorListDispatcher([this, invokeTarget](std::string_view handlerName) {
        const auto& actorList = movieProperties_.actorList();
        if (!actorList.isList()) {
            return;
        }

        std::vector<lingo::Datum> actors;
        for (const auto& item : actorList.listValue().items()) {
            if (item.type() == lingo::DatumType::ScriptInstanceRef) {
                actors.push_back(item);
            }
        }

        for (const auto& actor : actors) {
            event::EventTarget target;
            target.kind = event::EventTargetKind::ScriptInstance;
            target.scriptInstance = actor;
            (void)invokeTarget(target, handlerName, {actor});
        }
    });
}

void Player::prepareMovieFoundation() {
    TickDeadlineGuard deadlineGuard(vm_);
    applyInitialBuiltinVariables();
    (void)preloadAllCasts();
    castLibManager_.preloadCasts(2);
    if (timeoutSystemEventDispatcher_) {
        timeoutSystemEventDispatcher_("prepareMovie");
    }
    eventDispatcher().dispatchToMovieScripts(PlayerEvent::PrepareMovie);
    castLibManager_.preloadCasts(1);
    frameContext_.initializeFirstFrame();
    frameContext_.dispatchBeginSpriteEvents();
    if (timeoutSystemEventDispatcher_) {
        timeoutSystemEventDispatcher_("prepareFrame");
    }
    eventDispatcher().dispatchGlobalEvent(PlayerEvent::PrepareFrame);
    eventDispatcher().dispatchToMovieScripts(PlayerEvent::StartMovie);
    if (timeoutSystemEventDispatcher_) {
        timeoutSystemEventDispatcher_("startMovie");
    }
    eventDispatcher().dispatchGlobalEvent(PlayerEvent::EnterFrame);
    if (timeoutSystemEventDispatcher_) {
        timeoutSystemEventDispatcher_("exitFrame");
    }
    eventDispatcher().dispatchSpriteAndMovieEvent(handlerName(PlayerEvent::ExitFrame));
    castLibManager_.preloadCasts(1);
}

void Player::applyInitialBuiltinVariables() {
    for (const auto& [name, value] : initialBuiltinVariables_) {
        if (vm_.globals().find(name) == vm_.globals().end()) {
            vm_.setGlobal(name, value.deepCopy());
        }
    }
}

void Player::processUpdatingObjects() {
    if (updatingObjects_.empty() || !vm_.builtinContext().callTargetHandler) {
        return;
    }

    const auto snapshot = updatingObjects_;
    for (const auto& target : snapshot) {
        if (target.type() != lingo::DatumType::ScriptInstanceRef) {
            continue;
        }
        try {
            (void)vm_.builtinContext().callTargetHandler(target, "update", {});
        } catch (...) {
            // Java's update-provider dispatch suppresses per-target handler failures.
        }
    }
}

void Player::refreshDebugControllerGlobals() {
    if (!debugController_) {
        return;
    }

    std::map<std::string, lingo::Datum> globals;
    for (const auto& [name, value] : vm_.globals()) {
        globals.emplace(name, value);
    }
    debugController_->setGlobalsSnapshot(std::move(globals));
}

net::NetProvider& Player::activeNetProvider() {
    return overrideNetProvider_ != nullptr ? *overrideNetProvider_ : static_cast<net::NetProvider&>(netManager_);
}

const net::NetProvider& Player::activeNetProvider() const {
    return overrideNetProvider_ != nullptr ? *overrideNetProvider_ : static_cast<const net::NetProvider&>(netManager_);
}

void Player::refreshBuiltinNetProvider() {
    vm_.builtinContext().netManager = &activeNetProvider();
}

void Player::launchVmWorker(std::function<void()> work, std::function<void()> callback) {
    if (vmRunning_.exchange(true)) {
        return;
    }

    joinVmWorker();
    std::lock_guard<std::mutex> lock(vmThreadMutex_);
    vmThread_ = std::thread([this, work = std::move(work), callback = std::move(callback)]() mutable {
        try {
            if (work) {
                work();
            }
        } catch (const std::exception& error) {
            handleTraceError("Async VM error", error.what());
        } catch (...) {
            handleTraceError("Async VM error", "unknown exception");
        }

        vmRunning_.store(false);
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
    std::thread threadToJoin;
    {
        std::lock_guard<std::mutex> lock(vmThreadMutex_);
        if (!vmThread_.joinable()) {
            return;
        }
        if (vmThread_.get_id() == std::this_thread::get_id()) {
            vmThread_.detach();
            return;
        }
        threadToJoin = std::move(vmThread_);
    }

    threadToJoin.join();
}

void Player::loadCastFromNetCache(int castLibNumber, const std::string& fileName) {
    if (fileName.empty()) {
        return;
    }

    if (auto data = netManager_.getCachedData(fileName); data.has_value()) {
        (void)loadExternalCastFromCachedData(castLibNumber, *data);
        return;
    }

    const auto baseName = util::getFileNameWithoutExtension(util::getFileName(fileName));
    if (!baseName.empty()) {
        if (auto cached = castLibManager_.getCachedExternalData(baseName); cached.has_value()) {
            (void)loadExternalCastFromCachedData(castLibNumber, *cached);
            return;
        }
    }

    if (castDataRequestCallback_) {
        castDataRequestCallback_(castLibNumber, fileName);
        return;
    }
    (void)activeNetProvider().preloadNetThing(fileName);
}

void Player::handleExternalCastFetch(const std::string& url, const std::vector<std::uint8_t>& data) {
    castLibManager_.cacheExternalData(url, data);
    try {
        auto castNumbers = castLibManager_.getRequestedExternalCastSlots(url);
        for (const int castLibNumber : castLibManager_.getMatchingCastLibNumbersByUrl(url)) {
            if (std::find(castNumbers.begin(), castNumbers.end(), castLibNumber) == castNumbers.end()) {
                castNumbers.push_back(castLibNumber);
            }
        }

        for (const int castLibNumber : castNumbers) {
            (void)loadExternalCastFromCachedData(castLibNumber, data);
        }
    } catch (...) {
    }
}

} // namespace libreshockwave::player
