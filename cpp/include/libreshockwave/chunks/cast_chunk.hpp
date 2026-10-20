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
    CastChunk(const DirectorFile* file, id::ChunkId id, std::vector<int> member_ids);

    [[nodiscard]] const DirectorFile* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] const std::vector<int>& memberIds() const;
    [[nodiscard]] int memberCount() const;

    [[nodiscard]] static CastChunk read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version);

private:
    const DirectorFile* file_;
    id::ChunkId id_;
    std::vector<int> member_ids_;
};

} // namespace libreshockwave::chunks
