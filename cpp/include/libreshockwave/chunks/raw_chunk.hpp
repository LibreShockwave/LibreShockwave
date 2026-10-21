#pragma once

#include <cstdint>
#include <vector>

#include "libreshockwave/chunks/chunk.hpp"

namespace libreshockwave::chunks {

class RawChunk final : public Chunk {
public:
    RawChunk(const moviesource::MovieSource* file, id::ChunkId id, format::ChunkType type, std::vector<std::uint8_t> data);

    [[nodiscard]] const moviesource::MovieSource* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] const std::vector<std::uint8_t>& data() const;
    [[nodiscard]] int length() const;

private:
    const moviesource::MovieSource* file_;
    id::ChunkId id_;
    format::ChunkType type_;
    std::vector<std::uint8_t> data_;
};

} // namespace libreshockwave::chunks
