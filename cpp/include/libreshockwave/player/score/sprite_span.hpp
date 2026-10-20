#pragma once

#include <string>
#include <vector>

#include "libreshockwave/id/ids.hpp"
#include "libreshockwave/player/score/score_behavior_ref.hpp"

namespace libreshockwave::player::score {

class SpriteSpan {
public:
    SpriteSpan(int channel, int start_frame, int end_frame);

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
    id::FrameId start_frame_;
    id::FrameId end_frame_;
    std::vector<ScoreBehaviorRef> behaviors_;
};

} // namespace libreshockwave::player::score
