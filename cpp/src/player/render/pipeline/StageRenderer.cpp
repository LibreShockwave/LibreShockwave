#include "libreshockwave/player/render/pipeline/StageRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/bitmap/Palette.hpp"
#include "libreshockwave/cast/BitmapInfo.hpp"
#include "libreshockwave/cast/CastMember.hpp"
#include "libreshockwave/cast/FilmLoopInfo.hpp"
#include "libreshockwave/cast/MemberType.hpp"
#include "libreshockwave/chunks/CastMemberChunk.hpp"
#include "libreshockwave/chunks/ConfigChunk.hpp"
#include "libreshockwave/player/sprite/SpriteState.hpp"

namespace libreshockwave::player::render::pipeline {
namespace {

constexpr int DIRECTOR_SPRITE_RASTER_Y_ORIGIN = 0;
constexpr int DIRECTOR_D6_SCORE_SPRITE_RASTER_Y_ORIGIN = -6;

std::uint32_t opaqueArgb(int rgb) {
    return 0xFF000000U | static_cast<std::uint32_t>(rgb & 0xFFFFFF);
}

bool isDefaultCastLibSentinel(int castLib) {
    return castLib == 0 || castLib == 0xFFFF;
}

} // namespace

StageRenderer::StageRenderer(DirectorFile* file)
    : file_(file) {
    if (file_ != nullptr && file_->config() != nullptr) {
        const int stageColor = file_->config()->stageColorRGB();
        backgroundColor_ = stageColor;
        defaultBackgroundColor_ = stageColor;
    }
}

SpriteRegistry& StageRenderer::spriteRegistry() {
    return spriteRegistry_;
}

const SpriteRegistry& StageRenderer::spriteRegistry() const {
    return spriteRegistry_;
}

int StageRenderer::stageWidth() const {
    return file_ != nullptr ? file_->stageWidth() : 640;
}

int StageRenderer::stageHeight() const {
    return file_ != nullptr ? file_->stageHeight() : 480;
}

int StageRenderer::backgroundColor() const {
    return backgroundColor_;
}

void StageRenderer::setBackgroundColor(int color) {
    backgroundColor_ = color;
    if (stageImage_ != nullptr && !stageImage_->isScriptModified()) {
        stageImage_->fill(opaqueArgb(backgroundColor_));
    }
}

void StageRenderer::setDefaultBackgroundColor(int color) {
    defaultBackgroundColor_ = color;
    setBackgroundColor(color);
}

std::shared_ptr<bitmap::Bitmap> StageRenderer::stageImage() {
    if (stageImage_ == nullptr) {
        stageImage_ = std::make_shared<bitmap::Bitmap>(stageWidth(), stageHeight(), 32);
        stageImage_->fill(opaqueArgb(backgroundColor_));
    }
    return stageImage_;
}

std::shared_ptr<bitmap::Bitmap> StageRenderer::updateStageImageSnapshot(const bitmap::Bitmap& snapshot) {
    if (stageImage_ == nullptr ||
        stageImage_->width() != snapshot.width() ||
        stageImage_->height() != snapshot.height() ||
        stageImage_->bitDepth() != snapshot.bitDepth()) {
        stageImage_ = std::make_shared<bitmap::Bitmap>(snapshot.copy());
    } else {
        stageImage_->pixels() = snapshot.pixels();
        stageImage_->copyPaletteMetadataFrom(&snapshot);
        if (const auto indices = snapshot.paletteIndices()) {
            stageImage_->setPaletteIndices(*indices);
        } else {
            stageImage_->clearPaletteIndices();
        }
        if (snapshot.hasAnchorPoint()) {
            stageImage_->setAnchorPoint(snapshot.anchorX(), snapshot.anchorY());
        } else {
            stageImage_->clearAnchorPoint();
        }
        stageImage_->setNativeAlpha(snapshot.isNativeAlpha());
        stageImage_->setRectangularMedia(snapshot.isRectangularMedia());
    }
    stageImage_->clearScriptModified();
    return stageImage_;
}

bool StageRenderer::hasStageImage() const {
    return stageImage_ != nullptr;
}

std::shared_ptr<const bitmap::Bitmap> StageRenderer::renderableStageImage() const {
    if (stageImage_ == nullptr || !stageImage_->isScriptModified()) {
        return nullptr;
    }
    return stageImage_;
}

void StageRenderer::discardStageImage() {
    stageImage_.reset();
}

void StageRenderer::resetVisualState() {
    backgroundColor_ = defaultBackgroundColor_;
    stageImage_.reset();
}

void StageRenderer::setLastBakedSprites(std::vector<RenderSprite> sprites) {
    lastBakedSprites_ = std::move(sprites);
}

const std::vector<RenderSprite>& StageRenderer::lastBakedSprites() const {
    return lastBakedSprites_;
}

void StageRenderer::setCastMemberResolver(CastMemberResolver resolver) {
    castMemberResolver_ = std::move(resolver);
}

std::vector<RenderSprite> StageRenderer::getSpritesForFrame(int frame) {
    std::vector<RenderSprite> sprites;
    std::set<int> renderedChannels;
    collectScoreSprites(frame, sprites, renderedChannels);
    collectDynamicSprites(sprites, renderedChannels);
    sortSprites(sprites);
    return sprites;
}

void StageRenderer::collectScoreSprites(int frame, std::vector<RenderSprite>& sprites, std::set<int>& renderedChannels) {
    auto score = scoreChunk();
    if (score == nullptr) {
        return;
    }

    const int frameIndex = frame - 1;
    for (const auto& entry : score->frameData().frameChannelData) {
        if (entry.frameIndex.value() != frameIndex) {
            continue;
        }

        const int channel = entry.channelIndex.value();
        auto state = spriteRegistry_.get(channel);
        if (state != nullptr && state->hasDynamicMember()) {
            if (state->isDynamic()) {
                state->applyScoreDefaults(entry.data);
            }
            auto sprite = createDynamicRenderSprite(*state);
            if (sprite.has_value()) {
                sprites.push_back(*sprite);
                renderedChannels.insert(channel);
            }
            continue;
        }

        auto sprite = createRenderSprite(channel, entry.data);
        if (sprite.has_value()) {
            sprites.push_back(*sprite);
            renderedChannels.insert(channel);
        }
    }
}

void StageRenderer::collectDynamicSprites(std::vector<RenderSprite>& sprites, const std::set<int>& renderedChannels) {
    for (const auto& state : spriteRegistry_.getDynamicSprites()) {
        if (state == nullptr) {
            continue;
        }
        const int channel = state->channel();
        if (!renderedChannels.contains(channel) && (state->hasDynamicMember() || state->isPuppet())) {
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
    spriteRegistry_.clear();
    lastBakedSprites_.clear();
    resetVisualState();
}

void StageRenderer::onSpriteEnd(int channel) {
    spriteRegistry_.remove(channel);
}

void StageRenderer::onFrameEnter(int frame) {
    auto score = scoreChunk();
    if (score == nullptr) {
        return;
    }

    const int frameIndex = frame - 1;
    for (const auto& entry : score->frameData().frameChannelData) {
        if (entry.frameIndex.value() == frameIndex && spriteRegistry_.contains(entry.channelIndex.value())) {
            spriteRegistry_.updateFromScore(entry.channelIndex.value(), entry.data);
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
    if (data.isEmpty() || data.spriteType == 0) {
        return std::nullopt;
    }

    auto state = spriteRegistry_.getOrCreate(channel, data);
    const auto pos = state->snapshotPosition();
    int x = pos.locH;
    int y = pos.locV;
    const int locZ = pos.locZ;
    const int width = pos.width;
    const int height = pos.height;
    const bool visible = state->isVisible();

    auto dynamicMember = resolveCastMember(data.castLib, data.castMember);
    if (dynamicMember == nullptr && isDefaultCastLibSentinel(data.castLib)) {
        dynamicMember = resolveCastMember(1, data.castMember);
    }
    std::shared_ptr<chunks::CastMemberChunk> member =
        dynamicMember != nullptr && dynamicMember->rawChunk() != nullptr
            ? dynamicMember->rawChunk()
            : (file_ != nullptr ? file_->getCastMemberByNumber(data.castLib, data.castMember) : nullptr);
    if (member == nullptr && file_ != nullptr && isDefaultCastLibSentinel(data.castLib)) {
        member = file_->getCastMemberByNumber(1, data.castMember);
    }
    if (member == nullptr && file_ != nullptr) {
        member = file_->getCastMemberByIndex(data.castLib, data.castMember);
        if (member == nullptr && isDefaultCastLibSentinel(data.castLib)) {
            member = file_->getCastMemberByIndex(1, data.castMember);
        }
    }

    if (dynamicMember != nullptr) {
        const auto reg = scaledRegPoint(*dynamicMember, width, height, x, y,
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

    SpriteType type = dynamicMember != nullptr ? determineSpriteTypeFromMember(dynamicMember) :
        (member != nullptr ? determineSpriteTypeFromMember(member) : SpriteType::Unknown);
    if (type == SpriteType::Unknown && data.spriteType >= 2 && data.spriteType <= 8 &&
        member != nullptr && member->memberType() == ::libreshockwave::cast::MemberType::Shape) {
        type = SpriteType::Shape;
    }

    const int foreColor = state->hasForeColor()
        ? state->foreColor()
        : resolveScoreColor(data.resolvedForeColor(), data.isForeColorRGB());
    const int backColor = state->hasBackColor()
        ? state->backColor()
        : resolveScoreColor(data.resolvedBackColor(), data.isBackColorRGB());

    return applyLegacyRenderProperties(
        RenderSprite(channel,
                     x,
                     y,
                     width,
                     height,
                     locZ,
                     visible,
                     type,
                     member,
                     dynamicMember,
                     foreColor,
                     backColor,
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

    const int castLib = state.effectiveCastLib();
    const int castMember = state.effectiveCastMember();
    const auto pos = state.snapshotPosition();

    if (castMember <= 0) {
        if (state.isPuppet() && state.hasBackColor() && pos.width > 0 && pos.height > 0) {
            const int fillColor = state.backColor();
            return applyLegacyRenderProperties(
                RenderSprite(state.channel(),
                             pos.locH,
                             rasterY(pos.locV),
                             pos.width,
                             pos.height,
                             pos.locZ,
                             true,
                             SpriteType::Shape,
                             nullptr,
                             nullptr,
                             fillColor,
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

    int x = pos.locH;
    int y = pos.locV;
    int width = pos.width;
    int height = pos.height;
    auto dynamicMember = resolveCastMember(castLib, castMember);
    if (dynamicMember == nullptr && isDefaultCastLibSentinel(castLib)) {
        dynamicMember = resolveCastMember(1, castMember);
    }
    std::shared_ptr<chunks::CastMemberChunk> member =
        dynamicMember != nullptr && dynamicMember->rawChunk() != nullptr
            ? dynamicMember->rawChunk()
            : (file_ != nullptr ? file_->getCastMemberByIndex(castLib, castMember) : nullptr);
    if (member == nullptr && file_ != nullptr && isDefaultCastLibSentinel(castLib)) {
        member = file_->getCastMemberByIndex(1, castMember);
    }

    SpriteType type = SpriteType::Unknown;
    if (dynamicMember != nullptr) {
        type = determineSpriteTypeFromMember(dynamicMember);
        if (width == 0 && height == 0 && dynamicMember->width() > 0 && dynamicMember->height() > 0) {
            width = dynamicMember->width();
            height = dynamicMember->height();
        }
        const auto reg = scaledRegPoint(*dynamicMember, width, height, x, y,
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
                     pos.locZ,
                     state.isVisible(),
                     type,
                     member,
                     dynamicMember,
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

std::shared_ptr<const ::libreshockwave::cast::CastMember> StageRenderer::resolveCastMember(int castLib,
                                                                                           int memberNum) const {
    if (memberNum <= 0 || !castMemberResolver_) {
        return nullptr;
    }
    return castMemberResolver_(castLib, memberNum);
}

bool StageRenderer::hasAnyBehavior(const sprite::SpriteState& state) const {
    return state.hasScriptBehaviors() || spriteRegistry_.hasScoreBehaviorChannel(state.channel());
}

RenderSprite StageRenderer::applyLegacyRenderProperties(RenderSprite sprite, const sprite::SpriteState& state) {
    if (const auto dynamic = sprite.dynamicMember();
        dynamic != nullptr && dynamic->isShape() && dynamic->hasRuntimeShapeLineSize()) {
        sprite = sprite.withShapeLineSize(dynamic->shapeLineSize());
    }
    if (const auto dynamic = sprite.dynamicMember(); dynamic != nullptr && dynamic->isShape()) {
        sprite = sprite.withShapePattern(dynamic->shapePattern());
    }
    if (auto lineSize = state.legacyProperty("linesize");
        lineSize.has_value() && !lineSize->isVoid()) {
        sprite = sprite.withShapeLineSize(std::max(0, lineSize->intValue()));
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
                                                      int spriteWidth,
                                                      int spriteHeight,
                                                      int posX,
                                                      int posY,
                                                      bool flipH,
                                                      bool flipV) const {
    if (member.isBitmap() && member.specificData().size() >= 10) {
        const auto info = ::libreshockwave::cast::BitmapInfo::parse(
            member.specificData(),
            directorVersionForParsing());
        int regX = info.regXLocal();
        int regY = info.regYLocal();
        if (spriteWidth > 0 && info.width > 0 && info.width != spriteWidth) {
            regX = scaleRegistrationOffset(regX, spriteWidth, info.width);
        }
        if (spriteHeight > 0 && info.height > 0 && info.height != spriteHeight) {
            regY = scaleRegistrationOffset(regY, spriteHeight, info.height);
        }
        regX = mirrorOffset(regX, spriteWidth > 0 ? spriteWidth : info.width, flipH);
        regY = mirrorOffset(regY, spriteHeight > 0 ? spriteHeight : info.height, flipV);
        return RegPoint{regX, regY};
    }

    if (member.memberType() == ::libreshockwave::cast::MemberType::FilmLoop && member.specificData().size() >= 8) {
        const auto info = ::libreshockwave::cast::FilmLoopInfo::parse(member.specificData());
        return RegPoint{posX - info.rectLeft, posY - info.rectTop};
    }

    return RegPoint{
        mirrorOffset(member.regPointX(), spriteWidth, flipH),
        mirrorOffset(member.regPointY(), spriteHeight, flipV)
    };
}

StageRenderer::RegPoint StageRenderer::scaledRegPoint(const ::libreshockwave::cast::CastMember& member,
                                                      int spriteWidth,
                                                      int spriteHeight,
                                                      int posX,
                                                      int posY,
                                                      bool flipH,
                                                      bool flipV) const {
    if (member.rawChunk() != nullptr) {
        return scaledRegPoint(*member.rawChunk(), spriteWidth, spriteHeight, posX, posY, flipH, flipV);
    }

    int regX = member.regX();
    int regY = member.regY();
    const int memberWidth = member.width();
    const int memberHeight = member.height();
    if (member.isBitmap()) {
        if (spriteWidth > 0 && memberWidth > 0 && memberWidth != spriteWidth) {
            regX = scaleRegistrationOffset(regX, spriteWidth, memberWidth);
        }
        if (spriteHeight > 0 && memberHeight > 0 && memberHeight != spriteHeight) {
            regY = scaleRegistrationOffset(regY, spriteHeight, memberHeight);
        }
    }

    return RegPoint{
        mirrorOffset(regX, spriteWidth > 0 ? spriteWidth : memberWidth, flipH),
        mirrorOffset(regY, spriteHeight > 0 ? spriteHeight : memberHeight, flipV)
    };
}

int StageRenderer::resolveScoreColor(int color, bool isRgb) {
    if (isRgb) {
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
    const int version = score->frameData().header.framesVersion;
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

int StageRenderer::scaleRegistrationOffset(int reg, int spriteSpan, int bitmapSpan) const {
    if (usesLegacyRoundedRegistrationScale()) {
        return static_cast<int>(std::lround(static_cast<double>(reg) * static_cast<double>(spriteSpan) /
                                            static_cast<double>(bitmapSpan)));
    }
    return reg * spriteSpan / bitmapSpan;
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
    const int fiveBit = value >> 3;
    return (fiveBit << 3) | (fiveBit >> 2);
}

} // namespace libreshockwave::player::render::pipeline
