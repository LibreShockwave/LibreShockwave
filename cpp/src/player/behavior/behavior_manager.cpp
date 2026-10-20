#include "libreshockwave/player/behavior/behavior_manager.hpp"

#include <algorithm>
#include <utility>

#include "libreshockwave/chunks/cast_member_chunk.hpp"
#include "libreshockwave/chunks/config_chunk.hpp"

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
    debug_enabled_ = enabled;
}

bool BehaviorManager::debugEnabled() const {
    return debug_enabled_;
}

void BehaviorManager::setScriptResolver(ScriptResolver resolver) {
    script_resolver_ = std::move(resolver);
}

void BehaviorManager::setFrameScriptResolver(ScriptResolver resolver) {
    frame_script_resolver_ = std::move(resolver);
}

std::shared_ptr<BehaviorInstance> BehaviorManager::createInstance(const score::ScoreBehaviorRef& behavior_ref,
                                                                  int channel) {
    return createInstanceForScript(findScript(behavior_ref), behavior_ref, channel);
}

std::shared_ptr<BehaviorInstance> BehaviorManager::createInstanceForScript(
    std::shared_ptr<chunks::ScriptChunk> script,
    const score::ScoreBehaviorRef& behavior_ref,
    int channel) {
    if (!script) {
        return nullptr;
    }

    auto instance = std::make_shared<BehaviorInstance>(std::move(script), behavior_ref, channel);
    applyParameters(instance, behavior_ref);
    instances_by_id_[instance->id()] = instance;
    instances_by_channel_[channel].push_back(instance);
    return instance;
}

std::shared_ptr<BehaviorInstance> BehaviorManager::getOrCreateFrameScript(
    const score::ScoreBehaviorRef& behavior_ref,
    int frame) {
    auto script = frame_script_resolver_ ? frame_script_resolver_(behavior_ref) : nullptr;
    return getOrCreateFrameScriptForScript(script ? std::move(script) : findScript(behavior_ref), behavior_ref, frame);
}

std::shared_ptr<BehaviorInstance> BehaviorManager::getOrCreateFrameScriptForScript(
    std::shared_ptr<chunks::ScriptChunk> script,
    const score::ScoreBehaviorRef& behavior_ref,
    int frame) {
    if (frame_script_instance_ && frame_script_frame_ == frame) {
        return frame_script_instance_;
    }

    if (frame_script_instance_) {
        removeInstance(frame_script_instance_);
    }

    frame_script_instance_ = createInstanceForScript(std::move(script), behavior_ref, 0);
    frame_script_frame_ = frame;
    return frame_script_instance_;
}

std::shared_ptr<BehaviorInstance> BehaviorManager::frameScriptInstance() const {
    return frame_script_instance_;
}

void BehaviorManager::clearFrameScript() {
    if (frame_script_instance_) {
        removeInstance(frame_script_instance_);
        frame_script_instance_.reset();
        frame_script_frame_ = -1;
    }
}

std::vector<std::shared_ptr<BehaviorInstance>> BehaviorManager::getInstancesForChannel(int channel) const {
    const auto it = instances_by_channel_.find(channel);
    return it == instances_by_channel_.end() ? std::vector<std::shared_ptr<BehaviorInstance>>{} : it->second;
}

bool BehaviorManager::hasInstanceForChannel(int channel, const score::ScoreBehaviorRef& behavior_ref) const {
    const auto it = instances_by_channel_.find(channel);
    if (it == instances_by_channel_.end()) {
        return false;
    }
    return std::any_of(it->second.begin(), it->second.end(), [&](const auto& instance) {
        return instance && instance->behaviorRef() == behavior_ref;
    });
}

std::shared_ptr<BehaviorInstance> BehaviorManager::getInstance(int id) const {
    const auto it = instances_by_id_.find(id);
    return it == instances_by_id_.end() ? nullptr : it->second;
}

void BehaviorManager::removeInstance(const std::shared_ptr<BehaviorInstance>& instance) {
    if (!instance) {
        return;
    }

    instances_by_id_.erase(instance->id());
    const auto it = instances_by_channel_.find(instance->spriteNum());
    if (it != instances_by_channel_.end()) {
        auto& list = it->second;
        list.erase(std::remove(list.begin(), list.end(), instance), list.end());
        if (list.empty()) {
            instances_by_channel_.erase(it);
        }
    }
    if (frame_script_instance_ == instance) {
        frame_script_instance_.reset();
        frame_script_frame_ = -1;
    }
}

void BehaviorManager::removeInstancesForChannel(int channel) {
    const auto it = instances_by_channel_.find(channel);
    if (it == instances_by_channel_.end()) {
        return;
    }
    for (const auto& instance : it->second) {
        if (instance) {
            instances_by_id_.erase(instance->id());
            if (frame_script_instance_ == instance) {
                frame_script_instance_.reset();
                frame_script_frame_ = -1;
            }
        }
    }
    instances_by_channel_.erase(it);
}

void BehaviorManager::clear() {
    instances_by_id_.clear();
    instances_by_channel_.clear();
    frame_script_instance_.reset();
    frame_script_frame_ = -1;
}

std::vector<std::shared_ptr<BehaviorInstance>> BehaviorManager::getAllInstances() const {
    std::vector<std::shared_ptr<BehaviorInstance>> result;
    result.reserve(instances_by_id_.size());
    for (const auto& [id, instance] : instances_by_id_) {
        (void)id;
        result.push_back(instance);
    }
    return result;
}

std::vector<std::shared_ptr<BehaviorInstance>> BehaviorManager::getSpriteInstances() const {
    std::vector<std::shared_ptr<BehaviorInstance>> result;
    for (const auto& [channel, instances] : instances_by_channel_) {
        if (channel <= 0) {
            continue;
        }
        result.insert(result.end(), instances.begin(), instances.end());
    }
    return result;
}

int BehaviorManager::instanceCount() const {
    return static_cast<int>(instances_by_id_.size());
}

std::shared_ptr<chunks::ScriptChunk> BehaviorManager::findScript(const score::ScoreBehaviorRef& behavior_ref) {
    if (script_resolver_) {
        if (auto script = script_resolver_(behavior_ref)) {
            return script;
        }
    }

    if (file_ == nullptr) {
        return nullptr;
    }

    auto member = file_->getCastMemberByNumber(behavior_ref.castLib(), behavior_ref.castMember());
    if (member && member->isScript()) {
        auto script = file_->getScriptForCastMember(member, file_->getMappedCastChunk(behavior_ref.castLib()));
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
                                      const score::ScoreBehaviorRef& behavior_ref) {
    if (!instance || !behavior_ref.hasParameters()) {
        return;
    }

    for (const auto& param : behavior_ref.parameters()) {
        if (!param.isPropList()) {
            continue;
        }
        for (const auto& [key, value] : param.propListValue().properties()) {
            instance->setProperty(parameterKey(key), value);
        }
    }
}

} // namespace libreshockwave::player::behavior
