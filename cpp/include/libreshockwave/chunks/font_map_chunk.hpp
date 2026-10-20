#pragma once

#include <optional>
#include <string>
#include <vector>

#include "libreshockwave/chunks/chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class FontMapChunk final : public Chunk {
public:
    struct Entry {
        int font_id;
        int platform;
        std::string font_name;
    };

    FontMapChunk(const DirectorFile* file, id::ChunkId id, std::vector<Entry> entries);

    [[nodiscard]] const DirectorFile* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] const std::vector<Entry>& entries() const;
    [[nodiscard]] std::optional<std::string> fontNameForId(int font_id) const;

    [[nodiscard]] static FontMapChunk read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id);

private:
    const DirectorFile* file_;
    id::ChunkId id_;
    std::vector<Entry> entries_;
};

} // namespace libreshockwave::chunks
