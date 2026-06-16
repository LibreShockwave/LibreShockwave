#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/player/audio/AudioBackend.hpp"

namespace libreshockwave::player::audio {

class QueuedAudioBackend final : public AudioBackend {
public:
    static constexpr int MAX_CHANNELS = 8;
    using TimeProvider = std::function<std::int64_t()>;

    struct SoundCommand {
        std::string action;
        int channelNum{0};
        std::optional<std::vector<std::uint8_t>> audioData;
        std::optional<std::string> format;
        int loopCount{0};
        int volume{0};
    };

    QueuedAudioBackend();

    void play(int channelNum,
              const std::vector<std::uint8_t>& audioData,
              std::string_view format,
              int loopCount) override;
    void stop(int channelNum) override;
    void stopAll() override;
    void setVolume(int channelNum, int volume) override;
    [[nodiscard]] bool isPlaying(int channelNum) const override;
    [[nodiscard]] int getElapsedTime(int channelNum) const override;

    [[nodiscard]] int pendingCount() const;
    [[nodiscard]] const SoundCommand* getPending(int index) const;
    [[nodiscard]] const std::vector<SoundCommand>& pendingCommands() const;
    void drainPending();
    void notifyStopped(int channelNum);
    [[nodiscard]] int volume(int channelNum) const;
    void setTimeProvider(TimeProvider provider);

private:
    [[nodiscard]] static bool isValidChannel(int channelNum);
    [[nodiscard]] static std::int64_t defaultNowMs();
    void resetElapsed(int channelNum);
    void stopElapsed(int channelNum);

    std::vector<SoundCommand> pendingCommands_;
    std::array<bool, MAX_CHANNELS + 1> playing_{};
    std::array<int, MAX_CHANNELS + 1> volumes_{};
    std::array<std::int64_t, MAX_CHANNELS + 1> startedAtMs_{};
    std::array<std::int64_t, MAX_CHANNELS + 1> elapsedMs_{};
    TimeProvider timeProvider_;
};

} // namespace libreshockwave::player::audio
