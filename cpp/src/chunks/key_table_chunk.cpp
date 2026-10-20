#include "libreshockwave/chunks/key_table_chunk.hpp"

#include <algorithm>

#include "libreshockwave/io/binary_reader.hpp"

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

std::vector<KeyTableChunk::KeyTableEntry> KeyTableChunk::getEntriesForOwner(id::ChunkId owner_id) const {
    if (const auto found = entries_by_owner_.find(owner_id.value()); found != entries_by_owner_.end()) {
        return found->second;
    }
    return {};
}

std::optional<KeyTableChunk::KeyTableEntry> KeyTableChunk::findEntry(id::ChunkId owner_id, std::uint32_t fourcc) const {
    const auto entries = getEntriesForOwner(owner_id);
    for (const auto& entry : entries) {
        if (entry.fourcc == fourcc) {
            return entry;
        }
    }
    return std::nullopt;
}

std::optional<id::ChunkId> KeyTableChunk::getOwnerCastId(id::ChunkId section_id) const {
    if (const auto found = owner_by_section_id_.find(section_id.value()); found != owner_by_section_id_.end()) {
        return id::ChunkId(found->second);
    }
    return std::nullopt;
}

std::optional<KeyTableChunk::KeyTableEntry> KeyTableChunk::getEntryBySectionId(id::ChunkId section_id) const {
    if (!getOwnerCastId(section_id).has_value()) {
        return std::nullopt;
    }
    for (const auto& entry : entries_) {
        if (entry.section_id.value() == section_id.value()) {
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
    const int used_count = reader.readI32();

    std::vector<KeyTableEntry> entries;
    for (int index = 0; index < used_count; ++index) {
        auto section_id = id::ChunkId(sanitizeChunkId(reader.readI32()));
        auto cast_id = id::ChunkId(sanitizeChunkId(reader.readI32()));
        const auto fourcc = reader.readU32();
        entries.push_back(KeyTableEntry{section_id, cast_id, fourcc});
    }
    return KeyTableChunk(file, id, std::move(entries));
}

int KeyTableChunk::sanitizeChunkId(int value) {
    return std::max(0, value);
}

void KeyTableChunk::indexEntries() {
    entries_by_owner_.clear();
    owner_by_section_id_.clear();
    for (const auto& entry : entries_) {
        entries_by_owner_[entry.cast_id.value()].push_back(entry);
        owner_by_section_id_[entry.section_id.value()] = entry.cast_id.value();
    }
}

} // namespace libreshockwave::chunks
