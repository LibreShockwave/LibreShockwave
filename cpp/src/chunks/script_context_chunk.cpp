#include "libreshockwave/chunks/script_context_chunk.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::chunks {

ScriptContextChunk::ScriptContextChunk(const moviesource::MovieSource* file,
                                       id::ChunkId id,
                                       int unknown1,
                                       int unknown2,
                                       int entry_count,
                                       id::ChunkId lnam_section_id,
                                       int valid_count,
                                       int flags,
                                       int free_ptr,
                                       std::vector<ScriptEntry> entries)
    : file_(file),
      id_(id),
      unknown1_(unknown1),
      unknown2_(unknown2),
      entry_count_(entry_count),
      lnam_section_id_(lnam_section_id),
      valid_count_(valid_count),
      flags_(flags),
      free_ptr_(free_ptr),
      entries_(std::move(entries)) {}

const moviesource::MovieSource* ScriptContextChunk::file() const { return file_; }
format::ChunkType ScriptContextChunk::type() const { return format::ChunkType::Lctx; }
id::ChunkId ScriptContextChunk::id() const { return id_; }
int ScriptContextChunk::unknown1() const { return unknown1_; }
int ScriptContextChunk::unknown2() const { return unknown2_; }
int ScriptContextChunk::entryCount() const { return entry_count_; }
id::ChunkId ScriptContextChunk::lnamSectionId() const { return lnam_section_id_; }
int ScriptContextChunk::validCount() const { return valid_count_; }
int ScriptContextChunk::flags() const { return flags_; }
int ScriptContextChunk::freePtr() const { return free_ptr_; }
const std::vector<ScriptContextChunk::ScriptEntry>& ScriptContextChunk::entries() const { return entries_; }

ScriptContextChunk ScriptContextChunk::read(const moviesource::MovieSource* file, io::BinaryReader& reader, id::ChunkId chunk_id, int version) {
    (void)version;
    const auto original_order = reader.order();
    reader.setOrder(io::ByteOrder::BigEndian);

    int unknown1 = 0;
    int unknown2 = 0;
    int entry_count = 0;
    id::ChunkId lnam_section_id(0);
    int valid_count = 0;
    int flags = 0;
    int free_ptr = 0;
    std::vector<ScriptEntry> entries;

    if (reader.bytesLeft() >= 42) {
        unknown1 = reader.readI32();
        unknown2 = reader.readI32();
        entry_count = reader.readI32();
        (void)reader.readI32();
        const int entries_offset = reader.readU16();
        reader.skip(2);
        reader.skip(4);
        reader.skip(4);
        reader.skip(4);
        lnam_section_id = id::ChunkId(std::max(0, reader.readI32()));
        valid_count = reader.readU16();
        flags = reader.readU16();
        free_ptr = reader.readI16();

        if (entries_offset > 0 && static_cast<std::size_t>(entries_offset) < reader.length()) {
            reader.seek(static_cast<std::size_t>(entries_offset));
            if (entry_count > 0) {
                const auto available_entries = reader.bytesLeft() / 12;
                entries.reserve(std::min(static_cast<std::size_t>(entry_count), available_entries));
            }
            for (int index = 0; index < entry_count && reader.bytesLeft() >= 12; ++index) {
                const int entry_unknown = reader.readI32();
                const auto entry_id = id::ChunkId(std::max(0, reader.readI32()));
                const int entry_flags = reader.readU16();
                (void)reader.readI16();
                entries.push_back(ScriptEntry{entry_unknown, entry_id, entry_flags});
            }
        }
    }

    reader.setOrder(original_order);
    return ScriptContextChunk(file, chunk_id, unknown1, unknown2, entry_count, lnam_section_id, valid_count, flags, free_ptr,
                              std::move(entries));
}

} // namespace libreshockwave::chunks
