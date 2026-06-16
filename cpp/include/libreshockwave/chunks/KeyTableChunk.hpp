#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "libreshockwave/chunks/Chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class KeyTableChunk final : public Chunk {
public:
    struct KeyTableEntry {
        id::ChunkId sectionId;
        id::ChunkId castId;
        std::uint32_t fourcc;

        [[nodiscard]] std::string fourccString() const;
    };

    KeyTableChunk(const DirectorFile* file, id::ChunkId id, std::vector<KeyTableEntry> entries);

    [[nodiscard]] const DirectorFile* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] const std::vector<KeyTableEntry>& entries() const;
    [[nodiscard]] std::vector<KeyTableEntry> getEntriesForOwner(id::ChunkId ownerId) const;
    [[nodiscard]] std::optional<KeyTableEntry> findEntry(id::ChunkId ownerId, std::uint32_t fourcc) const;
    [[nodiscard]] std::optional<id::ChunkId> getOwnerCastId(id::ChunkId sectionId) const;
    [[nodiscard]] std::optional<KeyTableEntry> getEntryBySectionId(id::ChunkId sectionId) const;

    [[nodiscard]] static KeyTableChunk read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version);

private:
    static int sanitizeChunkId(int value);
    void indexEntries();

    const DirectorFile* file_;
    id::ChunkId id_;
    std::vector<KeyTableEntry> entries_;
    std::unordered_map<int, std::vector<KeyTableEntry>> entriesByOwner_;
    std::unordered_map<int, int> ownerBySectionId_;
};

} // namespace libreshockwave::chunks
