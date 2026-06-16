#pragma once

#include <string>
#include <vector>

#include "libreshockwave/chunks/Chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class CastListChunk final : public Chunk {
public:
    struct CastListEntry {
        std::string name;
        std::string path;
        int preloadSettings;
        int minMember;
        int maxMember;
        int memberCount;
        int id;
    };

    CastListChunk(const DirectorFile* file,
                  id::ChunkId id,
                  int dataOffset,
                  int itemsPerEntry,
                  int itemCount,
                  std::vector<CastListEntry> entries);

    [[nodiscard]] const DirectorFile* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] int dataOffset() const;
    [[nodiscard]] int itemsPerEntry() const;
    [[nodiscard]] int itemCount() const;
    [[nodiscard]] const std::vector<CastListEntry>& entries() const;

    [[nodiscard]] static CastListChunk read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version);

private:
    const DirectorFile* file_;
    id::ChunkId id_;
    int dataOffset_;
    int itemsPerEntry_;
    int itemCount_;
    std::vector<CastListEntry> entries_;
};

} // namespace libreshockwave::chunks
