#include "libreshockwave/format/afterburner_reader.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace libreshockwave::format {
namespace {

std::string trimChunkTag(std::string tag) {
    while (!tag.empty() && (tag.back() == ' ' || tag.back() == '\0')) {
        tag.pop_back();
    }
    return tag;
}

} // namespace

AfterburnerReader::AfterburnerReader(io::BinaryReader reader, io::ByteOrder byte_order)
    : reader_(std::move(reader)), byte_order_(byte_order) {
    reader_.setOrder(byte_order_);
}

void AfterburnerReader::parse() {
    readFverChunk();
    readFcdrChunk();
    readAbmpChunk();
    readIlsChunk();
}

std::optional<std::vector<std::uint8_t>> AfterburnerReader::getChunkData(int resource_id) {
    if (resource_id == 2) {
        const auto* info = getChunkInfo(resource_id);
        if (info && trimChunkTag(info->four_cc) == "ILS") {
            return ils_data_;
        }
    }

    if (const auto cached = cached_chunk_data_.find(resource_id); cached != cached_chunk_data_.end()) {
        auto chunk_data = cached->second;
        const auto* info = getChunkInfo(resource_id);
        if (info && info->compressed_size != info->uncompressed_size && info->isZlibCompressed()) {
            chunk_data = io::BinaryReader::decompressZlib(chunk_data);
        }
        return chunk_data;
    }

    const auto* info = getChunkInfo(resource_id);
    if (!info) {
        return std::nullopt;
    }

    const int file_offset = ils_body_offset_ + info->offset;
    if (file_offset < 0 || static_cast<std::size_t>(file_offset) >= reader_.length() ||
        info->compressed_size < 0 ||
        static_cast<std::size_t>(info->compressed_size) > reader_.length() - static_cast<std::size_t>(file_offset)) {
        return std::nullopt;
    }

    const auto saved_position = reader_.position();
    reader_.seek(static_cast<std::size_t>(file_offset));
    auto chunk_data = reader_.readBytes(static_cast<std::size_t>(info->compressed_size));
    reader_.seek(saved_position);

    cached_chunk_data_[resource_id] = chunk_data;
    if (info->compressed_size != info->uncompressed_size && info->isZlibCompressed()) {
        chunk_data = io::BinaryReader::decompressZlib(chunk_data);
    }

    return chunk_data;
}

std::optional<std::vector<std::uint8_t>> AfterburnerReader::getChunkDataByType(const std::string& four_cc) {
    for (const auto& [resource_id, info] : chunk_info_map_) {
        if (info.four_cc == four_cc) {
            return getChunkData(resource_id);
        }
    }
    return std::nullopt;
}

std::vector<ChunkInfo> AfterburnerReader::getChunksByType(const std::string& four_cc) const {
    std::vector<ChunkInfo> result;
    for (const auto& [resource_id, info] : chunk_info_map_) {
        if (info.four_cc == four_cc) {
            result.push_back(info);
        }
    }
    return result;
}

void AfterburnerReader::clearCachedData() {
    cached_chunk_data_.clear();
}

int AfterburnerReader::directorVersion() const { return director_version_; }
int AfterburnerReader::imapVersion() const { return imap_version_; }
const std::string& AfterburnerReader::versionString() const { return version_string_; }
const std::vector<MoaID>& AfterburnerReader::compressionTypes() const { return compression_types_; }
int AfterburnerReader::chunkCount() const { return static_cast<int>(chunk_info_map_.size()); }

std::vector<ChunkInfo> AfterburnerReader::chunkInfos() const {
    std::vector<ChunkInfo> result;
    result.reserve(chunk_info_map_.size());
    for (const auto& [resource_id, info] : chunk_info_map_) {
        result.push_back(info);
    }
    return result;
}

const ChunkInfo* AfterburnerReader::getChunkInfo(int resource_id) const {
    if (const auto found = chunk_info_map_.find(resource_id); found != chunk_info_map_.end()) {
        return &found->second;
    }
    return nullptr;
}

std::string AfterburnerReader::readFourCCOrdered() {
    return readFourCCOrdered(reader_);
}

std::string AfterburnerReader::readFourCCOrdered(io::BinaryReader& reader) const {
    auto bytes = reader.readBytes(4);
    if (byte_order_ == io::ByteOrder::LittleEndian) {
        std::reverse(bytes.begin(), bytes.end());
    }
    return std::string(bytes.begin(), bytes.end());
}

int AfterburnerReader::readVarInt() {
    return readVarInt(reader_);
}

int AfterburnerReader::readVarInt(io::BinaryReader& reader) const {
    int value = 0;
    int byte = 0;
    do {
        byte = reader.readU8();
        value = (value << 7) | (byte & 0x7F);
    } while ((byte & 0x80) != 0);
    return value;
}

void AfterburnerReader::readFverChunk() {
    const auto four_cc = readFourCCOrdered();
    if (four_cc != "Fver") {
        throw std::runtime_error("Expected Fver chunk, got: " + four_cc);
    }

    const int length = readVarInt();
    const auto end_position = reader_.position() + static_cast<std::size_t>(length);
    if (end_position > reader_.length()) {
        throw std::runtime_error("Fver chunk extends past end of data");
    }

    imap_version_ = readVarInt();
    director_version_ = readVarInt();

    if (reader_.position() < end_position && reader_.bytesLeft() > 0) {
        const int string_length = readVarInt();
        const auto max_string_length = end_position - reader_.position();
        if (string_length > 0 && static_cast<std::size_t>(string_length) <= max_string_length && string_length < 10000) {
            auto bytes = reader_.readBytes(static_cast<std::size_t>(string_length));
            version_string_ = std::string(bytes.begin(), bytes.end());
            while (!version_string_.empty() && (version_string_.back() == ' ' || version_string_.back() == '\0')) {
                version_string_.pop_back();
            }
        }
    }

    reader_.seek(end_position);
}

void AfterburnerReader::readFcdrChunk() {
    const auto four_cc = readFourCCOrdered();
    if (four_cc != "Fcdr") {
        throw std::runtime_error("Expected Fcdr chunk, got: " + four_cc);
    }

    const int compressed_length = readVarInt();
    auto data = io::BinaryReader::decompressZlib(reader_.readBytes(static_cast<std::size_t>(compressed_length)));
    io::BinaryReader fcdr_reader(data, byte_order_);

    const int type_count = fcdr_reader.readU16();
    compression_types_.reserve(static_cast<std::size_t>(type_count));
    for (int index = 0; index < type_count; ++index) {
        compression_types_.push_back(MoaID::read(fcdr_reader));
    }

    for (int index = 0; index < type_count && !fcdr_reader.eof(); ++index) {
        while (!fcdr_reader.eof() && fcdr_reader.readU8() != 0) {
        }
    }
}

void AfterburnerReader::readAbmpChunk() {
    const auto four_cc = readFourCCOrdered();
    if (four_cc != "ABMP") {
        throw std::runtime_error("Expected ABMP chunk, got: " + four_cc);
    }

    const int chunk_length = readVarInt();
    const auto chunk_end_position = reader_.position() + static_cast<std::size_t>(chunk_length);
    (void)readVarInt();
    (void)readVarInt();

    if (chunk_end_position > reader_.length() || chunk_end_position < reader_.position()) {
        throw std::runtime_error("ABMP chunk extends past end of data");
    }

    auto data = io::BinaryReader::decompressZlib(reader_.readBytes(chunk_end_position - reader_.position()));
    io::BinaryReader abmp_reader(data, byte_order_);

    (void)readVarInt(abmp_reader);
    (void)readVarInt(abmp_reader);
    const int resource_count = readVarInt(abmp_reader);

    int running_offset = 0;
    for (int index = 0; index < resource_count; ++index) {
        const int resource_id = readVarInt(abmp_reader);
        const int raw_offset = readVarInt(abmp_reader);
        const int compressed_size = readVarInt(abmp_reader);
        const int uncompressed_size = readVarInt(abmp_reader);
        const int compression_type_index = readVarInt(abmp_reader);
        const auto tag_int = abmp_reader.readU32();
        const auto tag = io::BinaryReader::fourCCToString(tag_int);

        int offset = raw_offset;
        if (raw_offset == 0 && index == 0 && trimChunkTag(tag) == "ILS") {
            offset = 0;
        } else {
            offset = raw_offset;
            running_offset = offset + compressed_size;
        }
        (void)running_offset;

        const auto compression_type = compression_type_index >= 0 &&
                                     static_cast<std::size_t>(compression_type_index) < compression_types_.size()
            ? compression_types_[static_cast<std::size_t>(compression_type_index)]
            : MoaID::NULL_COMPRESSION;
        chunk_info_map_.insert_or_assign(resource_id,
                                       ChunkInfo{resource_id, tag, offset, compressed_size, uncompressed_size, compression_type});
    }
}

void AfterburnerReader::readIlsChunk() {
    const auto four_cc = readFourCCOrdered();
    if (four_cc != "FGEI") {
        throw std::runtime_error("Expected FGEI chunk, got: " + four_cc);
    }

    const auto* ils_info = getChunkInfo(2);
    if (!ils_info) {
        throw std::runtime_error("No ILS entry found in ABMP");
    }

    (void)readVarInt();
    ils_body_offset_ = static_cast<int>(reader_.position());
    ils_data_ = io::BinaryReader::decompressZlib(reader_.readBytes(static_cast<std::size_t>(ils_info->compressed_size)));

    io::BinaryReader ils_reader(ils_data_, byte_order_);
    while (!ils_reader.eof() && ils_reader.bytesLeft() > 0) {
        const int resource_id = readVarInt(ils_reader);
        const auto* info = getChunkInfo(resource_id);
        if (!info) {
            break;
        }

        const int data_length = info->compressed_size;
        if (data_length < 0) {
            break;
        }
        if (ils_reader.bytesLeft() < static_cast<std::size_t>(data_length)) {
            cached_chunk_data_[resource_id] = ils_reader.readBytes(ils_reader.bytesLeft());
            break;
        }

        cached_chunk_data_[resource_id] = ils_reader.readBytes(static_cast<std::size_t>(data_length));
    }
}

} // namespace libreshockwave::format
