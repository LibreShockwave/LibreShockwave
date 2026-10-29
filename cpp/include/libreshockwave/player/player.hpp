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

#include "libreshockwave/director_file.hpp"
#include "libreshockwave/lingo/vm/lingo_vm.hpp"
#include "libreshockwave/lingo/xtra/xtra_manager.hpp"
#include "libreshockwave/player/bitmap_resolver.hpp"
#include "libreshockwave/player/cursor_manager.hpp"
#include "libreshockwave/player/external_cast_load_event.hpp"
#include "libreshockwave/player/input_handler.hpp"
#include "libreshockwave/player/movie_properties.hpp"
#include "libreshockwave/player/player_event_info.hpp"
#include "libreshockwave/player/player_state.hpp"
#include "libreshockwave/player/sprite_properties.hpp"
#include "libreshockwave/player/audio/sound_manager.hpp"
#include "libreshockwave/player/cast/cast_lib_manager.hpp"
#include "libreshockwave/player/frame/frame_context.hpp"
#include "libreshockwave/player/input/input_state.hpp"
#include "libreshockwave/player/net/net_manager.hpp"
#include "libreshockwave/player/render/pipeline/bitmap_cache.hpp"
#include "libreshockwave/player/render/pipeline/frame_render_pipeline.hpp"
#include "libreshockwave/player/render/pipeline/frame_snapshot.hpp"
#include "libreshockwave/player/render/pipeline/sprite_baker.hpp"
#include "libreshockwave/player/render/pipeline/stage_renderer.hpp"
#include "libreshockwave/player/timeout/timeout_manager.hpp"
#include "libreshockwave/player/xtra/socket_multiuser_bridge.hpp"

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
    using ErrorListener = std::function<void(std::string_view message, std::string_view error_detail)>;
    using CastDataRequestCallback = cast::CastLibManager::CastDataRequestCallback;

    explicit Player(std::shared_ptr<DirectorFile> file = nullptr);
    Player(std::shared_ptr<DirectorFile> file, net::NetProvider* net_provider);
    Player(std::shared_ptr<DirectorFile> file,
           net::NetProvider* net_provider,
           CastDataRequestCallback cast_data_request_callback);
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
    void setInitialBuiltinVariable(std::string variable_name, lingo::Datum default_value);
    void setInitialBuiltinVariables(std::vector<std::pair<std::string, lingo::Datum>> values);
    [[nodiscard]] bool debugEnabled() const;
    void dumpScriptInfo() const;
    void dumpScriptInfo(std::ostream& out) const;
    [[nodiscard]] std::vector<lingo::vm::LingoVM::CallStackFrame> getLingoCallStack() const;
    [[nodiscard]] std::string formatLingoCallStack() const;
    [[nodiscard]] std::string getRecentScriptErrorMessage(std::int64_t max_age_ms) const;
    [[nodiscard]] std::string getRecentScriptErrorStack(std::int64_t max_age_ms) const;
    [[nodiscard]] bool isVmRunning() const;

    void play();
    void playAsync(std::function<void()> on_ready = {});
    void pause();
    void resume();
    void stop();
    void shutdown();
    void goToFrame(int frame);
    void goToLabel(std::string_view label);
    void stepFrame();
    void stepFrameAsync(std::function<void()> on_complete = {});
    [[nodiscard]] bool tick();
    void tickAsync(std::function<void()> on_complete = {});
    [[nodiscard]] bool fireTestError(std::string_view error_message);
    void onNetFetchComplete(std::string_view url, const std::vector<std::uint8_t>& data);
    [[nodiscard]] int preloadAllCasts();
    [[nodiscard]] bool externalCastsReady() const;
    [[nodiscard]] bool networkReady() const;
    void onSynchronousExternalCastLoad(int cast_lib_number);
    [[nodiscard]] bool loadExternalCastFromCachedData(int cast_lib_number,
                                                      const std::vector<std::uint8_t>& data);
    [[nodiscard]] bool loadExternalCastFromCachedData(int cast_lib_number,
                                                      const std::vector<std::uint8_t>& data,
                                                      std::function<void()> after_load);
    [[nodiscard]] render::pipeline::FrameSnapshot frameSnapshot();

private:
    class PlayerTraceListener;

    Player(std::shared_ptr<DirectorFile> file, bool register_socket_multiuser_xtra);
    void wireComponents();
    void prepareMovieFoundation();
    void executeFrameCycle(bool process_updates);
    void applyInitialBuiltinVariables();
    void processUpdatingObjects();
    void refreshDebugControllerGlobals();
    [[nodiscard]] net::NetProvider& activeNetProvider();
    [[nodiscard]] const net::NetProvider& activeNetProvider() const;
    void refreshBuiltinNetProvider();
    void launchVmWorker(std::function<void()> work, std::function<void()> callback);
    void joinVmWorker();
    void handleTraceError(std::string_view message, std::string_view error_detail);
    [[nodiscard]] bool recentScriptErrorIsFresh(std::int64_t max_age_ms) const;
    [[nodiscard]] lingo::Datum currentStageImageDatum();
    void loadCastFromNetCache(int cast_lib_number, const std::string& file_name);
    void handleExternalCastFetch(const std::string& url, const std::vector<std::uint8_t>& data);
    [[nodiscard]] bool applyExternalCastDataNow(int cast_lib_number,
                                                const std::vector<std::uint8_t>& data,
                                                std::function<void()> after_load);
    void notifyExternalCastLoaded(int cast_lib_number);

    std::shared_ptr<DirectorFile> file_;
    frame::FrameContext frame_context_;
    render::pipeline::StageRenderer stage_renderer_;
    net::NetManager net_manager_;
    lingo::xtra::XtraManager xtra_manager_;
    std::unique_ptr<xtra::SocketMultiuserBridge> socket_multiuser_bridge_;
    MovieProperties movie_properties_;
    SpriteProperties sprite_properties_;
    cast::CastLibManager cast_lib_manager_;
    timeout::TimeoutManager timeout_manager_;
    audio::SoundManager sound_manager_;
    render::pipeline::BitmapCache bitmap_cache_;
    render::pipeline::SpriteBaker sprite_baker_;
    std::unique_ptr<render::output::SimpleTextRenderer> default_text_renderer_;
    render::pipeline::FrameRenderPipeline frame_render_pipeline_;
    input::InputState input_state_;
    BitmapResolver bitmap_resolver_;
    CursorManager cursor_manager_;
    InputHandler input_handler_;
    lingo::vm::LingoVM vm_;
    std::shared_ptr<lingo::vm::TraceListener> player_trace_listener_;
    PlayerState state_{PlayerState::Stopped};
    std::atomic_bool vm_running_{false};
    std::mutex vm_thread_mutex_;
    std::thread vm_thread_;
    net::NetProvider* override_net_provider_{nullptr};
    CastDataRequestCallback cast_data_request_callback_;
    int tempo_{15};
    bool debug_enabled_{false};
    std::function<void(const PlayerEventInfo&)> event_listener_;
    std::function<void()> cast_loaded_listener_;
    std::function<void()> invalidate_script_resolution_caches_;
    std::function<void(const ExternalCastLoadEvent&)> external_cast_load_listener_;
    std::shared_ptr<debug::DebugControllerApi> debug_controller_;
    ErrorListener error_listener_;
    std::vector<ExternalCastLoadHandler*> external_cast_load_handlers_;
    std::vector<std::pair<std::string, std::string>> external_params_;
    std::vector<std::pair<std::string, lingo::Datum>> initial_builtin_variables_;
    // A network-backed movie is not ready for recorded input when its socket
    // has only completed the crypto handshake. The first server session
    // response can schedule the security cast; remember that dependency so
    // playback waits for that async cast cycle to settle.
    mutable bool external_cast_load_observed_{false};
    std::vector<lingo::Datum> updating_objects_;
    std::string last_script_error_message_;
    std::string last_script_error_stack_;
    std::int64_t last_script_error_time_ms_{0};
    bool legacy_timeout_armed_{true};
    std::function<void()> timeout_processor_;
    std::function<void(std::string_view handler_name)> timeout_system_event_dispatcher_;
};

} // namespace libreshockwave::player
