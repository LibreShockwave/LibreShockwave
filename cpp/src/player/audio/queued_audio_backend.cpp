#include "libreshockwave/player/audio/queued_audio_backend.hpp"

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
    time_provider_ = defaultNowMs;
}

void QueuedAudioBackend::play(int channel_num,
                              const std::vector<std::uint8_t>& audio_data,
                              std::string_view format,
                              int loop_count) {
    if (!isValidChannel(channel_num)) {
        return;
    }

    SoundCommand command;
    command.action = "play";
    command.channel_num = channel_num;
    command.audio_data = audio_data;
    command.format = std::string(format);
    command.loop_count = loop_count;
    command.volume = volumes_[static_cast<std::size_t>(channel_num)];
    pending_commands_.push_back(std::move(command));
    resetElapsed(channel_num);
    playing_[static_cast<std::size_t>(channel_num)] = true;
}

void QueuedAudioBackend::stop(int channel_num) {
    if (!isValidChannel(channel_num)) {
        return;
    }

    SoundCommand command;
    command.action = "stop";
    command.channel_num = channel_num;
    pending_commands_.push_back(std::move(command));
    stopElapsed(channel_num);
}

void QueuedAudioBackend::stopAll() {
    for (int channel = 1; channel <= MAX_CHANNELS; ++channel) {
        stop(channel);
    }
}

void QueuedAudioBackend::setVolume(int channel_num, int volume) {
    if (!isValidChannel(channel_num)) {
        return;
    }

    const int clamped = std::clamp(volume, 0, 255);
    volumes_[static_cast<std::size_t>(channel_num)] = clamped;

    SoundCommand command;
    command.action = "volume";
    command.channel_num = channel_num;
    command.volume = clamped;
    pending_commands_.push_back(std::move(command));
}

bool QueuedAudioBackend::isPlaying(int channel_num) const {
    return isValidChannel(channel_num) && playing_[static_cast<std::size_t>(channel_num)];
}

int QueuedAudioBackend::getElapsedTime(int channel_num) const {
    if (!isValidChannel(channel_num)) {
        return 0;
    }

    const auto index = static_cast<std::size_t>(channel_num);
    std::int64_t elapsed = elapsed_ms_[index];
    if (playing_[index]) {
        elapsed += std::max<std::int64_t>(0, time_provider_() - started_at_ms_[index]);
    }
    if (elapsed > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(elapsed);
}

int QueuedAudioBackend::pendingCount() const {
    return static_cast<int>(pending_commands_.size());
}

const QueuedAudioBackend::SoundCommand* QueuedAudioBackend::getPending(int index) const {
    if (index < 0 || index >= pendingCount()) {
        return nullptr;
    }
    return &pending_commands_[static_cast<std::size_t>(index)];
}

const std::vector<QueuedAudioBackend::SoundCommand>& QueuedAudioBackend::pendingCommands() const {
    return pending_commands_;
}

void QueuedAudioBackend::drainPending() {
    pending_commands_.clear();
}

void QueuedAudioBackend::notifyStopped(int channel_num) {
    if (isValidChannel(channel_num)) {
        stopElapsed(channel_num);
    }
}

int QueuedAudioBackend::volume(int channel_num) const {
    return isValidChannel(channel_num) ? volumes_[static_cast<std::size_t>(channel_num)] : 0;
}

bool QueuedAudioBackend::isValidChannel(int channel_num) {
    return channel_num >= 1 && channel_num <= MAX_CHANNELS;
}

void QueuedAudioBackend::setTimeProvider(TimeProvider provider) {
    time_provider_ = provider ? std::move(provider) : TimeProvider(defaultNowMs);
}

std::int64_t QueuedAudioBackend::defaultNowMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

void QueuedAudioBackend::resetElapsed(int channel_num) {
    if (!isValidChannel(channel_num)) {
        return;
    }
    const auto index = static_cast<std::size_t>(channel_num);
    elapsed_ms_[index] = 0;
    started_at_ms_[index] = time_provider_();
}

void QueuedAudioBackend::stopElapsed(int channel_num) {
    if (!isValidChannel(channel_num)) {
        return;
    }
    const auto index = static_cast<std::size_t>(channel_num);
    if (playing_[index]) {
        elapsed_ms_[index] += std::max<std::int64_t>(0, time_provider_() - started_at_ms_[index]);
    }
    playing_[index] = false;
    started_at_ms_[index] = 0;
}

} // namespace libreshockwave::player::audio
