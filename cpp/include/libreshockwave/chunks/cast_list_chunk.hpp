#pragma once

#include <string>
#include <vector>

#include "libreshockwave/chunks/chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class CastListChunk final : public Chunk {
public:
    struct CastListEntry {
        std::string name;
        std::string path;
        int preload_settings;
        int min_member;
        int max_member;
        int member_count;
        int id;
    };

    CastListChunk(const moviesource::MovieSource* file,
                  id::ChunkId id,
                  int data_offset,
                  int items_per_entry,
                  int item_count,
                  std::vector<CastListEntry> entries);

    [[nodiscard]] const moviesource::MovieSource* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] int dataOffset() const;
    [[nodiscard]] int itemsPerEntry() const;
    [[nodiscard]] int itemCount() const;
    [[nodiscard]] const std::vector<CastListEntry>& entries() const;

    [[nodiscard]] static CastListChunk read(const moviesource::MovieSource* file, io::BinaryReader& reader, id::ChunkId id, int version);

private:
    const moviesource::MovieSource* file_;
    id::ChunkId id_;
    int data_offset_;
    int items_per_entry_;
    int item_count_;
    std::vector<CastListEntry> entries_;
};

} // namespace libreshockwave::chunks
