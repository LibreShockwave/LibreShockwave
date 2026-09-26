#include "libreshockwave/player/audio/SoundQueue.hpp"

#include <utility>

namespace libreshockwave::player::audio {

namespace {

lingo::Datum normalizeEntry(const lingo::Datum& entry) {
    if (entry.isList()) {
        const auto& items = entry.listValue().items();
        if (items.size() == 1 && items.front().isPropList()) {
            return items.front().deepCopy();
        }
    }
    return entry.deepCopy();
}

} // namespace

void SoundQueue::push(const lingo::Datum& entry) {
    entries_.push_back(normalizeEntry(entry));
}

void SoundQueue::replace(const lingo::Datum& playlist) {
    entries_.clear();
    if (playlist.isVoid()) {
        return;
    }
    if (playlist.isList()) {
        for (const auto& item : playlist.listValue().items()) {
            push(item);
        }
        return;
    }
    push(playlist);
}

std::vector<lingo::Datum> SoundQueue::entries() const {
    std::vector<lingo::Datum> result;
    result.reserve(entries_.size());
    for (const auto& entry : entries_) {
        result.push_back(entry.deepCopy());
    }
    return result;
}

std::optional<lingo::Datum> SoundQueue::popFront() {
    if (entries_.empty()) {
        return std::nullopt;
    }
    lingo::Datum front = std::move(entries_.front());
    entries_.erase(entries_.begin());
    return front;
}

void SoundQueue::start() {
    running_ = true;
}

void SoundQueue::clear() {
    entries_.clear();
    running_ = false;
}

bool SoundQueue::isRunning() const {
    return running_;
}

bool SoundQueue::hasWaiting() const {
    return running_ && !entries_.empty();
}

} // namespace libreshockwave::player::audio
