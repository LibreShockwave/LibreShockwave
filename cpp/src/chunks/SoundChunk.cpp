#include "libreshockwave/chunks/SoundChunk.hpp"

#include <cmath>

#include "libreshockwave/io/BinaryReader.hpp"
#include "libreshockwave/util/AudioCodecUtils.hpp"

namespace libreshockwave::chunks {

SoundChunk::SoundChunk(const DirectorFile* file,
                       id::ChunkId id,
                       int sampleRate,
                       int sampleCount,
                       int bitsPerSample,
                       int channelCount,
                       std::vector<std::uint8_t> audioData,
                       std::string codec)
    : file_(file),
      id_(id),
      sampleRate_(sampleRate),
      sampleCount_(sampleCount),
      bitsPerSample_(bitsPerSample),
      channelCount_(channelCount),
      audioData_(std::move(audioData)),
      codec_(std::move(codec)) {}

const DirectorFile* SoundChunk::file() const { return file_; }
format::ChunkType SoundChunk::type() const { return format::ChunkType::snd_; }
id::ChunkId SoundChunk::id() const { return id_; }
int SoundChunk::sampleRate() const { return sampleRate_; }
int SoundChunk::sampleCount() const { return sampleCount_; }
int SoundChunk::bitsPerSample() const { return bitsPerSample_; }
int SoundChunk::channelCount() const { return channelCount_; }
const std::vector<std::uint8_t>& SoundChunk::audioData() const { return audioData_; }
const std::string& SoundChunk::codec() const { return codec_; }
bool SoundChunk::isMp3() const { return codec_ == "mp3"; }
bool SoundChunk::isAdpcm() const { return codec_ == "ima_adpcm"; }

double SoundChunk::durationSeconds() const {
    if (sampleRate_ == 0) {
        return 0.0;
    }
    if (sampleCount_ > 0) {
        return static_cast<double>(sampleCount_) / sampleRate_;
    }
    int bytesPerSample = bitsPerSample_ / 8;
    if (bytesPerSample == 0) {
        bytesPerSample = 2;
    }
    const int samples = static_cast<int>(audioData_.size()) / (bytesPerSample * channelCount_);
    return static_cast<double>(samples) / sampleRate_;
}

SoundChunk SoundChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id) {
    const auto totalSize = reader.bytesLeft();
    if (totalSize < 64) {
        return SoundChunk(file, id, 22050, 0, 16, 1, {}, "raw_pcm");
    }

    auto allData = reader.readBytes(totalSize);
    int rateA = 22050;
    int rateB = 0;
    int rateC = 0;

    if (allData.size() >= 8) {
        const auto encodedRate =
            (static_cast<std::uint32_t>(allData[4]) << 24) |
            (static_cast<std::uint32_t>(allData[5]) << 16) |
            (static_cast<std::uint32_t>(allData[6]) << 8) |
            static_cast<std::uint32_t>(allData[7]);
        rateA = static_cast<int>(std::lround(encodedRate / 6.144));
        if (rateA > 15990 && rateA < 16020) {
            rateA = 16000;
        }
    }

    if (allData.size() >= 0x18) {
        rateB = (static_cast<int>(allData[0x16]) << 8) | allData[0x17];
    }
    if (allData.size() >= 0x2C) {
        rateC = (static_cast<int>(allData[0x2A]) << 8) | allData[0x2B];
    }

    int sampleRate = 22050;
    if (rateB == 22050) {
        sampleRate = 22050;
    } else if (rateC == 44100) {
        sampleRate = 44100;
    } else if (rateA >= 8000 && rateA <= 48000) {
        sampleRate = rateA;
    }

    const std::string codec = detectCodec(allData);
    constexpr int bitsPerSample = 16;
    constexpr int channelCount = 1;
    int sampleCount = 0;
    if (codec != "mp3" && allData.size() > 64) {
        sampleCount = (static_cast<int>(allData.size()) - 64) / ((bitsPerSample / 8) * channelCount);
    }

    return SoundChunk(file, id, sampleRate, sampleCount, bitsPerSample, channelCount, std::move(allData), codec);
}

std::string SoundChunk::detectCodec(const std::vector<std::uint8_t>& data) {
    return util::containsMp3SyncFrame(data, 512) ? "mp3" : "raw_pcm";
}

} // namespace libreshockwave::chunks
