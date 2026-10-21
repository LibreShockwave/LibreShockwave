#pragma once

#include <cstdint>
#include <vector>

#include "libreshockwave/chunks/chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class PaletteChunk final : public Chunk {
public:
    PaletteChunk(const moviesource::MovieSource* file, id::ChunkId id, std::vector<std::uint32_t> colors);

    [[nodiscard]] const moviesource::MovieSource* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] const std::vector<std::uint32_t>& colors() const;
    [[nodiscard]] std::uint32_t getColor(int index) const;
    [[nodiscard]] int colorCount() const;

    [[nodiscard]] static PaletteChunk read(const moviesource::MovieSource* file, io::BinaryReader& reader, id::ChunkId id, int version);

private:
    const moviesource::MovieSource* file_;
    id::ChunkId id_;
    std::vector<std::uint32_t> colors_;
};

} // namespace libreshockwave::chunks
