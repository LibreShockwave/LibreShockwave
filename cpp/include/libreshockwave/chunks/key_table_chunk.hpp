#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "libreshockwave/chunks/chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class KeyTableChunk final : public Chunk {
public:
    struct KeyTableEntry {
        id::ChunkId section_id;
        id::ChunkId cast_id;
        std::uint32_t fourcc;

        [[nodiscard]] std::string fourccString() const;
    };

    KeyTableChunk(const DirectorFile* file, id::ChunkId id, std::vector<KeyTableEntry> entries);

    [[nodiscard]] const DirectorFile* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] const std::vector<KeyTableEntry>& entries() const;
    [[nodiscard]] std::vector<KeyTableEntry> getEntriesForOwner(id::ChunkId owner_id) const;
    [[nodiscard]] std::optional<KeyTableEntry> findEntry(id::ChunkId owner_id, std::uint32_t fourcc) const;
    [[nodiscard]] std::optional<id::ChunkId> getOwnerCastId(id::ChunkId section_id) const;
    [[nodiscard]] std::optional<KeyTableEntry> getEntryBySectionId(id::ChunkId section_id) const;

    [[nodiscard]] static KeyTableChunk read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version);

private:
    static int sanitizeChunkId(int value);
    void indexEntries();

    const DirectorFile* file_;
    id::ChunkId id_;
    std::vector<KeyTableEntry> entries_;
    std::unordered_map<int, std::vector<KeyTableEntry>> entries_by_owner_;
    std::unordered_map<int, int> owner_by_section_id_;
};

} // namespace libreshockwave::chunks
