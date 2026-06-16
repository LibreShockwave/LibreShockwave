#include "libreshockwave/format/AfterburnerReader.hpp"

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

AfterburnerReader::AfterburnerReader(io::BinaryReader reader, io::ByteOrder byteOrder)
    : reader_(std::move(reader)), byteOrder_(byteOrder) {
    reader_.setOrder(byteOrder_);
}

void AfterburnerReader::parse() {
    readFverChunk();
    readFcdrChunk();
    readAbmpChunk();
    readIlsChunk();
}

std::optional<std::vector<std::uint8_t>> AfterburnerReader::getChunkData(int resourceId) {
    if (resourceId == 2) {
        const auto* info = getChunkInfo(resourceId);
        if (info && trimChunkTag(info->fourCC) == "ILS") {
            return ilsData_;
        }
    }

    if (const auto cached = cachedChunkData_.find(resourceId); cached != cachedChunkData_.end()) {
        auto chunkData = cached->second;
        const auto* info = getChunkInfo(resourceId);
        if (info && info->compressedSize != info->uncompressedSize && info->isZlibCompressed()) {
            chunkData = io::BinaryReader::decompressZlib(chunkData);
        }
        return chunkData;
    }

    const auto* info = getChunkInfo(resourceId);
    if (!info) {
        return std::nullopt;
    }

    const int fileOffset = ilsBodyOffset_ + info->offset;
    if (fileOffset < 0 || static_cast<std::size_t>(fileOffset) >= reader_.length() ||
        info->compressedSize < 0 ||
        static_cast<std::size_t>(info->compressedSize) > reader_.length() - static_cast<std::size_t>(fileOffset)) {
        return std::nullopt;
    }

    const auto savedPosition = reader_.position();
    reader_.seek(static_cast<std::size_t>(fileOffset));
    auto chunkData = reader_.readBytes(static_cast<std::size_t>(info->compressedSize));
    reader_.seek(savedPosition);

    cachedChunkData_[resourceId] = chunkData;
    if (info->compressedSize != info->uncompressedSize && info->isZlibCompressed()) {
        chunkData = io::BinaryReader::decompressZlib(chunkData);
    }

    return chunkData;
}

std::optional<std::vector<std::uint8_t>> AfterburnerReader::getChunkDataByType(const std::string& fourCC) {
    for (const auto& [resourceId, info] : chunkInfoMap_) {
        if (info.fourCC == fourCC) {
            return getChunkData(resourceId);
        }
    }
    return std::nullopt;
}

std::vector<ChunkInfo> AfterburnerReader::getChunksByType(const std::string& fourCC) const {
    std::vector<ChunkInfo> result;
    for (const auto& [resourceId, info] : chunkInfoMap_) {
        if (info.fourCC == fourCC) {
            result.push_back(info);
        }
    }
    return result;
}

void AfterburnerReader::clearCachedData() {
    cachedChunkData_.clear();
}

int AfterburnerReader::directorVersion() const { return directorVersion_; }
int AfterburnerReader::imapVersion() const { return imapVersion_; }
const std::string& AfterburnerReader::versionString() const { return versionString_; }
const std::vector<MoaID>& AfterburnerReader::compressionTypes() const { return compressionTypes_; }
int AfterburnerReader::chunkCount() const { return static_cast<int>(chunkInfoMap_.size()); }

std::vector<ChunkInfo> AfterburnerReader::chunkInfos() const {
    std::vector<ChunkInfo> result;
    result.reserve(chunkInfoMap_.size());
    for (const auto& [resourceId, info] : chunkInfoMap_) {
        result.push_back(info);
    }
    return result;
}

const ChunkInfo* AfterburnerReader::getChunkInfo(int resourceId) const {
    if (const auto found = chunkInfoMap_.find(resourceId); found != chunkInfoMap_.end()) {
        return &found->second;
    }
    return nullptr;
}

std::string AfterburnerReader::readFourCCOrdered() {
    return readFourCCOrdered(reader_);
}

std::string AfterburnerReader::readFourCCOrdered(io::BinaryReader& reader) const {
    auto bytes = reader.readBytes(4);
    if (byteOrder_ == io::ByteOrder::LittleEndian) {
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
    const auto fourCC = readFourCCOrdered();
    if (fourCC != "Fver") {
        throw std::runtime_error("Expected Fver chunk, got: " + fourCC);
    }

    const int length = readVarInt();
    const auto endPosition = reader_.position() + static_cast<std::size_t>(length);
    if (endPosition > reader_.length()) {
        throw std::runtime_error("Fver chunk extends past end of data");
    }

    imapVersion_ = readVarInt();
    directorVersion_ = readVarInt();

    if (reader_.position() < endPosition && reader_.bytesLeft() > 0) {
        const int stringLength = readVarInt();
        const auto maxStringLength = endPosition - reader_.position();
        if (stringLength > 0 && static_cast<std::size_t>(stringLength) <= maxStringLength && stringLength < 10000) {
            auto bytes = reader_.readBytes(static_cast<std::size_t>(stringLength));
            versionString_ = std::string(bytes.begin(), bytes.end());
            while (!versionString_.empty() && (versionString_.back() == ' ' || versionString_.back() == '\0')) {
                versionString_.pop_back();
            }
        }
    }

    reader_.seek(endPosition);
}

void AfterburnerReader::readFcdrChunk() {
    const auto fourCC = readFourCCOrdered();
    if (fourCC != "Fcdr") {
        throw std::runtime_error("Expected Fcdr chunk, got: " + fourCC);
    }

    const int compressedLength = readVarInt();
    auto data = io::BinaryReader::decompressZlib(reader_.readBytes(static_cast<std::size_t>(compressedLength)));
    io::BinaryReader fcdrReader(data, byteOrder_);

    const int typeCount = fcdrReader.readU16();
    compressionTypes_.reserve(static_cast<std::size_t>(typeCount));
    for (int index = 0; index < typeCount; ++index) {
        compressionTypes_.push_back(MoaID::read(fcdrReader));
    }

    for (int index = 0; index < typeCount && !fcdrReader.eof(); ++index) {
        while (!fcdrReader.eof() && fcdrReader.readU8() != 0) {
        }
    }
}

void AfterburnerReader::readAbmpChunk() {
    const auto fourCC = readFourCCOrdered();
    if (fourCC != "ABMP") {
        throw std::runtime_error("Expected ABMP chunk, got: " + fourCC);
    }

    const int chunkLength = readVarInt();
    const auto chunkEndPosition = reader_.position() + static_cast<std::size_t>(chunkLength);
    (void)readVarInt();
    (void)readVarInt();

    if (chunkEndPosition > reader_.length() || chunkEndPosition < reader_.position()) {
        throw std::runtime_error("ABMP chunk extends past end of data");
    }

    auto data = io::BinaryReader::decompressZlib(reader_.readBytes(chunkEndPosition - reader_.position()));
    io::BinaryReader abmpReader(data, byteOrder_);

    (void)readVarInt(abmpReader);
    (void)readVarInt(abmpReader);
    const int resourceCount = readVarInt(abmpReader);

    int runningOffset = 0;
    for (int index = 0; index < resourceCount; ++index) {
        const int resourceId = readVarInt(abmpReader);
        const int rawOffset = readVarInt(abmpReader);
        const int compressedSize = readVarInt(abmpReader);
        const int uncompressedSize = readVarInt(abmpReader);
        const int compressionTypeIndex = readVarInt(abmpReader);
        const auto tagInt = abmpReader.readU32();
        const auto tag = io::BinaryReader::fourCCToString(tagInt);

        int offset = rawOffset;
        if (rawOffset == 0 && index == 0 && trimChunkTag(tag) == "ILS") {
            offset = 0;
        } else {
            offset = rawOffset;
            runningOffset = offset + compressedSize;
        }
        (void)runningOffset;

        const auto compressionType = compressionTypeIndex >= 0 &&
                                     static_cast<std::size_t>(compressionTypeIndex) < compressionTypes_.size()
            ? compressionTypes_[static_cast<std::size_t>(compressionTypeIndex)]
            : MoaID::NULL_COMPRESSION;
        chunkInfoMap_.insert_or_assign(resourceId,
                                       ChunkInfo{resourceId, tag, offset, compressedSize, uncompressedSize, compressionType});
    }
}

void AfterburnerReader::readIlsChunk() {
    const auto fourCC = readFourCCOrdered();
    if (fourCC != "FGEI") {
        throw std::runtime_error("Expected FGEI chunk, got: " + fourCC);
    }

    const auto* ilsInfo = getChunkInfo(2);
    if (!ilsInfo) {
        throw std::runtime_error("No ILS entry found in ABMP");
    }

    (void)readVarInt();
    ilsBodyOffset_ = static_cast<int>(reader_.position());
    ilsData_ = io::BinaryReader::decompressZlib(reader_.readBytes(static_cast<std::size_t>(ilsInfo->compressedSize)));

    io::BinaryReader ilsReader(ilsData_, byteOrder_);
    while (!ilsReader.eof() && ilsReader.bytesLeft() > 0) {
        const int resourceId = readVarInt(ilsReader);
        const auto* info = getChunkInfo(resourceId);
        if (!info) {
            break;
        }

        const int dataLength = info->compressedSize;
        if (dataLength < 0) {
            break;
        }
        if (ilsReader.bytesLeft() < static_cast<std::size_t>(dataLength)) {
            cachedChunkData_[resourceId] = ilsReader.readBytes(ilsReader.bytesLeft());
            break;
        }

        cachedChunkData_[resourceId] = ilsReader.readBytes(static_cast<std::size_t>(dataLength));
    }
}

} // namespace libreshockwave::format
