#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/chunks/script_chunk.hpp"
#include "libreshockwave/chunks/script_names_chunk.hpp"
#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/player/player_event.hpp"
#include "libreshockwave/player/behavior/behavior_instance.hpp"
#include "libreshockwave/player/behavior/behavior_manager.hpp"
#include "libreshockwave/player/render/sprite_registry.hpp"

namespace libreshockwave {
class DirectorFile;
}

namespace libreshockwave::player::event {

enum class EventTargetKind {
    Behavior,
    ScriptInstance,
    MovieScript
};

struct EventTarget {
    EventTargetKind kind{EventTargetKind::Behavior};
    int channel{0};
    std::shared_ptr<behavior::BehaviorInstance> behavior;
    std::optional<lingo::Datum> script_instance;
    std::shared_ptr<chunks::ScriptChunk> script;
    std::shared_ptr<const chunks::ScriptNamesChunk> script_names;
    std::shared_ptr<const DirectorFile> script_owner;
    chunks::ScriptChunkType script_type{chunks::ScriptChunkType::Unknown};
};

struct HandlerResult {
    bool handled{false};
    bool passed{false};
};

class EventDispatcher {
public:
    using HandlerInvoker = std::function<HandlerResult(
        const EventTarget& target,
        std::string_view handler_name,
        const std::vector<lingo::Datum>& args)>;
    using RespondsPredicate = std::function<bool(const EventTarget& target, std::string_view handler_name)>;
    using ScriptNamesResolver = std::function<std::shared_ptr<const chunks::ScriptNamesChunk>(
        const std::shared_ptr<chunks::ScriptChunk>& script)>;

    struct MovieScriptTarget {
        std::shared_ptr<chunks::ScriptChunk> script;
        std::shared_ptr<const chunks::ScriptNamesChunk> script_names;
        std::shared_ptr<const DirectorFile> script_owner;
        chunks::ScriptChunkType script_type{chunks::ScriptChunkType::Unknown};
    };
    using MovieScriptSupplier = std::function<std::vector<MovieScriptTarget>()>;

    explicit EventDispatcher(behavior::BehaviorManager* behavior_manager = nullptr);

    void setBehaviorManager(behavior::BehaviorManager* behavior_manager);
    void setSpriteRegistry(render::SpriteRegistry* sprite_registry);
    void setDebugEnabled(bool enabled);
    [[nodiscard]] bool debugEnabled() const;

    void setHandlerInvoker(HandlerInvoker invoker);
    void setRespondsPredicate(RespondsPredicate predicate);
    void setScriptNamesResolver(ScriptNamesResolver resolver);
    void setMovieScripts(std::vector<MovieScriptTarget> scripts);
    void setMovieScriptSupplier(MovieScriptSupplier supplier);
    void addMovieScript(MovieScriptTarget script);
    [[nodiscard]] const std::vector<MovieScriptTarget>& movieScripts() const;

    void dispatchGlobalEvent(PlayerEvent event, const std::vector<lingo::Datum>& args = {});
    void dispatchGlobalEvent(std::string_view handler_name, const std::vector<lingo::Datum>& args = {});
    void dispatchFrameAndMovieEvent(PlayerEvent event, const std::vector<lingo::Datum>& args = {});
    void dispatchFrameAndMovieEvent(std::string_view handler_name, const std::vector<lingo::Datum>& args = {});
    void dispatchSpriteAndMovieEvent(std::string_view handler_name, const std::vector<lingo::Datum>& args = {});
    void dispatchSpriteEvent(int channel, PlayerEvent event, const std::vector<lingo::Datum>& args = {});
    void dispatchSpriteEvent(int channel, std::string_view handler_name, const std::vector<lingo::Datum>& args = {});
    void dispatchBehaviorEvent(const std::shared_ptr<behavior::BehaviorInstance>& instance,
                               PlayerEvent event,
                               const std::vector<lingo::Datum>& args = {});
    void dispatchBehaviorEvent(const std::shared_ptr<behavior::BehaviorInstance>& instance,
                               std::string_view handler_name,
                               const std::vector<lingo::Datum>& args = {});
    void dispatchToMovieScripts(PlayerEvent event, const std::vector<lingo::Datum>& args = {});
    void dispatchToMovieScripts(std::string_view handler_name, const std::vector<lingo::Datum>& args = {});

    [[nodiscard]] bool spriteHasHandler(int channel, std::string_view handler_name) const;
    [[nodiscard]] bool isSpriteMouseInteractive(int channel) const;
    [[nodiscard]] bool isMouseHandler(std::string_view handler_name) const;

    void pass();
    [[nodiscard]] bool isPropagationStopped() const;
    void stopEvent();
    [[nodiscard]] bool isEventStopped() const;
    void resetEventStopped();

private:
    [[nodiscard]] bool targetResponds(const EventTarget& target, std::string_view handler_name) const;
    [[nodiscard]] bool defaultTargetResponds(const EventTarget& target, std::string_view handler_name) const;
    [[nodiscard]] std::shared_ptr<const chunks::ScriptNamesChunk> resolveScriptNames(
        const std::shared_ptr<chunks::ScriptChunk>& script,
        const std::shared_ptr<const chunks::ScriptNamesChunk>& fallback) const;
    void invokeTarget(const EventTarget& target,
                      std::string_view handler_name,
                      const std::vector<lingo::Datum>& args);
    void resetDispatchState();

    behavior::BehaviorManager* behavior_manager_{nullptr};
    render::SpriteRegistry* sprite_registry_{nullptr};
    HandlerInvoker handler_invoker_;
    RespondsPredicate responds_predicate_;
    ScriptNamesResolver script_names_resolver_;
    std::vector<MovieScriptTarget> movie_scripts_;
    MovieScriptSupplier movie_script_supplier_;
    bool debug_enabled_{false};
    bool stop_propagation_{false};
    bool event_stopped_{false};
};

} // namespace libreshockwave::player::event
