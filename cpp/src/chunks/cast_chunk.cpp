#include "libreshockwave/chunks/cast_chunk.hpp"

#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::chunks {

CastChunk::CastChunk(const moviesource::MovieSource* file, id::ChunkId id, std::vector<int> member_ids)
    : file_(file), id_(id), member_ids_(std::move(member_ids)) {}

const moviesource::MovieSource* CastChunk::file() const { return file_; }
format::ChunkType CastChunk::type() const { return format::ChunkType::CASp; }
id::ChunkId CastChunk::id() const { return id_; }
const std::vector<int>& CastChunk::memberIds() const { return member_ids_; }
int CastChunk::memberCount() const { return static_cast<int>(member_ids_.size()); }

CastChunk CastChunk::read(const moviesource::MovieSource* file, io::BinaryReader& reader, id::ChunkId id, int version) {
    (void)version;
    reader.setOrder(io::ByteOrder::BigEndian);
    std::vector<int> member_ids;
    while (reader.bytesLeft() >= 4) {
        member_ids.push_back(reader.readI32());
    }
    return CastChunk(file, id, std::move(member_ids));
}

} // namespace libreshockwave::chunks
