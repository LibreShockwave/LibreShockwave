#include "libreshockwave/chunks/ScriptContextChunk.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::chunks {

ScriptContextChunk::ScriptContextChunk(const DirectorFile* file,
                                       id::ChunkId id,
                                       int unknown1,
                                       int unknown2,
                                       int entryCount,
                                       id::ChunkId lnamSectionId,
                                       int validCount,
                                       int flags,
                                       int freePtr,
                                       std::vector<ScriptEntry> entries)
    : file_(file),
      id_(id),
      unknown1_(unknown1),
      unknown2_(unknown2),
      entryCount_(entryCount),
      lnamSectionId_(lnamSectionId),
      validCount_(validCount),
      flags_(flags),
      freePtr_(freePtr),
      entries_(std::move(entries)) {}

const DirectorFile* ScriptContextChunk::file() const { return file_; }
format::ChunkType ScriptContextChunk::type() const { return format::ChunkType::Lctx; }
id::ChunkId ScriptContextChunk::id() const { return id_; }
int ScriptContextChunk::unknown1() const { return unknown1_; }
int ScriptContextChunk::unknown2() const { return unknown2_; }
int ScriptContextChunk::entryCount() const { return entryCount_; }
id::ChunkId ScriptContextChunk::lnamSectionId() const { return lnamSectionId_; }
int ScriptContextChunk::validCount() const { return validCount_; }
int ScriptContextChunk::flags() const { return flags_; }
int ScriptContextChunk::freePtr() const { return freePtr_; }
const std::vector<ScriptContextChunk::ScriptEntry>& ScriptContextChunk::entries() const { return entries_; }

ScriptContextChunk ScriptContextChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId chunkId, int version) {
    (void)version;
    const auto originalOrder = reader.order();
    reader.setOrder(io::ByteOrder::BigEndian);

    int unknown1 = 0;
    int unknown2 = 0;
    int entryCount = 0;
    id::ChunkId lnamSectionId(0);
    int validCount = 0;
    int flags = 0;
    int freePtr = 0;
    std::vector<ScriptEntry> entries;

    if (reader.bytesLeft() >= 42) {
        unknown1 = reader.readI32();
        unknown2 = reader.readI32();
        entryCount = reader.readI32();
        (void)reader.readI32();
        const int entriesOffset = reader.readU16();
        reader.skip(2);
        reader.skip(4);
        reader.skip(4);
        reader.skip(4);
        lnamSectionId = id::ChunkId(std::max(0, reader.readI32()));
        validCount = reader.readU16();
        flags = reader.readU16();
        freePtr = reader.readI16();

        if (entriesOffset > 0 && static_cast<std::size_t>(entriesOffset) < reader.length()) {
            reader.seek(static_cast<std::size_t>(entriesOffset));
            if (entryCount > 0) {
                const auto availableEntries = reader.bytesLeft() / 12;
                entries.reserve(std::min(static_cast<std::size_t>(entryCount), availableEntries));
            }
            for (int index = 0; index < entryCount && reader.bytesLeft() >= 12; ++index) {
                const int entryUnknown = reader.readI32();
                const auto entryId = id::ChunkId(std::max(0, reader.readI32()));
                const int entryFlags = reader.readU16();
                (void)reader.readI16();
                entries.push_back(ScriptEntry{entryUnknown, entryId, entryFlags});
            }
        }
    }

    reader.setOrder(originalOrder);
    return ScriptContextChunk(file, chunkId, unknown1, unknown2, entryCount, lnamSectionId, validCount, flags, freePtr,
                              std::move(entries));
}

} // namespace libreshockwave::chunks
