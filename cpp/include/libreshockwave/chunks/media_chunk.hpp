#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "libreshockwave/chunks/chunk.hpp"
#include "libreshockwave/chunks/sound_chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class MediaChunk final : public Chunk {
public:
    MediaChunk(const moviesource::MovieSource* file,
               id::ChunkId id,
               int sample_rate,
               int data_size_field,
               std::optional<std::vector<std::uint8_t>> guid,
               std::vector<std::uint8_t> audio_data,
               std::string codec);

    [[nodiscard]] const moviesource::MovieSource* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] int sampleRate() const;
    [[nodiscard]] int dataSizeField() const;
    [[nodiscard]] const std::optional<std::vector<std::uint8_t>>& guid() const;
    [[nodiscard]] const std::vector<std::uint8_t>& audioData() const;
    [[nodiscard]] const std::string& codec() const;
    [[nodiscard]] bool isMp3() const;
    [[nodiscard]] bool isAdpcm() const;
    [[nodiscard]] SoundChunk toSoundChunk() const;

    [[nodiscard]] static MediaChunk read(const moviesource::MovieSource* file, io::BinaryReader& reader, id::ChunkId id);

private:
    [[nodiscard]] static std::string detectCodec(const std::vector<std::uint8_t>& data,
                                                 const std::optional<std::vector<std::uint8_t>>& guid,
                                                 int data_size_field);

    const moviesource::MovieSource* file_;
    id::ChunkId id_;
    int sample_rate_;
    int data_size_field_;
    std::optional<std::vector<std::uint8_t>> guid_;
    std::vector<std::uint8_t> audio_data_;
    std::string codec_;
};

} // namespace libreshockwave::chunks
