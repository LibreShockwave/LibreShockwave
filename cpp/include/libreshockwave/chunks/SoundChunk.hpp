#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "libreshockwave/chunks/Chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class SoundChunk final : public Chunk {
public:
    SoundChunk(const DirectorFile* file,
               id::ChunkId id,
               int sampleRate,
               int sampleCount,
               int bitsPerSample,
               int channelCount,
               std::vector<std::uint8_t> audioData,
               std::string codec = "raw_pcm");

    [[nodiscard]] const DirectorFile* file() const override;
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

    [[nodiscard]] static SoundChunk read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id);

private:
    [[nodiscard]] static std::string detectCodec(const std::vector<std::uint8_t>& data);

    const DirectorFile* file_;
    id::ChunkId id_;
    int sampleRate_;
    int sampleCount_;
    int bitsPerSample_;
    int channelCount_;
    std::vector<std::uint8_t> audioData_;
    std::string codec_;
};

} // namespace libreshockwave::chunks
