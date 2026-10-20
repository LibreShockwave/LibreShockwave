#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace libreshockwave::player::audio {

class AudioBackend {
public:
    virtual ~AudioBackend() = default;

    virtual void play(int channel_num,
                      const std::vector<std::uint8_t>& audio_data,
                      std::string_view format,
                      int loop_count) = 0;
    virtual void stop(int channel_num) = 0;
    virtual void stopAll() = 0;
    virtual void setVolume(int channel_num, int volume) = 0;
    [[nodiscard]] virtual bool isPlaying(int channel_num) const = 0;
    [[nodiscard]] virtual int getElapsedTime(int channel_num) const = 0;
};

} // namespace libreshockwave::player::audio
