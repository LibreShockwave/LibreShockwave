#pragma once

#include <cstdint>
#include <vector>

#include "libreshockwave/chunks/Chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class BitmapChunk final : public Chunk {
public:
    BitmapChunk(const DirectorFile* file,
                id::ChunkId id,
                std::vector<std::uint8_t> data,
                int width,
                int height,
                int bitDepth,
                id::PaletteId paletteId);

    [[nodiscard]] const DirectorFile* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] const std::vector<std::uint8_t>& data() const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] int bitDepth() const;
    [[nodiscard]] id::PaletteId paletteId() const;

    [[nodiscard]] static BitmapChunk read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version);
    [[nodiscard]] BitmapChunk withDimensions(const DirectorFile* file, int width, int height, int bitDepth, id::PaletteId paletteId) const;

private:
    const DirectorFile* file_;
    id::ChunkId id_;
    std::vector<std::uint8_t> data_;
    int width_;
    int height_;
    int bitDepth_;
    id::PaletteId paletteId_;
};

} // namespace libreshockwave::chunks
