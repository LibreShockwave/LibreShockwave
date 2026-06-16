#include "libreshockwave/player/event/EventDispatcher.hpp"

#include <cctype>
#include <utility>

namespace libreshockwave::player::event {
namespace {

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
            std::tolower(static_cast<unsigned char>(rhs[index]))) {
            return false;
        }
    }
    return true;
}

EventTarget behaviorTarget(const std::shared_ptr<behavior::BehaviorInstance>& instance,
                           std::shared_ptr<const chunks::ScriptNamesChunk> names = nullptr) {
    EventTarget target;
    target.kind = EventTargetKind::Behavior;
    target.channel = instance ? instance->spriteNum() : 0;
    target.behavior = instance;
    target.script = instance ? instance->script() : nullptr;
    target.scriptNames = std::move(names);
    target.scriptType = target.script ? target.script->scriptType() : chunks::ScriptChunkType::Unknown;
    return target;
}

} // namespace

EventDispatcher::EventDispatcher(behavior::BehaviorManager* behaviorManager)
    : behaviorManager_(behaviorManager) {}

void EventDispatcher::setBehaviorManager(behavior::BehaviorManager* behaviorManager) {
    behaviorManager_ = behaviorManager;
}

void EventDispatcher::setSpriteRegistry(render::SpriteRegistry* spriteRegistry) {
    spriteRegistry_ = spriteRegistry;
}

void EventDispatcher::setDebugEnabled(bool enabled) {
    debugEnabled_ = enabled;
}

bool EventDispatcher::debugEnabled() const {
    return debugEnabled_;
}

void EventDispatcher::setHandlerInvoker(HandlerInvoker invoker) {
    handlerInvoker_ = std::move(invoker);
}

void EventDispatcher::setRespondsPredicate(RespondsPredicate predicate) {
    respondsPredicate_ = std::move(predicate);
}

void EventDispatcher::setScriptNamesResolver(ScriptNamesResolver resolver) {
    scriptNamesResolver_ = std::move(resolver);
}

void EventDispatcher::setMovieScripts(std::vector<MovieScriptTarget> scripts) {
    movieScripts_ = std::move(scripts);
}

void EventDispatcher::setMovieScriptSupplier(MovieScriptSupplier supplier) {
    movieScriptSupplier_ = std::move(supplier);
}

void EventDispatcher::addMovieScript(MovieScriptTarget script) {
    movieScripts_.push_back(std::move(script));
}

const std::vector<EventDispatcher::MovieScriptTarget>& EventDispatcher::movieScripts() const {
    return movieScripts_;
}

void EventDispatcher::dispatchGlobalEvent(PlayerEvent event, const std::vector<lingo::Datum>& args) {
    dispatchGlobalEvent(handlerName(event), args);
}

void EventDispatcher::dispatchGlobalEvent(std::string_view handlerName, const std::vector<lingo::Datum>& args) {
    resetDispatchState();

    if (behaviorManager_ != nullptr) {
        int lastChannel = -1;
        for (const auto& instance : behaviorManager_->getSpriteInstances()) {
            const int channel = instance ? instance->spriteNum() : 0;
            if (channel != lastChannel) {
                stopPropagation_ = false;
                lastChannel = channel;
            }
            if (stopPropagation_) {
                continue;
            }
            invokeTarget(behaviorTarget(instance), handlerName, args);
        }

        stopPropagation_ = false;
        if (auto frame = behaviorManager_->frameScriptInstance()) {
            invokeTarget(behaviorTarget(frame), handlerName, args);
        }
    }

    if (!stopPropagation_) {
        dispatchToMovieScripts(handlerName, args);
    }
}

void EventDispatcher::dispatchFrameAndMovieEvent(PlayerEvent event, const std::vector<lingo::Datum>& args) {
    dispatchFrameAndMovieEvent(handlerName(event), args);
}

void EventDispatcher::dispatchFrameAndMovieEvent(std::string_view handlerName, const std::vector<lingo::Datum>& args) {
    resetDispatchState();
    if (behaviorManager_ != nullptr) {
        if (auto frame = behaviorManager_->frameScriptInstance()) {
            EventTarget target = behaviorTarget(frame);
            if (targetResponds(target, handlerName)) {
                invokeTarget(target, handlerName, args);
            }
        }
    }
    if (!stopPropagation_) {
        dispatchToMovieScripts(handlerName, args);
    }
}

void EventDispatcher::dispatchSpriteAndMovieEvent(std::string_view handlerName, const std::vector<lingo::Datum>& args) {
    resetDispatchState();
    if (behaviorManager_ != nullptr) {
        int lastChannel = -1;
        for (const auto& instance : behaviorManager_->getSpriteInstances()) {
            const int channel = instance ? instance->spriteNum() : 0;
            if (channel != lastChannel) {
                stopPropagation_ = false;
                lastChannel = channel;
            }
            if (!stopPropagation_) {
                invokeTarget(behaviorTarget(instance), handlerName, args);
            }
        }
    }
    stopPropagation_ = false;
    dispatchToMovieScripts(handlerName, args);
}

void EventDispatcher::dispatchSpriteEvent(int channel, PlayerEvent event, const std::vector<lingo::Datum>& args) {
    dispatchSpriteEvent(channel, handlerName(event), args);
}

void EventDispatcher::dispatchSpriteEvent(int channel,
                                          std::string_view handlerName,
                                          const std::vector<lingo::Datum>& args) {
    resetDispatchState();
    if (behaviorManager_ != nullptr) {
        for (const auto& instance : behaviorManager_->getInstancesForChannel(channel)) {
            EventTarget target = behaviorTarget(instance);
            if (targetResponds(target, handlerName)) {
                invokeTarget(target, handlerName, args);
            }
        }
    }

    if (spriteRegistry_ == nullptr) {
        return;
    }

    const auto sprite = spriteRegistry_->get(channel);
    if (!sprite) {
        return;
    }

    auto snapshot = sprite->scriptInstanceList();
    for (const auto& scriptInstance : snapshot) {
        if (scriptInstance.type() != lingo::DatumType::ScriptInstanceRef) {
            continue;
        }
        EventTarget target;
        target.kind = EventTargetKind::ScriptInstance;
        target.channel = channel;
        target.scriptInstance = scriptInstance;
        if (targetResponds(target, handlerName)) {
            invokeTarget(target, handlerName, args);
        }
    }
}

void EventDispatcher::dispatchBehaviorEvent(const std::shared_ptr<behavior::BehaviorInstance>& instance,
                                            PlayerEvent event,
                                            const std::vector<lingo::Datum>& args) {
    dispatchBehaviorEvent(instance, handlerName(event), args);
}

void EventDispatcher::dispatchBehaviorEvent(const std::shared_ptr<behavior::BehaviorInstance>& instance,
                                            std::string_view handlerName,
                                            const std::vector<lingo::Datum>& args) {
    resetDispatchState();
    invokeTarget(behaviorTarget(instance), handlerName, args);
}

void EventDispatcher::dispatchToMovieScripts(PlayerEvent event, const std::vector<lingo::Datum>& args) {
    dispatchToMovieScripts(handlerName(event), args);
}

void EventDispatcher::dispatchToMovieScripts(std::string_view handlerName, const std::vector<lingo::Datum>& args) {
    std::vector<MovieScriptTarget> scripts = movieScripts_;
    if (movieScriptSupplier_) {
        auto supplied = movieScriptSupplier_();
        scripts.insert(scripts.end(), supplied.begin(), supplied.end());
    }

    for (const auto& movie : scripts) {
        const auto scriptType = movie.scriptType == chunks::ScriptChunkType::Unknown && movie.script
            ? movie.script->resolvedScriptType()
            : movie.scriptType;
        if (!movie.script || scriptType != chunks::ScriptChunkType::MovieScript) {
            continue;
        }

        EventTarget target;
        target.kind = EventTargetKind::MovieScript;
        target.script = movie.script;
        target.scriptNames = resolveScriptNames(movie.script, movie.scriptNames);
        target.scriptOwner = movie.scriptOwner;
        target.scriptType = scriptType;
        if (targetResponds(target, handlerName)) {
            invokeTarget(target, handlerName, args);
        }
    }
}

bool EventDispatcher::spriteHasHandler(int channel, std::string_view handlerName) const {
    if (behaviorManager_ != nullptr) {
        for (const auto& instance : behaviorManager_->getInstancesForChannel(channel)) {
            if (targetResponds(behaviorTarget(instance), handlerName)) {
                return true;
            }
        }
    }

    if (spriteRegistry_ != nullptr) {
        const auto sprite = spriteRegistry_->get(channel);
        if (sprite) {
            for (const auto& scriptInstance : sprite->scriptInstanceList()) {
                if (scriptInstance.type() != lingo::DatumType::ScriptInstanceRef) {
                    continue;
                }
                EventTarget target;
                target.kind = EventTargetKind::ScriptInstance;
                target.channel = channel;
                target.scriptInstance = scriptInstance;
                if (targetResponds(target, handlerName)) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool EventDispatcher::isSpriteMouseInteractive(int channel) const {
    return spriteHasHandler(channel, handlerName(PlayerEvent::MouseDown)) ||
           spriteHasHandler(channel, handlerName(PlayerEvent::MouseUp)) ||
           spriteHasHandler(channel, handlerName(PlayerEvent::MouseEnter)) ||
           spriteHasHandler(channel, handlerName(PlayerEvent::MouseLeave)) ||
           spriteHasHandler(channel, handlerName(PlayerEvent::MouseWithin)) ||
           spriteHasHandler(channel, handlerName(PlayerEvent::MouseUpOutside)) ||
           spriteHasHandler(channel, "mouseUpOutSide");
}

bool EventDispatcher::isMouseHandler(std::string_view value) const {
    return equalsIgnoreCase(value, handlerName(PlayerEvent::MouseDown)) ||
           equalsIgnoreCase(value, handlerName(PlayerEvent::MouseUp)) ||
           equalsIgnoreCase(value, handlerName(PlayerEvent::MouseEnter)) ||
           equalsIgnoreCase(value, handlerName(PlayerEvent::MouseLeave)) ||
           equalsIgnoreCase(value, handlerName(PlayerEvent::MouseWithin)) ||
           equalsIgnoreCase(value, handlerName(PlayerEvent::MouseUpOutside)) ||
           equalsIgnoreCase(value, "mouseUpOutSide");
}

void EventDispatcher::pass() {
    stopPropagation_ = false;
}

bool EventDispatcher::isPropagationStopped() const {
    return stopPropagation_;
}

void EventDispatcher::stopEvent() {
    eventStopped_ = true;
}

bool EventDispatcher::isEventStopped() const {
    return eventStopped_;
}

void EventDispatcher::resetEventStopped() {
    eventStopped_ = false;
}

bool EventDispatcher::targetResponds(const EventTarget& target, std::string_view handlerName) const {
    if (respondsPredicate_) {
        return respondsPredicate_(target, handlerName);
    }
    return defaultTargetResponds(target, handlerName);
}

bool EventDispatcher::defaultTargetResponds(const EventTarget& target, std::string_view handlerName) const {
    if (!target.script) {
        return false;
    }
    auto names = resolveScriptNames(target.script, target.scriptNames);
    return names && target.script->findHandler(handlerName, names.get()).has_value();
}

std::shared_ptr<const chunks::ScriptNamesChunk> EventDispatcher::resolveScriptNames(
    const std::shared_ptr<chunks::ScriptChunk>& script,
    const std::shared_ptr<const chunks::ScriptNamesChunk>& fallback) const {
    if (fallback) {
        return fallback;
    }
    if (scriptNamesResolver_) {
        return scriptNamesResolver_(script);
    }
    return nullptr;
}

void EventDispatcher::invokeTarget(const EventTarget& target,
                                   std::string_view handlerName,
                                   const std::vector<lingo::Datum>& args) {
    if (!targetResponds(target, handlerName)) {
        return;
    }

    const HandlerResult result = handlerInvoker_
        ? handlerInvoker_(target, handlerName, args)
        : HandlerResult{true, false};
    stopPropagation_ = result.handled && !result.passed;
}

void EventDispatcher::resetDispatchState() {
    stopPropagation_ = false;
}

} // namespace libreshockwave::player::event
