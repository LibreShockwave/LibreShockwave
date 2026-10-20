#include "libreshockwave/player/score/sprite_span.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace libreshockwave::player::score {

SpriteSpan::SpriteSpan(int channel, int start_frame, int end_frame)
    : channel_(channel),
      start_frame_(std::max(1, start_frame)),
      end_frame_(std::max(1, end_frame)) {}

int SpriteSpan::channel() const { return channel_.value(); }
id::ChannelId SpriteSpan::channelId() const { return channel_; }
int SpriteSpan::startFrame() const { return start_frame_.value(); }
id::FrameId SpriteSpan::startFrameId() const { return start_frame_; }
int SpriteSpan::endFrame() const { return end_frame_.value(); }
id::FrameId SpriteSpan::endFrameId() const { return end_frame_; }
const std::vector<ScoreBehaviorRef>& SpriteSpan::behaviors() const { return behaviors_; }

void SpriteSpan::addBehavior(ScoreBehaviorRef behavior) {
    behaviors_.push_back(std::move(behavior));
}

bool SpriteSpan::isFrameBehavior() const {
    return channel_.value() == 0;
}

bool SpriteSpan::containsFrame(int frame) const {
    return frame >= start_frame_.value() && frame <= end_frame_.value();
}

const ScoreBehaviorRef* SpriteSpan::firstBehavior() const {
    return behaviors_.empty() ? nullptr : &behaviors_.front();
}

std::string SpriteSpan::toString() const {
    std::ostringstream out;
    out << "SpriteSpan{channel=" << channel_.value()
        << ", frames=" << start_frame_.value() << "-" << end_frame_.value()
        << ", behaviors=" << behaviors_.size() << "}";
    return out.str();
}

} // namespace libreshockwave::player::score
