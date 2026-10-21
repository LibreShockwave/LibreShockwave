#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "libreshockwave/chunks/chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class ScoreChunk final : public Chunk {
public:
    struct Header {
        int total_length;
        int unk1;
        int unk2;
        int entry_count;
        int unk3;
        int entry_size_sum;
    };

    struct FrameDataHeader {
        int frame_count;
        int sprite_record_size;
        int num_channels;
        int frames_version;
    };

    struct TempoChannelData {
        int frame_index;
        int tempo;
    };

    struct PaletteChannelData {
        int frame_index;
        int cast_lib;
        int cast_member;
    };

    struct ChannelData {
        int sprite_type;
        int ink;
        int trails;
        int stretch;
        int fore_color;
        int back_color;
        int cast_lib;
        int cast_member;
        int unk1;
        int unk2;
        int pos_y;
        int pos_x;
        int height;
        int width;
        int color_flag;
        int blend_byte;
        int thickness_flags;
        int fore_color_g;
        int back_color_g;
        int fore_color_b;
        int back_color_b;

        [[nodiscard]] static ChannelData empty();
        [[nodiscard]] static ChannelData read(io::BinaryReader& reader, int sprite_record_size = 28);
        [[nodiscard]] bool isEmpty() const;
        [[nodiscard]] int resolvedForeColor() const;
        [[nodiscard]] int resolvedBackColor() const;
        [[nodiscard]] bool isForeColorRGB() const;
        [[nodiscard]] bool isBackColorRGB() const;
        [[nodiscard]] bool isFlipH() const;
        [[nodiscard]] bool isFlipV() const;
        [[nodiscard]] std::optional<id::CastLibId> castLibId() const;
        [[nodiscard]] std::optional<id::MemberId> memberId() const;
    };

    struct FrameChannelEntry {
        id::FrameIndex frame_index;
        id::ChannelId channel_index;
        ChannelData data;
    };

    struct ScoreFrameData {
        FrameDataHeader header;
        std::vector<std::uint8_t> decompressed_data;
        std::vector<FrameChannelEntry> frame_channel_data;
        std::vector<TempoChannelData> tempo_channel_data;
        std::vector<PaletteChannelData> palette_channel_data;

        [[nodiscard]] static ScoreFrameData empty();
    };

    struct FrameIntervalPrimary {
        int start_frame;
        int end_frame;
        int unk0;
        int unk1;
        int channel_index;
        int unk2;
        int unk3;
        int unk4;
        int unk5;
        int unk6;
        int unk7;
        int unk8;

        [[nodiscard]] id::FrameId startFrameId() const;
        [[nodiscard]] id::FrameId endFrameId() const;
        [[nodiscard]] id::ChannelId channelId() const;
    };

    struct FrameIntervalSecondary {
        int cast_lib;
        int cast_member;
        int unk0;

        [[nodiscard]] std::optional<id::CastLibId> castLibId() const;
        [[nodiscard]] std::optional<id::MemberId> memberId() const;
    };

    struct FrameInterval {
        FrameIntervalPrimary primary;
        std::optional<FrameIntervalSecondary> secondary;
    };

    ScoreChunk(const moviesource::MovieSource* file,
               id::ChunkId id,
               Header header,
               std::vector<std::vector<std::uint8_t>> entries,
               ScoreFrameData frame_data,
               std::vector<FrameInterval> frame_intervals);

    [[nodiscard]] const moviesource::MovieSource* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] const Header& header() const;
    [[nodiscard]] const std::vector<std::vector<std::uint8_t>>& entries() const;
    [[nodiscard]] const ScoreFrameData& frameData() const;
    [[nodiscard]] const std::vector<FrameInterval>& frameIntervals() const;
    [[nodiscard]] int getFrameCount() const;
    [[nodiscard]] int getChannelCount() const;
    [[nodiscard]] const std::vector<std::uint8_t>& getRawChannelData() const;
    [[nodiscard]] int getSpriteRecordSize() const;
    [[nodiscard]] int getFrameTempo(int frame) const;
    [[nodiscard]] std::optional<PaletteChannelData> getFramePalette(int frame) const;

    [[nodiscard]] static ScoreChunk read(const moviesource::MovieSource* file, io::BinaryReader& reader, id::ChunkId id, int version);

private:
    const moviesource::MovieSource* file_;
    id::ChunkId id_;
    Header header_;
    std::vector<std::vector<std::uint8_t>> entries_;
    ScoreFrameData frame_data_;
    std::vector<FrameInterval> frame_intervals_;
};

} // namespace libreshockwave::chunks
