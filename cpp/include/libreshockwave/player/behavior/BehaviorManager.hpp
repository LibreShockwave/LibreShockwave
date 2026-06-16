#pragma once

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/player/behavior/BehaviorInstance.hpp"

namespace libreshockwave::player::behavior {

class BehaviorManager {
public:
    using ScriptResolver = std::function<std::shared_ptr<chunks::ScriptChunk>(const score::ScoreBehaviorRef& behaviorRef)>;

    explicit BehaviorManager(DirectorFile* file = nullptr);

    void setDebugEnabled(bool enabled);
    [[nodiscard]] bool debugEnabled() const;
    void setScriptResolver(ScriptResolver resolver);

    [[nodiscard]] std::shared_ptr<BehaviorInstance> createInstance(
        const score::ScoreBehaviorRef& behaviorRef,
        int channel);
    [[nodiscard]] std::shared_ptr<BehaviorInstance> createInstanceForScript(
        std::shared_ptr<chunks::ScriptChunk> script,
        const score::ScoreBehaviorRef& behaviorRef,
        int channel);
    [[nodiscard]] std::shared_ptr<BehaviorInstance> getOrCreateFrameScript(
        const score::ScoreBehaviorRef& behaviorRef,
        int frame);
    [[nodiscard]] std::shared_ptr<BehaviorInstance> getOrCreateFrameScriptForScript(
        std::shared_ptr<chunks::ScriptChunk> script,
        const score::ScoreBehaviorRef& behaviorRef,
        int frame);
    [[nodiscard]] std::shared_ptr<BehaviorInstance> frameScriptInstance() const;
    void clearFrameScript();

    [[nodiscard]] std::vector<std::shared_ptr<BehaviorInstance>> getInstancesForChannel(int channel) const;
    [[nodiscard]] bool hasInstanceForChannel(int channel, const score::ScoreBehaviorRef& behaviorRef) const;
    [[nodiscard]] std::shared_ptr<BehaviorInstance> getInstance(int id) const;
    void removeInstance(const std::shared_ptr<BehaviorInstance>& instance);
    void removeInstancesForChannel(int channel);
    void clear();

    [[nodiscard]] std::vector<std::shared_ptr<BehaviorInstance>> getAllInstances() const;
    [[nodiscard]] std::vector<std::shared_ptr<BehaviorInstance>> getSpriteInstances() const;
    [[nodiscard]] int instanceCount() const;

private:
    [[nodiscard]] std::shared_ptr<chunks::ScriptChunk> findScript(const score::ScoreBehaviorRef& behaviorRef);
    [[nodiscard]] std::shared_ptr<chunks::ScriptChunk> selectBehaviorScript(
        const std::vector<std::shared_ptr<chunks::ScriptChunk>>& candidates) const;
    [[nodiscard]] bool usesLegacyDuplicateScriptContexts() const;
    void applyParameters(const std::shared_ptr<BehaviorInstance>& instance,
                         const score::ScoreBehaviorRef& behaviorRef);

    DirectorFile* file_{nullptr};
    ScriptResolver scriptResolver_;
    std::map<int, std::shared_ptr<BehaviorInstance>> instancesById_;
    std::map<int, std::vector<std::shared_ptr<BehaviorInstance>>> instancesByChannel_;
    std::shared_ptr<BehaviorInstance> frameScriptInstance_;
    int frameScriptFrame_{-1};
    bool debugEnabled_{false};
};

} // namespace libreshockwave::player::behavior
