#pragma once

#include <string>
#include <vector>

#include "libreshockwave/chunks/chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class TextChunk final : public Chunk {
public:
    struct TextRun {
        int start_offset;
        int end_offset;
        int font_id;
        int font_size;
        int font_style;
        int color_r;
        int color_g;
        int color_b;
    };

    TextChunk(const DirectorFile* file, id::ChunkId id, std::string text, std::vector<TextRun> runs);

    [[nodiscard]] const DirectorFile* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] const std::string& text() const;
    [[nodiscard]] const std::vector<TextRun>& runs() const;

    [[nodiscard]] static TextChunk read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int director_version = 0);

private:
    [[nodiscard]] static bool usesLegacyRunLayout(int director_version);

    const DirectorFile* file_;
    id::ChunkId id_;
    std::string text_;
    std::vector<TextRun> runs_;
};

} // namespace libreshockwave::chunks
