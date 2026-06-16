#include "libreshockwave/player/score/SpriteSpan.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace libreshockwave::player::score {

SpriteSpan::SpriteSpan(int channel, int startFrame, int endFrame)
    : channel_(channel),
      startFrame_(std::max(1, startFrame)),
      endFrame_(std::max(1, endFrame)) {}

int SpriteSpan::channel() const { return channel_.value(); }
id::ChannelId SpriteSpan::channelId() const { return channel_; }
int SpriteSpan::startFrame() const { return startFrame_.value(); }
id::FrameId SpriteSpan::startFrameId() const { return startFrame_; }
int SpriteSpan::endFrame() const { return endFrame_.value(); }
id::FrameId SpriteSpan::endFrameId() const { return endFrame_; }
const std::vector<ScoreBehaviorRef>& SpriteSpan::behaviors() const { return behaviors_; }

void SpriteSpan::addBehavior(ScoreBehaviorRef behavior) {
    behaviors_.push_back(std::move(behavior));
}

bool SpriteSpan::isFrameBehavior() const {
    return channel_.value() == 0;
}

bool SpriteSpan::containsFrame(int frame) const {
    return frame >= startFrame_.value() && frame <= endFrame_.value();
}

const ScoreBehaviorRef* SpriteSpan::firstBehavior() const {
    return behaviors_.empty() ? nullptr : &behaviors_.front();
}

std::string SpriteSpan::toString() const {
    std::ostringstream out;
    out << "SpriteSpan{channel=" << channel_.value()
        << ", frames=" << startFrame_.value() << "-" << endFrame_.value()
        << ", behaviors=" << behaviors_.size() << "}";
    return out.str();
}

} // namespace libreshockwave::player::score
