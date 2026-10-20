#include "libreshockwave/audio/sound_converter.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

#include "libreshockwave/chunks/sound_chunk.hpp"

namespace libreshockwave::audio {
namespace {

void appendU16LE(std::vector<std::uint8_t>& data, int value) {
    const auto raw = static_cast<std::uint16_t>(value);
    data.push_back(static_cast<std::uint8_t>(raw & 0xFF));
    data.push_back(static_cast<std::uint8_t>((raw >> 8) & 0xFF));
}

void appendU32LE(std::vector<std::uint8_t>& data, std::uint32_t value) {
    data.push_back(static_cast<std::uint8_t>(value & 0xFF));
    data.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    data.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    data.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

int detectHeaderSize(const std::vector<std::uint8_t>& data) {
    if (data.size() >= 128) {
        for (int index = 64; index < 128 && index < static_cast<int>(data.size()); ++index) {
            const int value = data[static_cast<std::size_t>(index)] & 0xFF;
            if (value >= 0x70 && value <= 0x90) {
                return 64;
            }
        }
        for (int index = 96; index < 128 && index < static_cast<int>(data.size()); ++index) {
            const int value = data[static_cast<std::size_t>(index)] & 0xFF;
            if (value >= 0x70 && value <= 0x90) {
                return 96;
            }
        }
        return 128;
    }
    if (data.size() >= 64) {
        return 64;
    }
    return 0;
}

int stripTrailingPadding(const std::vector<std::uint8_t>& data) {
    int end_offset = static_cast<int>(data.size());
    while (end_offset > 0 && (data[static_cast<std::size_t>(end_offset - 1)] & 0xFF) == 0xFF) {
        --end_offset;
    }
    return end_offset;
}

std::vector<std::uint8_t> swapEndianness16(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> result(data.size(), 0);
    for (std::size_t index = 0; index + 1 < data.size(); index += 2) {
        result[index] = data[index + 1];
        result[index + 1] = data[index];
    }
    return result;
}

std::vector<std::uint8_t> convertSignedToUnsigned8(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> result;
    result.reserve(data.size());
    for (const auto value : data) {
        result.push_back(static_cast<std::uint8_t>(value ^ 0x80U));
    }
    return result;
}

std::vector<std::uint8_t> buildWav(const std::vector<std::uint8_t>& audio_data,
                                   int sample_rate,
                                   int bits_per_sample,
                                   int channel_count) {
    const int bytes_per_sample = bits_per_sample / 8;
    const int byte_rate = sample_rate * channel_count * bytes_per_sample;
    const int block_align = channel_count * bytes_per_sample;

    std::vector<std::uint8_t> wav;
    wav.reserve(44 + audio_data.size());
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'});
    appendU32LE(wav, static_cast<std::uint32_t>(36 + audio_data.size()));
    wav.insert(wav.end(), {'W', 'A', 'V', 'E'});
    wav.insert(wav.end(), {'f', 'm', 't', ' '});
    appendU32LE(wav, 16);
    appendU16LE(wav, 1);
    appendU16LE(wav, channel_count);
    appendU32LE(wav, static_cast<std::uint32_t>(sample_rate));
    appendU32LE(wav, static_cast<std::uint32_t>(byte_rate));
    appendU16LE(wav, block_align);
    appendU16LE(wav, bits_per_sample);
    wav.insert(wav.end(), {'d', 'a', 't', 'a'});
    appendU32LE(wav, static_cast<std::uint32_t>(audio_data.size()));
    wav.insert(wav.end(), audio_data.begin(), audio_data.end());
    return wav;
}

std::vector<std::uint8_t> createEmptyWav(int sample_rate, int bits_per_sample, int channel_count) {
    return buildWav({}, sample_rate, bits_per_sample, channel_count);
}

int calculateMp3FrameSize(int version, int layer, int bitrate_index, int sample_rate_index, int padding) {
    static constexpr std::array<std::array<int, 16>, 2> k_bitrates{{
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}
    }};
    static constexpr std::array<std::array<int, 3>, 4> k_sample_rates{{
        {11025, 12000, 8000},
        {0, 0, 0},
        {22050, 24000, 16000},
        {44100, 48000, 32000}
    }};

    (void)layer;
    const int bitrate = k_bitrates[version == 3 ? 1 : 0][static_cast<std::size_t>(bitrate_index)];
    const int sample_rate = k_sample_rates[static_cast<std::size_t>(version)][static_cast<std::size_t>(sample_rate_index)];
    if (bitrate == 0 || sample_rate == 0) {
        return -1;
    }
    return (144 * bitrate * 1000 / sample_rate) + padding;
}

bool validateMp3Sequence(const std::vector<std::uint8_t>& data, int offset) {
    int valid_frames = 0;
    int position = offset;

    while (position < static_cast<int>(data.size()) - 4 && valid_frames < 3) {
        if ((data[static_cast<std::size_t>(position)] & 0xFF) != 0xFF ||
            (data[static_cast<std::size_t>(position + 1)] & 0xE0U) != 0xE0U) {
            break;
        }

        const int header =
            (static_cast<int>(data[static_cast<std::size_t>(position)] & 0xFF) << 24) |
            (static_cast<int>(data[static_cast<std::size_t>(position + 1)] & 0xFF) << 16) |
            (static_cast<int>(data[static_cast<std::size_t>(position + 2)] & 0xFF) << 8) |
            static_cast<int>(data[static_cast<std::size_t>(position + 3)] & 0xFF);

        const int version = (header >> 19) & 3;
        const int layer = (header >> 17) & 3;
        const int bitrate_index = (header >> 12) & 0xF;
        const int sample_rate_index = (header >> 10) & 3;
        const int padding = (header >> 9) & 1;

        if (version == 1 || layer == 0 || bitrate_index == 0 || bitrate_index == 15 || sample_rate_index == 3) {
            break;
        }

        const int frame_size = calculateMp3FrameSize(version, layer, bitrate_index, sample_rate_index, padding);
        if (frame_size < 0 || frame_size > static_cast<int>(data.size()) - position) {
            break;
        }

        ++valid_frames;
        position += frame_size;
    }

    return valid_frames >= 2;
}

int findMp3End(const std::vector<std::uint8_t>& data, int start_offset) {
    int position = start_offset;
    int last_valid_frame_end = start_offset;
    int frame_count = 0;

    while (position < static_cast<int>(data.size()) - 4) {
        if ((data[static_cast<std::size_t>(position)] & 0xFF) != 0xFF ||
            (data[static_cast<std::size_t>(position + 1)] & 0xE0U) != 0xE0U) {
            break;
        }

        const int header =
            (static_cast<int>(data[static_cast<std::size_t>(position)] & 0xFF) << 24) |
            (static_cast<int>(data[static_cast<std::size_t>(position + 1)] & 0xFF) << 16) |
            (static_cast<int>(data[static_cast<std::size_t>(position + 2)] & 0xFF) << 8) |
            static_cast<int>(data[static_cast<std::size_t>(position + 3)] & 0xFF);

        const int version = (header >> 19) & 3;
        const int layer = (header >> 17) & 3;
        const int bitrate_index = (header >> 12) & 0xF;
        const int sample_rate_index = (header >> 10) & 3;
        const int padding = (header >> 9) & 1;

        if (version == 1 || layer == 0 || bitrate_index == 0 || bitrate_index == 15 || sample_rate_index == 3) {
            break;
        }

        const int frame_size = calculateMp3FrameSize(version, layer, bitrate_index, sample_rate_index, padding);
        if (frame_size <= 0 || position + frame_size > static_cast<int>(data.size())) {
            break;
        }

        position += frame_size;
        last_valid_frame_end = position;
        ++frame_count;
        if (frame_count > 100000) {
            break;
        }
    }

    return last_valid_frame_end;
}

} // namespace

std::vector<std::uint8_t> SoundConverter::toWav(const chunks::SoundChunk& sound) {
    const auto& full_data = sound.audioData();
    const int header_size = detectHeaderSize(full_data);
    const int end_offset = stripTrailingPadding(full_data);

    if (end_offset <= header_size) {
        return createEmptyWav(sound.sampleRate(), sound.bitsPerSample(), sound.channelCount());
    }

    std::vector<std::uint8_t> pcm_data(
        full_data.begin() + header_size,
        full_data.begin() + end_offset);
    return toWav(pcm_data, sound.sampleRate(), sound.bitsPerSample(), sound.channelCount(), true);
}

std::optional<std::vector<std::uint8_t>> SoundConverter::extractMp3(const chunks::SoundChunk& sound) {
    if (!sound.isMp3()) {
        return std::nullopt;
    }

    const auto& full_data = sound.audioData();
    const int mp3_start = findMp3Start(full_data);
    if (mp3_start < 0) {
        return std::nullopt;
    }

    int mp3_end = findMp3End(full_data, mp3_start);
    if (mp3_end <= mp3_start) {
        mp3_end = static_cast<int>(full_data.size());
    }

    while (mp3_end > mp3_start && (full_data[static_cast<std::size_t>(mp3_end - 1)] & 0xFF) == 0xFF) {
        --mp3_end;
    }

    if (mp3_end <= mp3_start) {
        return std::nullopt;
    }

    return std::vector<std::uint8_t>(
        full_data.begin() + mp3_start,
        full_data.begin() + mp3_end);
}

std::vector<std::uint8_t> SoundConverter::toWav(const std::vector<std::uint8_t>& audio_data,
                                                int sample_rate,
                                                int bits_per_sample,
                                                int channel_count,
                                                bool big_endian) {
    if (audio_data.empty()) {
        return createEmptyWav(sample_rate, bits_per_sample, channel_count);
    }

    std::vector<std::uint8_t> wav_audio_data;
    if (bits_per_sample == 16 && big_endian) {
        wav_audio_data = swapEndianness16(audio_data);
    } else if (bits_per_sample == 8) {
        wav_audio_data = convertSignedToUnsigned8(audio_data);
    } else {
        wav_audio_data = audio_data;
    }

    return buildWav(wav_audio_data, sample_rate, bits_per_sample, channel_count);
}

std::vector<std::uint8_t> SoundConverter::toWav(const std::vector<std::uint8_t>& audio_data,
                                                int sample_rate,
                                                int bits_per_sample,
                                                int channel_count) {
    return toWav(audio_data, sample_rate, bits_per_sample, channel_count, true);
}

std::vector<std::uint8_t> SoundConverter::decodeImaAdpcm(const std::vector<std::uint8_t>& adpcm_data,
                                                         int initial_predictor,
                                                         int initial_index) {
    if (adpcm_data.empty()) {
        return {};
    }

    static constexpr std::array<int, 89> k_step_table{
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
        19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
        50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
        130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
        337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
        2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
        5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
        15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
    };
    static constexpr std::array<int, 16> k_index_table{
        -1, -1, -1, -1, 2, 4, 6, 8,
        -1, -1, -1, -1, 2, 4, 6, 8
    };

    int predictor = initial_predictor;
    int step_index = std::clamp(initial_index, 0, 88);
    std::vector<std::uint8_t> result;
    result.reserve(adpcm_data.size() * 4);

    for (const auto byte : adpcm_data) {
        for (int nibble_index = 0; nibble_index < 2; ++nibble_index) {
            const int nibble = nibble_index == 0 ? (byte & 0x0F) : ((byte >> 4) & 0x0F);
            const int step = k_step_table[static_cast<std::size_t>(step_index)];

            int diff = step >> 3;
            if ((nibble & 1) != 0) diff += step >> 2;
            if ((nibble & 2) != 0) diff += step >> 1;
            if ((nibble & 4) != 0) diff += step;
            if ((nibble & 8) != 0) diff = -diff;

            predictor = std::clamp(predictor + diff, -32768, 32767);
            const auto sample = static_cast<std::int16_t>(predictor);
            result.push_back(static_cast<std::uint8_t>(sample & 0xFF));
            result.push_back(static_cast<std::uint8_t>((sample >> 8) & 0xFF));

            step_index = std::clamp(step_index + k_index_table[static_cast<std::size_t>(nibble)], 0, 88);
        }
    }

    return result;
}

std::vector<std::uint8_t> SoundConverter::imaAdpcmToWav(const std::vector<std::uint8_t>& adpcm_data,
                                                        int sample_rate,
                                                        int channel_count,
                                                        int initial_predictor,
                                                        int initial_index) {
    return buildWav(decodeImaAdpcm(adpcm_data, initial_predictor, initial_index), sample_rate, 16, channel_count);
}

bool SoundConverter::isMp3(const std::vector<std::uint8_t>& data) {
    return findMp3Start(data) >= 0;
}

int SoundConverter::findMp3Start(const std::vector<std::uint8_t>& data) {
    if (data.size() < 4) {
        return -1;
    }
    for (int index = 0; index < static_cast<int>(data.size()) - 1; ++index) {
        if ((data[static_cast<std::size_t>(index)] & 0xFF) == 0xFF &&
            (data[static_cast<std::size_t>(index + 1)] & 0xE0U) == 0xE0U &&
            validateMp3Sequence(data, index)) {
            return index;
        }
    }
    return -1;
}

double SoundConverter::getDuration(const chunks::SoundChunk& sound) {
    return sound.durationSeconds();
}

double SoundConverter::getDuration(int data_length, int sample_rate, int bits_per_sample, int channel_count) {
    if (sample_rate == 0 || bits_per_sample == 0 || channel_count == 0) {
        return 0.0;
    }
    const int bytes_per_sample = bits_per_sample / 8;
    const int sample_count = data_length / (bytes_per_sample * channel_count);
    return static_cast<double>(sample_count) / static_cast<double>(sample_rate);
}

} // namespace libreshockwave::audio
