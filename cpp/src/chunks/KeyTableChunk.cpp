#include "libreshockwave/chunks/KeyTableChunk.hpp"

#include <algorithm>

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::chunks {

std::string KeyTableChunk::KeyTableEntry::fourccString() const {
    return io::BinaryReader::fourCCToString(fourcc);
}

KeyTableChunk::KeyTableChunk(const DirectorFile* file, id::ChunkId id, std::vector<KeyTableEntry> entries)
    : file_(file), id_(id), entries_(std::move(entries)) {
    indexEntries();
}

const DirectorFile* KeyTableChunk::file() const { return file_; }
format::ChunkType KeyTableChunk::type() const { return format::ChunkType::KEYp; }
id::ChunkId KeyTableChunk::id() const { return id_; }
const std::vector<KeyTableChunk::KeyTableEntry>& KeyTableChunk::entries() const { return entries_; }

std::vector<KeyTableChunk::KeyTableEntry> KeyTableChunk::getEntriesForOwner(id::ChunkId ownerId) const {
    if (const auto found = entriesByOwner_.find(ownerId.value()); found != entriesByOwner_.end()) {
        return found->second;
    }
    return {};
}

std::optional<KeyTableChunk::KeyTableEntry> KeyTableChunk::findEntry(id::ChunkId ownerId, std::uint32_t fourcc) const {
    const auto entries = getEntriesForOwner(ownerId);
    for (const auto& entry : entries) {
        if (entry.fourcc == fourcc) {
            return entry;
        }
    }
    return std::nullopt;
}

std::optional<id::ChunkId> KeyTableChunk::getOwnerCastId(id::ChunkId sectionId) const {
    if (const auto found = ownerBySectionId_.find(sectionId.value()); found != ownerBySectionId_.end()) {
        return id::ChunkId(found->second);
    }
    return std::nullopt;
}

std::optional<KeyTableChunk::KeyTableEntry> KeyTableChunk::getEntryBySectionId(id::ChunkId sectionId) const {
    if (!getOwnerCastId(sectionId).has_value()) {
        return std::nullopt;
    }
    for (const auto& entry : entries_) {
        if (entry.sectionId.value() == sectionId.value()) {
            return entry;
        }
    }
    return std::nullopt;
}

KeyTableChunk KeyTableChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version) {
    (void)version;
    (void)reader.readU16();
    (void)reader.readU16();
    (void)reader.readI32();
    const int usedCount = reader.readI32();

    std::vector<KeyTableEntry> entries;
    for (int index = 0; index < usedCount; ++index) {
        auto sectionId = id::ChunkId(sanitizeChunkId(reader.readI32()));
        auto castId = id::ChunkId(sanitizeChunkId(reader.readI32()));
        const auto fourcc = reader.readU32();
        entries.push_back(KeyTableEntry{sectionId, castId, fourcc});
    }
    return KeyTableChunk(file, id, std::move(entries));
}

int KeyTableChunk::sanitizeChunkId(int value) {
    return std::max(0, value);
}

void KeyTableChunk::indexEntries() {
    entriesByOwner_.clear();
    ownerBySectionId_.clear();
    for (const auto& entry : entries_) {
        entriesByOwner_[entry.castId.value()].push_back(entry);
        ownerBySectionId_[entry.sectionId.value()] = entry.castId.value();
    }
}

} // namespace libreshockwave::chunks
