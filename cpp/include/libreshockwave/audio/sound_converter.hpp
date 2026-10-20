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
    [[nodiscard]] static std::vector<std::uint8_t> toWav(const std::vector<std::uint8_t>& audio_data,
                                                         int sample_rate,
                                                         int bits_per_sample,
                                                         int channel_count,
                                                         bool big_endian);
    [[nodiscard]] static std::vector<std::uint8_t> toWav(const std::vector<std::uint8_t>& audio_data,
                                                         int sample_rate,
                                                         int bits_per_sample,
                                                         int channel_count);
    [[nodiscard]] static std::vector<std::uint8_t> decodeImaAdpcm(const std::vector<std::uint8_t>& adpcm_data,
                                                                  int initial_predictor,
                                                                  int initial_index);
    [[nodiscard]] static std::vector<std::uint8_t> imaAdpcmToWav(const std::vector<std::uint8_t>& adpcm_data,
                                                                 int sample_rate,
                                                                 int channel_count,
                                                                 int initial_predictor,
                                                                 int initial_index);
    [[nodiscard]] static bool isMp3(const std::vector<std::uint8_t>& data);
    [[nodiscard]] static int findMp3Start(const std::vector<std::uint8_t>& data);
    [[nodiscard]] static double getDuration(const chunks::SoundChunk& sound);
    [[nodiscard]] static double getDuration(int data_length, int sample_rate, int bits_per_sample, int channel_count);
};

} // namespace libreshockwave::audio
