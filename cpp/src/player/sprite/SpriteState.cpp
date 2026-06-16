#include "libreshockwave/player/sprite/SpriteState.hpp"

#include <cmath>
#include <utility>

namespace libreshockwave::player::sprite {

SpriteState::SpriteState(int channel)
    : channelId_(channel),
      scoreData_(std::nullopt),
      puppet_(true) {}

SpriteState::SpriteState(int channel, chunks::ScoreChunk::ChannelData data)
    : channelId_(channel) {
    rebindToScore(std::move(data));
}

id::ChannelId SpriteState::channelId() const { return channelId_; }
int SpriteState::channel() const { return channelId_.value(); }
int SpriteState::locH() const { return locH_; }
int SpriteState::locV() const { return locV_; }
int SpriteState::locZ() const { return locZ_; }
int SpriteState::width() const { return width_; }
int SpriteState::height() const { return height_; }
bool SpriteState::isVisible() const { return visible_; }
bool SpriteState::isPuppet() const { return puppet_; }
id::InkMode SpriteState::inkMode() const { return inkMode_; }
int SpriteState::ink() const { return id::code(inkMode_); }
int SpriteState::blend() const { return blend_; }
int SpriteState::trails() const { return trails_; }
int SpriteState::stretch() const { return stretch_; }
int SpriteState::foreColor() const { return foreColor_; }
int SpriteState::backColor() const { return backColor_; }
const std::optional<lingo::Datum>& SpriteState::colorDatum() const { return colorDatum_; }
const std::optional<lingo::Datum>& SpriteState::bgColorDatum() const { return bgColorDatum_; }
bool SpriteState::hasForeColor() const { return hasForeColor_; }
bool SpriteState::hasBackColor() const { return hasBackColor_; }

void SpriteState::setLocH(int locH) {
    locH_ = locH;
    locHExplicitlySet_ = true;
}

void SpriteState::setLocV(int locV) {
    locV_ = locV;
    locVExplicitlySet_ = true;
}

void SpriteState::setLocZ(int locZ) {
    locZ_ = locZ;
    locZExplicitlySet_ = true;
}

void SpriteState::setWidth(int width) {
    width_ = width;
    hasSizeChanged_ = true;
}

void SpriteState::setHeight(int height) {
    height_ = height;
    hasSizeChanged_ = true;
}

void SpriteState::setVisible(bool visible) { visible_ = visible; }
void SpriteState::setPuppet(bool puppet) { puppet_ = puppet; }

void SpriteState::setInk(int ink) {
    inkMode_ = id::inkModeFromCode(ink);
    inkExplicitlySet_ = true;
}

void SpriteState::setInkMode(id::InkMode ink) {
    inkMode_ = ink;
    inkExplicitlySet_ = true;
}

void SpriteState::setBlend(int blend) {
    blend_ = blend;
    blendExplicitlySet_ = true;
}

void SpriteState::setTrails(int trails) {
    trails_ = trails;
    trailsExplicitlySet_ = true;
}

void SpriteState::setStretch(int stretch) {
    stretch_ = stretch;
    stretchExplicitlySet_ = true;
}

void SpriteState::setForeColor(int foreColor) {
    foreColor_ = foreColor;
    colorDatum_.reset();
    hasForeColor_ = true;
}

void SpriteState::setBackColor(int backColor) {
    backColor_ = backColor;
    bgColorDatum_.reset();
    hasBackColor_ = true;
}

void SpriteState::setColorDatum(int foreColor, lingo::Datum color) {
    foreColor_ = foreColor;
    colorDatum_ = std::move(color);
    hasForeColor_ = true;
}

void SpriteState::setBgColorDatum(int backColor, lingo::Datum color) {
    backColor_ = backColor;
    bgColorDatum_ = std::move(color);
    hasBackColor_ = true;
}

bool SpriteState::isFlipH() const { return flipH_; }
bool SpriteState::isFlipV() const { return flipV_; }

void SpriteState::setFlipH(bool flipH) {
    flipH_ = flipH;
    flipHExplicitlySet_ = true;
}

void SpriteState::setFlipV(bool flipV) {
    flipV_ = flipV;
    flipVExplicitlySet_ = true;
}

double SpriteState::rotation() const { return rotation_; }
double SpriteState::skew() const { return skew_; }
void SpriteState::setRotation(double rotation) { rotation_ = rotation; }
void SpriteState::setSkew(double skew) { skew_ = skew; }

int SpriteState::cursor() const { return cursor_; }

void SpriteState::setCursor(int cursor) {
    cursor_ = cursor;
    cursorMemberNum_ = 0;
    cursorMaskNum_ = 0;
}

int SpriteState::cursorMemberNum() const { return cursorMemberNum_; }
int SpriteState::cursorMaskNum() const { return cursorMaskNum_; }
bool SpriteState::hasBitmapCursor() const { return cursorMemberNum_ != 0; }

void SpriteState::setCursorMembers(int member, int mask) {
    cursorMemberNum_ = member;
    cursorMaskNum_ = mask;
    cursor_ = 0;
}

const std::vector<lingo::Datum>& SpriteState::scriptInstanceList() const { return scriptInstanceList_; }
std::vector<lingo::Datum>& SpriteState::scriptInstanceList() { return scriptInstanceList_; }
bool SpriteState::hasScriptBehaviors() const { return !scriptInstanceList_.empty(); }

void SpriteState::setScriptInstanceList(std::vector<lingo::Datum> list) {
    scriptInstanceList_ = std::move(list);
}

std::optional<lingo::Datum> SpriteState::legacyProperty(std::string_view name) const {
    const auto found = legacyProperties_.find(std::string(name));
    return found == legacyProperties_.end() ? std::nullopt : std::optional<lingo::Datum>(found->second);
}

void SpriteState::setLegacyProperty(std::string name, lingo::Datum value) {
    legacyProperties_[std::move(name)] = std::move(value);
}

void SpriteState::clearLegacyProperties() {
    legacyProperties_.clear();
}

SpriteState::PositionSnapshot SpriteState::snapshotPosition() const {
    return PositionSnapshot{locH_, locV_, locZ_, width_, height_};
}

void SpriteState::setDynamicMember(int castLib, int member) {
    dynamicCastLib_ = castLib;
    dynamicCastMember_ = member;
    hasDynamicMember_ = true;
}

void SpriteState::clearDynamicMember() {
    dynamicCastLib_ = -1;
    dynamicCastMember_ = -1;
    hasDynamicMember_ = false;
}

int SpriteState::effectiveCastLib() const {
    if (hasDynamicMember_) {
        return dynamicCastLib_;
    }
    return scoreData_.has_value() ? scoreData_->castLib : 0;
}

int SpriteState::effectiveCastMember() const {
    if (hasDynamicMember_) {
        return dynamicCastMember_;
    }
    return scoreData_.has_value() ? scoreData_->castMember : 0;
}

bool SpriteState::hasDynamicMember() const { return hasDynamicMember_; }
bool SpriteState::isDynamic() const { return !scoreData_.has_value(); }
bool SpriteState::hasSizeChanged() const { return hasSizeChanged_; }

void SpriteState::resetReleasedSpriteTransforms() {
    flipH_ = false;
    flipV_ = false;
    rotation_ = 0.0;
    skew_ = 0.0;
    flipHExplicitlySet_ = false;
    flipVExplicitlySet_ = false;
}

void SpriteState::resetReleasedChannelGeometry() {
    width_ = 1;
    height_ = 1;
    hasSizeChanged_ = false;
}

void SpriteState::applyIntrinsicSize(int width, int height) {
    if (!hasSizeChanged_ && width > 0 && height > 0) {
        width_ = width;
        height_ = height;
    }
}

void SpriteState::applyMemberAssignmentSize(int width, int height) {
    if (width > 0 && height > 0) {
        width_ = width;
        height_ = height;
        hasSizeChanged_ = false;
    }
}

void SpriteState::applyScoreDefaults(const chunks::ScoreChunk::ChannelData& data) {
    if (scoreDefaultsApplied_) {
        return;
    }
    scoreDefaultsApplied_ = true;
    if (!inkExplicitlySet_) {
        inkMode_ = id::inkModeFromCode(data.ink);
    }
    if (!blendExplicitlySet_) {
        blend_ = scoreBlendPercent(data.blendByte);
    }
    if (!trailsExplicitlySet_) {
        trails_ = data.trails;
    }
    if (!stretchExplicitlySet_) {
        stretch_ = data.stretch;
    }
    if (!hasForeColor_) {
        foreColor_ = data.resolvedForeColor();
        hasForeColor_ = true;
    }
    if (!hasBackColor_) {
        backColor_ = data.resolvedBackColor();
        hasBackColor_ = true;
    }
}

void SpriteState::syncFromScore(const chunks::ScoreChunk::ChannelData& data) {
    scoreData_ = data;
    if (!locHExplicitlySet_) {
        locH_ = data.posX;
    }
    if (!locVExplicitlySet_) {
        locV_ = data.posY;
    }
    if (!locZExplicitlySet_) {
        locZ_ = 0;
    }
    if (!hasSizeChanged_ && data.width > 0 && data.height > 0) {
        width_ = data.width;
        height_ = data.height;
    }
    if (!flipHExplicitlySet_) {
        flipH_ = data.isFlipH();
    }
    if (!flipVExplicitlySet_) {
        flipV_ = data.isFlipV();
    }
    if (!inkExplicitlySet_) {
        inkMode_ = id::inkModeFromCode(data.ink);
    }
    if (!blendExplicitlySet_) {
        blend_ = scoreBlendPercent(data.blendByte);
    }
    if (!trailsExplicitlySet_) {
        trails_ = data.trails;
    }
    if (!stretchExplicitlySet_) {
        stretch_ = data.stretch;
    }
    if (!hasForeColor_) {
        foreColor_ = data.resolvedForeColor();
    }
    if (!hasBackColor_) {
        backColor_ = data.resolvedBackColor();
    }
}

bool SpriteState::matchesScoreIdentity(const chunks::ScoreChunk::ChannelData& data) const {
    if (!scoreData_.has_value()) {
        return false;
    }
    return scoreData_->castLib == data.castLib &&
           scoreData_->castMember == data.castMember &&
           scoreData_->spriteType == data.spriteType;
}

void SpriteState::rebindToScore(chunks::ScoreChunk::ChannelData data) {
    scoreData_ = data;
    locH_ = data.posX;
    locV_ = data.posY;
    locZ_ = 0;
    width_ = data.width;
    height_ = data.height;
    visible_ = true;
    puppet_ = false;
    inkMode_ = id::inkModeFromCode(data.ink);
    blend_ = scoreBlendPercent(data.blendByte);
    trails_ = data.trails;
    stretch_ = data.stretch;
    foreColor_ = data.resolvedForeColor();
    backColor_ = data.resolvedBackColor();
    colorDatum_.reset();
    bgColorDatum_.reset();
    hasForeColor_ = false;
    hasBackColor_ = false;
    hasSizeChanged_ = false;
    inkExplicitlySet_ = false;
    blendExplicitlySet_ = false;
    trailsExplicitlySet_ = false;
    stretchExplicitlySet_ = false;
    locHExplicitlySet_ = false;
    locVExplicitlySet_ = false;
    locZExplicitlySet_ = false;
    flipHExplicitlySet_ = false;
    flipVExplicitlySet_ = false;
    scoreDefaultsApplied_ = false;
    flipH_ = data.isFlipH();
    flipV_ = data.isFlipV();
    rotation_ = 0.0;
    skew_ = 0.0;
    cursor_ = 0;
    cursorMemberNum_ = 0;
    cursorMaskNum_ = 0;
    scriptInstanceList_.clear();
    legacyProperties_.clear();
    dynamicCastLib_ = -1;
    dynamicCastMember_ = -1;
    hasDynamicMember_ = false;
}

void SpriteState::rebindToScorePreservingScriptInstances(chunks::ScoreChunk::ChannelData data) {
    auto preserved = scriptInstanceList_;
    rebindToScore(std::move(data));
    scriptInstanceList_ = std::move(preserved);
}

const std::optional<chunks::ScoreChunk::ChannelData>& SpriteState::initialData() const {
    return scoreData_;
}

int SpriteState::scoreBlendPercent(int blendByte) {
    if (blendByte <= 0) {
        return 100;
    }
    if (blendByte >= 255) {
        return 0;
    }
    return static_cast<int>(std::lround(static_cast<float>(255 - blendByte) * 100.0F / 255.0F));
}

} // namespace libreshockwave::player::sprite
