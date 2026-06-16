#include "libreshockwave/chunks/CastChunk.hpp"

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::chunks {

CastChunk::CastChunk(const DirectorFile* file, id::ChunkId id, std::vector<int> memberIds)
    : file_(file), id_(id), memberIds_(std::move(memberIds)) {}

const DirectorFile* CastChunk::file() const { return file_; }
format::ChunkType CastChunk::type() const { return format::ChunkType::CASp; }
id::ChunkId CastChunk::id() const { return id_; }
const std::vector<int>& CastChunk::memberIds() const { return memberIds_; }
int CastChunk::memberCount() const { return static_cast<int>(memberIds_.size()); }

CastChunk CastChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version) {
    (void)version;
    reader.setOrder(io::ByteOrder::BigEndian);
    std::vector<int> memberIds;
    while (reader.bytesLeft() >= 4) {
        memberIds.push_back(reader.readI32());
    }
    return CastChunk(file, id, std::move(memberIds));
}

} // namespace libreshockwave::chunks
