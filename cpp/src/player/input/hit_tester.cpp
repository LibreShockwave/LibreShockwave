#include "libreshockwave/player/input/hit_tester.hpp"

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/cast/bitmap_info.hpp"
#include "libreshockwave/chunks/cast_member_chunk.hpp"
#include "libreshockwave/player/render/pipeline/stage_renderer.hpp"

namespace libreshockwave::player::input {
namespace {

bool never(int) {
    return false;
}

bool containsPoint(const render::pipeline::RenderSprite& sprite, int stage_x, int stage_y) {
    const int left = sprite.x();
    const int top = sprite.y();
    const int right = left + sprite.width();
    const int bottom = top + sprite.height();
    return stage_x >= left && stage_x < right && stage_y >= top && stage_y < bottom;
}

} // namespace

int HitTester::hitTest(const std::vector<render::pipeline::RenderSprite>& sprites, int stage_x, int stage_y) {
    return hitTest(sprites, stage_x, stage_y, never);
}

int HitTester::hitTest(render::pipeline::StageRenderer& renderer, int frame, int stage_x, int stage_y) {
    return hitTest(renderer, frame, stage_x, stage_y, never);
}

int HitTester::hitTest(const std::vector<render::pipeline::RenderSprite>& sprites,
                       int stage_x,
                       int stage_y,
                       const ChannelPredicate& force_bounding_box) {
    const auto* sprite = findHitSprite(sprites, stage_x, stage_y, force_bounding_box);
    return sprite != nullptr ? sprite->channel() : 0;
}

int HitTester::hitTest(render::pipeline::StageRenderer& renderer,
                       int frame,
                       int stage_x,
                       int stage_y,
                       const ChannelPredicate& force_bounding_box) {
    const auto& baked = renderer.lastBakedSprites();
    if (!baked.empty()) {
        return hitTest(baked, stage_x, stage_y, force_bounding_box);
    }
    auto sprites = renderer.getSpritesForFrame(frame);
    return hitTest(sprites, stage_x, stage_y, force_bounding_box);
}

std::optional<render::pipeline::SpriteType> HitTester::hitTestType(
    const std::vector<render::pipeline::RenderSprite>& sprites,
    int stage_x,
    int stage_y) {
    return hitTestType(sprites, stage_x, stage_y, never);
}

std::optional<render::pipeline::SpriteType> HitTester::hitTestType(
    render::pipeline::StageRenderer& renderer,
    int frame,
    int stage_x,
    int stage_y) {
    return hitTestType(renderer, frame, stage_x, stage_y, never);
}

std::optional<render::pipeline::SpriteType> HitTester::hitTestType(
    const std::vector<render::pipeline::RenderSprite>& sprites,
    int stage_x,
    int stage_y,
    const ChannelPredicate& force_bounding_box) {
    const auto* sprite = findHitSprite(sprites, stage_x, stage_y, force_bounding_box);
    if (sprite == nullptr) {
        return std::nullopt;
    }
    return sprite->type();
}

std::optional<render::pipeline::SpriteType> HitTester::hitTestType(
    render::pipeline::StageRenderer& renderer,
    int frame,
    int stage_x,
    int stage_y,
    const ChannelPredicate& force_bounding_box) {
    const auto& baked = renderer.lastBakedSprites();
    if (!baked.empty()) {
        return hitTestType(baked, stage_x, stage_y, force_bounding_box);
    }
    auto sprites = renderer.getSpritesForFrame(frame);
    return hitTestType(sprites, stage_x, stage_y, force_bounding_box);
}

std::vector<int> HitTester::hitTestAll(const std::vector<render::pipeline::RenderSprite>& sprites,
                                       int stage_x,
                                       int stage_y,
                                       const ChannelPredicate& filter) {
    std::vector<int> result;
    for (auto it = sprites.rbegin(); it != sprites.rend(); ++it) {
        const auto& sprite = *it;
        if (!sprite.isVisible() || sprite.channel() <= 0 || !containsPoint(sprite, stage_x, stage_y)) {
            continue;
        }
        if (filter(sprite.channel()) || hitTestSpritePixel(sprite, stage_x, stage_y)) {
            result.push_back(sprite.channel());
        }
    }
    return result;
}

std::vector<int> HitTester::hitTestAll(render::pipeline::StageRenderer& renderer,
                                       int frame,
                                       int stage_x,
                                       int stage_y,
                                       const ChannelPredicate& filter) {
    const auto& baked = renderer.lastBakedSprites();
    if (!baked.empty()) {
        return hitTestAll(baked, stage_x, stage_y, filter);
    }
    auto sprites = renderer.getSpritesForFrame(frame);
    return hitTestAll(sprites, stage_x, stage_y, filter);
}

bool HitTester::hitTestSpritePixel(const render::pipeline::RenderSprite& sprite, int stage_x, int stage_y) {
    const auto baked = sprite.bakedBitmap();
    if (!baked) {
        return true;
    }

    const int sprite_width = sprite.width() > 0 ? sprite.width() : baked->width();
    const int sprite_height = sprite.height() > 0 ? sprite.height() : baked->height();
    if (sprite_width <= 0 || sprite_height <= 0 || baked->width() <= 0 || baked->height() <= 0) {
        return true;
    }

    const int local_x = stage_x - sprite.x();
    const int local_y = stage_y - sprite.y();
    if (local_x < 0 || local_y < 0 || local_x >= sprite_width || local_y >= sprite_height) {
        return false;
    }

    if (sprite.type() == render::pipeline::SpriteType::Button) {
        return true;
    }

    const auto alpha_rule = getAlphaHitRule(sprite);
    if (!alpha_rule.enabled || alpha_rule.threshold <= 0) {
        return true;
    }

    int src_x = (local_x * baked->width()) / sprite_width;
    int src_y = (local_y * baked->height()) / sprite_height;

    if (sprite.isFlipH() != sprite.hasDirectorHorizontalMirror()) {
        src_x = baked->width() - 1 - src_x;
    }
    if (sprite.isFlipV()) {
        src_y = baked->height() - 1 - src_y;
    }

    const int alpha = static_cast<int>((baked->getPixel(src_x, src_y) >> 24) & 0xFFU);
    return alpha >= alpha_rule.threshold;
}

AlphaHitRule HitTester::getAlphaHitRule(const render::pipeline::RenderSprite& sprite) {
    const auto baked = sprite.bakedBitmap();
    const auto dynamic_member = sprite.dynamicMember();
    if (dynamic_member) {
        if (usesDynamicInkTransparency(sprite)) {
            return AlphaHitRule{true, 1};
        }
        return AlphaHitRule{};
    }

    if (!baked || !baked->isNativeAlpha() || baked->bitDepth() != 32) {
        return AlphaHitRule{};
    }

    const auto cast_member = sprite.castMember();
    if (!cast_member || !cast_member->isBitmap() || cast_member->specificData().size() < 10) {
        return AlphaHitRule{};
    }

    const auto info = ::libreshockwave::cast::BitmapInfo::parse(cast_member->specificData());
    if (info.bit_depth != 32) {
        return AlphaHitRule{};
    }

    return AlphaHitRule{true, info.alpha_threshold};
}

bool HitTester::usesDynamicInkTransparency(const render::pipeline::RenderSprite& sprite) {
    switch (sprite.inkMode()) {
        case id::InkMode::TRANSPARENT:
        case id::InkMode::REVERSE:
        case id::InkMode::GHOST:
        case id::InkMode::NOT_COPY:
        case id::InkMode::NOT_TRANSPARENT:
        case id::InkMode::NOT_REVERSE:
        case id::InkMode::NOT_GHOST:
        case id::InkMode::MATTE:
        case id::InkMode::MASK:
        case id::InkMode::ADD_PIN:
        case id::InkMode::ADD:
        case id::InkMode::SUBTRACT_PIN:
        case id::InkMode::SUBTRACT:
        case id::InkMode::BACKGROUND_TRANSPARENT:
        case id::InkMode::BLEND:
        case id::InkMode::LIGHTEN:
        case id::InkMode::DARKEN:
            return true;
        case id::InkMode::COPY:
        case id::InkMode::LIGHTEST:
        case id::InkMode::DARKEST:
            return false;
    }
    return false;
}

const render::pipeline::RenderSprite* HitTester::findHitSprite(
    const std::vector<render::pipeline::RenderSprite>& sprites,
    int stage_x,
    int stage_y,
    const ChannelPredicate& force_bounding_box) {
    for (auto it = sprites.rbegin(); it != sprites.rend(); ++it) {
        const auto& sprite = *it;
        if (!sprite.isVisible() || sprite.channel() <= 0 || !containsPoint(sprite, stage_x, stage_y)) {
            continue;
        }
        if (force_bounding_box(sprite.channel()) || hitTestSpritePixel(sprite, stage_x, stage_y)) {
            return &sprite;
        }
    }
    return nullptr;
}

} // namespace libreshockwave::player::input
