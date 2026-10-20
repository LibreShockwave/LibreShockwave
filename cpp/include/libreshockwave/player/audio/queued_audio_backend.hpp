#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/player/audio/audio_backend.hpp"

namespace libreshockwave::player::audio {

class QueuedAudioBackend final : public AudioBackend {
public:
    static constexpr int MAX_CHANNELS = 8;
    using TimeProvider = std::function<std::int64_t()>;

    struct SoundCommand {
        std::string action;
        int channel_num{0};
        std::optional<std::vector<std::uint8_t>> audio_data;
        std::optional<std::string> format;
        int loop_count{0};
        int volume{0};
    };

    QueuedAudioBackend();

    void play(int channel_num,
              const std::vector<std::uint8_t>& audio_data,
              std::string_view format,
              int loop_count) override;
    void stop(int channel_num) override;
    void stopAll() override;
    void setVolume(int channel_num, int volume) override;
    [[nodiscard]] bool isPlaying(int channel_num) const override;
    [[nodiscard]] int getElapsedTime(int channel_num) const override;

    [[nodiscard]] int pendingCount() const;
    [[nodiscard]] const SoundCommand* getPending(int index) const;
    [[nodiscard]] const std::vector<SoundCommand>& pendingCommands() const;
    void drainPending();
    void notifyStopped(int channel_num);
    [[nodiscard]] int volume(int channel_num) const;
    void setTimeProvider(TimeProvider provider);

private:
    [[nodiscard]] static bool isValidChannel(int channel_num);
    [[nodiscard]] static std::int64_t defaultNowMs();
    void resetElapsed(int channel_num);
    void stopElapsed(int channel_num);

    std::vector<SoundCommand> pending_commands_;
    std::array<bool, MAX_CHANNELS + 1> playing_{};
    std::array<int, MAX_CHANNELS + 1> volumes_{};
    std::array<std::int64_t, MAX_CHANNELS + 1> started_at_ms_{};
    std::array<std::int64_t, MAX_CHANNELS + 1> elapsed_ms_{};
    TimeProvider time_provider_;
};

} // namespace libreshockwave::player::audio
