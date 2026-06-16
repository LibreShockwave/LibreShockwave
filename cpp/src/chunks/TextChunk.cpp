#include "libreshockwave/chunks/TextChunk.hpp"

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::chunks {

TextChunk::TextChunk(const DirectorFile* file, id::ChunkId id, std::string text, std::vector<TextRun> runs)
    : file_(file), id_(id), text_(std::move(text)), runs_(std::move(runs)) {}

const DirectorFile* TextChunk::file() const { return file_; }
format::ChunkType TextChunk::type() const { return format::ChunkType::STXT; }
id::ChunkId TextChunk::id() const { return id_; }
const std::string& TextChunk::text() const { return text_; }
const std::vector<TextChunk::TextRun>& TextChunk::runs() const { return runs_; }

TextChunk TextChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int directorVersion) {
    const auto originalOrder = reader.order();
    reader.setOrder(io::ByteOrder::BigEndian);

    const int headerLen = reader.readI32();
    const int textLen = reader.readI32();
    if (headerLen > 8) {
        reader.skip(static_cast<std::size_t>(headerLen - 8));
    }

    auto text = reader.readStringMacRoman(static_cast<std::size_t>(textLen));
    std::vector<TextRun> runs;

    if (reader.bytesLeft() >= 4) {
        const int runCount = reader.readI16();
        reader.skip(2);

        for (int index = 0; index < runCount && reader.bytesLeft() >= 16; ++index) {
            const int startOffset = reader.readI32();
            const int fontId = reader.readI16();
            const int fontStyle = reader.readU8();
            int fontSize = 0;
            if (usesLegacyRunLayout(directorVersion)) {
                reader.skip(3);
                fontSize = reader.readI16();
            } else {
                reader.skip(1);
                fontSize = reader.readI16();
                reader.skip(2);
            }
            const int colorR = reader.readU8();
            const int colorG = reader.readU8();
            const int colorB = reader.readU8();
            reader.skip(1);

            runs.push_back(TextRun{startOffset, textLen, fontId, fontSize, fontStyle, colorR, colorG, colorB});
        }
    }

    reader.setOrder(originalOrder);
    return TextChunk(file, id, std::move(text), std::move(runs));
}

bool TextChunk::usesLegacyRunLayout(int directorVersion) {
    return directorVersion > 0 && directorVersion <= 1600;
}

} // namespace libreshockwave::chunks
