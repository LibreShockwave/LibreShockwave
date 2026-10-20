#include "libreshockwave/player/event/event_dispatcher.hpp"

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
    target.script_names = std::move(names);
    target.script_type = target.script ? target.script->scriptType() : chunks::ScriptChunkType::Unknown;
    return target;
}

} // namespace

EventDispatcher::EventDispatcher(behavior::BehaviorManager* behavior_manager)
    : behavior_manager_(behavior_manager) {}

void EventDispatcher::setBehaviorManager(behavior::BehaviorManager* behavior_manager) {
    behavior_manager_ = behavior_manager;
}

void EventDispatcher::setSpriteRegistry(render::SpriteRegistry* sprite_registry) {
    sprite_registry_ = sprite_registry;
}

void EventDispatcher::setDebugEnabled(bool enabled) {
    debug_enabled_ = enabled;
}

bool EventDispatcher::debugEnabled() const {
    return debug_enabled_;
}

void EventDispatcher::setHandlerInvoker(HandlerInvoker invoker) {
    handler_invoker_ = std::move(invoker);
}

void EventDispatcher::setRespondsPredicate(RespondsPredicate predicate) {
    responds_predicate_ = std::move(predicate);
}

void EventDispatcher::setScriptNamesResolver(ScriptNamesResolver resolver) {
    script_names_resolver_ = std::move(resolver);
}

void EventDispatcher::setMovieScripts(std::vector<MovieScriptTarget> scripts) {
    movie_scripts_ = std::move(scripts);
}

void EventDispatcher::setMovieScriptSupplier(MovieScriptSupplier supplier) {
    movie_script_supplier_ = std::move(supplier);
}

void EventDispatcher::addMovieScript(MovieScriptTarget script) {
    movie_scripts_.push_back(std::move(script));
}

const std::vector<EventDispatcher::MovieScriptTarget>& EventDispatcher::movieScripts() const {
    return movie_scripts_;
}

void EventDispatcher::dispatchGlobalEvent(PlayerEvent event, const std::vector<lingo::Datum>& args) {
    dispatchGlobalEvent(handlerName(event), args);
}

void EventDispatcher::dispatchGlobalEvent(std::string_view handler_name, const std::vector<lingo::Datum>& args) {
    resetDispatchState();

    if (behavior_manager_ != nullptr) {
        int last_channel = -1;
        for (const auto& instance : behavior_manager_->getSpriteInstances()) {
            const int channel = instance ? instance->spriteNum() : 0;
            if (channel != last_channel) {
                stop_propagation_ = false;
                last_channel = channel;
            }
            if (stop_propagation_) {
                continue;
            }
            invokeTarget(behaviorTarget(instance), handler_name, args);
        }

        stop_propagation_ = false;
        if (auto frame = behavior_manager_->frameScriptInstance()) {
            invokeTarget(behaviorTarget(frame), handler_name, args);
        }
    }

    if (!stop_propagation_) {
        dispatchToMovieScripts(handler_name, args);
    }
}

void EventDispatcher::dispatchFrameAndMovieEvent(PlayerEvent event, const std::vector<lingo::Datum>& args) {
    dispatchFrameAndMovieEvent(handlerName(event), args);
}

void EventDispatcher::dispatchFrameAndMovieEvent(std::string_view handler_name, const std::vector<lingo::Datum>& args) {
    resetDispatchState();
    if (behavior_manager_ != nullptr) {
        if (auto frame = behavior_manager_->frameScriptInstance()) {
            EventTarget target = behaviorTarget(frame);
            if (targetResponds(target, handler_name)) {
                invokeTarget(target, handler_name, args);
            }
        }
    }
    if (!stop_propagation_) {
        dispatchToMovieScripts(handler_name, args);
    }
}

void EventDispatcher::dispatchSpriteAndMovieEvent(std::string_view handler_name, const std::vector<lingo::Datum>& args) {
    resetDispatchState();
    if (behavior_manager_ != nullptr) {
        int last_channel = -1;
        for (const auto& instance : behavior_manager_->getSpriteInstances()) {
            const int channel = instance ? instance->spriteNum() : 0;
            if (channel != last_channel) {
                stop_propagation_ = false;
                last_channel = channel;
            }
            if (!stop_propagation_) {
                invokeTarget(behaviorTarget(instance), handler_name, args);
            }
        }
    }
    stop_propagation_ = false;
    dispatchToMovieScripts(handler_name, args);
}

void EventDispatcher::dispatchSpriteEvent(int channel, PlayerEvent event, const std::vector<lingo::Datum>& args) {
    dispatchSpriteEvent(channel, handlerName(event), args);
}

void EventDispatcher::dispatchSpriteEvent(int channel,
                                          std::string_view handler_name,
                                          const std::vector<lingo::Datum>& args) {
    resetDispatchState();
    if (behavior_manager_ != nullptr) {
        for (const auto& instance : behavior_manager_->getInstancesForChannel(channel)) {
            EventTarget target = behaviorTarget(instance);
            if (targetResponds(target, handler_name)) {
                invokeTarget(target, handler_name, args);
            }
        }
    }

    if (sprite_registry_ == nullptr) {
        return;
    }

    const auto sprite = sprite_registry_->get(channel);
    if (!sprite) {
        return;
    }

    auto snapshot = sprite->scriptInstanceList();
    for (const auto& script_instance : snapshot) {
        if (script_instance.type() != lingo::DatumType::ScriptInstanceRef) {
            continue;
        }
        EventTarget target;
        target.kind = EventTargetKind::ScriptInstance;
        target.channel = channel;
        target.script_instance = script_instance;
        if (targetResponds(target, handler_name)) {
            invokeTarget(target, handler_name, args);
        }
    }
}

void EventDispatcher::dispatchBehaviorEvent(const std::shared_ptr<behavior::BehaviorInstance>& instance,
                                            PlayerEvent event,
                                            const std::vector<lingo::Datum>& args) {
    dispatchBehaviorEvent(instance, handlerName(event), args);
}

void EventDispatcher::dispatchBehaviorEvent(const std::shared_ptr<behavior::BehaviorInstance>& instance,
                                            std::string_view handler_name,
                                            const std::vector<lingo::Datum>& args) {
    resetDispatchState();
    invokeTarget(behaviorTarget(instance), handler_name, args);
}

void EventDispatcher::dispatchToMovieScripts(PlayerEvent event, const std::vector<lingo::Datum>& args) {
    dispatchToMovieScripts(handlerName(event), args);
}

void EventDispatcher::dispatchToMovieScripts(std::string_view handler_name, const std::vector<lingo::Datum>& args) {
    std::vector<MovieScriptTarget> scripts = movie_scripts_;
    if (movie_script_supplier_) {
        auto supplied = movie_script_supplier_();
        scripts.insert(scripts.end(), supplied.begin(), supplied.end());
    }

    for (const auto& movie : scripts) {
        const auto script_type = movie.script_type == chunks::ScriptChunkType::Unknown && movie.script
            ? movie.script->resolvedScriptType()
            : movie.script_type;
        if (!movie.script || script_type != chunks::ScriptChunkType::MovieScript) {
            continue;
        }

        EventTarget target;
        target.kind = EventTargetKind::MovieScript;
        target.script = movie.script;
        target.script_names = resolveScriptNames(movie.script, movie.script_names);
        target.script_owner = movie.script_owner;
        target.script_type = script_type;
        if (targetResponds(target, handler_name)) {
            invokeTarget(target, handler_name, args);
        }
    }
}

bool EventDispatcher::spriteHasHandler(int channel, std::string_view handler_name) const {
    if (behavior_manager_ != nullptr) {
        for (const auto& instance : behavior_manager_->getInstancesForChannel(channel)) {
            if (targetResponds(behaviorTarget(instance), handler_name)) {
                return true;
            }
        }
    }

    if (sprite_registry_ != nullptr) {
        const auto sprite = sprite_registry_->get(channel);
        if (sprite) {
            for (const auto& script_instance : sprite->scriptInstanceList()) {
                if (script_instance.type() != lingo::DatumType::ScriptInstanceRef) {
                    continue;
                }
                EventTarget target;
                target.kind = EventTargetKind::ScriptInstance;
                target.channel = channel;
                target.script_instance = script_instance;
                if (targetResponds(target, handler_name)) {
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
    stop_propagation_ = false;
}

bool EventDispatcher::isPropagationStopped() const {
    return stop_propagation_;
}

void EventDispatcher::stopEvent() {
    event_stopped_ = true;
}

bool EventDispatcher::isEventStopped() const {
    return event_stopped_;
}

void EventDispatcher::resetEventStopped() {
    event_stopped_ = false;
}

bool EventDispatcher::targetResponds(const EventTarget& target, std::string_view handler_name) const {
    if (responds_predicate_) {
        return responds_predicate_(target, handler_name);
    }
    return defaultTargetResponds(target, handler_name);
}

bool EventDispatcher::defaultTargetResponds(const EventTarget& target, std::string_view handler_name) const {
    if (!target.script) {
        return false;
    }
    auto names = resolveScriptNames(target.script, target.script_names);
    return names && target.script->findHandler(handler_name, names.get()).has_value();
}

std::shared_ptr<const chunks::ScriptNamesChunk> EventDispatcher::resolveScriptNames(
    const std::shared_ptr<chunks::ScriptChunk>& script,
    const std::shared_ptr<const chunks::ScriptNamesChunk>& fallback) const {
    if (fallback) {
        return fallback;
    }
    if (script_names_resolver_) {
        return script_names_resolver_(script);
    }
    return nullptr;
}

void EventDispatcher::invokeTarget(const EventTarget& target,
                                   std::string_view handler_name,
                                   const std::vector<lingo::Datum>& args) {
    if (!targetResponds(target, handler_name)) {
        return;
    }

    const HandlerResult result = handler_invoker_
        ? handler_invoker_(target, handler_name, args)
        : HandlerResult{true, false};
    stop_propagation_ = result.handled && !result.passed;
}

void EventDispatcher::resetDispatchState() {
    stop_propagation_ = false;
}

} // namespace libreshockwave::player::event
