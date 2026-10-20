#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/player/player_event.hpp"
#include "libreshockwave/player/frame/frame_event.hpp"

namespace libreshockwave {
class DirectorFile;
}

namespace libreshockwave::chunks {
class ScriptChunk;
}

namespace libreshockwave::player::behavior {
class BehaviorManager;
class BehaviorInstance;
}

namespace libreshockwave::player::event {
class EventDispatcher;
}

namespace libreshockwave::player::render {
class SpriteRegistry;
}

namespace libreshockwave::player::score {
class ScoreNavigator;
}

namespace libreshockwave::player::frame {

class FrameContext {
public:
    using EventListener = std::function<void(const FrameEvent&)>;
    using ActorListDispatcher = std::function<void(std::string_view handler_name)>;
    using TimeoutEventDispatcher = std::function<void(std::string_view handler_name)>;
    using ScriptNameResolver = std::function<std::string(const std::shared_ptr<chunks::ScriptChunk>& script)>;

    explicit FrameContext(DirectorFile* file = nullptr);
    FrameContext(DirectorFile* file,
                 score::ScoreNavigator* navigator,
                 behavior::BehaviorManager* behavior_manager,
                 event::EventDispatcher* event_dispatcher);
    ~FrameContext();

    FrameContext(const FrameContext&) = delete;
    FrameContext& operator=(const FrameContext&) = delete;
    FrameContext(FrameContext&&) noexcept;
    FrameContext& operator=(FrameContext&&) noexcept;

    void setDebugEnabled(bool enabled);
    [[nodiscard]] bool debugEnabled() const;
    void setEventListener(EventListener listener);
    void setActorListDispatcher(ActorListDispatcher dispatcher);
    void setTimeoutEventDispatcher(TimeoutEventDispatcher dispatcher);
    void setScriptNameResolver(ScriptNameResolver resolver);
    void setSpriteRegistry(render::SpriteRegistry* registry);

    [[nodiscard]] int currentFrame() const;
    [[nodiscard]] int effectiveFrame() const;
    [[nodiscard]] int frameCount() const;
    [[nodiscard]] score::ScoreNavigator& navigator();
    [[nodiscard]] const score::ScoreNavigator& navigator() const;
    [[nodiscard]] behavior::BehaviorManager& behaviorManager();
    [[nodiscard]] const behavior::BehaviorManager& behaviorManager() const;
    [[nodiscard]] event::EventDispatcher& eventDispatcher();
    [[nodiscard]] const event::EventDispatcher& eventDispatcher() const;
    [[nodiscard]] const std::set<int>& activeChannels() const;
    [[nodiscard]] bool inFrameScript() const;

    void goToFrame(int frame);
    void forceGoToFrame(int frame);
    void goToLabel(std::string_view label);
    void initializeFirstFrame();
    [[nodiscard]] bool executeFrame(bool suppress_sprite_enter_frame_once = false);
    void dispatchBeginSpriteEvents();
    void rebindBehaviorsForLoadedCast(int cast_lib_number);
    [[nodiscard]] int advanceFrame();
    void reset();

private:
    void bindDispatcher();
    void enterFrame(int frame);
    void beginSpritesForFrame(int frame);
    void ensureScoreSpriteState(int frame, int channel);
    void endSpritesLeavingFrame(int old_frame, int new_frame);
    void initializeFrameScript(int frame);
    void dispatchToActorList(std::string_view handler_name);
    void dispatchEvent(PlayerEvent event);
    void dispatchBeginSprite();
    [[nodiscard]] bool hasScoreSpriteBehaviors(int frame) const;
    [[nodiscard]] bool hasFrameOneFrameScriptAndFrameTwoSpriteBehaviorStartupShape() const;
    [[nodiscard]] std::string scriptNameFor(const std::shared_ptr<chunks::ScriptChunk>& script) const;
    void maybeDelayFlagsStartupCycle(const std::shared_ptr<behavior::BehaviorInstance>& instance,
                                     bool v1_startup_shape);
    void logEvent(std::string_view message) const;
    void notifyEvent(PlayerEvent event);

    DirectorFile* file_{nullptr};
    std::unique_ptr<score::ScoreNavigator> owned_navigator_;
    score::ScoreNavigator* navigator_{nullptr};
    std::unique_ptr<behavior::BehaviorManager> owned_behavior_manager_;
    behavior::BehaviorManager* behavior_manager_{nullptr};
    std::unique_ptr<event::EventDispatcher> owned_event_dispatcher_;
    event::EventDispatcher* event_dispatcher_{nullptr};
    render::SpriteRegistry* sprite_registry_{nullptr};

    ActorListDispatcher actor_list_dispatcher_;
    TimeoutEventDispatcher timeout_event_dispatcher_;
    ScriptNameResolver script_name_resolver_;
    EventListener event_listener_;
    int current_frame_{1};
    int pending_frame_{0};
    bool in_frame_script_{false};
    std::set<int> active_channels_;
    std::set<int> entered_channels_;
    bool debug_enabled_{false};
};

} // namespace libreshockwave::player::frame
