#include "libreshockwave/player/frame/frame_context.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "libreshockwave/moviesource/movie_source.hpp"
#include "libreshockwave/chunks/score_chunk.hpp"
#include "libreshockwave/chunks/script_chunk.hpp"
#include "libreshockwave/player/behavior/behavior_instance.hpp"
#include "libreshockwave/player/behavior/behavior_manager.hpp"
#include "libreshockwave/player/event/event_dispatcher.hpp"
#include "libreshockwave/player/render/sprite_registry.hpp"
#include "libreshockwave/player/score/score_navigator.hpp"
#include "libreshockwave/player/score/sprite_span.hpp"
#include "libreshockwave/player/sprite/sprite_state.hpp"

namespace libreshockwave::player::frame {
namespace {

template <typename T>
T& requireDependency(T* value, const char* name) {
    if (value == nullptr) {
        throw std::runtime_error(std::string("FrameContext missing dependency: ") + name);
    }
    return *value;
}

std::string behaviorParameterMetadataKey(const lingo::Datum& key) {
    std::string name;
    if (const auto* symbol = key.asSymbol()) {
        name = symbol->name;
    } else {
        name = key.stringValue();
    }
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return "behaviorparam:" + name;
}

void applyBehaviorParameterMetadata(render::SpriteRegistry* registry,
                                    int channel,
                                    const score::ScoreBehaviorRef& behavior_ref) {
    if (registry == nullptr || !behavior_ref.hasParameters()) {
        return;
    }
    auto sprite = registry->get(channel);
    if (sprite == nullptr) {
        return;
    }
    for (const auto& param : behavior_ref.parameters()) {
        if (!param.isPropList()) {
            continue;
        }
        for (const auto& [key, value] : param.propListValue().properties()) {
            sprite->setLegacyProperty(behaviorParameterMetadataKey(key), value);
        }
    }
}

} // namespace

FrameContext::FrameContext(moviesource::MovieSource* file)
    : file_(file),
      owned_navigator_(std::make_unique<score::ScoreNavigator>(file)),
      navigator_(owned_navigator_.get()),
      owned_behavior_manager_(std::make_unique<behavior::BehaviorManager>(file)),
      behavior_manager_(owned_behavior_manager_.get()),
      owned_event_dispatcher_(std::make_unique<event::EventDispatcher>(behavior_manager_)),
      event_dispatcher_(owned_event_dispatcher_.get()) {
    bindDispatcher();
}

FrameContext::FrameContext(moviesource::MovieSource* file,
                           score::ScoreNavigator* navigator,
                           behavior::BehaviorManager* behavior_manager,
                           event::EventDispatcher* event_dispatcher)
    : file_(file),
      navigator_(navigator),
      behavior_manager_(behavior_manager),
      event_dispatcher_(event_dispatcher) {
    requireDependency(navigator_, "navigator");
    requireDependency(behavior_manager_, "behaviorManager");
    requireDependency(event_dispatcher_, "eventDispatcher");
    bindDispatcher();
}

FrameContext::~FrameContext() = default;
FrameContext::FrameContext(FrameContext&&) noexcept = default;
FrameContext& FrameContext::operator=(FrameContext&&) noexcept = default;

void FrameContext::setDebugEnabled(bool enabled) {
    debug_enabled_ = enabled;
    behaviorManager().setDebugEnabled(enabled);
    eventDispatcher().setDebugEnabled(enabled);
}

bool FrameContext::debugEnabled() const {
    return debug_enabled_;
}

void FrameContext::setEventListener(EventListener listener) {
    event_listener_ = std::move(listener);
}

void FrameContext::setActorListDispatcher(ActorListDispatcher dispatcher) {
    actor_list_dispatcher_ = std::move(dispatcher);
}

void FrameContext::setTimeoutEventDispatcher(TimeoutEventDispatcher dispatcher) {
    timeout_event_dispatcher_ = std::move(dispatcher);
}

void FrameContext::setScriptNameResolver(ScriptNameResolver resolver) {
    script_name_resolver_ = std::move(resolver);
}

void FrameContext::setSpriteRegistry(render::SpriteRegistry* registry) {
    sprite_registry_ = registry;
    eventDispatcher().setSpriteRegistry(registry);
}

int FrameContext::currentFrame() const { return current_frame_; }
int FrameContext::effectiveFrame() const { return pending_frame_ > 0 ? pending_frame_ : current_frame_; }
int FrameContext::frameCount() const { return navigator().getFrameCount(); }
score::ScoreNavigator& FrameContext::navigator() { return requireDependency(navigator_, "navigator"); }
const score::ScoreNavigator& FrameContext::navigator() const { return requireDependency(navigator_, "navigator"); }
behavior::BehaviorManager& FrameContext::behaviorManager() { return requireDependency(behavior_manager_, "behaviorManager"); }
const behavior::BehaviorManager& FrameContext::behaviorManager() const { return requireDependency(behavior_manager_, "behaviorManager"); }
event::EventDispatcher& FrameContext::eventDispatcher() { return requireDependency(event_dispatcher_, "eventDispatcher"); }
const event::EventDispatcher& FrameContext::eventDispatcher() const { return requireDependency(event_dispatcher_, "eventDispatcher"); }
const std::set<int>& FrameContext::activeChannels() const { return active_channels_; }
bool FrameContext::inFrameScript() const { return in_frame_script_; }

void FrameContext::goToFrame(int frame) {
    const int max = frameCount();
    if (frame >= 1 && frame <= max) {
        pending_frame_ = frame;
    }
}

void FrameContext::forceGoToFrame(int frame) {
    const int max = frameCount();
    if (frame < 1 || frame > max) {
        return;
    }

    const int old_frame = current_frame_;
    logEvent("forceGoToFrame");
    endSpritesLeavingFrame(old_frame, frame);
    behaviorManager().clearFrameScript();

    current_frame_ = frame;
    pending_frame_ = 0;
    enterFrame(frame);
}

void FrameContext::goToLabel(std::string_view label) {
    const int frame = navigator().getFrameForLabel(label);
    if (frame > 0) {
        goToFrame(frame);
    }
}

void FrameContext::initializeFirstFrame() {
    current_frame_ = 1;
    pending_frame_ = 0;
    active_channels_.clear();
    entered_channels_.clear();
    behaviorManager().clear();

    logEvent("initializeFirstFrame");
    beginSpritesForFrame(current_frame_);
    initializeFrameScript(current_frame_);
}

bool FrameContext::executeFrame(bool suppress_sprite_enter_frame_once) {
    dispatchToActorList("stepFrame");
    dispatchEvent(PlayerEvent::StepFrame);

    dispatchToActorList("prepareFrame");
    if (timeout_event_dispatcher_) {
        timeout_event_dispatcher_("prepareFrame");
    }
    dispatchEvent(PlayerEvent::PrepareFrame);

    dispatchToActorList("enterFrame");
    in_frame_script_ = true;
    if (suppress_sprite_enter_frame_once) {
        eventDispatcher().dispatchFrameAndMovieEvent(PlayerEvent::EnterFrame);
        notifyEvent(PlayerEvent::EnterFrame);
    } else {
        dispatchEvent(PlayerEvent::EnterFrame);
    }
    in_frame_script_ = false;
    return true;
}

void FrameContext::dispatchBeginSpriteEvents() {
    dispatchBeginSprite();
}

void FrameContext::rebindBehaviorsForLoadedCast(int cast_lib_number) {
    if (cast_lib_number <= 0) {
        return;
    }

    for (const auto& span : navigator().getActiveSprites(current_frame_)) {
        const int channel = span.channel();
        if (!active_channels_.contains(channel)) {
            continue;
        }
        for (const auto& behavior_ref : span.behaviors()) {
            if (behavior_ref.castLib() != cast_lib_number ||
                behaviorManager().hasInstanceForChannel(channel, behavior_ref)) {
                continue;
            }
            if (sprite_registry_ != nullptr) {
                sprite_registry_->markScoreBehaviorChannel(channel);
                applyBehaviorParameterMetadata(sprite_registry_, channel, behavior_ref);
            }
            auto instance = behaviorManager().createInstance(behavior_ref, channel);
            if (!instance) {
                continue;
            }
            eventDispatcher().dispatchBehaviorEvent(instance, PlayerEvent::BeginSprite);
            instance->setBeginSpriteCalled(true);
            logEvent("rebindBehavior");
        }
    }

    const auto* frame_script = navigator().getFrameScript(current_frame_);
    if (frame_script == nullptr ||
        frame_script->castLib() != cast_lib_number ||
        behaviorManager().frameScriptInstance() != nullptr) {
        return;
    }

    auto frame_instance = behaviorManager().getOrCreateFrameScript(*frame_script, current_frame_);
    if (frame_instance) {
        eventDispatcher().dispatchBehaviorEvent(frame_instance, PlayerEvent::BeginSprite);
        frame_instance->setBeginSpriteCalled(true);
        logEvent("rebindFrameScript");
    }
}

int FrameContext::advanceFrame() {
    const int old_frame = current_frame_;

    dispatchToActorList("exitFrame");
    if (timeout_event_dispatcher_) {
        timeout_event_dispatcher_("exitFrame");
    }
    dispatchEvent(PlayerEvent::ExitFrame);

    int new_frame = 0;
    if (pending_frame_ > 0) {
        new_frame = pending_frame_;
        pending_frame_ = 0;
    } else {
        new_frame = current_frame_ + 1;
    }

    const int max = frameCount();
    if (max > 0 && new_frame > max) {
        new_frame = 1;
    }

    if (new_frame != old_frame) {
        logEvent("advanceFrame");
        endSpritesLeavingFrame(old_frame, new_frame);
        behaviorManager().clearFrameScript();
        current_frame_ = new_frame;
        enterFrame(new_frame);
    }

    return current_frame_;
}

void FrameContext::reset() {
    current_frame_ = 1;
    pending_frame_ = 0;
    active_channels_.clear();
    entered_channels_.clear();
    behaviorManager().clear();
    in_frame_script_ = false;
}

void FrameContext::bindDispatcher() {
    if (event_dispatcher_ != nullptr) {
        event_dispatcher_->setBehaviorManager(behavior_manager_);
        event_dispatcher_->setSpriteRegistry(sprite_registry_);
    }
}

void FrameContext::enterFrame(int frame) {
    beginSpritesForFrame(frame);
    initializeFrameScript(frame);
    dispatchBeginSprite();
    entered_channels_.clear();
}

void FrameContext::beginSpritesForFrame(int frame) {
    auto spans = navigator().getActiveSprites(frame);
    std::ranges::sort(spans, [](const score::SpriteSpan& left, const score::SpriteSpan& right) {
        return left.channel() < right.channel();
    });

    for (const auto& span : spans) {
        const int channel = span.channel();
        const bool newly_active = !active_channels_.contains(channel);
        if (newly_active) {
            active_channels_.insert(channel);
            entered_channels_.insert(channel);
            ensureScoreSpriteState(frame, channel);
        }

        for (const auto& behavior_ref : span.behaviors()) {
            if (behaviorManager().hasInstanceForChannel(channel, behavior_ref)) {
                continue;
            }
            if (sprite_registry_ != nullptr) {
                sprite_registry_->markScoreBehaviorChannel(channel);
                applyBehaviorParameterMetadata(sprite_registry_, channel, behavior_ref);
            }
            (void)behaviorManager().createInstance(behavior_ref, channel);
            entered_channels_.insert(channel);
        }
        if (newly_active) {
            logEvent("beginSprite");
        }
    }
}

void FrameContext::ensureScoreSpriteState(int frame, int channel) {
    if (sprite_registry_ == nullptr || file_ == nullptr) {
        return;
    }
    auto score = file_->scoreChunk();
    if (!score) {
        return;
    }
    const int frame_index = frame - 1;
    for (const auto& entry : score->frameData().frame_channel_data) {
        if (entry.frame_index.value() == frame_index && entry.channel_index.value() == channel) {
            (void)sprite_registry_->getOrCreate(channel, entry.data);
            return;
        }
    }
}

void FrameContext::endSpritesLeavingFrame(int old_frame, int new_frame) {
    (void)old_frame;
    const auto new_active_channels = navigator().getActiveChannels(new_frame);
    std::vector<int> leaving;
    for (const int channel : active_channels_) {
        if (new_active_channels.contains(channel)) {
            continue;
        }
        if (sprite_registry_ != nullptr) {
            auto state = sprite_registry_->get(channel);
            if (state && state->isPuppet()) {
                logEvent("endSprite skipped for puppeted sprite");
                continue;
            }
        }
        leaving.push_back(channel);
    }

    for (const int channel : leaving) {
        eventDispatcher().dispatchSpriteEvent(channel, PlayerEvent::EndSprite);
        for (const auto& instance : behaviorManager().getInstancesForChannel(channel)) {
            if (instance) {
                instance->setEndSpriteCalled(true);
            }
        }
        active_channels_.erase(channel);
        behaviorManager().removeInstancesForChannel(channel);
        if (sprite_registry_ != nullptr) {
            sprite_registry_->remove(channel);
        }
        logEvent("endSprite");
    }
}

void FrameContext::initializeFrameScript(int frame) {
    if (const auto* frame_script = navigator().getFrameScript(frame); frame_script != nullptr) {
        (void)behaviorManager().getOrCreateFrameScript(*frame_script, frame);
        logEvent("initializeFrameScript");
    } else {
        logEvent("initializeFrameScript: none");
    }
}

void FrameContext::dispatchToActorList(std::string_view handler_name) {
    if (actor_list_dispatcher_) {
        actor_list_dispatcher_(handler_name);
    }
}

void FrameContext::dispatchEvent(PlayerEvent event) {
    eventDispatcher().dispatchGlobalEvent(event);
    notifyEvent(event);
}

void FrameContext::dispatchBeginSprite() {
    const bool v1_startup_shape = hasFrameOneFrameScriptAndFrameTwoSpriteBehaviorStartupShape();
    for (const int channel : entered_channels_) {
        for (const auto& instance : behaviorManager().getInstancesForChannel(channel)) {
            if (!instance || instance->isBeginSpriteCalled()) {
                continue;
            }
            eventDispatcher().dispatchBehaviorEvent(instance, PlayerEvent::BeginSprite);
            maybeDelayFlagsStartupCycle(instance, v1_startup_shape);
            instance->setBeginSpriteCalled(true);
        }
    }

    auto frame_instance = behaviorManager().frameScriptInstance();
    if (frame_instance && !frame_instance->isBeginSpriteCalled()) {
        eventDispatcher().dispatchFrameAndMovieEvent(PlayerEvent::BeginSprite);
        frame_instance->setBeginSpriteCalled(true);
    }
}

bool FrameContext::hasScoreSpriteBehaviors(int frame) const {
    for (const auto& span : navigator().getActiveSprites(frame)) {
        if (!span.behaviors().empty()) {
            return true;
        }
    }
    return false;
}

bool FrameContext::hasFrameOneFrameScriptAndFrameTwoSpriteBehaviorStartupShape() const {
    return navigator().getFrameScript(1) != nullptr &&
           !hasScoreSpriteBehaviors(1) &&
           hasScoreSpriteBehaviors(2);
}

std::string FrameContext::scriptNameFor(const std::shared_ptr<chunks::ScriptChunk>& script) const {
    if (!script) {
        return "";
    }
    if (script_name_resolver_) {
        return script_name_resolver_(script);
    }
    if (script->file() != nullptr) {
        return script->file()->scriptNameForScript(*script);
    }
    return "";
}

void FrameContext::maybeDelayFlagsStartupCycle(const std::shared_ptr<behavior::BehaviorInstance>& instance,
                                               bool v1_startup_shape) {
    if (!v1_startup_shape || !instance || !instance->script()) {
        return;
    }
    if (scriptNameFor(instance->script()) != "Flags behavior") {
        return;
    }

    const auto wait = instance->getProperty("pWait");
    if (const auto* wait_int = wait.asInt(); wait_int != nullptr && wait_int->value == 3) {
        instance->setProperty("pWait", lingo::Datum::of(7));
    }
}

void FrameContext::logEvent(std::string_view message) const {
    if (debug_enabled_) {
        std::cout << "[FrameContext] " << message << '\n';
    }
}

void FrameContext::notifyEvent(PlayerEvent event) {
    if (event_listener_) {
        event_listener_(FrameEvent{event, current_frame_});
    }
}

} // namespace libreshockwave::player::frame
