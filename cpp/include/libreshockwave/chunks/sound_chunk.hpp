#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "libreshockwave/chunks/chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class SoundChunk final : public Chunk {
public:
    SoundChunk(const moviesource::MovieSource* file,
               id::ChunkId id,
               int sample_rate,
               int sample_count,
               int bits_per_sample,
               int channel_count,
               std::vector<std::uint8_t> audio_data,
               std::string codec = "raw_pcm");

    [[nodiscard]] const moviesource::MovieSource* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] int sampleRate() const;
    [[nodiscard]] int sampleCount() const;
    [[nodiscard]] int bitsPerSample() const;
    [[nodiscard]] int channelCount() const;
    [[nodiscard]] const std::vector<std::uint8_t>& audioData() const;
    [[nodiscard]] const std::string& codec() const;
    [[nodiscard]] bool isMp3() const;
    [[nodiscard]] bool isAdpcm() const;
    [[nodiscard]] double durationSeconds() const;

    [[nodiscard]] static SoundChunk read(const moviesource::MovieSource* file, io::BinaryReader& reader, id::ChunkId id);

private:
    [[nodiscard]] static std::string detectCodec(const std::vector<std::uint8_t>& data);

    const moviesource::MovieSource* file_;
    id::ChunkId id_;
    int sample_rate_;
    int sample_count_;
    int bits_per_sample_;
    int channel_count_;
    std::vector<std::uint8_t> audio_data_;
    std::string codec_;
};

} // namespace libreshockwave::chunks
