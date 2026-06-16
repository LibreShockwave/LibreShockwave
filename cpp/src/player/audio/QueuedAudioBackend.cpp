#include "libreshockwave/player/audio/QueuedAudioBackend.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <limits>
#include <utility>

namespace libreshockwave::player::audio {

QueuedAudioBackend::QueuedAudioBackend() {
    volumes_.fill(0);
    for (int channel = 1; channel <= MAX_CHANNELS; ++channel) {
        volumes_[static_cast<std::size_t>(channel)] = 255;
    }
    timeProvider_ = defaultNowMs;
}

void QueuedAudioBackend::play(int channelNum,
                              const std::vector<std::uint8_t>& audioData,
                              std::string_view format,
                              int loopCount) {
    if (!isValidChannel(channelNum)) {
        return;
    }

    SoundCommand command;
    command.action = "play";
    command.channelNum = channelNum;
    command.audioData = audioData;
    command.format = std::string(format);
    command.loopCount = loopCount;
    command.volume = volumes_[static_cast<std::size_t>(channelNum)];
    pendingCommands_.push_back(std::move(command));
    resetElapsed(channelNum);
    playing_[static_cast<std::size_t>(channelNum)] = true;
}

void QueuedAudioBackend::stop(int channelNum) {
    if (!isValidChannel(channelNum)) {
        return;
    }

    SoundCommand command;
    command.action = "stop";
    command.channelNum = channelNum;
    pendingCommands_.push_back(std::move(command));
    stopElapsed(channelNum);
}

void QueuedAudioBackend::stopAll() {
    for (int channel = 1; channel <= MAX_CHANNELS; ++channel) {
        stop(channel);
    }
}

void QueuedAudioBackend::setVolume(int channelNum, int volume) {
    if (!isValidChannel(channelNum)) {
        return;
    }

    const int clamped = std::clamp(volume, 0, 255);
    volumes_[static_cast<std::size_t>(channelNum)] = clamped;

    SoundCommand command;
    command.action = "volume";
    command.channelNum = channelNum;
    command.volume = clamped;
    pendingCommands_.push_back(std::move(command));
}

bool QueuedAudioBackend::isPlaying(int channelNum) const {
    return isValidChannel(channelNum) && playing_[static_cast<std::size_t>(channelNum)];
}

int QueuedAudioBackend::getElapsedTime(int channelNum) const {
    if (!isValidChannel(channelNum)) {
        return 0;
    }

    const auto index = static_cast<std::size_t>(channelNum);
    std::int64_t elapsed = elapsedMs_[index];
    if (playing_[index]) {
        elapsed += std::max<std::int64_t>(0, timeProvider_() - startedAtMs_[index]);
    }
    if (elapsed > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(elapsed);
}

int QueuedAudioBackend::pendingCount() const {
    return static_cast<int>(pendingCommands_.size());
}

const QueuedAudioBackend::SoundCommand* QueuedAudioBackend::getPending(int index) const {
    if (index < 0 || index >= pendingCount()) {
        return nullptr;
    }
    return &pendingCommands_[static_cast<std::size_t>(index)];
}

const std::vector<QueuedAudioBackend::SoundCommand>& QueuedAudioBackend::pendingCommands() const {
    return pendingCommands_;
}

void QueuedAudioBackend::drainPending() {
    pendingCommands_.clear();
}

void QueuedAudioBackend::notifyStopped(int channelNum) {
    if (isValidChannel(channelNum)) {
        stopElapsed(channelNum);
    }
}

int QueuedAudioBackend::volume(int channelNum) const {
    return isValidChannel(channelNum) ? volumes_[static_cast<std::size_t>(channelNum)] : 0;
}

bool QueuedAudioBackend::isValidChannel(int channelNum) {
    return channelNum >= 1 && channelNum <= MAX_CHANNELS;
}

void QueuedAudioBackend::setTimeProvider(TimeProvider provider) {
    timeProvider_ = provider ? std::move(provider) : TimeProvider(defaultNowMs);
}

std::int64_t QueuedAudioBackend::defaultNowMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

void QueuedAudioBackend::resetElapsed(int channelNum) {
    if (!isValidChannel(channelNum)) {
        return;
    }
    const auto index = static_cast<std::size_t>(channelNum);
    elapsedMs_[index] = 0;
    startedAtMs_[index] = timeProvider_();
}

void QueuedAudioBackend::stopElapsed(int channelNum) {
    if (!isValidChannel(channelNum)) {
        return;
    }
    const auto index = static_cast<std::size_t>(channelNum);
    if (playing_[index]) {
        elapsedMs_[index] += std::max<std::int64_t>(0, timeProvider_() - startedAtMs_[index]);
    }
    playing_[index] = false;
    startedAtMs_[index] = 0;
}

} // namespace libreshockwave::player::audio
