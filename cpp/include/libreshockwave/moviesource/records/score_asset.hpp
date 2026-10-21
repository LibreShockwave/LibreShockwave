#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/id/ids.hpp"

namespace libreshockwave::moviesource {

// A score asset. Mirrors the score data a runtime needs: frame channel data,
// tempo, palette channels, and frame intervals.
class ScoreAsset {
public:
    struct Header {
        int total_length = 0;
        int unk1 = 0;
        int unk2 = 0;
        int entry_count = 0;
        int unk3 = 0;
        int entry_size_sum = 0;
    };

    struct FrameDataHeader {
        int frame_count = 0;
        int sprite_record_size = 0;
        int num_channels = 0;
        int frames_version = 0;
    };

    struct TempoChannelData {
        int frame_index = 0;
        int tempo = 0;
    };

    struct PaletteChannelData {
        int frame_index = 0;
        int cast_lib = 0;
        int cast_member = 0;
    };

    struct ChannelData {
        int sprite_type = 0;
        int ink = 0;
        int trails = 0;
        int stretch = 0;
        int fore_color = 0;
        int back_color = 0;
        int cast_lib = 0;
        int cast_member = 0;
        int unk1 = 0;
        int unk2 = 0;
        int pos_y = 0;
        int pos_x = 0;
        int height = 0;
        int width = 0;
        int color_flag = 0;
        int blend_byte = 0;
        int thickness_flags = 0;
        int fore_color_g = 0;
        int back_color_g = 0;
        int fore_color_b = 0;
        int back_color_b = 0;

        [[nodiscard]] static ChannelData empty();
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
        id::FrameIndex frame_index{0};
        id::ChannelId channel_index{0};
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
        int start_frame = 0;
        int end_frame = 0;
        int unk0 = 0;
        int unk1 = 0;
        int channel_index = 0;
        int unk2 = 0;
        int unk3 = 0;
        int unk4 = 0;
        int unk5 = 0;
        int unk6 = 0;
        int unk7 = 0;
        int unk8 = 0;

        [[nodiscard]] id::FrameId startFrameId() const;
        [[nodiscard]] id::FrameId endFrameId() const;
        [[nodiscard]] id::ChannelId channelId() const;
    };

    struct FrameIntervalSecondary {
        int cast_lib = 0;
        int cast_member = 0;
        int unk0 = 0;

        [[nodiscard]] std::optional<id::CastLibId> castLibId() const;
        [[nodiscard]] std::optional<id::MemberId> memberId() const;
    };

    struct FrameInterval {
        FrameIntervalPrimary primary;
        std::optional<FrameIntervalSecondary> secondary;
    };

    Header header;
    std::vector<std::vector<std::uint8_t>> entries;
    ScoreFrameData frame_data;
    std::vector<FrameInterval> frame_intervals;

    [[nodiscard]] int getFrameCount() const;
    [[nodiscard]] int getChannelCount() const;
    [[nodiscard]] const std::vector<std::uint8_t>& getRawChannelData() const;
    [[nodiscard]] int getSpriteRecordSize() const;
    [[nodiscard]] int getFrameTempo(int frame) const;
    [[nodiscard]] std::optional<PaletteChannelData> getFramePalette(int frame) const;
};

// A frame label asset: the ordered frame labels.
struct FrameLabelAsset {
    struct FrameLabel {
        id::FrameId frame_num{0};
        std::string label;
    };
    std::vector<FrameLabel> labels;

    [[nodiscard]] int getFrameByLabel(std::string_view label_name) const;
    [[nodiscard]] std::string getLabelForFrame(int frame_num) const;
};

} // namespace libreshockwave::moviesource
