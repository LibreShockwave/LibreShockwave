#pragma once

#include <functional>
#include <optional>
#include <vector>

#include "libreshockwave/player/render/pipeline/render_sprite.hpp"

namespace libreshockwave::player::render::pipeline {
class StageRenderer;
}

namespace libreshockwave::player::input {

struct AlphaHitRule {
    bool enabled{false};
    int threshold{0};

    friend bool operator==(const AlphaHitRule&, const AlphaHitRule&) = default;
};

class HitTester {
public:
    using ChannelPredicate = std::function<bool(int)>;

    [[nodiscard]] static int hitTest(const std::vector<render::pipeline::RenderSprite>& sprites,
                                     int stage_x,
                                     int stage_y);
    [[nodiscard]] static int hitTest(render::pipeline::StageRenderer& renderer,
                                     int frame,
                                     int stage_x,
                                     int stage_y);
    [[nodiscard]] static int hitTest(const std::vector<render::pipeline::RenderSprite>& sprites,
                                     int stage_x,
                                     int stage_y,
                                     const ChannelPredicate& force_bounding_box);
    [[nodiscard]] static int hitTest(render::pipeline::StageRenderer& renderer,
                                     int frame,
                                     int stage_x,
                                     int stage_y,
                                     const ChannelPredicate& force_bounding_box);
    [[nodiscard]] static std::optional<render::pipeline::SpriteType> hitTestType(
        const std::vector<render::pipeline::RenderSprite>& sprites,
        int stage_x,
        int stage_y);
    [[nodiscard]] static std::optional<render::pipeline::SpriteType> hitTestType(
        render::pipeline::StageRenderer& renderer,
        int frame,
        int stage_x,
        int stage_y);
    [[nodiscard]] static std::optional<render::pipeline::SpriteType> hitTestType(
        const std::vector<render::pipeline::RenderSprite>& sprites,
        int stage_x,
        int stage_y,
        const ChannelPredicate& force_bounding_box);
    [[nodiscard]] static std::optional<render::pipeline::SpriteType> hitTestType(
        render::pipeline::StageRenderer& renderer,
        int frame,
        int stage_x,
        int stage_y,
        const ChannelPredicate& force_bounding_box);
    [[nodiscard]] static std::vector<int> hitTestAll(
        const std::vector<render::pipeline::RenderSprite>& sprites,
        int stage_x,
        int stage_y,
        const ChannelPredicate& filter);
    [[nodiscard]] static std::vector<int> hitTestAll(
        render::pipeline::StageRenderer& renderer,
        int frame,
        int stage_x,
        int stage_y,
        const ChannelPredicate& filter);

    [[nodiscard]] static bool hitTestSpritePixel(const render::pipeline::RenderSprite& sprite,
                                                 int stage_x,
                                                 int stage_y);
    [[nodiscard]] static AlphaHitRule getAlphaHitRule(const render::pipeline::RenderSprite& sprite);
    [[nodiscard]] static bool usesDynamicInkTransparency(const render::pipeline::RenderSprite& sprite);

private:
    [[nodiscard]] static const render::pipeline::RenderSprite* findHitSprite(
        const std::vector<render::pipeline::RenderSprite>& sprites,
        int stage_x,
        int stage_y,
        const ChannelPredicate& force_bounding_box);
};

} // namespace libreshockwave::player::input
