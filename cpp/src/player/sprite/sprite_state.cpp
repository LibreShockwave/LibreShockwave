#include "libreshockwave/player/sprite/sprite_state.hpp"

#include <cmath>
#include <utility>

namespace libreshockwave::player::sprite {

SpriteState::SpriteState(int channel)
    : channel_id_(channel),
      score_data_(std::nullopt),
      puppet_(true) {}

SpriteState::SpriteState(int channel, chunks::ScoreChunk::ChannelData data)
    : channel_id_(channel) {
    rebindToScore(std::move(data));
}

id::ChannelId SpriteState::channelId() const { return channel_id_; }
int SpriteState::channel() const { return channel_id_.value(); }
int SpriteState::locH() const { return loc_h_; }
int SpriteState::locV() const { return loc_v_; }
int SpriteState::locZ() const { return loc_z_; }
int SpriteState::width() const { return width_; }
int SpriteState::height() const { return height_; }
bool SpriteState::isVisible() const { return visible_; }
bool SpriteState::isPuppet() const { return puppet_; }
id::InkMode SpriteState::inkMode() const { return ink_mode_; }
int SpriteState::ink() const { return id::code(ink_mode_); }
int SpriteState::blend() const { return blend_; }
int SpriteState::trails() const { return trails_; }
int SpriteState::stretch() const { return stretch_; }
int SpriteState::foreColor() const { return fore_color_; }
int SpriteState::backColor() const { return back_color_; }
const std::optional<lingo::Datum>& SpriteState::colorDatum() const { return color_datum_; }
const std::optional<lingo::Datum>& SpriteState::bgColorDatum() const { return bg_color_datum_; }
bool SpriteState::hasForeColor() const { return has_fore_color_; }
bool SpriteState::hasBackColor() const { return has_back_color_; }

void SpriteState::setLocH(int loc_h) {
    loc_h_ = loc_h;
    loc_h_explicitly_set_ = true;
}

void SpriteState::setLocV(int loc_v) {
    loc_v_ = loc_v;
    loc_v_explicitly_set_ = true;
}

void SpriteState::setLocZ(int loc_z) {
    loc_z_ = loc_z;
    loc_z_explicitly_set_ = true;
}

void SpriteState::setWidth(int width) {
    width_ = width;
    has_size_changed_ = true;
}

void SpriteState::setHeight(int height) {
    height_ = height;
    has_size_changed_ = true;
}

void SpriteState::setVisible(bool visible) { visible_ = visible; }
void SpriteState::setPuppet(bool puppet) { puppet_ = puppet; }

void SpriteState::setInk(int ink) {
    ink_mode_ = id::inkModeFromCode(ink);
    ink_explicitly_set_ = true;
}

void SpriteState::setInkMode(id::InkMode ink) {
    ink_mode_ = ink;
    ink_explicitly_set_ = true;
}

void SpriteState::setBlend(int blend) {
    blend_ = blend;
    blend_explicitly_set_ = true;
}

void SpriteState::setTrails(int trails) {
    trails_ = trails;
    trails_explicitly_set_ = true;
}

void SpriteState::setStretch(int stretch) {
    stretch_ = stretch;
    stretch_explicitly_set_ = true;
}

void SpriteState::setForeColor(int fore_color) {
    fore_color_ = fore_color;
    color_datum_.reset();
    has_fore_color_ = true;
}

void SpriteState::setBackColor(int back_color) {
    back_color_ = back_color;
    bg_color_datum_.reset();
    has_back_color_ = true;
}

void SpriteState::setColorDatum(int fore_color, lingo::Datum color) {
    fore_color_ = fore_color;
    color_datum_ = std::move(color);
    has_fore_color_ = true;
}

void SpriteState::setBgColorDatum(int back_color, lingo::Datum color) {
    back_color_ = back_color;
    bg_color_datum_ = std::move(color);
    has_back_color_ = true;
}

bool SpriteState::isFlipH() const { return flip_h_; }
bool SpriteState::isFlipV() const { return flip_v_; }

void SpriteState::setFlipH(bool flip_h) {
    flip_h_ = flip_h;
    flip_h_explicitly_set_ = true;
}

void SpriteState::setFlipV(bool flip_v) {
    flip_v_ = flip_v;
    flip_v_explicitly_set_ = true;
}

double SpriteState::rotation() const { return rotation_; }
double SpriteState::skew() const { return skew_; }
void SpriteState::setRotation(double rotation) { rotation_ = rotation; }
void SpriteState::setSkew(double skew) { skew_ = skew; }

int SpriteState::cursor() const { return cursor_; }

void SpriteState::setCursor(int cursor) {
    cursor_ = cursor;
    cursor_member_num_ = 0;
    cursor_mask_num_ = 0;
}

int SpriteState::cursorMemberNum() const { return cursor_member_num_; }
int SpriteState::cursorMaskNum() const { return cursor_mask_num_; }
bool SpriteState::hasBitmapCursor() const { return cursor_member_num_ != 0; }

void SpriteState::setCursorMembers(int member, int mask) {
    cursor_member_num_ = member;
    cursor_mask_num_ = mask;
    cursor_ = 0;
}

const std::vector<lingo::Datum>& SpriteState::scriptInstanceList() const { return script_instance_list_; }
std::vector<lingo::Datum>& SpriteState::scriptInstanceList() { return script_instance_list_; }
bool SpriteState::hasScriptBehaviors() const { return !script_instance_list_.empty(); }

void SpriteState::setScriptInstanceList(std::vector<lingo::Datum> list) {
    script_instance_list_ = std::move(list);
}

std::optional<lingo::Datum> SpriteState::legacyProperty(std::string_view name) const {
    const auto found = legacy_properties_.find(std::string(name));
    return found == legacy_properties_.end() ? std::nullopt : std::optional<lingo::Datum>(found->second);
}

void SpriteState::setLegacyProperty(std::string name, lingo::Datum value) {
    legacy_properties_[std::move(name)] = std::move(value);
}

void SpriteState::clearLegacyProperties() {
    legacy_properties_.clear();
}

SpriteState::PositionSnapshot SpriteState::snapshotPosition() const {
    return PositionSnapshot{loc_h_, loc_v_, loc_z_, width_, height_};
}

void SpriteState::setDynamicMember(int cast_lib, int member) {
    dynamic_cast_lib_ = cast_lib;
    dynamic_cast_member_ = member;
    has_dynamic_member_ = true;
    if (member > 0) {
        member_cleared_ = false;
    }
}

void SpriteState::clearDynamicMember() {
    dynamic_cast_lib_ = -1;
    dynamic_cast_member_ = -1;
    has_dynamic_member_ = false;
}

int SpriteState::effectiveCastLib() const {
    if (has_dynamic_member_) {
        return dynamic_cast_lib_;
    }
    return score_data_.has_value() ? score_data_->cast_lib : 0;
}

int SpriteState::effectiveCastMember() const {
    if (has_dynamic_member_) {
        return dynamic_cast_member_;
    }
    return score_data_.has_value() ? score_data_->cast_member : 0;
}

bool SpriteState::hasDynamicMember() const { return has_dynamic_member_; }
bool SpriteState::isDynamic() const { return !score_data_.has_value(); }
bool SpriteState::hasSizeChanged() const { return has_size_changed_; }

bool SpriteState::isMemberCleared() const { return member_cleared_; }
void SpriteState::setMemberCleared(bool cleared) { member_cleared_ = cleared; }

void SpriteState::resetReleasedSpriteTransforms() {
    flip_h_ = false;
    flip_v_ = false;
    rotation_ = 0.0;
    skew_ = 0.0;
    flip_h_explicitly_set_ = false;
    flip_v_explicitly_set_ = false;
}

void SpriteState::resetReleasedChannelGeometry() {
    width_ = 1;
    height_ = 1;
    has_size_changed_ = false;
}

void SpriteState::applyIntrinsicSize(int width, int height) {
    if (!has_size_changed_ && width > 0 && height > 0) {
        width_ = width;
        height_ = height;
    }
}

void SpriteState::applyMemberAssignmentSize(int width, int height) {
    if (width > 0 && height > 0) {
        width_ = width;
        height_ = height;
        has_size_changed_ = false;
    }
}

void SpriteState::applyScoreDefaults(const chunks::ScoreChunk::ChannelData& data) {
    if (score_defaults_applied_) {
        return;
    }
    score_defaults_applied_ = true;
    if (!ink_explicitly_set_) {
        ink_mode_ = id::inkModeFromCode(data.ink);
    }
    if (!blend_explicitly_set_) {
        blend_ = scoreBlendPercent(data.blend_byte);
    }
    if (!trails_explicitly_set_) {
        trails_ = data.trails;
    }
    if (!stretch_explicitly_set_) {
        stretch_ = data.stretch;
    }
    if (!has_fore_color_) {
        fore_color_ = data.resolvedForeColor();
        has_fore_color_ = true;
    }
    if (!has_back_color_) {
        back_color_ = data.resolvedBackColor();
        has_back_color_ = true;
    }
}

void SpriteState::syncFromScore(const chunks::ScoreChunk::ChannelData& data) {
    score_data_ = data;
    if (!loc_h_explicitly_set_) {
        loc_h_ = data.pos_x;
    }
    if (!loc_v_explicitly_set_) {
        loc_v_ = data.pos_y;
    }
    if (!loc_z_explicitly_set_) {
        loc_z_ = 0;
    }
    if (!has_size_changed_ && data.width > 0 && data.height > 0) {
        width_ = data.width;
        height_ = data.height;
    }
    if (!flip_h_explicitly_set_) {
        flip_h_ = data.isFlipH();
    }
    if (!flip_v_explicitly_set_) {
        flip_v_ = data.isFlipV();
    }
    if (!ink_explicitly_set_) {
        ink_mode_ = id::inkModeFromCode(data.ink);
    }
    if (!blend_explicitly_set_) {
        blend_ = scoreBlendPercent(data.blend_byte);
    }
    if (!trails_explicitly_set_) {
        trails_ = data.trails;
    }
    if (!stretch_explicitly_set_) {
        stretch_ = data.stretch;
    }
    if (!has_fore_color_) {
        fore_color_ = data.resolvedForeColor();
    }
    if (!has_back_color_) {
        back_color_ = data.resolvedBackColor();
    }
}

bool SpriteState::matchesScoreIdentity(const chunks::ScoreChunk::ChannelData& data) const {
    if (!score_data_.has_value()) {
        return false;
    }
    return score_data_->cast_lib == data.cast_lib &&
           score_data_->cast_member == data.cast_member &&
           score_data_->sprite_type == data.sprite_type;
}

void SpriteState::rebindToScore(chunks::ScoreChunk::ChannelData data) {
    score_data_ = data;
    loc_h_ = data.pos_x;
    loc_v_ = data.pos_y;
    loc_z_ = 0;
    width_ = data.width;
    height_ = data.height;
    visible_ = true;
    puppet_ = false;
    ink_mode_ = id::inkModeFromCode(data.ink);
    blend_ = scoreBlendPercent(data.blend_byte);
    trails_ = data.trails;
    stretch_ = data.stretch;
    fore_color_ = data.resolvedForeColor();
    back_color_ = data.resolvedBackColor();
    color_datum_.reset();
    bg_color_datum_.reset();
    has_fore_color_ = false;
    has_back_color_ = false;
    has_size_changed_ = false;
    ink_explicitly_set_ = false;
    blend_explicitly_set_ = false;
    trails_explicitly_set_ = false;
    stretch_explicitly_set_ = false;
    loc_h_explicitly_set_ = false;
    loc_v_explicitly_set_ = false;
    loc_z_explicitly_set_ = false;
    flip_h_explicitly_set_ = false;
    flip_v_explicitly_set_ = false;
    score_defaults_applied_ = false;
    flip_h_ = data.isFlipH();
    flip_v_ = data.isFlipV();
    rotation_ = 0.0;
    skew_ = 0.0;
    cursor_ = 0;
    cursor_member_num_ = 0;
    cursor_mask_num_ = 0;
    script_instance_list_.clear();
    legacy_properties_.clear();
    dynamic_cast_lib_ = -1;
    dynamic_cast_member_ = -1;
    has_dynamic_member_ = false;
}

void SpriteState::rebindToScorePreservingScriptInstances(chunks::ScoreChunk::ChannelData data) {
    auto preserved = script_instance_list_;
    rebindToScore(std::move(data));
    script_instance_list_ = std::move(preserved);
}

const std::optional<chunks::ScoreChunk::ChannelData>& SpriteState::initialData() const {
    return score_data_;
}

int SpriteState::scoreBlendPercent(int blend_byte) {
    if (blend_byte <= 0) {
        return 100;
    }
    if (blend_byte >= 255) {
        return 0;
    }
    return static_cast<int>(std::lround(static_cast<float>(255 - blend_byte) * 100.0F / 255.0F));
}

} // namespace libreshockwave::player::sprite
