#include "libreshockwave/moviesource/records/score_asset.hpp"

#include <algorithm>

#include "libreshockwave/util/string_utils.hpp"

namespace libreshockwave::moviesource {

ScoreAsset::ChannelData ScoreAsset::ChannelData::empty() {
    return ChannelData{};
}

bool ScoreAsset::ChannelData::isEmpty() const {
    if (width <= 0 || height <= 0) {
        return true;
    }
    return sprite_type == 0 && ink == 0 && fore_color == 0 && back_color == 0 &&
           cast_lib == 0 && cast_member == 0 && pos_y == 0 && pos_x == 0;
}

int ScoreAsset::ChannelData::resolvedForeColor() const {
    if ((color_flag & 0x1) != 0) {
        return (fore_color << 16) | (fore_color_g << 8) | fore_color_b;
    }
    return fore_color;
}

int ScoreAsset::ChannelData::resolvedBackColor() const {
    if ((color_flag & 0x2) != 0) {
        return (back_color << 16) | (back_color_g << 8) | back_color_b;
    }
    return back_color;
}

bool ScoreAsset::ChannelData::isForeColorRGB() const { return (color_flag & 0x1) != 0; }
bool ScoreAsset::ChannelData::isBackColorRGB() const { return (color_flag & 0x2) != 0; }
bool ScoreAsset::ChannelData::isFlipH() const { return (thickness_flags & 0x20) != 0; }
bool ScoreAsset::ChannelData::isFlipV() const { return (thickness_flags & 0x40) != 0; }

std::optional<id::CastLibId> ScoreAsset::ChannelData::castLibId() const {
    if (cast_lib > 0) {
        return id::CastLibId(cast_lib);
    }
    return std::nullopt;
}

std::optional<id::MemberId> ScoreAsset::ChannelData::memberId() const {
    if (cast_member > 0) {
        return id::MemberId(cast_member);
    }
    return std::nullopt;
}

ScoreAsset::ScoreFrameData ScoreAsset::ScoreFrameData::empty() {
    return ScoreFrameData{FrameDataHeader{0, 24, 0, 0}, {}, {}, {}, {}};
}

id::FrameId ScoreAsset::FrameIntervalPrimary::startFrameId() const {
    return id::FrameId(std::max(1, start_frame));
}

id::FrameId ScoreAsset::FrameIntervalPrimary::endFrameId() const {
    return id::FrameId(std::max(1, end_frame));
}

id::ChannelId ScoreAsset::FrameIntervalPrimary::channelId() const {
    return id::ChannelId(channel_index);
}

std::optional<id::CastLibId> ScoreAsset::FrameIntervalSecondary::castLibId() const {
    if (cast_lib > 0) {
        return id::CastLibId(cast_lib);
    }
    return std::nullopt;
}

std::optional<id::MemberId> ScoreAsset::FrameIntervalSecondary::memberId() const {
    if (cast_member > 0) {
        return id::MemberId(cast_member);
    }
    return std::nullopt;
}

int ScoreAsset::getFrameCount() const { return frame_data.header.frame_count; }
int ScoreAsset::getChannelCount() const { return frame_data.header.num_channels; }
const std::vector<std::uint8_t>& ScoreAsset::getRawChannelData() const {
    return frame_data.decompressed_data;
}
int ScoreAsset::getSpriteRecordSize() const { return frame_data.header.sprite_record_size; }

int ScoreAsset::getFrameTempo(int frame) const {
    int result = -1;
    for (const auto& tempo : frame_data.tempo_channel_data) {
        if (tempo.frame_index <= frame) {
            result = tempo.tempo;
        } else {
            break;
        }
    }
    return result;
}

std::optional<ScoreAsset::PaletteChannelData> ScoreAsset::getFramePalette(int frame) const {
    std::optional<PaletteChannelData> result;
    for (const auto& palette : frame_data.palette_channel_data) {
        if (palette.frame_index <= frame) {
            result = palette;
        } else {
            break;
        }
    }
    return result;
}

int FrameLabelAsset::getFrameByLabel(std::string_view label_name) const {
    for (const auto& label : labels) {
        if (util::equalsIgnoreCase(label.label, label_name)) {
            return label.frame_num.value();
        }
    }
    return -1;
}

std::string FrameLabelAsset::getLabelForFrame(int frame_num) const {
    for (const auto& label : labels) {
        if (label.frame_num.value() == frame_num) {
            return label.label;
        }
    }
    return "";
}

} // namespace libreshockwave::moviesource
