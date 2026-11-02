#include "libreshockwave/player/render/pipeline/render_sprite.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "libreshockwave/cast/cast_member.hpp"
#include "libreshockwave/chunks/cast_member_chunk.hpp"

namespace libreshockwave::player::render::pipeline {
namespace {

int normalizeTransformAngle(double angle) {
    int normalized = static_cast<int>(std::lround(angle)) % 360;
    if (normalized < 0) {
        normalized += 360;
    }
    return normalized;
}

} // namespace

std::string_view name(SpriteType type) {
    switch (type) {
        case SpriteType::Bitmap: return "BITMAP";
        case SpriteType::Shape: return "SHAPE";
        case SpriteType::Text: return "TEXT";
        case SpriteType::Button: return "BUTTON";
        case SpriteType::FilmLoop: return "FILM_LOOP";
        case SpriteType::Shockwave3D: return "SHOCKWAVE_3D";
        case SpriteType::Unknown: return "UNKNOWN";
    }
    throw std::logic_error("Unknown SpriteType enum value");
}

RenderSprite::RenderSprite(int channel,
                           int x,
                           int y,
                           int width,
                           int height,
                           bool visible,
                           SpriteType type,
                           std::shared_ptr<const chunks::CastMemberChunk> cast_member,
                           int fore_color,
                           int back_color,
                           int ink,
                           int blend)
    : RenderSprite(channel,
                   x,
                   y,
                   width,
                   height,
                   0,
                   visible,
                   type,
                   std::move(cast_member),
                   nullptr,
                   fore_color,
                   back_color,
                   false,
                   false,
                   ink,
                   blend,
                   false,
                   false,
                   nullptr,
                   false) {}

RenderSprite::RenderSprite(int channel,
                           int x,
                           int y,
                           int width,
                           int height,
                           int loc_z,
                           bool visible,
                           SpriteType type,
                           std::shared_ptr<const chunks::CastMemberChunk> cast_member,
                           std::shared_ptr<const ::libreshockwave::cast::CastMember> dynamic_member,
                           int fore_color,
                           int back_color,
                           bool has_fore_color,
                           bool has_back_color,
                           int ink,
                           int blend,
                           bool flip_h,
                           bool flip_v,
                           std::shared_ptr<const bitmap::Bitmap> baked_bitmap,
                           bool has_behaviors)
    : RenderSprite(channel,
                   x,
                   y,
                   width,
                   height,
                   loc_z,
                   visible,
                   type,
                   std::move(cast_member),
                   std::move(dynamic_member),
                   fore_color,
                   back_color,
                   has_fore_color,
                   has_back_color,
                   ink,
                   blend,
                   flip_h,
                   flip_v,
                   0.0,
                   0.0,
                   std::move(baked_bitmap),
                   has_behaviors) {}

RenderSprite::RenderSprite(int channel,
                           int x,
                           int y,
                           int width,
                           int height,
                           int loc_z,
                           bool visible,
                           SpriteType type,
                           std::shared_ptr<const chunks::CastMemberChunk> cast_member,
                           std::shared_ptr<const ::libreshockwave::cast::CastMember> dynamic_member,
                           int fore_color,
                           int back_color,
                           bool has_fore_color,
                           bool has_back_color,
                           int ink,
                           int blend,
                           bool flip_h,
                           bool flip_v,
                           double rotation,
                           double skew,
                           std::shared_ptr<const bitmap::Bitmap> baked_bitmap,
                           bool has_behaviors)
    : channel_id_(channel),
      x_(x),
      y_(y),
      width_(width),
      height_(height),
      loc_z_(loc_z),
      visible_(visible),
      type_(type),
      cast_member_(std::move(cast_member)),
      dynamic_member_(std::move(dynamic_member)),
      fore_color_(fore_color),
      back_color_(back_color),
      has_fore_color_(has_fore_color),
      has_back_color_(has_back_color),
      ink_mode_(id::inkModeFromCode(ink)),
      blend_(blend),
      flip_h_(flip_h),
      flip_v_(flip_v),
      rotation_(rotation),
      skew_(skew),
      baked_bitmap_(std::move(baked_bitmap)),
      has_behaviors_(has_behaviors) {}

id::ChannelId RenderSprite::channelId() const { return channel_id_; }
int RenderSprite::channel() const { return channel_id_.value(); }
int RenderSprite::x() const { return x_; }
int RenderSprite::y() const { return y_; }
int RenderSprite::width() const { return width_; }
int RenderSprite::height() const { return height_; }
int RenderSprite::locZ() const { return loc_z_; }
bool RenderSprite::isVisible() const { return visible_; }
SpriteType RenderSprite::type() const { return type_; }
std::shared_ptr<const chunks::CastMemberChunk> RenderSprite::castMember() const { return cast_member_; }
std::shared_ptr<const ::libreshockwave::cast::CastMember> RenderSprite::dynamicMember() const {
    return dynamic_member_;
}
int RenderSprite::foreColor() const { return fore_color_; }
int RenderSprite::backColor() const { return back_color_; }
bool RenderSprite::hasForeColor() const { return has_fore_color_; }
bool RenderSprite::hasBackColor() const { return has_back_color_; }
id::InkMode RenderSprite::inkMode() const { return ink_mode_; }
int RenderSprite::ink() const { return id::code(ink_mode_); }
int RenderSprite::blend() const { return blend_; }
bool RenderSprite::isFlipH() const { return flip_h_; }
bool RenderSprite::isFlipV() const { return flip_v_; }
double RenderSprite::rotation() const { return rotation_; }
double RenderSprite::skew() const { return skew_; }
std::shared_ptr<const bitmap::Bitmap> RenderSprite::bakedBitmap() const { return baked_bitmap_; }
bool RenderSprite::hasBehaviors() const { return has_behaviors_; }
std::optional<int> RenderSprite::shapeLineSize() const { return shape_line_size_; }
std::optional<int> RenderSprite::shapePattern() const { return shape_pattern_; }

bool RenderSprite::hasDirectorHorizontalMirror() const {
    return normalizeTransformAngle(rotation_) == 180 && normalizeTransformAngle(skew_) == 180;
}

RenderSprite RenderSprite::withBakedBitmap(std::shared_ptr<const bitmap::Bitmap> baked) const {
    RenderSprite result(channel_id_.value(),
                        x_,
                        y_,
                        width_,
                        height_,
                        loc_z_,
                        visible_,
                        type_,
                        cast_member_,
                        dynamic_member_,
                        fore_color_,
                        back_color_,
                        has_fore_color_,
                        has_back_color_,
                        id::code(ink_mode_),
                        blend_,
                        flip_h_,
                        flip_v_,
                        rotation_,
                        skew_,
                        std::move(baked),
                        has_behaviors_);
    result.shape_line_size_ = shape_line_size_;
    result.shape_pattern_ = shape_pattern_;
    return result;
}

RenderSprite RenderSprite::withBakedBitmapAndSize(std::shared_ptr<const bitmap::Bitmap> baked,
                                                  int new_width,
                                                  int new_height) const {
    RenderSprite result(channel_id_.value(),
                        x_,
                        y_,
                        new_width,
                        new_height,
                        loc_z_,
                        visible_,
                        type_,
                        cast_member_,
                        dynamic_member_,
                        fore_color_,
                        back_color_,
                        has_fore_color_,
                        has_back_color_,
                        id::code(ink_mode_),
                        blend_,
                        flip_h_,
                        flip_v_,
                        rotation_,
                        skew_,
                        std::move(baked),
                        has_behaviors_);
    result.shape_line_size_ = shape_line_size_;
    result.shape_pattern_ = shape_pattern_;
    return result;
}

RenderSprite RenderSprite::withShapeLineSize(int line_size) const {
    RenderSprite result(*this);
    result.shape_line_size_ = line_size;
    return result;
}

RenderSprite RenderSprite::withShapePattern(int pattern) const {
    RenderSprite result(*this);
    result.shape_pattern_ = std::max(1, pattern);
    return result;
}

int RenderSprite::castMemberId() const {
    if (cast_member_) {
        return cast_member_->id().value();
    }
    if (dynamic_member_) {
        return dynamic_member_->memberNum();
    }
    return -1;
}

std::optional<std::string> RenderSprite::memberName() const {
    if (cast_member_) {
        return cast_member_->name();
    }
    if (dynamic_member_) {
        return dynamic_member_->name();
    }
    return std::nullopt;
}

} // namespace libreshockwave::player::render::pipeline
