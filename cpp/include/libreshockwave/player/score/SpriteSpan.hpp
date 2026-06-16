#pragma once

#include <string>
#include <vector>

#include "libreshockwave/id/Ids.hpp"
#include "libreshockwave/player/score/ScoreBehaviorRef.hpp"

namespace libreshockwave::player::score {

class SpriteSpan {
public:
    SpriteSpan(int channel, int startFrame, int endFrame);

    [[nodiscard]] int channel() const;
    [[nodiscard]] id::ChannelId channelId() const;
    [[nodiscard]] int startFrame() const;
    [[nodiscard]] id::FrameId startFrameId() const;
    [[nodiscard]] int endFrame() const;
    [[nodiscard]] id::FrameId endFrameId() const;
    [[nodiscard]] const std::vector<ScoreBehaviorRef>& behaviors() const;
    void addBehavior(ScoreBehaviorRef behavior);

    [[nodiscard]] bool isFrameBehavior() const;
    [[nodiscard]] bool containsFrame(int frame) const;
    [[nodiscard]] const ScoreBehaviorRef* firstBehavior() const;
    [[nodiscard]] std::string toString() const;

    friend bool operator==(const SpriteSpan&, const SpriteSpan&) = default;

private:
    id::ChannelId channel_;
    id::FrameId startFrame_;
    id::FrameId endFrame_;
    std::vector<ScoreBehaviorRef> behaviors_;
};

} // namespace libreshockwave::player::score
