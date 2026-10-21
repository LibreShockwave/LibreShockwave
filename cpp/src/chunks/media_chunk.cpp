#include "libreshockwave/chunks/media_chunk.hpp"

#include <algorithm>

#include "libreshockwave/io/binary_reader.hpp"
#include "libreshockwave/util/audio_codec_utils.hpp"

namespace libreshockwave::chunks {

MediaChunk::MediaChunk(const moviesource::MovieSource* file,
                       id::ChunkId id,
                       int sample_rate,
                       int data_size_field,
                       std::optional<std::vector<std::uint8_t>> guid,
                       std::vector<std::uint8_t> audio_data,
                       std::string codec)
    : file_(file),
      id_(id),
      sample_rate_(sample_rate),
      data_size_field_(data_size_field),
      guid_(std::move(guid)),
      audio_data_(std::move(audio_data)),
      codec_(std::move(codec)) {}

const moviesource::MovieSource* MediaChunk::file() const { return file_; }
format::ChunkType MediaChunk::type() const { return format::ChunkType::ediM; }
id::ChunkId MediaChunk::id() const { return id_; }
int MediaChunk::sampleRate() const { return sample_rate_; }
int MediaChunk::dataSizeField() const { return data_size_field_; }
const std::optional<std::vector<std::uint8_t>>& MediaChunk::guid() const { return guid_; }
const std::vector<std::uint8_t>& MediaChunk::audioData() const { return audio_data_; }
const std::string& MediaChunk::codec() const { return codec_; }
bool MediaChunk::isMp3() const { return codec_ == "mp3"; }
bool MediaChunk::isAdpcm() const { return codec_ == "ima_adpcm"; }

SoundChunk MediaChunk::toSoundChunk() const {
    constexpr int bits_per_sample = 16;
    const int channel_count = isMp3() ? 2 : 1;
    int sample_count = 0;

    if (!isMp3() && !isAdpcm() && !audio_data_.empty()) {
        const int bytes_per_sample = bits_per_sample / 8;
        sample_count = static_cast<int>(audio_data_.size()) / (bytes_per_sample * channel_count);
    }

    return SoundChunk(file_, id_, sample_rate_, sample_count, bits_per_sample, channel_count, audio_data_, codec_);
}

MediaChunk MediaChunk::read(const moviesource::MovieSource* file, io::BinaryReader& reader, id::ChunkId id) {
    const auto total_size = reader.bytesLeft();

    if (total_size < 4) {
        auto data = reader.readBytes(total_size);
        return MediaChunk(file, id, 22050, 0, std::nullopt, data, detectCodec(data, std::nullopt, 0));
    }

    const auto start_position = reader.position();
    auto peek = reader.readBytes(std::min<std::size_t>(4, total_size));
    reader.setPosition(start_position);

    bool is_raw_mp3 = false;
    if (peek.size() >= 3 && peek[0] == 'I' && peek[1] == 'D' && peek[2] == '3') {
        is_raw_mp3 = true;
    } else if (peek.size() >= 2 && peek[0] == 0xFF && (peek[1] & 0xE0U) == 0xE0U) {
        is_raw_mp3 = true;
    }

    if (is_raw_mp3) {
        auto audio_data = reader.readBytes(total_size);
        return MediaChunk(file, id, 22050, 0, std::nullopt, std::move(audio_data), "mp3");
    }

    if (total_size < 24) {
        auto data = reader.readBytes(total_size);
        return MediaChunk(file, id, 22050, 0, std::nullopt, data, detectCodec(data, std::nullopt, 0));
    }

    const auto original_order = reader.order();
    reader.setOrder(io::ByteOrder::BigEndian);
    const int header_size = reader.readI32();

    if (header_size > static_cast<int>(total_size) || header_size < 24) {
        reader.setOrder(original_order);
        reader.setPosition(start_position);
        auto audio_data = reader.readBytes(total_size);
        return MediaChunk(file, id, 22050, 0, std::nullopt, audio_data, detectCodec(audio_data, std::nullopt, 0));
    }

    (void)reader.readI32();
    int sample_rate = reader.readI32();
    (void)reader.readI32();
    (void)reader.readI32();
    const int data_size_field = reader.readI32();

    int bytes_read = 24;
    int skip_bytes = std::max(0, header_size - bytes_read);
    std::optional<std::vector<std::uint8_t>> guid;
    if (skip_bytes >= 16) {
        guid = reader.readBytes(16);
        skip_bytes -= 16;
    }
    if (skip_bytes > 0 && reader.bytesLeft() >= static_cast<std::size_t>(skip_bytes)) {
        reader.skip(static_cast<std::size_t>(skip_bytes));
    }

    auto audio_data = reader.readBytes(reader.bytesLeft());
    reader.setOrder(original_order);

    if (sample_rate <= 0 || sample_rate > 96000) {
        sample_rate = 22050;
    }

    const auto codec = detectCodec(audio_data, guid, data_size_field);
    return MediaChunk(file, id, sample_rate, data_size_field, std::move(guid), std::move(audio_data), codec);
}

std::string MediaChunk::detectCodec(const std::vector<std::uint8_t>& data,
                                    const std::optional<std::vector<std::uint8_t>>& guid,
                                    int data_size_field) {
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

    if (!data.empty() && data_size_field > 0) {
        const auto compression_ratio = static_cast<float>(data_size_field) / static_cast<float>(data.size());
        if (compression_ratio > 2.0F) {
            return "ima_adpcm";
        }
    }

    return "raw_pcm";
}

} // namespace libreshockwave::chunks
