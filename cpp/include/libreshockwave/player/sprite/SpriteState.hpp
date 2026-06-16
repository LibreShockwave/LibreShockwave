#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "libreshockwave/chunks/ScoreChunk.hpp"
#include "libreshockwave/id/Ids.hpp"
#include "libreshockwave/lingo/Datum.hpp"

namespace libreshockwave::player::sprite {

class SpriteState {
public:
    struct PositionSnapshot {
        int locH{0};
        int locV{0};
        int locZ{0};
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

    void setLocH(int locH);
    void setLocV(int locV);
    void setLocZ(int locZ);
    void setWidth(int width);
    void setHeight(int height);
    void setVisible(bool visible);
    void setPuppet(bool puppet);
    void setInk(int ink);
    void setInkMode(id::InkMode ink);
    void setBlend(int blend);
    void setTrails(int trails);
    void setStretch(int stretch);
    void setForeColor(int foreColor);
    void setBackColor(int backColor);
    void setColorDatum(int foreColor, lingo::Datum color);
    void setBgColorDatum(int backColor, lingo::Datum color);

    [[nodiscard]] bool isFlipH() const;
    [[nodiscard]] bool isFlipV() const;
    void setFlipH(bool flipH);
    void setFlipV(bool flipV);
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

    void setDynamicMember(int castLib, int member);
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
    [[nodiscard]] static int scoreBlendPercent(int blendByte);

private:
    id::ChannelId channelId_;
    std::optional<chunks::ScoreChunk::ChannelData> scoreData_;

    int locH_{0};
    int locV_{0};
    int locZ_{0};
    int width_{0};
    int height_{0};
    bool visible_{true};
    bool puppet_{false};
    id::InkMode inkMode_{id::InkMode::COPY};
    int blend_{100};
    int trails_{0};
    int stretch_{0};
    int foreColor_{0};
    int backColor_{0xFFFFFF};
    std::optional<lingo::Datum> colorDatum_;
    std::optional<lingo::Datum> bgColorDatum_;
    bool hasForeColor_{false};
    bool hasBackColor_{false};
    bool hasSizeChanged_{false};
    bool inkExplicitlySet_{false};
    bool blendExplicitlySet_{false};
    bool trailsExplicitlySet_{false};
    bool stretchExplicitlySet_{false};
    bool locHExplicitlySet_{false};
    bool locVExplicitlySet_{false};
    bool locZExplicitlySet_{false};
    bool flipHExplicitlySet_{false};
    bool flipVExplicitlySet_{false};
    bool scoreDefaultsApplied_{false};
    bool flipH_{false};
    bool flipV_{false};
    double rotation_{0.0};
    double skew_{0.0};
    int cursor_{0};
    int cursorMemberNum_{0};
    int cursorMaskNum_{0};
    std::vector<lingo::Datum> scriptInstanceList_;
    std::unordered_map<std::string, lingo::Datum> legacyProperties_;
    int dynamicCastLib_{-1};
    int dynamicCastMember_{-1};
    bool hasDynamicMember_{false};
};

} // namespace libreshockwave::player::sprite
