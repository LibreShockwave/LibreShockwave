#include "libreshockwave/chunks/text_chunk.hpp"

#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::chunks {

TextChunk::TextChunk(const DirectorFile* file, id::ChunkId id, std::string text, std::vector<TextRun> runs)
    : file_(file), id_(id), text_(std::move(text)), runs_(std::move(runs)) {}

const DirectorFile* TextChunk::file() const { return file_; }
format::ChunkType TextChunk::type() const { return format::ChunkType::STXT; }
id::ChunkId TextChunk::id() const { return id_; }
const std::string& TextChunk::text() const { return text_; }
const std::vector<TextChunk::TextRun>& TextChunk::runs() const { return runs_; }

TextChunk TextChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int director_version) {
    const auto original_order = reader.order();
    reader.setOrder(io::ByteOrder::BigEndian);

    const int header_len = reader.readI32();
    const int text_len = reader.readI32();
    if (header_len > 8) {
        reader.skip(static_cast<std::size_t>(header_len - 8));
    }

    auto text = reader.readStringMacRoman(static_cast<std::size_t>(text_len));
    std::vector<TextRun> runs;

    if (reader.bytesLeft() >= 4) {
        const int run_count = reader.readI16();
        reader.skip(2);

        for (int index = 0; index < run_count && reader.bytesLeft() >= 16; ++index) {
            const int start_offset = reader.readI32();
            const int font_id = reader.readI16();
            const int font_style = reader.readU8();
            int font_size = 0;
            if (usesLegacyRunLayout(director_version)) {
                reader.skip(3);
                font_size = reader.readI16();
            } else {
                reader.skip(1);
                font_size = reader.readI16();
                reader.skip(2);
            }
            const int color_r = reader.readU8();
            const int color_g = reader.readU8();
            const int color_b = reader.readU8();
            reader.skip(1);

            runs.push_back(TextRun{start_offset, text_len, font_id, font_size, font_style, color_r, color_g, color_b});
        }
    }

    reader.setOrder(original_order);
    return TextChunk(file, id, std::move(text), std::move(runs));
}

bool TextChunk::usesLegacyRunLayout(int director_version) {
    return director_version > 0 && director_version <= 1600;
}

} // namespace libreshockwave::chunks
