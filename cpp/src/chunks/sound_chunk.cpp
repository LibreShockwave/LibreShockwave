#include "libreshockwave/chunks/sound_chunk.hpp"

#include <cmath>

#include "libreshockwave/io/binary_reader.hpp"
#include "libreshockwave/util/audio_codec_utils.hpp"

namespace libreshockwave::chunks {

SoundChunk::SoundChunk(const moviesource::MovieSource* file,
                       id::ChunkId id,
                       int sample_rate,
                       int sample_count,
                       int bits_per_sample,
                       int channel_count,
                       std::vector<std::uint8_t> audio_data,
                       std::string codec)
    : file_(file),
      id_(id),
      sample_rate_(sample_rate),
      sample_count_(sample_count),
      bits_per_sample_(bits_per_sample),
      channel_count_(channel_count),
      audio_data_(std::move(audio_data)),
      codec_(std::move(codec)) {}

const moviesource::MovieSource* SoundChunk::file() const { return file_; }
format::ChunkType SoundChunk::type() const { return format::ChunkType::snd_; }
id::ChunkId SoundChunk::id() const { return id_; }
int SoundChunk::sampleRate() const { return sample_rate_; }
int SoundChunk::sampleCount() const { return sample_count_; }
int SoundChunk::bitsPerSample() const { return bits_per_sample_; }
int SoundChunk::channelCount() const { return channel_count_; }
const std::vector<std::uint8_t>& SoundChunk::audioData() const { return audio_data_; }
const std::string& SoundChunk::codec() const { return codec_; }
bool SoundChunk::isMp3() const { return codec_ == "mp3"; }
bool SoundChunk::isAdpcm() const { return codec_ == "ima_adpcm"; }

double SoundChunk::durationSeconds() const {
    if (sample_rate_ == 0) {
        return 0.0;
    }
    if (sample_count_ > 0) {
        return static_cast<double>(sample_count_) / sample_rate_;
    }
    int bytes_per_sample = bits_per_sample_ / 8;
    if (bytes_per_sample == 0) {
        bytes_per_sample = 2;
    }
    const int samples = static_cast<int>(audio_data_.size()) / (bytes_per_sample * channel_count_);
    return static_cast<double>(samples) / sample_rate_;
}

SoundChunk SoundChunk::read(const moviesource::MovieSource* file, io::BinaryReader& reader, id::ChunkId id) {
    const auto total_size = reader.bytesLeft();
    if (total_size < 64) {
        return SoundChunk(file, id, 22050, 0, 16, 1, {}, "raw_pcm");
    }

    auto all_data = reader.readBytes(total_size);
    int rate_a = 22050;
    int rate_b = 0;
    int rate_c = 0;

    if (all_data.size() >= 8) {
        const auto encoded_rate =
            (static_cast<std::uint32_t>(all_data[4]) << 24) |
            (static_cast<std::uint32_t>(all_data[5]) << 16) |
            (static_cast<std::uint32_t>(all_data[6]) << 8) |
            static_cast<std::uint32_t>(all_data[7]);
        rate_a = static_cast<int>(std::lround(encoded_rate / 6.144));
        if (rate_a > 15990 && rate_a < 16020) {
            rate_a = 16000;
        }
    }

    if (all_data.size() >= 0x18) {
        rate_b = (static_cast<int>(all_data[0x16]) << 8) | all_data[0x17];
    }
    if (all_data.size() >= 0x2C) {
        rate_c = (static_cast<int>(all_data[0x2A]) << 8) | all_data[0x2B];
    }

    int sample_rate = 22050;
    if (rate_b == 22050) {
        sample_rate = 22050;
    } else if (rate_c == 44100) {
        sample_rate = 44100;
    } else if (rate_a >= 8000 && rate_a <= 48000) {
        sample_rate = rate_a;
    }

    const std::string codec = detectCodec(all_data);
    constexpr int bits_per_sample = 16;
    constexpr int channel_count = 1;
    int sample_count = 0;
    if (codec != "mp3" && all_data.size() > 64) {
        sample_count = (static_cast<int>(all_data.size()) - 64) / ((bits_per_sample / 8) * channel_count);
    }

    return SoundChunk(file, id, sample_rate, sample_count, bits_per_sample, channel_count, std::move(all_data), codec);
}

std::string SoundChunk::detectCodec(const std::vector<std::uint8_t>& data) {
    return util::containsMp3SyncFrame(data, 512) ? "mp3" : "raw_pcm";
}

} // namespace libreshockwave::chunks
