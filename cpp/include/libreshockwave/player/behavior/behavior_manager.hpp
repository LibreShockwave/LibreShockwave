#pragma once

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "libreshockwave/director_file.hpp"
#include "libreshockwave/player/behavior/behavior_instance.hpp"

namespace libreshockwave::player::behavior {

class BehaviorManager {
public:
    using ScriptResolver = std::function<std::shared_ptr<chunks::ScriptChunk>(const score::ScoreBehaviorRef& behavior_ref)>;

    explicit BehaviorManager(DirectorFile* file = nullptr);

    void setDebugEnabled(bool enabled);
    [[nodiscard]] bool debugEnabled() const;
    void setScriptResolver(ScriptResolver resolver);
    void setFrameScriptResolver(ScriptResolver resolver);

    [[nodiscard]] std::shared_ptr<BehaviorInstance> createInstance(
        const score::ScoreBehaviorRef& behavior_ref,
        int channel);
    [[nodiscard]] std::shared_ptr<BehaviorInstance> createInstanceForScript(
        std::shared_ptr<chunks::ScriptChunk> script,
        const score::ScoreBehaviorRef& behavior_ref,
        int channel);
    [[nodiscard]] std::shared_ptr<BehaviorInstance> getOrCreateFrameScript(
        const score::ScoreBehaviorRef& behavior_ref,
        int frame);
    [[nodiscard]] std::shared_ptr<BehaviorInstance> getOrCreateFrameScriptForScript(
        std::shared_ptr<chunks::ScriptChunk> script,
        const score::ScoreBehaviorRef& behavior_ref,
        int frame);
    [[nodiscard]] std::shared_ptr<BehaviorInstance> frameScriptInstance() const;
    void clearFrameScript();

    [[nodiscard]] std::vector<std::shared_ptr<BehaviorInstance>> getInstancesForChannel(int channel) const;
    [[nodiscard]] bool hasInstanceForChannel(int channel, const score::ScoreBehaviorRef& behavior_ref) const;
    [[nodiscard]] std::shared_ptr<BehaviorInstance> getInstance(int id) const;
    void removeInstance(const std::shared_ptr<BehaviorInstance>& instance);
    void removeInstancesForChannel(int channel);
    void clear();

    [[nodiscard]] std::vector<std::shared_ptr<BehaviorInstance>> getAllInstances() const;
    [[nodiscard]] std::vector<std::shared_ptr<BehaviorInstance>> getSpriteInstances() const;
    [[nodiscard]] int instanceCount() const;

private:
    [[nodiscard]] std::shared_ptr<chunks::ScriptChunk> findScript(const score::ScoreBehaviorRef& behavior_ref);
    [[nodiscard]] std::shared_ptr<chunks::ScriptChunk> selectBehaviorScript(
        const std::vector<std::shared_ptr<chunks::ScriptChunk>>& candidates) const;
    [[nodiscard]] bool usesLegacyDuplicateScriptContexts() const;
    void applyParameters(const std::shared_ptr<BehaviorInstance>& instance,
                         const score::ScoreBehaviorRef& behavior_ref);

    DirectorFile* file_{nullptr};
    ScriptResolver script_resolver_;
    ScriptResolver frame_script_resolver_;
    std::map<int, std::shared_ptr<BehaviorInstance>> instances_by_id_;
    std::map<int, std::vector<std::shared_ptr<BehaviorInstance>>> instances_by_channel_;
    std::shared_ptr<BehaviorInstance> frame_script_instance_;
    int frame_script_frame_{-1};
    bool debug_enabled_{false};
};

} // namespace libreshockwave::player::behavior
