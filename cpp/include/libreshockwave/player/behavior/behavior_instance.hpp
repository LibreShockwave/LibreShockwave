#pragma once

#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/chunks/script_chunk.hpp"
#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/player/score/score_behavior_ref.hpp"

namespace libreshockwave::player::behavior {

class BehaviorInstance {
public:
    BehaviorInstance(std::shared_ptr<chunks::ScriptChunk> script,
                     score::ScoreBehaviorRef behavior_ref,
                     int sprite_num);

    [[nodiscard]] int id() const;
    [[nodiscard]] const std::shared_ptr<chunks::ScriptChunk>& script() const;
    [[nodiscard]] const score::ScoreBehaviorRef& behaviorRef() const;
    [[nodiscard]] int spriteNum() const;
    [[nodiscard]] bool isFrameBehavior() const;

    [[nodiscard]] lingo::Datum getProperty(const std::string& name) const;
    void setProperty(const std::string& name, lingo::Datum value);
    [[nodiscard]] const std::vector<std::pair<std::string, lingo::Datum>>& properties() const;

    [[nodiscard]] bool isBeginSpriteCalled() const;
    void setBeginSpriteCalled(bool called);
    [[nodiscard]] bool isEndSpriteCalled() const;
    void setEndSpriteCalled(bool called);

    [[nodiscard]] lingo::Datum toDatum() const;
    [[nodiscard]] std::string toString() const;

private:
    [[nodiscard]] static int nextId();
    [[nodiscard]] static std::string scriptDisplayName(const std::shared_ptr<chunks::ScriptChunk>& script);

    int id_{0};
    std::shared_ptr<chunks::ScriptChunk> script_;
    score::ScoreBehaviorRef behavior_ref_;
    int sprite_num_{0};
    lingo::Datum receiver_;
    bool begin_sprite_called_{false};
    bool end_sprite_called_{false};
};

} // namespace libreshockwave::player::behavior
