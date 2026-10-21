#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "libreshockwave/chunks/score_chunk.hpp"
#include "libreshockwave/moviesource/records/score_asset.hpp"
#include "libreshockwave/id/ids.hpp"
#include "libreshockwave/lingo/datum.hpp"

namespace libreshockwave::player::sprite {

class SpriteState {
public:
    struct PositionSnapshot {
        int loc_h{0};
        int loc_v{0};
        int loc_z{0};
        int width{0};
        int height{0};

        friend bool operator==(const PositionSnapshot&, const PositionSnapshot&) = default;
    };

    explicit SpriteState(int channel);
    SpriteState(int channel, chunks::ScoreChunk::ChannelData data);

    [[nodiscard]] id::ChannelId channelId() const;
    [[nodiscard]] int channel() const;
    [[nodiscard]] int locH() const;
    [[nodiscard]] int locV() const;
    [[nodiscard]] int locZ() const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] bool isVisible() const;
    [[nodiscard]] bool isPuppet() const;
    [[nodiscard]] id::InkMode inkMode() const;
    [[nodiscard]] int ink() const;
    [[nodiscard]] int blend() const;
    [[nodiscard]] int trails() const;
    [[nodiscard]] int stretch() const;
    [[nodiscard]] int foreColor() const;
    [[nodiscard]] int backColor() const;
    [[nodiscard]] const std::optional<lingo::Datum>& colorDatum() const;
    [[nodiscard]] const std::optional<lingo::Datum>& bgColorDatum() const;
    [[nodiscard]] bool hasForeColor() const;
    [[nodiscard]] bool hasBackColor() const;

    void setLocH(int loc_h);
    void setLocV(int loc_v);
    void setLocZ(int loc_z);
    void setWidth(int width);
    void setHeight(int height);
    void setVisible(bool visible);
    void setPuppet(bool puppet);
    void setInk(int ink);
    void setInkMode(id::InkMode ink);
    void setBlend(int blend);
    void setTrails(int trails);
    void setStretch(int stretch);
    void setForeColor(int fore_color);
    void setBackColor(int back_color);
    void setColorDatum(int fore_color, lingo::Datum color);
    void setBgColorDatum(int back_color, lingo::Datum color);

    [[nodiscard]] bool isFlipH() const;
    [[nodiscard]] bool isFlipV() const;
    void setFlipH(bool flip_h);
    void setFlipV(bool flip_v);
    [[nodiscard]] double rotation() const;
    [[nodiscard]] double skew() const;
    void setRotation(double rotation);
    void setSkew(double skew);

    [[nodiscard]] int cursor() const;
    void setCursor(int cursor);
    [[nodiscard]] int cursorMemberNum() const;
    [[nodiscard]] int cursorMaskNum() const;
    [[nodiscard]] bool hasBitmapCursor() const;
    void setCursorMembers(int member, int mask);

    [[nodiscard]] const std::vector<lingo::Datum>& scriptInstanceList() const;
    [[nodiscard]] std::vector<lingo::Datum>& scriptInstanceList();
    [[nodiscard]] bool hasScriptBehaviors() const;
    void setScriptInstanceList(std::vector<lingo::Datum> list);
    [[nodiscard]] std::optional<lingo::Datum> legacyProperty(std::string_view name) const;
    void setLegacyProperty(std::string name, lingo::Datum value);
    void clearLegacyProperties();

    [[nodiscard]] PositionSnapshot snapshotPosition() const;

    void setDynamicMember(int cast_lib, int member);
    void clearDynamicMember();
    [[nodiscard]] int effectiveCastLib() const;
    [[nodiscard]] int effectiveCastMember() const;
    [[nodiscard]] bool hasDynamicMember() const;
    [[nodiscard]] bool isDynamic() const;
    [[nodiscard]] bool hasSizeChanged() const;

    void resetReleasedSpriteTransforms();
    void resetReleasedChannelGeometry();
    void applyIntrinsicSize(int width, int height);
    void applyMemberAssignmentSize(int width, int height);
    void applyScoreDefaults(const chunks::ScoreChunk::ChannelData& data);
    void syncFromScore(const chunks::ScoreChunk::ChannelData& data);
    [[nodiscard]] bool matchesScoreIdentity(const chunks::ScoreChunk::ChannelData& data) const;
    void rebindToScore(chunks::ScoreChunk::ChannelData data);
    void rebindToScorePreservingScriptInstances(chunks::ScoreChunk::ChannelData data);

    [[nodiscard]] const std::optional<chunks::ScoreChunk::ChannelData>& initialData() const;
    [[nodiscard]] static int scoreBlendPercent(int blend_byte);

private:
    id::ChannelId channel_id_;
    std::optional<chunks::ScoreChunk::ChannelData> score_data_;

    int loc_h_{0};
    int loc_v_{0};
    int loc_z_{0};
    int width_{0};
    int height_{0};
    bool visible_{true};
    bool puppet_{false};
    id::InkMode ink_mode_{id::InkMode::COPY};
    int blend_{100};
    int trails_{0};
    int stretch_{0};
    int fore_color_{0};
    int back_color_{0xFFFFFF};
    std::optional<lingo::Datum> color_datum_;
    std::optional<lingo::Datum> bg_color_datum_;
    bool has_fore_color_{false};
    bool has_back_color_{false};
    bool has_size_changed_{false};
    bool ink_explicitly_set_{false};
    bool blend_explicitly_set_{false};
    bool trails_explicitly_set_{false};
    bool stretch_explicitly_set_{false};
    bool loc_h_explicitly_set_{false};
    bool loc_v_explicitly_set_{false};
    bool loc_z_explicitly_set_{false};
    bool flip_h_explicitly_set_{false};
    bool flip_v_explicitly_set_{false};
    bool score_defaults_applied_{false};
    bool flip_h_{false};
    bool flip_v_{false};
    double rotation_{0.0};
    double skew_{0.0};
    int cursor_{0};
    int cursor_member_num_{0};
    int cursor_mask_num_{0};
    std::vector<lingo::Datum> script_instance_list_;
    std::unordered_map<std::string, lingo::Datum> legacy_properties_;
    int dynamic_cast_lib_{-1};
    int dynamic_cast_member_{-1};
    bool has_dynamic_member_{false};
};

} // namespace libreshockwave::player::sprite
