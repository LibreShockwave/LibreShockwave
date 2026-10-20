#include "libreshockwave/player/render/pipeline/stage_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include "libreshockwave/director_file.hpp"
#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/bitmap/palette.hpp"
#include "libreshockwave/cast/bitmap_info.hpp"
#include "libreshockwave/cast/cast_member.hpp"
#include "libreshockwave/cast/film_loop_info.hpp"
#include "libreshockwave/cast/member_type.hpp"
#include "libreshockwave/chunks/cast_member_chunk.hpp"
#include "libreshockwave/chunks/config_chunk.hpp"
#include "libreshockwave/player/sprite/sprite_state.hpp"

namespace libreshockwave::player::render::pipeline {
namespace {

constexpr int DIRECTOR_SPRITE_RASTER_Y_ORIGIN = 0;
constexpr int DIRECTOR_D6_SCORE_SPRITE_RASTER_Y_ORIGIN = -6;

std::uint32_t opaqueArgb(int rgb) {
    return 0xFF000000U | static_cast<std::uint32_t>(rgb & 0xFFFFFF);
}

bool isDefaultCastLibSentinel(int cast_lib) {
    return cast_lib == 0 || cast_lib == 0xFFFF;
}

void restoreRuntimeBitmapSizeIfPlaceholder(const std::shared_ptr<const ::libreshockwave::cast::CastMember>& member,
                                           int& width,
                                           int& height) {
    if (member == nullptr || width > 1 || height > 1) {
        return;
    }

    const auto bitmap = member->runtimeBitmap();
    if (bitmap == nullptr || (bitmap->width() <= 1 && bitmap->height() <= 1)) {
        return;
    }

    width = bitmap->width();
    height = bitmap->height();
}

} // namespace

StageRenderer::StageRenderer(DirectorFile* file)
    : file_(file) {
    if (file_ != nullptr && file_->config() != nullptr) {
        const int stage_color = file_->config()->stageColorRGB();
        background_color_ = stage_color;
        default_background_color_ = stage_color;
    }
}

SpriteRegistry& StageRenderer::spriteRegistry() {
    return sprite_registry_;
}

const SpriteRegistry& StageRenderer::spriteRegistry() const {
    return sprite_registry_;
}

int StageRenderer::stageWidth() const {
    return file_ != nullptr ? file_->stageWidth() : 640;
}

int StageRenderer::stageHeight() const {
    return file_ != nullptr ? file_->stageHeight() : 480;
}

int StageRenderer::backgroundColor() const {
    return background_color_;
}

void StageRenderer::setBackgroundColor(int color) {
    background_color_ = color;
    if (stage_image_ != nullptr && !stage_image_->isScriptModified()) {
        stage_image_->fill(opaqueArgb(background_color_));
    }
}

void StageRenderer::setDefaultBackgroundColor(int color) {
    default_background_color_ = color;
    setBackgroundColor(color);
}

std::shared_ptr<bitmap::Bitmap> StageRenderer::stageImage() {
    if (stage_image_ == nullptr) {
        stage_image_ = std::make_shared<bitmap::Bitmap>(stageWidth(), stageHeight(), 32);
        stage_image_->fill(opaqueArgb(background_color_));
    }
    return stage_image_;
}

std::shared_ptr<bitmap::Bitmap> StageRenderer::updateStageImageSnapshot(const bitmap::Bitmap& snapshot) {
    if (stage_image_ == nullptr ||
        stage_image_->width() != snapshot.width() ||
        stage_image_->height() != snapshot.height() ||
        stage_image_->bitDepth() != snapshot.bitDepth()) {
        stage_image_ = std::make_shared<bitmap::Bitmap>(snapshot.copy());
    } else {
        stage_image_->pixels() = snapshot.pixels();
        stage_image_->copyPaletteMetadataFrom(&snapshot);
        if (const auto indices = snapshot.paletteIndices()) {
            stage_image_->setPaletteIndices(*indices);
        } else {
            stage_image_->clearPaletteIndices();
        }
        if (snapshot.hasAnchorPoint()) {
            stage_image_->setAnchorPoint(snapshot.anchorX(), snapshot.anchorY());
        } else {
            stage_image_->clearAnchorPoint();
        }
        stage_image_->setNativeAlpha(snapshot.isNativeAlpha());
        stage_image_->setRectangularMedia(snapshot.isRectangularMedia());
    }
    stage_image_->clearScriptModified();
    return stage_image_;
}

bool StageRenderer::hasStageImage() const {
    return stage_image_ != nullptr;
}

std::shared_ptr<const bitmap::Bitmap> StageRenderer::renderableStageImage() const {
    if (stage_image_ == nullptr || !stage_image_->isScriptModified()) {
        return nullptr;
    }
    return stage_image_;
}

void StageRenderer::discardStageImage() {
    stage_image_.reset();
}

void StageRenderer::resetVisualState() {
    background_color_ = default_background_color_;
    stage_image_.reset();
}

void StageRenderer::setLastBakedSprites(std::vector<RenderSprite> sprites) {
    last_baked_sprites_ = std::move(sprites);
}

const std::vector<RenderSprite>& StageRenderer::lastBakedSprites() const {
    return last_baked_sprites_;
}

void StageRenderer::setCastMemberResolver(CastMemberResolver resolver) {
    cast_member_resolver_ = std::move(resolver);
}

std::vector<RenderSprite> StageRenderer::getSpritesForFrame(int frame) {
    std::vector<RenderSprite> sprites;
    std::set<int> rendered_channels;
    collectScoreSprites(frame, sprites, rendered_channels);
    collectDynamicSprites(sprites, rendered_channels);
    sortSprites(sprites);
    return sprites;
}

void StageRenderer::collectScoreSprites(int frame, std::vector<RenderSprite>& sprites, std::set<int>& rendered_channels) {
    auto score = scoreChunk();
    if (score == nullptr) {
        return;
    }

    const int frame_index = frame - 1;
    for (const auto& entry : score->frameData().frame_channel_data) {
        if (entry.frame_index.value() != frame_index) {
            continue;
        }

        const int channel = entry.channel_index.value();
        auto state = sprite_registry_.get(channel);
        if (state != nullptr && state->hasDynamicMember()) {
            if (state->isDynamic()) {
                state->applyScoreDefaults(entry.data);
            }
            auto sprite = createDynamicRenderSprite(*state);
            if (sprite.has_value()) {
                sprites.push_back(*sprite);
                rendered_channels.insert(channel);
            }
            continue;
        }

        auto sprite = createRenderSprite(channel, entry.data);
        if (sprite.has_value()) {
            sprites.push_back(*sprite);
            rendered_channels.insert(channel);
        }
    }
}

void StageRenderer::collectDynamicSprites(std::vector<RenderSprite>& sprites, const std::set<int>& rendered_channels) {
    for (const auto& state : sprite_registry_.getDynamicSprites()) {
        if (state == nullptr) {
            continue;
        }
        const int channel = state->channel();
        if (!rendered_channels.contains(channel) && (state->hasDynamicMember() || state->isPuppet())) {
            auto sprite = createDynamicRenderSprite(*state);
            if (sprite.has_value()) {
                sprites.push_back(*sprite);
            }
        }
    }
}

void StageRenderer::sortSprites(std::vector<RenderSprite>& sprites) {
    std::ranges::sort(sprites, [](const RenderSprite& left, const RenderSprite& right) {
        if (left.locZ() != right.locZ()) {
            return left.locZ() < right.locZ();
        }
        return left.channel() < right.channel();
    });
}

void StageRenderer::reset() {
    sprite_registry_.clear();
    last_baked_sprites_.clear();
    resetVisualState();
}

void StageRenderer::onSpriteEnd(int channel) {
    sprite_registry_.remove(channel);
}

void StageRenderer::onFrameEnter(int frame) {
    auto score = scoreChunk();
    if (score == nullptr) {
        return;
    }

    const int frame_index = frame - 1;
    for (const auto& entry : score->frameData().frame_channel_data) {
        if (entry.frame_index.value() == frame_index && sprite_registry_.contains(entry.channel_index.value())) {
            sprite_registry_.updateFromScore(entry.channel_index.value(), entry.data);
        }
    }
}

int StageRenderer::expandScoreRgb555(int color) {
    const int r = expand5Bit((color >> 16) & 0xFF);
    const int g = expand5Bit((color >> 8) & 0xFF);
    const int b = expand5Bit(color & 0xFF);
    return (r << 16) | (g << 8) | b;
}

std::shared_ptr<chunks::ScoreChunk> StageRenderer::scoreChunk() const {
    return file_ != nullptr ? file_->scoreChunk() : nullptr;
}

std::optional<RenderSprite> StageRenderer::createRenderSprite(int channel,
                                                              const chunks::ScoreChunk::ChannelData& data) {
    if (data.isEmpty() || data.sprite_type == 0) {
        return std::nullopt;
    }

    auto state = sprite_registry_.getOrCreate(channel, data);
    const auto pos = state->snapshotPosition();
    int x = pos.loc_h;
    int y = pos.loc_v;
    const int loc_z = pos.loc_z;
    int width = pos.width;
    int height = pos.height;
    const bool visible = state->isVisible();

    auto dynamic_member = resolveCastMember(data.cast_lib, data.cast_member);
    if (dynamic_member == nullptr && isDefaultCastLibSentinel(data.cast_lib)) {
        dynamic_member = resolveCastMember(1, data.cast_member);
    }
    std::shared_ptr<chunks::CastMemberChunk> member =
        dynamic_member != nullptr && dynamic_member->rawChunk() != nullptr
            ? dynamic_member->rawChunk()
            : (file_ != nullptr ? file_->getCastMemberByNumber(data.cast_lib, data.cast_member) : nullptr);
    if (member == nullptr && file_ != nullptr && isDefaultCastLibSentinel(data.cast_lib)) {
        member = file_->getCastMemberByNumber(1, data.cast_member);
    }
    if (member == nullptr && file_ != nullptr) {
        member = file_->getCastMemberByIndex(data.cast_lib, data.cast_member);
        if (member == nullptr && isDefaultCastLibSentinel(data.cast_lib)) {
            member = file_->getCastMemberByIndex(1, data.cast_member);
        }
    }

    if (dynamic_member != nullptr) {
        restoreRuntimeBitmapSizeIfPlaceholder(dynamic_member, width, height);
        const auto reg = scaledRegPoint(*dynamic_member, width, height, x, y,
                                        state->isFlipH() ^ hasDirectorHorizontalMirror(state->rotation(), state->skew()),
                                        state->isFlipV());
        x -= reg.x;
        y -= reg.y;
    } else if (member != nullptr) {
        const auto reg = scaledRegPoint(*member, width, height, x, y,
                                        state->isFlipH() ^ hasDirectorHorizontalMirror(state->rotation(), state->skew()),
                                        state->isFlipV());
        x -= reg.x;
        y -= reg.y;
    }
    y = scoreSpriteRasterY(y);

    SpriteType type = dynamic_member != nullptr ? determineSpriteTypeFromMember(dynamic_member) :
        (member != nullptr ? determineSpriteTypeFromMember(member) : SpriteType::Unknown);
    if (type == SpriteType::Unknown && data.sprite_type >= 2 && data.sprite_type <= 8 &&
        member != nullptr && member->memberType() == ::libreshockwave::cast::MemberType::Shape) {
        type = SpriteType::Shape;
    }

    const int fore_color = state->hasForeColor()
        ? state->foreColor()
        : resolveScoreColor(data.resolvedForeColor(), data.isForeColorRGB());
    const int back_color = state->hasBackColor()
        ? state->backColor()
        : resolveScoreColor(data.resolvedBackColor(), data.isBackColorRGB());

    return applyLegacyRenderProperties(
        RenderSprite(channel,
                     x,
                     y,
                     width,
                     height,
                     loc_z,
                     visible,
                     type,
                     member,
                     dynamic_member,
                     fore_color,
                     back_color,
                     state->hasForeColor(),
                     state->hasBackColor(),
                     state->ink(),
                     state->blend(),
                     state->isFlipH(),
                     state->isFlipV(),
                     nullptr,
                     hasAnyBehavior(*state)),
        *state);
}

std::optional<RenderSprite> StageRenderer::createDynamicRenderSprite(const sprite::SpriteState& state) {
    if (!state.isVisible()) {
        return std::nullopt;
    }

    const int cast_lib = state.effectiveCastLib();
    const int cast_member = state.effectiveCastMember();
    const auto pos = state.snapshotPosition();

    if (cast_member <= 0) {
        if (state.isPuppet() && state.hasBackColor() && pos.width > 0 && pos.height > 0) {
            const int fill_color = state.backColor();
            return applyLegacyRenderProperties(
                RenderSprite(state.channel(),
                             pos.loc_h,
                             rasterY(pos.loc_v),
                             pos.width,
                             pos.height,
                             pos.loc_z,
                             true,
                             SpriteType::Shape,
                             nullptr,
                             nullptr,
                             fill_color,
                             state.backColor(),
                             true,
                             state.hasBackColor(),
                             0,
                             state.blend(),
                             state.isFlipH(),
                             state.isFlipV(),
                             nullptr,
                             hasAnyBehavior(state)),
                state);
        }
        return std::nullopt;
    }

    int x = pos.loc_h;
    int y = pos.loc_v;
    int width = pos.width;
    int height = pos.height;
    auto dynamic_member = resolveCastMember(cast_lib, cast_member);
    if (dynamic_member == nullptr && isDefaultCastLibSentinel(cast_lib)) {
        dynamic_member = resolveCastMember(1, cast_member);
    }
    std::shared_ptr<chunks::CastMemberChunk> member =
        dynamic_member != nullptr && dynamic_member->rawChunk() != nullptr
            ? dynamic_member->rawChunk()
            : (file_ != nullptr ? file_->getCastMemberByIndex(cast_lib, cast_member) : nullptr);
    if (member == nullptr && file_ != nullptr && isDefaultCastLibSentinel(cast_lib)) {
        member = file_->getCastMemberByIndex(1, cast_member);
    }

    SpriteType type = SpriteType::Unknown;
    if (dynamic_member != nullptr) {
        type = determineSpriteTypeFromMember(dynamic_member);
        restoreRuntimeBitmapSizeIfPlaceholder(dynamic_member, width, height);
        if (width == 0 && height == 0 && dynamic_member->width() > 0 && dynamic_member->height() > 0) {
            width = dynamic_member->width();
            height = dynamic_member->height();
        }
        const auto reg = scaledRegPoint(*dynamic_member, width, height, x, y,
                                        state.isFlipH() ^ hasDirectorHorizontalMirror(state.rotation(), state.skew()),
                                        state.isFlipV());
        x -= reg.x;
        y -= reg.y;
    } else if (member != nullptr) {
        type = determineSpriteTypeFromMember(member);
        const auto reg = scaledRegPoint(*member, width, height, x, y,
                                        state.isFlipH() ^ hasDirectorHorizontalMirror(state.rotation(), state.skew()),
                                        state.isFlipV());
        x -= reg.x;
        y -= reg.y;

        if (width == 0 && height == 0 && member->isBitmap() && member->specificData().size() >= 10) {
            const auto info = ::libreshockwave::cast::BitmapInfo::parse(
                member->specificData(),
                directorVersionForParsing());
            width = info.width;
            height = info.height;
        }
    }
    y = rasterY(y);

    return applyLegacyRenderProperties(
        RenderSprite(state.channel(),
                     x,
                     y,
                     width,
                     height,
                     pos.loc_z,
                     state.isVisible(),
                     type,
                     member,
                     dynamic_member,
                     state.foreColor(),
                     state.backColor(),
                     state.hasForeColor(),
                     state.hasBackColor(),
                     state.ink(),
                     state.blend(),
                     state.isFlipH(),
                     state.isFlipV(),
                     state.rotation(),
                     state.skew(),
                     nullptr,
                     hasAnyBehavior(state)),
        state);
}

std::shared_ptr<const ::libreshockwave::cast::CastMember> StageRenderer::resolveCastMember(int cast_lib,
                                                                                           int member_num) const {
    if (member_num <= 0 || !cast_member_resolver_) {
        return nullptr;
    }
    return cast_member_resolver_(cast_lib, member_num);
}

bool StageRenderer::hasAnyBehavior(const sprite::SpriteState& state) const {
    return state.hasScriptBehaviors() || sprite_registry_.hasScoreBehaviorChannel(state.channel());
}

RenderSprite StageRenderer::applyLegacyRenderProperties(RenderSprite sprite, const sprite::SpriteState& state) {
    if (const auto dynamic = sprite.dynamicMember();
        dynamic != nullptr && dynamic->isShape() && dynamic->hasRuntimeShapeLineSize()) {
        sprite = sprite.withShapeLineSize(dynamic->shapeLineSize());
    }
    if (const auto dynamic = sprite.dynamicMember(); dynamic != nullptr && dynamic->isShape()) {
        sprite = sprite.withShapePattern(dynamic->shapePattern());
    }
    if (auto line_size = state.legacyProperty("linesize");
        line_size.has_value() && !line_size->isVoid()) {
        sprite = sprite.withShapeLineSize(std::max(0, line_size->intValue()));
    }
    if (auto pattern = state.legacyProperty("pattern");
        pattern.has_value() && !pattern->isVoid()) {
        sprite = sprite.withShapePattern(pattern->intValue());
    }
    return sprite;
}

SpriteType StageRenderer::determineSpriteTypeFromMember(
    const std::shared_ptr<const chunks::CastMemberChunk>& member) const {
    if (member == nullptr) {
        return SpriteType::Unknown;
    }
    if (member->isBitmap()) {
        return SpriteType::Bitmap;
    }
    if (member->isTextXtra()) {
        return SpriteType::Text;
    }

    switch (member->memberType()) {
        case ::libreshockwave::cast::MemberType::Shape:
            return SpriteType::Shape;
        case ::libreshockwave::cast::MemberType::Text:
        case ::libreshockwave::cast::MemberType::RichText:
            return SpriteType::Text;
        case ::libreshockwave::cast::MemberType::Button:
            return SpriteType::Button;
        case ::libreshockwave::cast::MemberType::FilmLoop:
            return SpriteType::FilmLoop;
        case ::libreshockwave::cast::MemberType::Shockwave3D:
            return SpriteType::Shockwave3D;
        default:
            return SpriteType::Unknown;
    }
}

SpriteType StageRenderer::determineSpriteTypeFromMember(
    const std::shared_ptr<const ::libreshockwave::cast::CastMember>& member) const {
    if (member == nullptr) {
        return SpriteType::Unknown;
    }
    if (member->rawChunk() != nullptr) {
        return determineSpriteTypeFromMember(member->rawChunk());
    }

    switch (member->memberType()) {
        case ::libreshockwave::cast::MemberType::Bitmap:
            return SpriteType::Bitmap;
        case ::libreshockwave::cast::MemberType::Shape:
            return SpriteType::Shape;
        case ::libreshockwave::cast::MemberType::Text:
        case ::libreshockwave::cast::MemberType::RichText:
            return SpriteType::Text;
        case ::libreshockwave::cast::MemberType::Button:
            return SpriteType::Button;
        case ::libreshockwave::cast::MemberType::FilmLoop:
            return SpriteType::FilmLoop;
        case ::libreshockwave::cast::MemberType::Shockwave3D:
            return SpriteType::Shockwave3D;
        default:
            return SpriteType::Unknown;
    }
}

StageRenderer::RegPoint StageRenderer::scaledRegPoint(const chunks::CastMemberChunk& member,
                                                      int sprite_width,
                                                      int sprite_height,
                                                      int pos_x,
                                                      int pos_y,
                                                      bool flip_h,
                                                      bool flip_v) const {
    if (member.isBitmap() && member.specificData().size() >= 10) {
        const auto info = ::libreshockwave::cast::BitmapInfo::parse(
            member.specificData(),
            directorVersionForParsing());
        int reg_x = info.regXLocal();
        int reg_y = info.regYLocal();
        if (sprite_width > 0 && info.width > 0 && info.width != sprite_width) {
            reg_x = scaleRegistrationOffset(reg_x, sprite_width, info.width);
        }
        if (sprite_height > 0 && info.height > 0 && info.height != sprite_height) {
            reg_y = scaleRegistrationOffset(reg_y, sprite_height, info.height);
        }
        reg_x = mirrorOffset(reg_x, sprite_width > 0 ? sprite_width : info.width, flip_h);
        reg_y = mirrorOffset(reg_y, sprite_height > 0 ? sprite_height : info.height, flip_v);
        return RegPoint{reg_x, reg_y};
    }

    if (member.memberType() == ::libreshockwave::cast::MemberType::FilmLoop && member.specificData().size() >= 8) {
        const auto info = ::libreshockwave::cast::FilmLoopInfo::parse(member.specificData());
        return RegPoint{pos_x - info.rect_left, pos_y - info.rect_top};
    }

    return RegPoint{
        mirrorOffset(member.regPointX(), sprite_width, flip_h),
        mirrorOffset(member.regPointY(), sprite_height, flip_v)
    };
}

StageRenderer::RegPoint StageRenderer::scaledRegPoint(const ::libreshockwave::cast::CastMember& member,
                                                      int sprite_width,
                                                      int sprite_height,
                                                      int pos_x,
                                                      int pos_y,
                                                      bool flip_h,
                                                      bool flip_v) const {
    if (member.rawChunk() != nullptr) {
        return scaledRegPoint(*member.rawChunk(), sprite_width, sprite_height, pos_x, pos_y, flip_h, flip_v);
    }

    int reg_x = member.regX();
    int reg_y = member.regY();
    const int member_width = member.width();
    const int member_height = member.height();
    if (member.isBitmap()) {
        if (sprite_width > 0 && member_width > 0 && member_width != sprite_width) {
            reg_x = scaleRegistrationOffset(reg_x, sprite_width, member_width);
        }
        if (sprite_height > 0 && member_height > 0 && member_height != sprite_height) {
            reg_y = scaleRegistrationOffset(reg_y, sprite_height, member_height);
        }
    }

    return RegPoint{
        mirrorOffset(reg_x, sprite_width > 0 ? sprite_width : member_width, flip_h),
        mirrorOffset(reg_y, sprite_height > 0 ? sprite_height : member_height, flip_v)
    };
}

int StageRenderer::resolveScoreColor(int color, bool is_rgb) {
    if (is_rgb) {
        return usesRgb555ScoreColors() ? expandScoreRgb555(color) : color;
    }

    if (color >= 0 && color <= 255 && file_ != nullptr) {
        auto palette = file_->resolvePalette(bitmap::Palette::SYSTEM_MAC);
        if (palette != nullptr) {
            return static_cast<int>(palette->getColor(color) & 0xFFFFFFU);
        }
    }
    return color;
}

bool StageRenderer::usesRgb555ScoreColors() const {
    return file_ != nullptr && file_->config() != nullptr && file_->config()->directorVersion() <= 1600;
}

bool StageRenderer::usesLegacyRoundedRegistrationScale() const {
    return file_ != nullptr && file_->config() != nullptr && file_->config()->directorVersion() <= 1600;
}

int StageRenderer::directorVersionForParsing() const {
    return file_ != nullptr && file_->config() != nullptr ? file_->config()->directorVersion() : 1200;
}

int StageRenderer::scoreSpriteRasterY(int y) const {
    return y + scoreSpriteRasterYOffset();
}

int StageRenderer::scoreSpriteRasterYOffset() const {
    auto score = scoreChunk();
    if (score == nullptr) {
        return DIRECTOR_SPRITE_RASTER_Y_ORIGIN;
    }
    const int version = score->frameData().header.frames_version;
    if (version > 7 && version <= 13) {
        return DIRECTOR_D6_SCORE_SPRITE_RASTER_Y_ORIGIN;
    }
    return DIRECTOR_SPRITE_RASTER_Y_ORIGIN;
}

int StageRenderer::rasterY(int y) {
    return y + DIRECTOR_SPRITE_RASTER_Y_ORIGIN;
}

int StageRenderer::mirrorOffset(int reg, int span, bool flipped) {
    if (!flipped || span <= 0) {
        return reg;
    }
    return span - reg;
}

int StageRenderer::scaleRegistrationOffset(int reg, int sprite_span, int bitmap_span) const {
    if (usesLegacyRoundedRegistrationScale()) {
        return static_cast<int>(std::lround(static_cast<double>(reg) * static_cast<double>(sprite_span) /
                                            static_cast<double>(bitmap_span)));
    }
    return reg * sprite_span / bitmap_span;
}

bool StageRenderer::hasDirectorHorizontalMirror(double rotation, double skew) {
    return normalizeTransformAngle(rotation) == 180 && normalizeTransformAngle(skew) == 180;
}

int StageRenderer::normalizeTransformAngle(double angle) {
    int normalized = static_cast<int>(std::lround(angle)) % 360;
    if (normalized < 0) {
        normalized += 360;
    }
    return normalized;
}

int StageRenderer::expand5Bit(int value) {
    const int five_bit = value >> 3;
    return (five_bit << 3) | (five_bit >> 2);
}

} // namespace libreshockwave::player::render::pipeline
