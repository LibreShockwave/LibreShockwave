#include "libreshockwave/chunks/RawChunk.hpp"

namespace libreshockwave::chunks {

RawChunk::RawChunk(const DirectorFile* file, id::ChunkId id, format::ChunkType type, std::vector<std::uint8_t> data)
    : file_(file), id_(id), type_(type), data_(std::move(data)) {}

const DirectorFile* RawChunk::file() const { return file_; }
format::ChunkType RawChunk::type() const { return type_; }
id::ChunkId RawChunk::id() const { return id_; }
const std::vector<std::uint8_t>& RawChunk::data() const { return data_; }
int RawChunk::length() const { return static_cast<int>(data_.size()); }

} // namespace libreshockwave::chunks
