#include "libreshockwave/player/render/pipeline/RenderSprite.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "libreshockwave/cast/CastMember.hpp"
#include "libreshockwave/chunks/CastMemberChunk.hpp"

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
                           std::shared_ptr<const chunks::CastMemberChunk> castMember,
                           int foreColor,
                           int backColor,
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
                   std::move(castMember),
                   nullptr,
                   foreColor,
                   backColor,
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
                           int locZ,
                           bool visible,
                           SpriteType type,
                           std::shared_ptr<const chunks::CastMemberChunk> castMember,
                           std::shared_ptr<const ::libreshockwave::cast::CastMember> dynamicMember,
                           int foreColor,
                           int backColor,
                           bool hasForeColor,
                           bool hasBackColor,
                           int ink,
                           int blend,
                           bool flipH,
                           bool flipV,
                           std::shared_ptr<const bitmap::Bitmap> bakedBitmap,
                           bool hasBehaviors)
    : RenderSprite(channel,
                   x,
                   y,
                   width,
                   height,
                   locZ,
                   visible,
                   type,
                   std::move(castMember),
                   std::move(dynamicMember),
                   foreColor,
                   backColor,
                   hasForeColor,
                   hasBackColor,
                   ink,
                   blend,
                   flipH,
                   flipV,
                   0.0,
                   0.0,
                   std::move(bakedBitmap),
                   hasBehaviors) {}

RenderSprite::RenderSprite(int channel,
                           int x,
                           int y,
                           int width,
                           int height,
                           int locZ,
                           bool visible,
                           SpriteType type,
                           std::shared_ptr<const chunks::CastMemberChunk> castMember,
                           std::shared_ptr<const ::libreshockwave::cast::CastMember> dynamicMember,
                           int foreColor,
                           int backColor,
                           bool hasForeColor,
                           bool hasBackColor,
                           int ink,
                           int blend,
                           bool flipH,
                           bool flipV,
                           double rotation,
                           double skew,
                           std::shared_ptr<const bitmap::Bitmap> bakedBitmap,
                           bool hasBehaviors)
    : channelId_(channel),
      x_(x),
      y_(y),
      width_(width),
      height_(height),
      locZ_(locZ),
      visible_(visible),
      type_(type),
      castMember_(std::move(castMember)),
      dynamicMember_(std::move(dynamicMember)),
      foreColor_(foreColor),
      backColor_(backColor),
      hasForeColor_(hasForeColor),
      hasBackColor_(hasBackColor),
      inkMode_(id::inkModeFromCode(ink)),
      blend_(blend),
      flipH_(flipH),
      flipV_(flipV),
      rotation_(rotation),
      skew_(skew),
      bakedBitmap_(std::move(bakedBitmap)),
      hasBehaviors_(hasBehaviors) {}

id::ChannelId RenderSprite::channelId() const { return channelId_; }
int RenderSprite::channel() const { return channelId_.value(); }
int RenderSprite::x() const { return x_; }
int RenderSprite::y() const { return y_; }
int RenderSprite::width() const { return width_; }
int RenderSprite::height() const { return height_; }
int RenderSprite::locZ() const { return locZ_; }
bool RenderSprite::isVisible() const { return visible_; }
SpriteType RenderSprite::type() const { return type_; }
std::shared_ptr<const chunks::CastMemberChunk> RenderSprite::castMember() const { return castMember_; }
std::shared_ptr<const ::libreshockwave::cast::CastMember> RenderSprite::dynamicMember() const {
    return dynamicMember_;
}
int RenderSprite::foreColor() const { return foreColor_; }
int RenderSprite::backColor() const { return backColor_; }
bool RenderSprite::hasForeColor() const { return hasForeColor_; }
bool RenderSprite::hasBackColor() const { return hasBackColor_; }
id::InkMode RenderSprite::inkMode() const { return inkMode_; }
int RenderSprite::ink() const { return id::code(inkMode_); }
int RenderSprite::blend() const { return blend_; }
bool RenderSprite::isFlipH() const { return flipH_; }
bool RenderSprite::isFlipV() const { return flipV_; }
double RenderSprite::rotation() const { return rotation_; }
double RenderSprite::skew() const { return skew_; }
std::shared_ptr<const bitmap::Bitmap> RenderSprite::bakedBitmap() const { return bakedBitmap_; }
bool RenderSprite::hasBehaviors() const { return hasBehaviors_; }
std::optional<int> RenderSprite::shapeLineSize() const { return shapeLineSize_; }
std::optional<int> RenderSprite::shapePattern() const { return shapePattern_; }

bool RenderSprite::hasDirectorHorizontalMirror() const {
    return normalizeTransformAngle(rotation_) == 180 && normalizeTransformAngle(skew_) == 180;
}

RenderSprite RenderSprite::withBakedBitmap(std::shared_ptr<const bitmap::Bitmap> baked) const {
    RenderSprite result(channelId_.value(),
                        x_,
                        y_,
                        width_,
                        height_,
                        locZ_,
                        visible_,
                        type_,
                        castMember_,
                        dynamicMember_,
                        foreColor_,
                        backColor_,
                        hasForeColor_,
                        hasBackColor_,
                        id::code(inkMode_),
                        blend_,
                        flipH_,
                        flipV_,
                        rotation_,
                        skew_,
                        std::move(baked),
                        hasBehaviors_);
    result.shapeLineSize_ = shapeLineSize_;
    result.shapePattern_ = shapePattern_;
    return result;
}

RenderSprite RenderSprite::withBakedBitmapAndSize(std::shared_ptr<const bitmap::Bitmap> baked,
                                                  int newWidth,
                                                  int newHeight) const {
    RenderSprite result(channelId_.value(),
                        x_,
                        y_,
                        newWidth,
                        newHeight,
                        locZ_,
                        visible_,
                        type_,
                        castMember_,
                        dynamicMember_,
                        foreColor_,
                        backColor_,
                        hasForeColor_,
                        hasBackColor_,
                        id::code(inkMode_),
                        blend_,
                        flipH_,
                        flipV_,
                        rotation_,
                        skew_,
                        std::move(baked),
                        hasBehaviors_);
    result.shapeLineSize_ = shapeLineSize_;
    result.shapePattern_ = shapePattern_;
    return result;
}

RenderSprite RenderSprite::withShapeLineSize(int lineSize) const {
    RenderSprite result(*this);
    result.shapeLineSize_ = lineSize;
    return result;
}

RenderSprite RenderSprite::withShapePattern(int pattern) const {
    RenderSprite result(*this);
    result.shapePattern_ = std::max(1, pattern);
    return result;
}

int RenderSprite::castMemberId() const {
    if (castMember_) {
        return castMember_->id().value();
    }
    if (dynamicMember_) {
        return dynamicMember_->memberNum();
    }
    return -1;
}

std::optional<std::string> RenderSprite::memberName() const {
    if (castMember_) {
        return castMember_->name();
    }
    if (dynamicMember_) {
        return dynamicMember_->name();
    }
    return std::nullopt;
}

} // namespace libreshockwave::player::render::pipeline
