#pragma once

#include <cstdint>
#include <vector>

#include "libreshockwave/chunks/chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class CastChunk final : public Chunk {
public:
    CastChunk(const moviesource::MovieSource* file, id::ChunkId id, std::vector<int> member_ids);

    [[nodiscard]] const moviesource::MovieSource* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] const std::vector<int>& memberIds() const;
    [[nodiscard]] int memberCount() const;

    [[nodiscard]] static CastChunk read(const moviesource::MovieSource* file, io::BinaryReader& reader, id::ChunkId id, int version);

private:
    const moviesource::MovieSource* file_;
    id::ChunkId id_;
    std::vector<int> member_ids_;
};

} // namespace libreshockwave::chunks
