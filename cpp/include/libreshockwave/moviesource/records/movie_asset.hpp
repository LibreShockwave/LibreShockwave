#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "libreshockwave/id/ids.hpp"

namespace libreshockwave::moviesource {

// A movie configuration asset. Mirrors the Director config chunk.
struct ConfigAsset {
    int director_version = 0;
    int stage_top = 0;
    int stage_left = 0;
    int stage_bottom = 0;
    int stage_right = 0;
    int min_member = 0;
    int max_member = 0;
    int tempo = 0;
    int bg_color = 0;
    int stage_color = 0;
    int stage_color_rgb = 0;
    int comment_font = 0;
    int comment_size = 0;
    int comment_style = 0;
    int default_palette_cast_lib = 0;
    int default_palette_member = 0;
    int movie_version = 0;
    std::int16_t platform = 0;

    [[nodiscard]] int stageWidth() const { return stage_right - stage_left; }
    [[nodiscard]] int stageHeight() const { return stage_bottom - stage_top; }
};

// A key table asset. Maps owner chunk ids to linked section ids by fourcc.
class KeyTableAsset {
public:
    struct Entry {
        id::ChunkId section_id{0};
        id::ChunkId cast_id{0};
        std::uint32_t fourcc = 0;

        [[nodiscard]] std::string fourccString() const;
    };

    std::vector<Entry> entries;

    // Build the owner and section indexes. Call after populating entries.
    void indexEntries();

    [[nodiscard]] std::vector<Entry> getEntriesForOwner(id::ChunkId owner_id) const;
    [[nodiscard]] std::optional<Entry> findEntry(id::ChunkId owner_id, std::uint32_t fourcc) const;
    [[nodiscard]] std::optional<id::ChunkId> getOwnerCastId(id::ChunkId section_id) const;
    [[nodiscard]] std::optional<Entry> getEntryBySectionId(id::ChunkId section_id) const;

private:
    [[nodiscard]] static int sanitizeChunkId(int value);
    std::unordered_map<int, std::vector<Entry>> entries_by_owner_;
    std::unordered_map<int, int> owner_by_section_id_;
};

// A font map asset: font id to font name.
struct FontMapAsset {
    struct Entry {
        int font_id = 0;
        int platform = 0;
        std::string font_name;
    };
    std::vector<Entry> entries;

    [[nodiscard]] std::optional<std::string> fontNameForId(int font_id) const;
};

// A text asset: the text payload and its styled runs.
struct TextAsset {
    struct TextRun {
        int start_offset = 0;
        int end_offset = 0;
        int font_id = 0;
        int font_size = 0;
        int font_style = 0;
        int color_r = 0;
        int color_g = 0;
        int color_b = 0;
    };
    std::string text;
    std::vector<TextRun> runs;
};

// A sound asset: decoded audio parameters and payload.
struct SoundAsset {
    int sample_rate = 0;
    int sample_count = 0;
    int bits_per_sample = 0;
    int channel_count = 0;
    std::vector<std::uint8_t> audio_data;
    std::string codec = "raw_pcm";

    [[nodiscard]] bool isMp3() const;
    [[nodiscard]] bool isAdpcm() const;
    [[nodiscard]] double durationSeconds() const;
};

// A media asset: a media container entry (audio with optional guid).
struct MediaAsset {
    int sample_rate = 0;
    int data_size_field = 0;
    std::optional<std::vector<std::uint8_t>> guid;
    std::vector<std::uint8_t> audio_data;
    std::string codec;

    [[nodiscard]] bool isMp3() const;
    [[nodiscard]] bool isAdpcm() const;
    [[nodiscard]] SoundAsset toSoundAsset() const;
};

} // namespace libreshockwave::moviesource
