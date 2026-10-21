#include "libreshockwave/audio/wav_decoder.hpp"

#include <algorithm>
#include <cstring>

namespace libreshockwave::audio {
namespace {

// RIFF/WAVE chunk tags and fixed sizes.
constexpr std::uint32_t kRiffTag = 0x46464952; // "RIFF" little-endian
constexpr std::uint32_t kWaveTag = 0x45564157; // "WAVE"
constexpr std::uint32_t kFmtTag = 0x20746D66;  // "fmt "
constexpr std::uint32_t kDataTag = 0x61746164; // "data"
constexpr std::size_t kRiffHeaderSize = 12;
constexpr std::size_t kChunkHeaderSize = 8;
constexpr std::size_t kFmtMinimumSize = 16;
constexpr int kPcmFormat = 1;
constexpr int kFloatFormat = 3;

std::uint16_t readU16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[offset + 1]) << 8U);
}

std::uint32_t readU32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
        (static_cast<std::uint32_t>(data[offset + 1]) << 8U) |
        (static_cast<std::uint32_t>(data[offset + 2]) << 16U) |
        (static_cast<std::uint32_t>(data[offset + 3]) << 24U);
}

bool rangeFits(std::size_t offset, std::size_t length, std::size_t total) {
    return offset <= total && length <= total - offset;
}

} // namespace

std::optional<WavDecoder::WavData> WavDecoder::decode(const std::vector<std::uint8_t>& data) {
    if (data.size() < kRiffHeaderSize || readU32(data, 0) != kRiffTag ||
        readU32(data, 8) != kWaveTag) {
        return std::nullopt;
    }
    WavData result;
    bool have_format = false;
    std::size_t offset = kRiffHeaderSize;
    while (rangeFits(offset, kChunkHeaderSize, data.size())) {
        const std::uint32_t tag = readU32(data, offset);
        const std::uint32_t size = readU32(data, offset + 4);
        const std::size_t payload = offset + kChunkHeaderSize;
        if (!rangeFits(payload, size, data.size())) {
            return std::nullopt;
        }
        if (tag == kFmtTag) {
            if (size < kFmtMinimumSize) {
                return std::nullopt;
            }
            result.format = readU16(data, payload);
            result.channel_count = readU16(data, payload + 2);
            result.sample_rate = static_cast<int>(readU32(data, payload + 4));
            result.bits_per_sample = readU16(data, payload + 14);
            if (result.format != kPcmFormat && result.format != kFloatFormat) {
                return std::nullopt;
            }
            if (result.channel_count <= 0 || result.bits_per_sample <= 0) {
                return std::nullopt;
            }
            have_format = true;
        } else if (tag == kDataTag) {
            if (!have_format) {
                return std::nullopt;
            }
            result.pcm_data.assign(data.begin() + static_cast<std::ptrdiff_t>(payload),
                                   data.begin() + static_cast<std::ptrdiff_t>(payload + size));
            const int bytes_per_sample = result.bits_per_sample / 8;
            if (bytes_per_sample > 0) {
                result.sample_count =
                    static_cast<int>(result.pcm_data.size()) / bytes_per_sample;
            }
            return result;
        }
        // Chunks are word aligned.
        offset = payload + size + (size % 2U);
    }
    return std::nullopt;
}

} // namespace libreshockwave::audio
