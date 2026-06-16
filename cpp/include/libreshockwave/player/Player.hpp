#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/lingo/vm/LingoVM.hpp"
#include "libreshockwave/lingo/xtra/XtraManager.hpp"
#include "libreshockwave/player/BitmapResolver.hpp"
#include "libreshockwave/player/CursorManager.hpp"
#include "libreshockwave/player/ExternalCastLoadEvent.hpp"
#include "libreshockwave/player/InputHandler.hpp"
#include "libreshockwave/player/MovieProperties.hpp"
#include "libreshockwave/player/PlayerEventInfo.hpp"
#include "libreshockwave/player/PlayerState.hpp"
#include "libreshockwave/player/SpriteProperties.hpp"
#include "libreshockwave/player/audio/SoundManager.hpp"
#include "libreshockwave/player/cast/CastLibManager.hpp"
#include "libreshockwave/player/frame/FrameContext.hpp"
#include "libreshockwave/player/input/InputState.hpp"
#include "libreshockwave/player/net/NetManager.hpp"
#include "libreshockwave/player/render/pipeline/BitmapCache.hpp"
#include "libreshockwave/player/render/pipeline/FrameRenderPipeline.hpp"
#include "libreshockwave/player/render/pipeline/FrameSnapshot.hpp"
#include "libreshockwave/player/render/pipeline/SpriteBaker.hpp"
#include "libreshockwave/player/render/pipeline/StageRenderer.hpp"
#include "libreshockwave/player/timeout/TimeoutManager.hpp"
#include "libreshockwave/player/xtra/SocketMultiuserBridge.hpp"

namespace libreshockwave::player::behavior {
class BehaviorManager;
}

namespace libreshockwave::player::event {
class EventDispatcher;
}

namespace libreshockwave::player::score {
class ScoreNavigator;
}

namespace libreshockwave::player::debug {
class DebugControllerApi;
}

namespace libreshockwave::player::render::output {
class SimpleTextRenderer;
}

namespace libreshockwave::player {

class ExternalCastLoadHandler;

class Player {
public:
    using ErrorListener = std::function<void(std::string_view message, std::string_view errorDetail)>;
    using CastDataRequestCallback = cast::CastLibManager::CastDataRequestCallback;

    explicit Player(std::shared_ptr<DirectorFile> file = nullptr);
    Player(std::shared_ptr<DirectorFile> file, net::NetProvider* netProvider);
    Player(std::shared_ptr<DirectorFile> file,
           net::NetProvider* netProvider,
           CastDataRequestCallback castDataRequestCallback);
    ~Player();

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player(Player&&) = delete;
    Player& operator=(Player&&) = delete;

    [[nodiscard]] std::shared_ptr<DirectorFile> file() const;
    [[nodiscard]] frame::FrameContext& frameContext();
    [[nodiscard]] score::ScoreNavigator& navigator();
    [[nodiscard]] behavior::BehaviorManager& behaviorManager();
    [[nodiscard]] event::EventDispatcher& eventDispatcher();
    [[nodiscard]] render::pipeline::StageRenderer& stageRenderer();
    [[nodiscard]] net::NetManager& netManager();
    [[nodiscard]] lingo::xtra::XtraManager& xtraManager();
    [[nodiscard]] MovieProperties& movieProperties();
    [[nodiscard]] SpriteProperties& spriteProperties();
    [[nodiscard]] cast::CastLibManager& castLibManager();
    [[nodiscard]] timeout::TimeoutManager& timeoutManager();
    [[nodiscard]] audio::SoundManager& soundManager();
    [[nodiscard]] render::pipeline::BitmapCache& bitmapCache();
    [[nodiscard]] render::pipeline::SpriteBaker& spriteBaker();
    [[nodiscard]] render::pipeline::FrameRenderPipeline& frameRenderPipeline();
    [[nodiscard]] input::InputState& inputState();
    [[nodiscard]] BitmapResolver& bitmapResolver();
    [[nodiscard]] CursorManager& cursorManager();
    [[nodiscard]] InputHandler& inputHandler();
    [[nodiscard]] lingo::vm::LingoVM& vm();
    [[nodiscard]] lingo::builtin::BuiltinRegistry& builtinRegistry();
    [[nodiscard]] lingo::builtin::BuiltinContext& builtinContext();

    void receiveUpdate(const lingo::Datum& target);
    void removeUpdate(const lingo::Datum& target);

    [[nodiscard]] PlayerState state() const;
    [[nodiscard]] int currentFrame() const;
    [[nodiscard]] int effectiveFrame() const;
    [[nodiscard]] int frameCount() const;
    [[nodiscard]] int tempo() const;
    [[nodiscard]] int baseTempo() const;
    [[nodiscard]] int getFrameForLabel(std::string_view label) const;
    [[nodiscard]] std::set<std::string> getFrameLabels() const;
    void setTempo(int tempo);

    void setEventListener(std::function<void(const PlayerEventInfo&)> listener);
    void setCastLoadedListener(std::function<void()> listener);
    void setExternalCastLoadListener(std::function<void(const ExternalCastLoadEvent&)> listener);
    void setErrorListener(ErrorListener listener);
    void addExternalCastLoadHandler(ExternalCastLoadHandler* handler);
    void setDebugEnabled(bool enabled);
    void setSlowHandlerWarningThresholdMs(std::int64_t milliseconds);
    void setDebugController(std::shared_ptr<debug::DebugControllerApi> controller);
    [[nodiscard]] std::shared_ptr<debug::DebugControllerApi> getDebugController() const;
    void setNetProvider(net::NetProvider* provider);
    void setAudioBackend(audio::AudioBackend* backend);
    void setTextRenderer(render::output::TextRenderer* renderer);
    void registerMultiuserXtra(lingo::xtra::MultiuserNetBridge& bridge);
    void setExternalParams(std::vector<std::pair<std::string, std::string>> params);
    [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& externalParams() const;
    void setInitialBuiltinVariable(std::string variableName, lingo::Datum defaultValue);
    void setInitialBuiltinVariables(std::vector<std::pair<std::string, lingo::Datum>> values);
    [[nodiscard]] bool debugEnabled() const;
    void dumpScriptInfo() const;
    void dumpScriptInfo(std::ostream& out) const;
    [[nodiscard]] std::vector<lingo::vm::LingoVM::CallStackFrame> getLingoCallStack() const;
    [[nodiscard]] std::string formatLingoCallStack() const;
    [[nodiscard]] std::string getRecentScriptErrorMessage(std::int64_t maxAgeMs) const;
    [[nodiscard]] std::string getRecentScriptErrorStack(std::int64_t maxAgeMs) const;
    [[nodiscard]] bool isVmRunning() const;

    void play();
    void playAsync(std::function<void()> onReady = {});
    void pause();
    void resume();
    void stop();
    void shutdown();
    void goToFrame(int frame);
    void goToLabel(std::string_view label);
    void stepFrame();
    void stepFrameAsync(std::function<void()> onComplete = {});
    [[nodiscard]] bool tick();
    void tickAsync(std::function<void()> onComplete = {});
    [[nodiscard]] bool fireTestError(std::string_view errorMessage);
    void onNetFetchComplete(std::string_view url, const std::vector<std::uint8_t>& data);
    [[nodiscard]] int preloadAllCasts();
    void onSynchronousExternalCastLoad(int castLibNumber);
    [[nodiscard]] bool loadExternalCastFromCachedData(int castLibNumber,
                                                      const std::vector<std::uint8_t>& data);
    [[nodiscard]] bool loadExternalCastFromCachedData(int castLibNumber,
                                                      const std::vector<std::uint8_t>& data,
                                                      std::function<void()> afterLoad);
    [[nodiscard]] render::pipeline::FrameSnapshot frameSnapshot();

private:
    class PlayerTraceListener;

    Player(std::shared_ptr<DirectorFile> file, bool registerSocketMultiuserXtra);
    void wireComponents();
    void prepareMovieFoundation();
    void executeFrameCycle(bool processUpdates);
    void applyInitialBuiltinVariables();
    void processUpdatingObjects();
    void refreshDebugControllerGlobals();
    [[nodiscard]] net::NetProvider& activeNetProvider();
    [[nodiscard]] const net::NetProvider& activeNetProvider() const;
    void refreshBuiltinNetProvider();
    void launchVmWorker(std::function<void()> work, std::function<void()> callback);
    void joinVmWorker();
    void handleTraceError(std::string_view message, std::string_view errorDetail);
    [[nodiscard]] bool recentScriptErrorIsFresh(std::int64_t maxAgeMs) const;
    void loadCastFromNetCache(int castLibNumber, const std::string& fileName);
    void handleExternalCastFetch(const std::string& url, const std::vector<std::uint8_t>& data);
    [[nodiscard]] bool applyExternalCastDataNow(int castLibNumber,
                                                const std::vector<std::uint8_t>& data,
                                                std::function<void()> afterLoad);
    void notifyExternalCastLoaded(int castLibNumber);

    std::shared_ptr<DirectorFile> file_;
    frame::FrameContext frameContext_;
    render::pipeline::StageRenderer stageRenderer_;
    net::NetManager netManager_;
    lingo::xtra::XtraManager xtraManager_;
    std::unique_ptr<xtra::SocketMultiuserBridge> socketMultiuserBridge_;
    MovieProperties movieProperties_;
    SpriteProperties spriteProperties_;
    cast::CastLibManager castLibManager_;
    timeout::TimeoutManager timeoutManager_;
    audio::SoundManager soundManager_;
    render::pipeline::BitmapCache bitmapCache_;
    render::pipeline::SpriteBaker spriteBaker_;
    std::unique_ptr<render::output::SimpleTextRenderer> defaultTextRenderer_;
    render::pipeline::FrameRenderPipeline frameRenderPipeline_;
    input::InputState inputState_;
    BitmapResolver bitmapResolver_;
    CursorManager cursorManager_;
    InputHandler inputHandler_;
    lingo::vm::LingoVM vm_;
    std::shared_ptr<lingo::vm::TraceListener> playerTraceListener_;
    PlayerState state_{PlayerState::Stopped};
    std::atomic_bool vmRunning_{false};
    std::mutex vmThreadMutex_;
    std::thread vmThread_;
    net::NetProvider* overrideNetProvider_{nullptr};
    CastDataRequestCallback castDataRequestCallback_;
    int tempo_{15};
    bool debugEnabled_{false};
    std::function<void(const PlayerEventInfo&)> eventListener_;
    std::function<void()> castLoadedListener_;
    std::function<void(const ExternalCastLoadEvent&)> externalCastLoadListener_;
    std::shared_ptr<debug::DebugControllerApi> debugController_;
    ErrorListener errorListener_;
    std::vector<ExternalCastLoadHandler*> externalCastLoadHandlers_;
    std::vector<std::pair<std::string, std::string>> externalParams_;
    std::vector<std::pair<std::string, lingo::Datum>> initialBuiltinVariables_;
    std::vector<lingo::Datum> updatingObjects_;
    std::string lastScriptErrorMessage_;
    std::string lastScriptErrorStack_;
    std::int64_t lastScriptErrorTimeMs_{0};
    bool legacyTimeoutArmed_{true};
    std::function<void()> timeoutProcessor_;
    std::function<void(std::string_view handlerName)> timeoutSystemEventDispatcher_;
};

} // namespace libreshockwave::player
