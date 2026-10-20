#include "libreshockwave/chunks/cast_list_chunk.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::chunks {
namespace {

class ScopedByteOrder {
public:
    ScopedByteOrder(io::BinaryReader& reader, io::ByteOrder order)
        : reader_(reader), original_order_(reader.order()) {
        reader_.setOrder(order);
    }

    ~ScopedByteOrder() {
        reader_.setOrder(original_order_);
    }

private:
    io::BinaryReader& reader_;
    io::ByteOrder original_order_;
};

std::string readPascalItem(const std::vector<std::uint8_t>& data) {
    if (data.empty()) {
        return "";
    }
    io::BinaryReader reader(data, io::ByteOrder::BigEndian);
    const int length = reader.readU8();
    if (length <= 0 || static_cast<std::size_t>(length) > reader.bytesLeft()) {
        return "";
    }
    return reader.readStringMacRoman(static_cast<std::size_t>(length));
}

int readPreloadSettings(const std::vector<std::uint8_t>& data) {
    if (data.size() < 2) {
        return 0;
    }
    return (static_cast<int>(data[0]) << 8) | static_cast<int>(data[1]);
}

const std::vector<std::uint8_t>& itemOrEmpty(const std::vector<std::vector<std::uint8_t>>& items, int index) {
    static const std::vector<std::uint8_t> k_empty;
    if (index < 0 || static_cast<std::size_t>(index) >= items.size()) {
        return k_empty;
    }
    return items[static_cast<std::size_t>(index)];
}

} // namespace

CastListChunk::CastListChunk(const DirectorFile* file,
                             id::ChunkId id,
                             int data_offset,
                             int items_per_entry,
                             int item_count,
                             std::vector<CastListEntry> entries)
    : file_(file),
      id_(id),
      data_offset_(data_offset),
      items_per_entry_(items_per_entry),
      item_count_(item_count),
      entries_(std::move(entries)) {}

const DirectorFile* CastListChunk::file() const { return file_; }
format::ChunkType CastListChunk::type() const { return format::ChunkType::MCsL; }
id::ChunkId CastListChunk::id() const { return id_; }
int CastListChunk::dataOffset() const { return data_offset_; }
int CastListChunk::itemsPerEntry() const { return items_per_entry_; }
int CastListChunk::itemCount() const { return item_count_; }
const std::vector<CastListChunk::CastListEntry>& CastListChunk::entries() const { return entries_; }

CastListChunk CastListChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version) {
    (void)version;
    ScopedByteOrder order(reader, io::ByteOrder::BigEndian);

    int data_offset = 0;
    int item_count = 0;
    int items_per_entry = 0;
    std::vector<CastListEntry> entries;

    if (reader.bytesLeft() < 12) {
        return CastListChunk(file, id, data_offset, items_per_entry, item_count, std::move(entries));
    }

    data_offset = reader.readI32();
    (void)reader.readU16();
    item_count = reader.readU16();
    items_per_entry = reader.readU16();
    (void)reader.readU16();

    if (data_offset < 0 || static_cast<std::size_t>(data_offset) >= reader.length() || item_count > 1000) {
        return CastListChunk(file, id, data_offset, items_per_entry, item_count, std::move(entries));
    }

    reader.seek(static_cast<std::size_t>(data_offset));
    if (reader.bytesLeft() < 2) {
        return CastListChunk(file, id, data_offset, items_per_entry, item_count, std::move(entries));
    }

    const int offset_table_len = reader.readU16();
    if (offset_table_len < 0 || offset_table_len > 10000 ||
        reader.bytesLeft() < static_cast<std::size_t>(offset_table_len) * 4U + 4U) {
        return CastListChunk(file, id, data_offset, items_per_entry, item_count, std::move(entries));
    }

    std::vector<int> offsets;
    offsets.reserve(static_cast<std::size_t>(offset_table_len));
    for (int index = 0; index < offset_table_len; ++index) {
        offsets.push_back(reader.readI32());
    }

    const int items_len = reader.readI32();
    const auto list_offset = reader.position();
    std::vector<std::vector<std::uint8_t>> items;
    items.reserve(offsets.size());

    for (int index = 0; index < static_cast<int>(offsets.size()); ++index) {
        const int offset = offsets[static_cast<std::size_t>(index)];
        const int next_offset = index + 1 < static_cast<int>(offsets.size())
            ? offsets[static_cast<std::size_t>(index + 1)]
            : items_len;
        const int item_len = next_offset - offset;

        const auto item_start = list_offset + static_cast<std::size_t>(offset >= 0 ? offset : 0);
        if (offset >= 0 && item_len > 0 && item_len < 10000 &&
            item_start <= reader.length() &&
            static_cast<std::size_t>(item_len) <= reader.length() - item_start) {
            const auto& data = reader.data();
            items.emplace_back(data.begin() + static_cast<std::ptrdiff_t>(item_start),
                               data.begin() + static_cast<std::ptrdiff_t>(item_start + static_cast<std::size_t>(item_len)));
        } else {
            items.emplace_back();
        }
    }

    entries.reserve(static_cast<std::size_t>(item_count));
    for (int cast_index = 0; cast_index < item_count; ++cast_index) {
        const int base_index = cast_index * items_per_entry;
        CastListEntry entry{
            readPascalItem(itemOrEmpty(items, base_index + 1)),
            readPascalItem(itemOrEmpty(items, base_index + 2)),
            readPreloadSettings(itemOrEmpty(items, base_index + 3)),
            0,
            0,
            0,
            cast_index + 1
        };

        const auto& member_data = itemOrEmpty(items, base_index + 4);
        if (member_data.size() >= 8) {
            io::BinaryReader member_reader(member_data, io::ByteOrder::BigEndian);
            entry.min_member = member_reader.readU16();
            entry.max_member = member_reader.readU16();
            entry.id = member_reader.readI32();
            if (entry.max_member >= entry.min_member) {
                entry.member_count = entry.max_member - entry.min_member + 1;
            }
        }

        entries.push_back(std::move(entry));
    }

    return CastListChunk(file, id, data_offset, items_per_entry, item_count, std::move(entries));
}

} // namespace libreshockwave::chunks
