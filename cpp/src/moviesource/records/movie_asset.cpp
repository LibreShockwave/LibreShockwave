#include "libreshockwave/moviesource/records/movie_asset.hpp"

#include <algorithm>

#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::moviesource {

std::string KeyTableAsset::Entry::fourccString() const {
    return io::BinaryReader::fourCCToString(fourcc);
}

int KeyTableAsset::sanitizeChunkId(int value) {
    return std::max(0, value);
}

void KeyTableAsset::indexEntries() {
    entries_by_owner_.clear();
    owner_by_section_id_.clear();
    for (const auto& entry : entries) {
        entries_by_owner_[entry.cast_id.value()].push_back(entry);
        owner_by_section_id_[entry.section_id.value()] = entry.cast_id.value();
    }
}

std::vector<KeyTableAsset::Entry> KeyTableAsset::getEntriesForOwner(id::ChunkId owner_id) const {
    if (const auto found = entries_by_owner_.find(owner_id.value()); found != entries_by_owner_.end()) {
        return found->second;
    }
    return {};
}

std::optional<KeyTableAsset::Entry> KeyTableAsset::findEntry(id::ChunkId owner_id, std::uint32_t fourcc) const {
    const auto entries = getEntriesForOwner(owner_id);
    for (const auto& entry : entries) {
        if (entry.fourcc == fourcc) {
            return entry;
        }
    }
    return std::nullopt;
}

std::optional<id::ChunkId> KeyTableAsset::getOwnerCastId(id::ChunkId section_id) const {
    if (const auto found = owner_by_section_id_.find(section_id.value()); found != owner_by_section_id_.end()) {
        return id::ChunkId(found->second);
    }
    return std::nullopt;
}

std::optional<KeyTableAsset::Entry> KeyTableAsset::getEntryBySectionId(id::ChunkId section_id) const {
    if (!getOwnerCastId(section_id).has_value()) {
        return std::nullopt;
    }
    for (const auto& entry : entries) {
        if (entry.section_id.value() == section_id.value()) {
            return entry;
        }
    }
    return std::nullopt;
}

std::optional<std::string> FontMapAsset::fontNameForId(int font_id) const {
    for (const auto& entry : entries) {
        if (entry.font_id == font_id) {
            return entry.font_name;
        }
    }
    return std::nullopt;
}

bool SoundAsset::isMp3() const { return codec == "mp3"; }
bool SoundAsset::isAdpcm() const { return codec == "ima_adpcm"; }

double SoundAsset::durationSeconds() const {
    if (sample_rate == 0) {
        return 0.0;
    }
    if (sample_count > 0) {
        return static_cast<double>(sample_count) / sample_rate;
    }
    int bytes_per_sample = bits_per_sample / 8;
    if (bytes_per_sample == 0) {
        bytes_per_sample = 2;
    }
    const int samples = static_cast<int>(audio_data.size()) / (bytes_per_sample * channel_count);
    return static_cast<double>(samples) / sample_rate;
}

bool MediaAsset::isMp3() const { return codec == "mp3"; }
bool MediaAsset::isAdpcm() const { return codec == "ima_adpcm"; }

SoundAsset MediaAsset::toSoundAsset() const {
    constexpr int kBitsPerSample = 16;
    const int channels = isMp3() ? 2 : 1;
    int sample_count = 0;
    if (!isMp3() && !isAdpcm() && !audio_data.empty()) {
        const int bytes_per_sample = kBitsPerSample / 8;
        sample_count = static_cast<int>(audio_data.size()) / (bytes_per_sample * channels);
    }
    SoundAsset sound;
    sound.sample_rate = sample_rate;
    sound.sample_count = sample_count;
    sound.bits_per_sample = kBitsPerSample;
    sound.channel_count = channels;
    sound.audio_data = audio_data;
    sound.codec = codec;
    return sound;
}

} // namespace libreshockwave::moviesource
