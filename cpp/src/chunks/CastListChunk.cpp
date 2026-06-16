#include "libreshockwave/chunks/CastListChunk.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::chunks {
namespace {

class ScopedByteOrder {
public:
    ScopedByteOrder(io::BinaryReader& reader, io::ByteOrder order)
        : reader_(reader), originalOrder_(reader.order()) {
        reader_.setOrder(order);
    }

    ~ScopedByteOrder() {
        reader_.setOrder(originalOrder_);
    }

private:
    io::BinaryReader& reader_;
    io::ByteOrder originalOrder_;
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
    static const std::vector<std::uint8_t> kEmpty;
    if (index < 0 || static_cast<std::size_t>(index) >= items.size()) {
        return kEmpty;
    }
    return items[static_cast<std::size_t>(index)];
}

} // namespace

CastListChunk::CastListChunk(const DirectorFile* file,
                             id::ChunkId id,
                             int dataOffset,
                             int itemsPerEntry,
                             int itemCount,
                             std::vector<CastListEntry> entries)
    : file_(file),
      id_(id),
      dataOffset_(dataOffset),
      itemsPerEntry_(itemsPerEntry),
      itemCount_(itemCount),
      entries_(std::move(entries)) {}

const DirectorFile* CastListChunk::file() const { return file_; }
format::ChunkType CastListChunk::type() const { return format::ChunkType::MCsL; }
id::ChunkId CastListChunk::id() const { return id_; }
int CastListChunk::dataOffset() const { return dataOffset_; }
int CastListChunk::itemsPerEntry() const { return itemsPerEntry_; }
int CastListChunk::itemCount() const { return itemCount_; }
const std::vector<CastListChunk::CastListEntry>& CastListChunk::entries() const { return entries_; }

CastListChunk CastListChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version) {
    (void)version;
    ScopedByteOrder order(reader, io::ByteOrder::BigEndian);

    int dataOffset = 0;
    int itemCount = 0;
    int itemsPerEntry = 0;
    std::vector<CastListEntry> entries;

    if (reader.bytesLeft() < 12) {
        return CastListChunk(file, id, dataOffset, itemsPerEntry, itemCount, std::move(entries));
    }

    dataOffset = reader.readI32();
    (void)reader.readU16();
    itemCount = reader.readU16();
    itemsPerEntry = reader.readU16();
    (void)reader.readU16();

    if (dataOffset < 0 || static_cast<std::size_t>(dataOffset) >= reader.length() || itemCount > 1000) {
        return CastListChunk(file, id, dataOffset, itemsPerEntry, itemCount, std::move(entries));
    }

    reader.seek(static_cast<std::size_t>(dataOffset));
    if (reader.bytesLeft() < 2) {
        return CastListChunk(file, id, dataOffset, itemsPerEntry, itemCount, std::move(entries));
    }

    const int offsetTableLen = reader.readU16();
    if (offsetTableLen < 0 || offsetTableLen > 10000 ||
        reader.bytesLeft() < static_cast<std::size_t>(offsetTableLen) * 4U + 4U) {
        return CastListChunk(file, id, dataOffset, itemsPerEntry, itemCount, std::move(entries));
    }

    std::vector<int> offsets;
    offsets.reserve(static_cast<std::size_t>(offsetTableLen));
    for (int index = 0; index < offsetTableLen; ++index) {
        offsets.push_back(reader.readI32());
    }

    const int itemsLen = reader.readI32();
    const auto listOffset = reader.position();
    std::vector<std::vector<std::uint8_t>> items;
    items.reserve(offsets.size());

    for (int index = 0; index < static_cast<int>(offsets.size()); ++index) {
        const int offset = offsets[static_cast<std::size_t>(index)];
        const int nextOffset = index + 1 < static_cast<int>(offsets.size())
            ? offsets[static_cast<std::size_t>(index + 1)]
            : itemsLen;
        const int itemLen = nextOffset - offset;

        const auto itemStart = listOffset + static_cast<std::size_t>(offset >= 0 ? offset : 0);
        if (offset >= 0 && itemLen > 0 && itemLen < 10000 &&
            itemStart <= reader.length() &&
            static_cast<std::size_t>(itemLen) <= reader.length() - itemStart) {
            const auto& data = reader.data();
            items.emplace_back(data.begin() + static_cast<std::ptrdiff_t>(itemStart),
                               data.begin() + static_cast<std::ptrdiff_t>(itemStart + static_cast<std::size_t>(itemLen)));
        } else {
            items.emplace_back();
        }
    }

    entries.reserve(static_cast<std::size_t>(itemCount));
    for (int castIndex = 0; castIndex < itemCount; ++castIndex) {
        const int baseIndex = castIndex * itemsPerEntry;
        CastListEntry entry{
            readPascalItem(itemOrEmpty(items, baseIndex + 1)),
            readPascalItem(itemOrEmpty(items, baseIndex + 2)),
            readPreloadSettings(itemOrEmpty(items, baseIndex + 3)),
            0,
            0,
            0,
            castIndex + 1
        };

        const auto& memberData = itemOrEmpty(items, baseIndex + 4);
        if (memberData.size() >= 8) {
            io::BinaryReader memberReader(memberData, io::ByteOrder::BigEndian);
            entry.minMember = memberReader.readU16();
            entry.maxMember = memberReader.readU16();
            entry.id = memberReader.readI32();
            if (entry.maxMember >= entry.minMember) {
                entry.memberCount = entry.maxMember - entry.minMember + 1;
            }
        }

        entries.push_back(std::move(entry));
    }

    return CastListChunk(file, id, dataOffset, itemsPerEntry, itemCount, std::move(entries));
}

} // namespace libreshockwave::chunks
