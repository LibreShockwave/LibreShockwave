#include "libreshockwave/chunks/MediaChunk.hpp"

#include <algorithm>

#include "libreshockwave/io/BinaryReader.hpp"
#include "libreshockwave/util/AudioCodecUtils.hpp"

namespace libreshockwave::chunks {

MediaChunk::MediaChunk(const DirectorFile* file,
                       id::ChunkId id,
                       int sampleRate,
                       int dataSizeField,
                       std::optional<std::vector<std::uint8_t>> guid,
                       std::vector<std::uint8_t> audioData,
                       std::string codec)
    : file_(file),
      id_(id),
      sampleRate_(sampleRate),
      dataSizeField_(dataSizeField),
      guid_(std::move(guid)),
      audioData_(std::move(audioData)),
      codec_(std::move(codec)) {}

const DirectorFile* MediaChunk::file() const { return file_; }
format::ChunkType MediaChunk::type() const { return format::ChunkType::ediM; }
id::ChunkId MediaChunk::id() const { return id_; }
int MediaChunk::sampleRate() const { return sampleRate_; }
int MediaChunk::dataSizeField() const { return dataSizeField_; }
const std::optional<std::vector<std::uint8_t>>& MediaChunk::guid() const { return guid_; }
const std::vector<std::uint8_t>& MediaChunk::audioData() const { return audioData_; }
const std::string& MediaChunk::codec() const { return codec_; }
bool MediaChunk::isMp3() const { return codec_ == "mp3"; }
bool MediaChunk::isAdpcm() const { return codec_ == "ima_adpcm"; }

SoundChunk MediaChunk::toSoundChunk() const {
    constexpr int bitsPerSample = 16;
    const int channelCount = isMp3() ? 2 : 1;
    int sampleCount = 0;

    if (!isMp3() && !isAdpcm() && !audioData_.empty()) {
        const int bytesPerSample = bitsPerSample / 8;
        sampleCount = static_cast<int>(audioData_.size()) / (bytesPerSample * channelCount);
    }

    return SoundChunk(file_, id_, sampleRate_, sampleCount, bitsPerSample, channelCount, audioData_, codec_);
}

MediaChunk MediaChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id) {
    const auto totalSize = reader.bytesLeft();

    if (totalSize < 4) {
        auto data = reader.readBytes(totalSize);
        return MediaChunk(file, id, 22050, 0, std::nullopt, data, detectCodec(data, std::nullopt, 0));
    }

    const auto startPosition = reader.position();
    auto peek = reader.readBytes(std::min<std::size_t>(4, totalSize));
    reader.setPosition(startPosition);

    bool isRawMp3 = false;
    if (peek.size() >= 3 && peek[0] == 'I' && peek[1] == 'D' && peek[2] == '3') {
        isRawMp3 = true;
    } else if (peek.size() >= 2 && peek[0] == 0xFF && (peek[1] & 0xE0U) == 0xE0U) {
        isRawMp3 = true;
    }

    if (isRawMp3) {
        auto audioData = reader.readBytes(totalSize);
        return MediaChunk(file, id, 22050, 0, std::nullopt, std::move(audioData), "mp3");
    }

    if (totalSize < 24) {
        auto data = reader.readBytes(totalSize);
        return MediaChunk(file, id, 22050, 0, std::nullopt, data, detectCodec(data, std::nullopt, 0));
    }

    const auto originalOrder = reader.order();
    reader.setOrder(io::ByteOrder::BigEndian);
    const int headerSize = reader.readI32();

    if (headerSize > static_cast<int>(totalSize) || headerSize < 24) {
        reader.setOrder(originalOrder);
        reader.setPosition(startPosition);
        auto audioData = reader.readBytes(totalSize);
        return MediaChunk(file, id, 22050, 0, std::nullopt, audioData, detectCodec(audioData, std::nullopt, 0));
    }

    (void)reader.readI32();
    int sampleRate = reader.readI32();
    (void)reader.readI32();
    (void)reader.readI32();
    const int dataSizeField = reader.readI32();

    int bytesRead = 24;
    int skipBytes = std::max(0, headerSize - bytesRead);
    std::optional<std::vector<std::uint8_t>> guid;
    if (skipBytes >= 16) {
        guid = reader.readBytes(16);
        skipBytes -= 16;
    }
    if (skipBytes > 0 && reader.bytesLeft() >= static_cast<std::size_t>(skipBytes)) {
        reader.skip(static_cast<std::size_t>(skipBytes));
    }

    auto audioData = reader.readBytes(reader.bytesLeft());
    reader.setOrder(originalOrder);

    if (sampleRate <= 0 || sampleRate > 96000) {
        sampleRate = 22050;
    }

    const auto codec = detectCodec(audioData, guid, dataSizeField);
    return MediaChunk(file, id, sampleRate, dataSizeField, std::move(guid), std::move(audioData), codec);
}

std::string MediaChunk::detectCodec(const std::vector<std::uint8_t>& data,
                                    const std::optional<std::vector<std::uint8_t>>& guid,
                                    int dataSizeField) {
    if (data.size() < 4) {
        return "raw_pcm";
    }

    if (guid.has_value() && guid->size() >= 8 &&
        (*guid)[0] == 0x5A && (*guid)[1] == 0x08 && (*guid)[2] == 0xCD && (*guid)[3] == 0x40 &&
        (*guid)[4] == 0x53 && (*guid)[5] == 0x5B && (*guid)[6] == 0x11 && (*guid)[7] == 0xD0) {
        return "ima_adpcm";
    }

    if (data.size() >= 10 && data[0] == 'I' && data[1] == 'D' && data[2] == '3') {
        return "mp3";
    }

    if (util::containsMp3SyncFrame(data, 512)) {
        return "mp3";
    }

    if (!data.empty() && dataSizeField > 0) {
        const auto compressionRatio = static_cast<float>(dataSizeField) / static_cast<float>(data.size());
        if (compressionRatio > 2.0F) {
            return "ima_adpcm";
        }
    }

    return "raw_pcm";
}

} // namespace libreshockwave::chunks
