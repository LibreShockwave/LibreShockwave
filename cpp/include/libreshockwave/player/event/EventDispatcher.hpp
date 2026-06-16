#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/chunks/ScriptNamesChunk.hpp"
#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/player/PlayerEvent.hpp"
#include "libreshockwave/player/behavior/BehaviorInstance.hpp"
#include "libreshockwave/player/behavior/BehaviorManager.hpp"
#include "libreshockwave/player/render/SpriteRegistry.hpp"

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
    std::optional<lingo::Datum> scriptInstance;
    std::shared_ptr<chunks::ScriptChunk> script;
    std::shared_ptr<const chunks::ScriptNamesChunk> scriptNames;
    std::shared_ptr<const DirectorFile> scriptOwner;
    chunks::ScriptChunkType scriptType{chunks::ScriptChunkType::Unknown};
};

struct HandlerResult {
    bool handled{false};
    bool passed{false};
};

class EventDispatcher {
public:
    using HandlerInvoker = std::function<HandlerResult(
        const EventTarget& target,
        std::string_view handlerName,
        const std::vector<lingo::Datum>& args)>;
    using RespondsPredicate = std::function<bool(const EventTarget& target, std::string_view handlerName)>;
    using ScriptNamesResolver = std::function<std::shared_ptr<const chunks::ScriptNamesChunk>(
        const std::shared_ptr<chunks::ScriptChunk>& script)>;

    struct MovieScriptTarget {
        std::shared_ptr<chunks::ScriptChunk> script;
        std::shared_ptr<const chunks::ScriptNamesChunk> scriptNames;
        std::shared_ptr<const DirectorFile> scriptOwner;
        chunks::ScriptChunkType scriptType{chunks::ScriptChunkType::Unknown};
    };
    using MovieScriptSupplier = std::function<std::vector<MovieScriptTarget>()>;

    explicit EventDispatcher(behavior::BehaviorManager* behaviorManager = nullptr);

    void setBehaviorManager(behavior::BehaviorManager* behaviorManager);
    void setSpriteRegistry(render::SpriteRegistry* spriteRegistry);
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
    void dispatchGlobalEvent(std::string_view handlerName, const std::vector<lingo::Datum>& args = {});
    void dispatchFrameAndMovieEvent(PlayerEvent event, const std::vector<lingo::Datum>& args = {});
    void dispatchFrameAndMovieEvent(std::string_view handlerName, const std::vector<lingo::Datum>& args = {});
    void dispatchSpriteAndMovieEvent(std::string_view handlerName, const std::vector<lingo::Datum>& args = {});
    void dispatchSpriteEvent(int channel, PlayerEvent event, const std::vector<lingo::Datum>& args = {});
    void dispatchSpriteEvent(int channel, std::string_view handlerName, const std::vector<lingo::Datum>& args = {});
    void dispatchBehaviorEvent(const std::shared_ptr<behavior::BehaviorInstance>& instance,
                               PlayerEvent event,
                               const std::vector<lingo::Datum>& args = {});
    void dispatchBehaviorEvent(const std::shared_ptr<behavior::BehaviorInstance>& instance,
                               std::string_view handlerName,
                               const std::vector<lingo::Datum>& args = {});
    void dispatchToMovieScripts(PlayerEvent event, const std::vector<lingo::Datum>& args = {});
    void dispatchToMovieScripts(std::string_view handlerName, const std::vector<lingo::Datum>& args = {});

    [[nodiscard]] bool spriteHasHandler(int channel, std::string_view handlerName) const;
    [[nodiscard]] bool isSpriteMouseInteractive(int channel) const;
    [[nodiscard]] bool isMouseHandler(std::string_view handlerName) const;

    void pass();
    [[nodiscard]] bool isPropagationStopped() const;
    void stopEvent();
    [[nodiscard]] bool isEventStopped() const;
    void resetEventStopped();

private:
    [[nodiscard]] bool targetResponds(const EventTarget& target, std::string_view handlerName) const;
    [[nodiscard]] bool defaultTargetResponds(const EventTarget& target, std::string_view handlerName) const;
    [[nodiscard]] std::shared_ptr<const chunks::ScriptNamesChunk> resolveScriptNames(
        const std::shared_ptr<chunks::ScriptChunk>& script,
        const std::shared_ptr<const chunks::ScriptNamesChunk>& fallback) const;
    void invokeTarget(const EventTarget& target,
                      std::string_view handlerName,
                      const std::vector<lingo::Datum>& args);
    void resetDispatchState();

    behavior::BehaviorManager* behaviorManager_{nullptr};
    render::SpriteRegistry* spriteRegistry_{nullptr};
    HandlerInvoker handlerInvoker_;
    RespondsPredicate respondsPredicate_;
    ScriptNamesResolver scriptNamesResolver_;
    std::vector<MovieScriptTarget> movieScripts_;
    MovieScriptSupplier movieScriptSupplier_;
    bool debugEnabled_{false};
    bool stopPropagation_{false};
    bool eventStopped_{false};
};

} // namespace libreshockwave::player::event
