#pragma once

#include <vector>

#include "libreshockwave/chunks/Chunk.hpp"

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

    ScriptContextChunk(const DirectorFile* file,
                       id::ChunkId id,
                       int unknown1,
                       int unknown2,
                       int entryCount,
                       id::ChunkId lnamSectionId,
                       int validCount,
                       int flags,
                       int freePtr,
                       std::vector<ScriptEntry> entries);

    [[nodiscard]] const DirectorFile* file() const override;
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

    [[nodiscard]] static ScriptContextChunk read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version);

private:
    const DirectorFile* file_;
    id::ChunkId id_;
    int unknown1_;
    int unknown2_;
    int entryCount_;
    id::ChunkId lnamSectionId_;
    int validCount_;
    int flags_;
    int freePtr_;
    std::vector<ScriptEntry> entries_;
};

} // namespace libreshockwave::chunks
