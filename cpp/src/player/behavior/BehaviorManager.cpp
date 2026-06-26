#include "libreshockwave/player/behavior/BehaviorManager.hpp"

#include <algorithm>
#include <utility>

#include "libreshockwave/chunks/CastMemberChunk.hpp"
#include "libreshockwave/chunks/ConfigChunk.hpp"

namespace libreshockwave::player::behavior {
namespace {

std::string parameterKey(const lingo::Datum& key) {
    if (const auto* symbol = key.asSymbol()) {
        return symbol->name;
    }
    return key.stringValue();
}

} // namespace

BehaviorManager::BehaviorManager(DirectorFile* file) : file_(file) {}

void BehaviorManager::setDebugEnabled(bool enabled) {
    debugEnabled_ = enabled;
}

bool BehaviorManager::debugEnabled() const {
    return debugEnabled_;
}

void BehaviorManager::setScriptResolver(ScriptResolver resolver) {
    scriptResolver_ = std::move(resolver);
}

void BehaviorManager::setFrameScriptResolver(ScriptResolver resolver) {
    frameScriptResolver_ = std::move(resolver);
}

std::shared_ptr<BehaviorInstance> BehaviorManager::createInstance(const score::ScoreBehaviorRef& behaviorRef,
                                                                  int channel) {
    return createInstanceForScript(findScript(behaviorRef), behaviorRef, channel);
}

std::shared_ptr<BehaviorInstance> BehaviorManager::createInstanceForScript(
    std::shared_ptr<chunks::ScriptChunk> script,
    const score::ScoreBehaviorRef& behaviorRef,
    int channel) {
    if (!script) {
        return nullptr;
    }

    auto instance = std::make_shared<BehaviorInstance>(std::move(script), behaviorRef, channel);
    applyParameters(instance, behaviorRef);
    instancesById_[instance->id()] = instance;
    instancesByChannel_[channel].push_back(instance);
    return instance;
}

std::shared_ptr<BehaviorInstance> BehaviorManager::getOrCreateFrameScript(
    const score::ScoreBehaviorRef& behaviorRef,
    int frame) {
    auto script = frameScriptResolver_ ? frameScriptResolver_(behaviorRef) : nullptr;
    return getOrCreateFrameScriptForScript(script ? std::move(script) : findScript(behaviorRef), behaviorRef, frame);
}

std::shared_ptr<BehaviorInstance> BehaviorManager::getOrCreateFrameScriptForScript(
    std::shared_ptr<chunks::ScriptChunk> script,
    const score::ScoreBehaviorRef& behaviorRef,
    int frame) {
    if (frameScriptInstance_ && frameScriptFrame_ == frame) {
        return frameScriptInstance_;
    }

    if (frameScriptInstance_) {
        removeInstance(frameScriptInstance_);
    }

    frameScriptInstance_ = createInstanceForScript(std::move(script), behaviorRef, 0);
    frameScriptFrame_ = frame;
    return frameScriptInstance_;
}

std::shared_ptr<BehaviorInstance> BehaviorManager::frameScriptInstance() const {
    return frameScriptInstance_;
}

void BehaviorManager::clearFrameScript() {
    if (frameScriptInstance_) {
        removeInstance(frameScriptInstance_);
        frameScriptInstance_.reset();
        frameScriptFrame_ = -1;
    }
}

std::vector<std::shared_ptr<BehaviorInstance>> BehaviorManager::getInstancesForChannel(int channel) const {
    const auto it = instancesByChannel_.find(channel);
    return it == instancesByChannel_.end() ? std::vector<std::shared_ptr<BehaviorInstance>>{} : it->second;
}

bool BehaviorManager::hasInstanceForChannel(int channel, const score::ScoreBehaviorRef& behaviorRef) const {
    const auto it = instancesByChannel_.find(channel);
    if (it == instancesByChannel_.end()) {
        return false;
    }
    return std::any_of(it->second.begin(), it->second.end(), [&](const auto& instance) {
        return instance && instance->behaviorRef() == behaviorRef;
    });
}

std::shared_ptr<BehaviorInstance> BehaviorManager::getInstance(int id) const {
    const auto it = instancesById_.find(id);
    return it == instancesById_.end() ? nullptr : it->second;
}

void BehaviorManager::removeInstance(const std::shared_ptr<BehaviorInstance>& instance) {
    if (!instance) {
        return;
    }

    instancesById_.erase(instance->id());
    const auto it = instancesByChannel_.find(instance->spriteNum());
    if (it != instancesByChannel_.end()) {
        auto& list = it->second;
        list.erase(std::remove(list.begin(), list.end(), instance), list.end());
        if (list.empty()) {
            instancesByChannel_.erase(it);
        }
    }
    if (frameScriptInstance_ == instance) {
        frameScriptInstance_.reset();
        frameScriptFrame_ = -1;
    }
}

void BehaviorManager::removeInstancesForChannel(int channel) {
    const auto it = instancesByChannel_.find(channel);
    if (it == instancesByChannel_.end()) {
        return;
    }
    for (const auto& instance : it->second) {
        if (instance) {
            instancesById_.erase(instance->id());
            if (frameScriptInstance_ == instance) {
                frameScriptInstance_.reset();
                frameScriptFrame_ = -1;
            }
        }
    }
    instancesByChannel_.erase(it);
}

void BehaviorManager::clear() {
    instancesById_.clear();
    instancesByChannel_.clear();
    frameScriptInstance_.reset();
    frameScriptFrame_ = -1;
}

std::vector<std::shared_ptr<BehaviorInstance>> BehaviorManager::getAllInstances() const {
    std::vector<std::shared_ptr<BehaviorInstance>> result;
    result.reserve(instancesById_.size());
    for (const auto& [id, instance] : instancesById_) {
        (void)id;
        result.push_back(instance);
    }
    return result;
}

std::vector<std::shared_ptr<BehaviorInstance>> BehaviorManager::getSpriteInstances() const {
    std::vector<std::shared_ptr<BehaviorInstance>> result;
    for (const auto& [channel, instances] : instancesByChannel_) {
        if (channel <= 0) {
            continue;
        }
        result.insert(result.end(), instances.begin(), instances.end());
    }
    return result;
}

int BehaviorManager::instanceCount() const {
    return static_cast<int>(instancesById_.size());
}

std::shared_ptr<chunks::ScriptChunk> BehaviorManager::findScript(const score::ScoreBehaviorRef& behaviorRef) {
    if (scriptResolver_) {
        if (auto script = scriptResolver_(behaviorRef)) {
            return script;
        }
    }

    if (file_ == nullptr) {
        return nullptr;
    }

    auto member = file_->getCastMemberByNumber(behaviorRef.castLib(), behaviorRef.castMember());
    if (member && member->isScript()) {
        auto script = file_->getScriptForCastMember(member, file_->getMappedCastChunk(behaviorRef.castLib()));
        if (!script) {
            script = selectBehaviorScript(file_->getScriptsByContextId(member->scriptId()));
        }
        if (script) {
            return script;
        }
    }
    return nullptr;
}

std::shared_ptr<chunks::ScriptChunk> BehaviorManager::selectBehaviorScript(
    const std::vector<std::shared_ptr<chunks::ScriptChunk>>& candidates) const {
    if (candidates.empty()) {
        return nullptr;
    }
    if (usesLegacyDuplicateScriptContexts()) {
        for (const auto& script : candidates) {
            if (script && script->scriptType() == chunks::ScriptChunkType::Behavior) {
                return script;
            }
        }
    }
    return candidates.front();
}

bool BehaviorManager::usesLegacyDuplicateScriptContexts() const {
    return file_ != nullptr &&
           file_->config() != nullptr &&
           file_->config()->directorVersion() > 0 &&
           file_->config()->directorVersion() <= 1600;
}

void BehaviorManager::applyParameters(const std::shared_ptr<BehaviorInstance>& instance,
                                      const score::ScoreBehaviorRef& behaviorRef) {
    if (!instance || !behaviorRef.hasParameters()) {
        return;
    }

    for (const auto& param : behaviorRef.parameters()) {
        if (!param.isPropList()) {
            continue;
        }
        for (const auto& [key, value] : param.propListValue().properties()) {
            instance->setProperty(parameterKey(key), value);
        }
    }
}

} // namespace libreshockwave::player::behavior
