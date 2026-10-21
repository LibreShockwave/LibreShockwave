#include "libreshockwave/moviesource/zip_archive.hpp"

#include <algorithm>
#include <cstring>

#if defined(LIBRESHOCKWAVE_HAVE_ZLIB)
#include <zlib.h>
#endif

namespace libreshockwave::moviesource {
namespace {

// ZIP signatures and fixed record sizes, from the PKZIP appnote.
constexpr std::uint32_t kEndOfCentralDirectorySignature = 0x06054B50;
constexpr std::uint32_t kCentralDirectorySignature = 0x02014B50;
constexpr std::uint32_t kLocalHeaderSignature = 0x04034B50;
constexpr std::size_t kEndOfCentralDirectorySize = 22;
constexpr std::size_t kCentralDirectoryHeaderSize = 46;
constexpr std::size_t kLocalHeaderSize = 30;
// The end-of-central-directory record may be followed by a comment of at most
// 0xFFFF bytes.
constexpr std::size_t kMaxCommentSize = 0xFFFF;

std::uint16_t readU16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[offset + 1]) << 8U);
}

std::uint32_t readU32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
        (static_cast<std::uint32_t>(data[offset + 1]) << 8U) |
        (static_cast<std::uint32_t>(data[offset + 2]) << 16U) |
        (static_cast<std::uint32_t>(data[offset + 3]) << 24U);
}

bool rangeFits(std::size_t offset, std::size_t length, std::size_t total) {
    return offset <= total && length <= total - offset;
}

// Inflate a raw deflate stream. Returns std::nullopt when zlib is unavailable
// or the stream is malformed.
std::optional<std::vector<std::uint8_t>> inflateRaw(const std::uint8_t* data,
                                                    std::size_t size,
                                                    std::size_t expected_size) {
#if defined(LIBRESHOCKWAVE_HAVE_ZLIB)
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(data);
    stream.avail_in = static_cast<uInt>(size);
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> output(expected_size);
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    const int result = inflate(&stream, Z_FINISH);
    const std::size_t produced = output.size() - stream.avail_out;
    inflateEnd(&stream);
    if (result != Z_STREAM_END) {
        return std::nullopt;
    }
    output.resize(produced);
    return output;
#else
    (void)data;
    (void)size;
    (void)expected_size;
    return std::nullopt;
#endif
}

} // namespace

std::optional<ZipArchive> ZipArchive::parse(std::vector<std::uint8_t> data) {
    if (data.size() < kEndOfCentralDirectorySize) {
        return std::nullopt;
    }
    const std::size_t search_start = data.size() > kMaxCommentSize + kEndOfCentralDirectorySize
        ? data.size() - kMaxCommentSize - kEndOfCentralDirectorySize
        : 0;
    std::size_t eocd = data.size();
    for (std::size_t offset = data.size() - kEndOfCentralDirectorySize + 1; offset-- > search_start;) {
        if (readU32(data, offset) == kEndOfCentralDirectorySignature) {
            eocd = offset;
            break;
        }
    }
    if (eocd == data.size()) {
        return std::nullopt;
    }

    const std::uint32_t entry_count = readU16(data, eocd + 10);
    const std::uint32_t directory_offset = readU32(data, eocd + 16);
    if (entry_count == 0 || entry_count > data.size() / kCentralDirectoryHeaderSize ||
        !rangeFits(directory_offset, kCentralDirectoryHeaderSize, data.size())) {
        return std::nullopt;
    }

    ZipArchive archive;
    archive.data_ = std::move(data);
    archive.entries_.reserve(entry_count);
    std::size_t offset = directory_offset;
    for (std::uint32_t index = 0; index < entry_count; ++index) {
        if (!rangeFits(offset, kCentralDirectoryHeaderSize, archive.data_.size()) ||
            readU32(archive.data_, offset) != kCentralDirectorySignature) {
            return std::nullopt;
        }
        const std::uint16_t name_length = readU16(archive.data_, offset + 28);
        const std::uint16_t extra_length = readU16(archive.data_, offset + 30);
        const std::uint16_t comment_length = readU16(archive.data_, offset + 32);
        const std::size_t name_offset = offset + kCentralDirectoryHeaderSize;
        if (!rangeFits(name_offset, name_length, archive.data_.size())) {
            return std::nullopt;
        }
        Entry entry;
        entry.name.assign(reinterpret_cast<const char*>(archive.data_.data() + name_offset),
                          name_length);
        entry.method = readU16(archive.data_, offset + 10);
        entry.crc32 = readU32(archive.data_, offset + 16);
        entry.compressed_size = readU32(archive.data_, offset + 20);
        entry.uncompressed_size = readU32(archive.data_, offset + 24);
        entry.local_header_offset = readU32(archive.data_, offset + 42);
        archive.entries_.push_back(std::move(entry));
        offset = name_offset + name_length + extra_length + comment_length;
    }
    return archive;
}

const ZipArchive::Entry* ZipArchive::find(std::string_view name) const {
    for (const auto& entry : entries_) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

std::optional<std::vector<std::uint8_t>> ZipArchive::extract(const Entry& entry) const {
    if (!rangeFits(entry.local_header_offset, kLocalHeaderSize, data_.size()) ||
        readU32(data_, entry.local_header_offset) != kLocalHeaderSignature) {
        return std::nullopt;
    }
    const std::uint16_t name_length = readU16(data_, entry.local_header_offset + 26);
    const std::uint16_t extra_length = readU16(data_, entry.local_header_offset + 28);
    const std::size_t data_offset =
        entry.local_header_offset + kLocalHeaderSize + name_length + extra_length;
    if (!rangeFits(data_offset, entry.compressed_size, data_.size())) {
        return std::nullopt;
    }
    if (entry.isStored()) {
        if (entry.compressed_size != entry.uncompressed_size) {
            return std::nullopt;
        }
        return std::vector<std::uint8_t>(
            data_.begin() + static_cast<std::ptrdiff_t>(data_offset),
            data_.begin() + static_cast<std::ptrdiff_t>(data_offset + entry.compressed_size));
    }
    if (entry.isDeflated()) {
        auto output = inflateRaw(data_.data() + data_offset, entry.compressed_size,
                                 entry.uncompressed_size);
        if (!output || output->size() != entry.uncompressed_size) {
            return std::nullopt;
        }
        return output;
    }
    return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> ZipArchive::extract(std::string_view name) const {
    const Entry* entry = find(name);
    if (entry == nullptr) {
        return std::nullopt;
    }
    return extract(*entry);
}

} // namespace libreshockwave::moviesource
