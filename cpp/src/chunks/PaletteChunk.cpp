#include "libreshockwave/chunks/PaletteChunk.hpp"

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::chunks {

PaletteChunk::PaletteChunk(const DirectorFile* file, id::ChunkId id, std::vector<std::uint32_t> colors)
    : file_(file), id_(id), colors_(std::move(colors)) {}

const DirectorFile* PaletteChunk::file() const { return file_; }
format::ChunkType PaletteChunk::type() const { return format::ChunkType::CLUT; }
id::ChunkId PaletteChunk::id() const { return id_; }
const std::vector<std::uint32_t>& PaletteChunk::colors() const { return colors_; }

std::uint32_t PaletteChunk::getColor(int index) const {
    if (index >= 0 && index < static_cast<int>(colors_.size())) {
        return colors_[static_cast<std::size_t>(index)];
    }
    return 0;
}

int PaletteChunk::colorCount() const {
    return static_cast<int>(colors_.size());
}

PaletteChunk PaletteChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version) {
    (void)version;
    const auto colorCount = static_cast<int>(reader.bytesLeft() / 6);
    std::vector<std::uint32_t> colors;
    colors.reserve(static_cast<std::size_t>(colorCount));

    for (int index = 0; index < colorCount; ++index) {
        const auto r = reader.readU8();
        reader.skip(1);
        const auto g = reader.readU8();
        reader.skip(1);
        const auto b = reader.readU8();
        reader.skip(1);
        colors.push_back((static_cast<std::uint32_t>(r) << 16) |
                         (static_cast<std::uint32_t>(g) << 8) |
                         static_cast<std::uint32_t>(b));
    }

    return PaletteChunk(file, id, std::move(colors));
}

} // namespace libreshockwave::chunks
