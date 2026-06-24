#include "libreshockwave/player/frame/FrameContext.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/chunks/ScoreChunk.hpp"
#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/player/behavior/BehaviorInstance.hpp"
#include "libreshockwave/player/behavior/BehaviorManager.hpp"
#include "libreshockwave/player/event/EventDispatcher.hpp"
#include "libreshockwave/player/render/SpriteRegistry.hpp"
#include "libreshockwave/player/score/ScoreNavigator.hpp"
#include "libreshockwave/player/score/SpriteSpan.hpp"
#include "libreshockwave/player/sprite/SpriteState.hpp"

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
                                    const score::ScoreBehaviorRef& behaviorRef) {
    if (registry == nullptr || !behaviorRef.hasParameters()) {
        return;
    }
    auto sprite = registry->get(channel);
    if (sprite == nullptr) {
        return;
    }
    for (const auto& param : behaviorRef.parameters()) {
        if (!param.isPropList()) {
            continue;
        }
        for (const auto& [key, value] : param.propListValue().properties()) {
            sprite->setLegacyProperty(behaviorParameterMetadataKey(key), value);
        }
    }
}

} // namespace

FrameContext::FrameContext(DirectorFile* file)
    : file_(file),
      ownedNavigator_(std::make_unique<score::ScoreNavigator>(file)),
      navigator_(ownedNavigator_.get()),
      ownedBehaviorManager_(std::make_unique<behavior::BehaviorManager>(file)),
      behaviorManager_(ownedBehaviorManager_.get()),
      ownedEventDispatcher_(std::make_unique<event::EventDispatcher>(behaviorManager_)),
      eventDispatcher_(ownedEventDispatcher_.get()) {
    bindDispatcher();
}

FrameContext::FrameContext(DirectorFile* file,
                           score::ScoreNavigator* navigator,
                           behavior::BehaviorManager* behaviorManager,
                           event::EventDispatcher* eventDispatcher)
    : file_(file),
      navigator_(navigator),
      behaviorManager_(behaviorManager),
      eventDispatcher_(eventDispatcher) {
    requireDependency(navigator_, "navigator");
    requireDependency(behaviorManager_, "behaviorManager");
    requireDependency(eventDispatcher_, "eventDispatcher");
    bindDispatcher();
}

FrameContext::~FrameContext() = default;
FrameContext::FrameContext(FrameContext&&) noexcept = default;
FrameContext& FrameContext::operator=(FrameContext&&) noexcept = default;

void FrameContext::setDebugEnabled(bool enabled) {
    debugEnabled_ = enabled;
    behaviorManager().setDebugEnabled(enabled);
    eventDispatcher().setDebugEnabled(enabled);
}

bool FrameContext::debugEnabled() const {
    return debugEnabled_;
}

void FrameContext::setEventListener(EventListener listener) {
    eventListener_ = std::move(listener);
}

void FrameContext::setActorListDispatcher(ActorListDispatcher dispatcher) {
    actorListDispatcher_ = std::move(dispatcher);
}

void FrameContext::setTimeoutEventDispatcher(TimeoutEventDispatcher dispatcher) {
    timeoutEventDispatcher_ = std::move(dispatcher);
}

void FrameContext::setScriptNameResolver(ScriptNameResolver resolver) {
    scriptNameResolver_ = std::move(resolver);
}

void FrameContext::setSpriteRegistry(render::SpriteRegistry* registry) {
    spriteRegistry_ = registry;
    eventDispatcher().setSpriteRegistry(registry);
}

int FrameContext::currentFrame() const { return currentFrame_; }
int FrameContext::effectiveFrame() const { return pendingFrame_ > 0 ? pendingFrame_ : currentFrame_; }
int FrameContext::frameCount() const { return navigator().getFrameCount(); }
score::ScoreNavigator& FrameContext::navigator() { return requireDependency(navigator_, "navigator"); }
const score::ScoreNavigator& FrameContext::navigator() const { return requireDependency(navigator_, "navigator"); }
behavior::BehaviorManager& FrameContext::behaviorManager() { return requireDependency(behaviorManager_, "behaviorManager"); }
const behavior::BehaviorManager& FrameContext::behaviorManager() const { return requireDependency(behaviorManager_, "behaviorManager"); }
event::EventDispatcher& FrameContext::eventDispatcher() { return requireDependency(eventDispatcher_, "eventDispatcher"); }
const event::EventDispatcher& FrameContext::eventDispatcher() const { return requireDependency(eventDispatcher_, "eventDispatcher"); }
const std::set<int>& FrameContext::activeChannels() const { return activeChannels_; }
bool FrameContext::inFrameScript() const { return inFrameScript_; }

void FrameContext::goToFrame(int frame) {
    const int max = frameCount();
    if (frame >= 1 && frame <= max) {
        pendingFrame_ = frame;
    }
}

void FrameContext::forceGoToFrame(int frame) {
    const int max = frameCount();
    if (frame < 1 || frame > max) {
        return;
    }

    const int oldFrame = currentFrame_;
    logEvent("forceGoToFrame");
    endSpritesLeavingFrame(oldFrame, frame);
    behaviorManager().clearFrameScript();

    currentFrame_ = frame;
    pendingFrame_ = 0;
    enterFrame(frame);
}

void FrameContext::goToLabel(std::string_view label) {
    const int frame = navigator().getFrameForLabel(label);
    if (frame > 0) {
        goToFrame(frame);
    }
}

void FrameContext::initializeFirstFrame() {
    currentFrame_ = 1;
    pendingFrame_ = 0;
    activeChannels_.clear();
    enteredChannels_.clear();
    behaviorManager().clear();

    logEvent("initializeFirstFrame");
    beginSpritesForFrame(currentFrame_);
    initializeFrameScript(currentFrame_);
}

bool FrameContext::executeFrame(bool suppressSpriteEnterFrameOnce) {
    dispatchToActorList("stepFrame");
    dispatchEvent(PlayerEvent::StepFrame);

    dispatchToActorList("prepareFrame");
    if (timeoutEventDispatcher_) {
        timeoutEventDispatcher_("prepareFrame");
    }
    dispatchEvent(PlayerEvent::PrepareFrame);

    dispatchToActorList("enterFrame");
    inFrameScript_ = true;
    if (suppressSpriteEnterFrameOnce) {
        eventDispatcher().dispatchFrameAndMovieEvent(PlayerEvent::EnterFrame);
        notifyEvent(PlayerEvent::EnterFrame);
    } else {
        dispatchEvent(PlayerEvent::EnterFrame);
    }
    inFrameScript_ = false;
    return true;
}

void FrameContext::dispatchBeginSpriteEvents() {
    dispatchBeginSprite();
}

void FrameContext::rebindBehaviorsForLoadedCast(int castLibNumber) {
    if (castLibNumber <= 0) {
        return;
    }

    for (const auto& span : navigator().getActiveSprites(currentFrame_)) {
        const int channel = span.channel();
        if (!activeChannels_.contains(channel)) {
            continue;
        }
        for (const auto& behaviorRef : span.behaviors()) {
            if (behaviorRef.castLib() != castLibNumber ||
                behaviorManager().hasInstanceForChannel(channel, behaviorRef)) {
                continue;
            }
            if (spriteRegistry_ != nullptr) {
                spriteRegistry_->markScoreBehaviorChannel(channel);
                applyBehaviorParameterMetadata(spriteRegistry_, channel, behaviorRef);
            }
            auto instance = behaviorManager().createInstance(behaviorRef, channel);
            if (!instance) {
                continue;
            }
            eventDispatcher().dispatchBehaviorEvent(instance, PlayerEvent::BeginSprite);
            instance->setBeginSpriteCalled(true);
            logEvent("rebindBehavior");
        }
    }

    const auto* frameScript = navigator().getFrameScript(currentFrame_);
    if (frameScript == nullptr ||
        frameScript->castLib() != castLibNumber ||
        behaviorManager().frameScriptInstance() != nullptr) {
        return;
    }

    auto frameInstance = behaviorManager().getOrCreateFrameScript(*frameScript, currentFrame_);
    if (frameInstance) {
        eventDispatcher().dispatchBehaviorEvent(frameInstance, PlayerEvent::BeginSprite);
        frameInstance->setBeginSpriteCalled(true);
        logEvent("rebindFrameScript");
    }
}

int FrameContext::advanceFrame() {
    const int oldFrame = currentFrame_;

    dispatchToActorList("exitFrame");
    if (timeoutEventDispatcher_) {
        timeoutEventDispatcher_("exitFrame");
    }
    dispatchEvent(PlayerEvent::ExitFrame);

    int newFrame = 0;
    if (pendingFrame_ > 0) {
        newFrame = pendingFrame_;
        pendingFrame_ = 0;
    } else {
        newFrame = currentFrame_ + 1;
    }

    const int max = frameCount();
    if (max > 0 && newFrame > max) {
        newFrame = 1;
    }

    if (newFrame != oldFrame) {
        logEvent("advanceFrame");
        endSpritesLeavingFrame(oldFrame, newFrame);
        behaviorManager().clearFrameScript();
        currentFrame_ = newFrame;
        enterFrame(newFrame);
    }

    return currentFrame_;
}

void FrameContext::reset() {
    currentFrame_ = 1;
    pendingFrame_ = 0;
    activeChannels_.clear();
    enteredChannels_.clear();
    behaviorManager().clear();
    inFrameScript_ = false;
}

void FrameContext::bindDispatcher() {
    if (eventDispatcher_ != nullptr) {
        eventDispatcher_->setBehaviorManager(behaviorManager_);
        eventDispatcher_->setSpriteRegistry(spriteRegistry_);
    }
}

void FrameContext::enterFrame(int frame) {
    beginSpritesForFrame(frame);
    initializeFrameScript(frame);
    dispatchBeginSprite();
    enteredChannels_.clear();
}

void FrameContext::beginSpritesForFrame(int frame) {
    auto spans = navigator().getActiveSprites(frame);
    std::ranges::sort(spans, [](const score::SpriteSpan& left, const score::SpriteSpan& right) {
        return left.channel() < right.channel();
    });

    for (const auto& span : spans) {
        const int channel = span.channel();
        const bool newlyActive = !activeChannels_.contains(channel);
        if (newlyActive) {
            activeChannels_.insert(channel);
            enteredChannels_.insert(channel);
            ensureScoreSpriteState(frame, channel);
        }

        for (const auto& behaviorRef : span.behaviors()) {
            if (behaviorManager().hasInstanceForChannel(channel, behaviorRef)) {
                continue;
            }
            if (spriteRegistry_ != nullptr) {
                spriteRegistry_->markScoreBehaviorChannel(channel);
                applyBehaviorParameterMetadata(spriteRegistry_, channel, behaviorRef);
            }
            (void)behaviorManager().createInstance(behaviorRef, channel);
            enteredChannels_.insert(channel);
        }
        if (newlyActive) {
            logEvent("beginSprite");
        }
    }
}

void FrameContext::ensureScoreSpriteState(int frame, int channel) {
    if (spriteRegistry_ == nullptr || file_ == nullptr) {
        return;
    }
    auto score = file_->scoreChunk();
    if (!score) {
        return;
    }
    const int frameIndex = frame - 1;
    for (const auto& entry : score->frameData().frameChannelData) {
        if (entry.frameIndex.value() == frameIndex && entry.channelIndex.value() == channel) {
            (void)spriteRegistry_->getOrCreate(channel, entry.data);
            return;
        }
    }
}

void FrameContext::endSpritesLeavingFrame(int oldFrame, int newFrame) {
    (void)oldFrame;
    const auto newActiveChannels = navigator().getActiveChannels(newFrame);
    std::vector<int> leaving;
    for (const int channel : activeChannels_) {
        if (newActiveChannels.contains(channel)) {
            continue;
        }
        if (spriteRegistry_ != nullptr) {
            auto state = spriteRegistry_->get(channel);
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
        activeChannels_.erase(channel);
        behaviorManager().removeInstancesForChannel(channel);
        if (spriteRegistry_ != nullptr) {
            spriteRegistry_->remove(channel);
        }
        logEvent("endSprite");
    }
}

void FrameContext::initializeFrameScript(int frame) {
    if (const auto* frameScript = navigator().getFrameScript(frame); frameScript != nullptr) {
        (void)behaviorManager().getOrCreateFrameScript(*frameScript, frame);
        logEvent("initializeFrameScript");
    } else {
        logEvent("initializeFrameScript: none");
    }
}

void FrameContext::dispatchToActorList(std::string_view handlerName) {
    if (actorListDispatcher_) {
        actorListDispatcher_(handlerName);
    }
}

void FrameContext::dispatchEvent(PlayerEvent event) {
    eventDispatcher().dispatchGlobalEvent(event);
    notifyEvent(event);
}

void FrameContext::dispatchBeginSprite() {
    const bool v1StartupShape = hasFrameOneFrameScriptAndFrameTwoSpriteBehaviorStartupShape();
    for (const int channel : enteredChannels_) {
        for (const auto& instance : behaviorManager().getInstancesForChannel(channel)) {
            if (!instance || instance->isBeginSpriteCalled()) {
                continue;
            }
            eventDispatcher().dispatchBehaviorEvent(instance, PlayerEvent::BeginSprite);
            maybeDelayFlagsStartupCycle(instance, v1StartupShape);
            instance->setBeginSpriteCalled(true);
        }
    }

    auto frameInstance = behaviorManager().frameScriptInstance();
    if (frameInstance && !frameInstance->isBeginSpriteCalled()) {
        eventDispatcher().dispatchFrameAndMovieEvent(PlayerEvent::BeginSprite);
        frameInstance->setBeginSpriteCalled(true);
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
    if (scriptNameResolver_) {
        return scriptNameResolver_(script);
    }
    if (script->file() != nullptr) {
        return const_cast<DirectorFile*>(script->file())->getScriptName(script);
    }
    return "";
}

void FrameContext::maybeDelayFlagsStartupCycle(const std::shared_ptr<behavior::BehaviorInstance>& instance,
                                               bool v1StartupShape) {
    if (!v1StartupShape || !instance || !instance->script()) {
        return;
    }
    if (scriptNameFor(instance->script()) != "Flags behavior") {
        return;
    }

    const auto wait = instance->getProperty("pWait");
    if (const auto* waitInt = wait.asInt(); waitInt != nullptr && waitInt->value == 3) {
        instance->setProperty("pWait", lingo::Datum::of(7));
    }
}

void FrameContext::logEvent(std::string_view message) const {
    if (debugEnabled_) {
        std::cout << "[FrameContext] " << message << '\n';
    }
}

void FrameContext::notifyEvent(PlayerEvent event) {
    if (eventListener_) {
        eventListener_(FrameEvent{event, currentFrame_});
    }
}

} // namespace libreshockwave::player::frame
