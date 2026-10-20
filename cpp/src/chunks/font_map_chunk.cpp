#include "libreshockwave/chunks/font_map_chunk.hpp"

#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::chunks {

FontMapChunk::FontMapChunk(const DirectorFile* file, id::ChunkId id, std::vector<Entry> entries)
    : file_(file), id_(id), entries_(std::move(entries)) {}

const DirectorFile* FontMapChunk::file() const { return file_; }
format::ChunkType FontMapChunk::type() const { return format::ChunkType::Fmap; }
id::ChunkId FontMapChunk::id() const { return id_; }
const std::vector<FontMapChunk::Entry>& FontMapChunk::entries() const { return entries_; }

std::optional<std::string> FontMapChunk::fontNameForId(int font_id) const {
    for (const auto& entry : entries_) {
        if (entry.font_id == font_id) {
            return entry.font_name;
        }
    }
    return std::nullopt;
}

FontMapChunk FontMapChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id) {
    const auto original_order = reader.order();
    reader.setOrder(io::ByteOrder::BigEndian);

    const int map_length = reader.readI32();
    (void)reader.readI32();
    const int body_start = static_cast<int>(reader.position());
    const int names_start = body_start + map_length + 2;

    reader.skip(8);
    const int entries_used = reader.readI32();
    reader.skip(16);

    std::vector<Entry> entries;
    for (int index = 0; index < entries_used && reader.bytesLeft() >= 8; ++index) {
        const int name_offset = reader.readI32();
        const int platform = reader.readU16();
        const int font_id = reader.readU16();

        const auto return_position = reader.position();
        std::string font_name;
        const int name_position = names_start + name_offset;
        if (name_position >= 0 && static_cast<std::size_t>(name_position + 2) <= reader.length()) {
            reader.seek(static_cast<std::size_t>(name_position));
            const int name_length = reader.readU16();
            if (name_length >= 0 && static_cast<std::size_t>(name_length) <= reader.bytesLeft()) {
                font_name = reader.readStringMacRoman(static_cast<std::size_t>(name_length));
            }
        }
        reader.seek(return_position);
        entries.push_back(Entry{font_id, platform, std::move(font_name)});
    }

    reader.setOrder(original_order);
    return FontMapChunk(file, id, std::move(entries));
}

} // namespace libreshockwave::chunks
