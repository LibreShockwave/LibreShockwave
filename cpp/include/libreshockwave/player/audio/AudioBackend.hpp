#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace libreshockwave::player::audio {

class AudioBackend {
public:
    virtual ~AudioBackend() = default;

    virtual void play(int channelNum,
                      const std::vector<std::uint8_t>& audioData,
                      std::string_view format,
                      int loopCount) = 0;
    virtual void stop(int channelNum) = 0;
    virtual void stopAll() = 0;
    virtual void setVolume(int channelNum, int volume) = 0;
    [[nodiscard]] virtual bool isPlaying(int channelNum) const = 0;
    [[nodiscard]] virtual int getElapsedTime(int channelNum) const = 0;
};

} // namespace libreshockwave::player::audio
