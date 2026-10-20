#include "libreshockwave/chunks/bitmap_chunk.hpp"

#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::chunks {

BitmapChunk::BitmapChunk(const DirectorFile* file,
                         id::ChunkId id,
                         std::vector<std::uint8_t> data,
                         int width,
                         int height,
                         int bit_depth,
                         id::PaletteId palette_id)
    : file_(file),
      id_(id),
      data_(std::move(data)),
      width_(width),
      height_(height),
      bit_depth_(bit_depth),
      palette_id_(palette_id) {}

const DirectorFile* BitmapChunk::file() const { return file_; }
format::ChunkType BitmapChunk::type() const { return format::ChunkType::BITD; }
id::ChunkId BitmapChunk::id() const { return id_; }
const std::vector<std::uint8_t>& BitmapChunk::data() const { return data_; }
int BitmapChunk::width() const { return width_; }
int BitmapChunk::height() const { return height_; }
int BitmapChunk::bitDepth() const { return bit_depth_; }
id::PaletteId BitmapChunk::paletteId() const { return palette_id_; }

BitmapChunk BitmapChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version) {
    (void)version;
    return BitmapChunk(file, id, reader.readBytes(reader.bytesLeft()), 0, 0, 0, id::PaletteId(0));
}

BitmapChunk BitmapChunk::withDimensions(const DirectorFile* file, int width, int height, int bit_depth, id::PaletteId palette_id) const {
    return BitmapChunk(file, id_, data_, width, height, bit_depth, palette_id);
}

} // namespace libreshockwave::chunks
