#include "libreshockwave/audio/SoundConverter.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

#include "libreshockwave/chunks/SoundChunk.hpp"

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
    int endOffset = static_cast<int>(data.size());
    while (endOffset > 0 && (data[static_cast<std::size_t>(endOffset - 1)] & 0xFF) == 0xFF) {
        --endOffset;
    }
    return endOffset;
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

std::vector<std::uint8_t> buildWav(const std::vector<std::uint8_t>& audioData,
                                   int sampleRate,
                                   int bitsPerSample,
                                   int channelCount) {
    const int bytesPerSample = bitsPerSample / 8;
    const int byteRate = sampleRate * channelCount * bytesPerSample;
    const int blockAlign = channelCount * bytesPerSample;

    std::vector<std::uint8_t> wav;
    wav.reserve(44 + audioData.size());
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'});
    appendU32LE(wav, static_cast<std::uint32_t>(36 + audioData.size()));
    wav.insert(wav.end(), {'W', 'A', 'V', 'E'});
    wav.insert(wav.end(), {'f', 'm', 't', ' '});
    appendU32LE(wav, 16);
    appendU16LE(wav, 1);
    appendU16LE(wav, channelCount);
    appendU32LE(wav, static_cast<std::uint32_t>(sampleRate));
    appendU32LE(wav, static_cast<std::uint32_t>(byteRate));
    appendU16LE(wav, blockAlign);
    appendU16LE(wav, bitsPerSample);
    wav.insert(wav.end(), {'d', 'a', 't', 'a'});
    appendU32LE(wav, static_cast<std::uint32_t>(audioData.size()));
    wav.insert(wav.end(), audioData.begin(), audioData.end());
    return wav;
}

std::vector<std::uint8_t> createEmptyWav(int sampleRate, int bitsPerSample, int channelCount) {
    return buildWav({}, sampleRate, bitsPerSample, channelCount);
}

int calculateMp3FrameSize(int version, int layer, int bitrateIndex, int sampleRateIndex, int padding) {
    static constexpr std::array<std::array<int, 16>, 2> kBitrates{{
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}
    }};
    static constexpr std::array<std::array<int, 3>, 4> kSampleRates{{
        {11025, 12000, 8000},
        {0, 0, 0},
        {22050, 24000, 16000},
        {44100, 48000, 32000}
    }};

    (void)layer;
    const int bitrate = kBitrates[version == 3 ? 1 : 0][static_cast<std::size_t>(bitrateIndex)];
    const int sampleRate = kSampleRates[static_cast<std::size_t>(version)][static_cast<std::size_t>(sampleRateIndex)];
    if (bitrate == 0 || sampleRate == 0) {
        return -1;
    }
    return (144 * bitrate * 1000 / sampleRate) + padding;
}

bool validateMp3Sequence(const std::vector<std::uint8_t>& data, int offset) {
    int validFrames = 0;
    int position = offset;

    while (position < static_cast<int>(data.size()) - 4 && validFrames < 3) {
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
        const int bitrateIndex = (header >> 12) & 0xF;
        const int sampleRateIndex = (header >> 10) & 3;
        const int padding = (header >> 9) & 1;

        if (version == 1 || layer == 0 || bitrateIndex == 0 || bitrateIndex == 15 || sampleRateIndex == 3) {
            break;
        }

        const int frameSize = calculateMp3FrameSize(version, layer, bitrateIndex, sampleRateIndex, padding);
        if (frameSize < 0 || frameSize > static_cast<int>(data.size()) - position) {
            break;
        }

        ++validFrames;
        position += frameSize;
    }

    return validFrames >= 2;
}

int findMp3End(const std::vector<std::uint8_t>& data, int startOffset) {
    int position = startOffset;
    int lastValidFrameEnd = startOffset;
    int frameCount = 0;

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
        const int bitrateIndex = (header >> 12) & 0xF;
        const int sampleRateIndex = (header >> 10) & 3;
        const int padding = (header >> 9) & 1;

        if (version == 1 || layer == 0 || bitrateIndex == 0 || bitrateIndex == 15 || sampleRateIndex == 3) {
            break;
        }

        const int frameSize = calculateMp3FrameSize(version, layer, bitrateIndex, sampleRateIndex, padding);
        if (frameSize <= 0 || position + frameSize > static_cast<int>(data.size())) {
            break;
        }

        position += frameSize;
        lastValidFrameEnd = position;
        ++frameCount;
        if (frameCount > 100000) {
            break;
        }
    }

    return lastValidFrameEnd;
}

} // namespace

std::vector<std::uint8_t> SoundConverter::toWav(const chunks::SoundChunk& sound) {
    const auto& fullData = sound.audioData();
    const int headerSize = detectHeaderSize(fullData);
    const int endOffset = stripTrailingPadding(fullData);

    if (endOffset <= headerSize) {
        return createEmptyWav(sound.sampleRate(), sound.bitsPerSample(), sound.channelCount());
    }

    std::vector<std::uint8_t> pcmData(
        fullData.begin() + headerSize,
        fullData.begin() + endOffset);
    return toWav(pcmData, sound.sampleRate(), sound.bitsPerSample(), sound.channelCount(), true);
}

std::optional<std::vector<std::uint8_t>> SoundConverter::extractMp3(const chunks::SoundChunk& sound) {
    if (!sound.isMp3()) {
        return std::nullopt;
    }

    const auto& fullData = sound.audioData();
    const int mp3Start = findMp3Start(fullData);
    if (mp3Start < 0) {
        return std::nullopt;
    }

    int mp3End = findMp3End(fullData, mp3Start);
    if (mp3End <= mp3Start) {
        mp3End = static_cast<int>(fullData.size());
    }

    while (mp3End > mp3Start && (fullData[static_cast<std::size_t>(mp3End - 1)] & 0xFF) == 0xFF) {
        --mp3End;
    }

    if (mp3End <= mp3Start) {
        return std::nullopt;
    }

    return std::vector<std::uint8_t>(
        fullData.begin() + mp3Start,
        fullData.begin() + mp3End);
}

std::vector<std::uint8_t> SoundConverter::toWav(const std::vector<std::uint8_t>& audioData,
                                                int sampleRate,
                                                int bitsPerSample,
                                                int channelCount,
                                                bool bigEndian) {
    if (audioData.empty()) {
        return createEmptyWav(sampleRate, bitsPerSample, channelCount);
    }

    std::vector<std::uint8_t> wavAudioData;
    if (bitsPerSample == 16 && bigEndian) {
        wavAudioData = swapEndianness16(audioData);
    } else if (bitsPerSample == 8) {
        wavAudioData = convertSignedToUnsigned8(audioData);
    } else {
        wavAudioData = audioData;
    }

    return buildWav(wavAudioData, sampleRate, bitsPerSample, channelCount);
}

std::vector<std::uint8_t> SoundConverter::toWav(const std::vector<std::uint8_t>& audioData,
                                                int sampleRate,
                                                int bitsPerSample,
                                                int channelCount) {
    return toWav(audioData, sampleRate, bitsPerSample, channelCount, true);
}

std::vector<std::uint8_t> SoundConverter::decodeImaAdpcm(const std::vector<std::uint8_t>& adpcmData,
                                                         int initialPredictor,
                                                         int initialIndex) {
    if (adpcmData.empty()) {
        return {};
    }

    static constexpr std::array<int, 89> kStepTable{
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
    static constexpr std::array<int, 16> kIndexTable{
        -1, -1, -1, -1, 2, 4, 6, 8,
        -1, -1, -1, -1, 2, 4, 6, 8
    };

    int predictor = initialPredictor;
    int stepIndex = std::clamp(initialIndex, 0, 88);
    std::vector<std::uint8_t> result;
    result.reserve(adpcmData.size() * 4);

    for (const auto byte : adpcmData) {
        for (int nibbleIndex = 0; nibbleIndex < 2; ++nibbleIndex) {
            const int nibble = nibbleIndex == 0 ? (byte & 0x0F) : ((byte >> 4) & 0x0F);
            const int step = kStepTable[static_cast<std::size_t>(stepIndex)];

            int diff = step >> 3;
            if ((nibble & 1) != 0) diff += step >> 2;
            if ((nibble & 2) != 0) diff += step >> 1;
            if ((nibble & 4) != 0) diff += step;
            if ((nibble & 8) != 0) diff = -diff;

            predictor = std::clamp(predictor + diff, -32768, 32767);
            const auto sample = static_cast<std::int16_t>(predictor);
            result.push_back(static_cast<std::uint8_t>(sample & 0xFF));
            result.push_back(static_cast<std::uint8_t>((sample >> 8) & 0xFF));

            stepIndex = std::clamp(stepIndex + kIndexTable[static_cast<std::size_t>(nibble)], 0, 88);
        }
    }

    return result;
}

std::vector<std::uint8_t> SoundConverter::imaAdpcmToWav(const std::vector<std::uint8_t>& adpcmData,
                                                        int sampleRate,
                                                        int channelCount,
                                                        int initialPredictor,
                                                        int initialIndex) {
    return buildWav(decodeImaAdpcm(adpcmData, initialPredictor, initialIndex), sampleRate, 16, channelCount);
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

double SoundConverter::getDuration(int dataLength, int sampleRate, int bitsPerSample, int channelCount) {
    if (sampleRate == 0 || bitsPerSample == 0 || channelCount == 0) {
        return 0.0;
    }
    const int bytesPerSample = bitsPerSample / 8;
    const int sampleCount = dataLength / (bytesPerSample * channelCount);
    return static_cast<double>(sampleCount) / static_cast<double>(sampleRate);
}

} // namespace libreshockwave::audio
