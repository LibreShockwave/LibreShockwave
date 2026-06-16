#include "libreshockwave/chunks/FontMapChunk.hpp"

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::chunks {

FontMapChunk::FontMapChunk(const DirectorFile* file, id::ChunkId id, std::vector<Entry> entries)
    : file_(file), id_(id), entries_(std::move(entries)) {}

const DirectorFile* FontMapChunk::file() const { return file_; }
format::ChunkType FontMapChunk::type() const { return format::ChunkType::Fmap; }
id::ChunkId FontMapChunk::id() const { return id_; }
const std::vector<FontMapChunk::Entry>& FontMapChunk::entries() const { return entries_; }

std::optional<std::string> FontMapChunk::fontNameForId(int fontId) const {
    for (const auto& entry : entries_) {
        if (entry.fontId == fontId) {
            return entry.fontName;
        }
    }
    return std::nullopt;
}

FontMapChunk FontMapChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id) {
    const auto originalOrder = reader.order();
    reader.setOrder(io::ByteOrder::BigEndian);

    const int mapLength = reader.readI32();
    (void)reader.readI32();
    const int bodyStart = static_cast<int>(reader.position());
    const int namesStart = bodyStart + mapLength + 2;

    reader.skip(8);
    const int entriesUsed = reader.readI32();
    reader.skip(16);

    std::vector<Entry> entries;
    for (int index = 0; index < entriesUsed && reader.bytesLeft() >= 8; ++index) {
        const int nameOffset = reader.readI32();
        const int platform = reader.readU16();
        const int fontId = reader.readU16();

        const auto returnPosition = reader.position();
        std::string fontName;
        const int namePosition = namesStart + nameOffset;
        if (namePosition >= 0 && static_cast<std::size_t>(namePosition + 2) <= reader.length()) {
            reader.seek(static_cast<std::size_t>(namePosition));
            const int nameLength = reader.readU16();
            if (nameLength >= 0 && static_cast<std::size_t>(nameLength) <= reader.bytesLeft()) {
                fontName = reader.readStringMacRoman(static_cast<std::size_t>(nameLength));
            }
        }
        reader.seek(returnPosition);
        entries.push_back(Entry{fontId, platform, std::move(fontName)});
    }

    reader.setOrder(originalOrder);
    return FontMapChunk(file, id, std::move(entries));
}

} // namespace libreshockwave::chunks
