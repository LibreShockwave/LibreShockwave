#include "libreshockwave/chunks/PaletteChunk.hpp"

#include "libreshockwave/io/BinaryReader.hpp"

#include <algorithm>

namespace libreshockwave::chunks {
namespace {

int component16To8(int value) {
    return std::clamp((value + 128) / 257, 0, 255);
}

int readPaletteComponent16(io::BinaryReader& reader) {
    const auto high = static_cast<int>(reader.readU8());
    const auto low = static_cast<int>(reader.readU8());
    return component16To8((high << 8) | low);
}

} // namespace

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
        const auto r = readPaletteComponent16(reader);
        const auto g = readPaletteComponent16(reader);
        const auto b = readPaletteComponent16(reader);
        colors.push_back((static_cast<std::uint32_t>(r) << 16) |
                         (static_cast<std::uint32_t>(g) << 8) |
                         static_cast<std::uint32_t>(b));
    }

    return PaletteChunk(file, id, std::move(colors));
}

} // namespace libreshockwave::chunks
