#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace libreshockwave::chunks {
class SoundChunk;
}

namespace libreshockwave::audio {

class SoundConverter {
public:
    [[nodiscard]] static std::vector<std::uint8_t> toWav(const chunks::SoundChunk& sound);
    [[nodiscard]] static std::optional<std::vector<std::uint8_t>> extractMp3(const chunks::SoundChunk& sound);
    [[nodiscard]] static std::vector<std::uint8_t> toWav(const std::vector<std::uint8_t>& audioData,
                                                         int sampleRate,
                                                         int bitsPerSample,
                                                         int channelCount,
                                                         bool bigEndian);
    [[nodiscard]] static std::vector<std::uint8_t> toWav(const std::vector<std::uint8_t>& audioData,
                                                         int sampleRate,
                                                         int bitsPerSample,
                                                         int channelCount);
    [[nodiscard]] static std::vector<std::uint8_t> decodeImaAdpcm(const std::vector<std::uint8_t>& adpcmData,
                                                                  int initialPredictor,
                                                                  int initialIndex);
    [[nodiscard]] static std::vector<std::uint8_t> imaAdpcmToWav(const std::vector<std::uint8_t>& adpcmData,
                                                                 int sampleRate,
                                                                 int channelCount,
                                                                 int initialPredictor,
                                                                 int initialIndex);
    [[nodiscard]] static bool isMp3(const std::vector<std::uint8_t>& data);
    [[nodiscard]] static int findMp3Start(const std::vector<std::uint8_t>& data);
    [[nodiscard]] static double getDuration(const chunks::SoundChunk& sound);
    [[nodiscard]] static double getDuration(int dataLength, int sampleRate, int bitsPerSample, int channelCount);
};

} // namespace libreshockwave::audio
