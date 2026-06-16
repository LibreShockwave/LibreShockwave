#pragma once

#include <string>
#include <vector>

#include "libreshockwave/chunks/Chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class TextChunk final : public Chunk {
public:
    struct TextRun {
        int startOffset;
        int endOffset;
        int fontId;
        int fontSize;
        int fontStyle;
        int colorR;
        int colorG;
        int colorB;
    };

    TextChunk(const DirectorFile* file, id::ChunkId id, std::string text, std::vector<TextRun> runs);

    [[nodiscard]] const DirectorFile* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] const std::string& text() const;
    [[nodiscard]] const std::vector<TextRun>& runs() const;

    [[nodiscard]] static TextChunk read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int directorVersion = 0);

private:
    [[nodiscard]] static bool usesLegacyRunLayout(int directorVersion);

    const DirectorFile* file_;
    id::ChunkId id_;
    std::string text_;
    std::vector<TextRun> runs_;
};

} // namespace libreshockwave::chunks
