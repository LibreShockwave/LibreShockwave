#pragma once

#include <cstdint>
#include <vector>

#include "libreshockwave/chunks/Chunk.hpp"

namespace libreshockwave::chunks {

class RawChunk final : public Chunk {
public:
    RawChunk(const DirectorFile* file, id::ChunkId id, format::ChunkType type, std::vector<std::uint8_t> data);

    [[nodiscard]] const DirectorFile* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] const std::vector<std::uint8_t>& data() const;
    [[nodiscard]] int length() const;

private:
    const DirectorFile* file_;
    id::ChunkId id_;
    format::ChunkType type_;
    std::vector<std::uint8_t> data_;
};

} // namespace libreshockwave::chunks
