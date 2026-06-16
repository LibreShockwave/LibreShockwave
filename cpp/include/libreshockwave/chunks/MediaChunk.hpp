#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "libreshockwave/chunks/Chunk.hpp"
#include "libreshockwave/chunks/SoundChunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class MediaChunk final : public Chunk {
public:
    MediaChunk(const DirectorFile* file,
               id::ChunkId id,
               int sampleRate,
               int dataSizeField,
               std::optional<std::vector<std::uint8_t>> guid,
               std::vector<std::uint8_t> audioData,
               std::string codec);

    [[nodiscard]] const DirectorFile* file() const override;
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

    [[nodiscard]] static MediaChunk read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id);

private:
    [[nodiscard]] static std::string detectCodec(const std::vector<std::uint8_t>& data,
                                                 const std::optional<std::vector<std::uint8_t>>& guid,
                                                 int dataSizeField);

    const DirectorFile* file_;
    id::ChunkId id_;
    int sampleRate_;
    int dataSizeField_;
    std::optional<std::vector<std::uint8_t>> guid_;
    std::vector<std::uint8_t> audioData_;
    std::string codec_;
};

} // namespace libreshockwave::chunks
