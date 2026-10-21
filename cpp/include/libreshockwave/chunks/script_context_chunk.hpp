#pragma once

#include <vector>

#include "libreshockwave/chunks/chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class ScriptContextChunk final : public Chunk {
public:
    struct ScriptEntry {
        int unknown;
        id::ChunkId id;
        int flags;
    };

    ScriptContextChunk(const moviesource::MovieSource* file,
                       id::ChunkId id,
                       int unknown1,
                       int unknown2,
                       int entry_count,
                       id::ChunkId lnam_section_id,
                       int valid_count,
                       int flags,
                       int free_ptr,
                       std::vector<ScriptEntry> entries);

    [[nodiscard]] const moviesource::MovieSource* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] int unknown1() const;
    [[nodiscard]] int unknown2() const;
    [[nodiscard]] int entryCount() const;
    [[nodiscard]] id::ChunkId lnamSectionId() const;
    [[nodiscard]] int validCount() const;
    [[nodiscard]] int flags() const;
    [[nodiscard]] int freePtr() const;
    [[nodiscard]] const std::vector<ScriptEntry>& entries() const;

    [[nodiscard]] static ScriptContextChunk read(const moviesource::MovieSource* file, io::BinaryReader& reader, id::ChunkId id, int version);

private:
    const moviesource::MovieSource* file_;
    id::ChunkId id_;
    int unknown1_;
    int unknown2_;
    int entry_count_;
    id::ChunkId lnam_section_id_;
    int valid_count_;
    int flags_;
    int free_ptr_;
    std::vector<ScriptEntry> entries_;
};

} // namespace libreshockwave::chunks
