#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace libreshockwave::audio {

// Decodes a RIFF/WAVE file into the sample parameters the runtime uses. The
// decoder supports uncompressed PCM (format 1) and IEEE float (format 3). Other
// codecs are rejected so the caller can keep the raw payload.
//
// The load path is in-memory, so the decoder works in native and WASM builds.
class WavDecoder {
public:
    struct WavData {
        int sample_rate = 0;
        int channel_count = 0;
        int bits_per_sample = 0;
        int sample_count = 0;
        int format = 1;
        // Interleaved little-endian PCM samples.
        std::vector<std::uint8_t> pcm_data;
    };

    // Decode a WAV payload. Returns std::nullopt on a malformed or unsupported
    // file.
    [[nodiscard]] static std::optional<WavData> decode(const std::vector<std::uint8_t>& data);
};

} // namespace libreshockwave::audio
