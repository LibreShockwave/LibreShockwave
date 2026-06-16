#pragma once

#include <functional>
#include <optional>
#include <vector>

#include "libreshockwave/player/render/pipeline/RenderSprite.hpp"

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
                                     int stageX,
                                     int stageY);
    [[nodiscard]] static int hitTest(render::pipeline::StageRenderer& renderer,
                                     int frame,
                                     int stageX,
                                     int stageY);
    [[nodiscard]] static int hitTest(const std::vector<render::pipeline::RenderSprite>& sprites,
                                     int stageX,
                                     int stageY,
                                     const ChannelPredicate& forceBoundingBox);
    [[nodiscard]] static int hitTest(render::pipeline::StageRenderer& renderer,
                                     int frame,
                                     int stageX,
                                     int stageY,
                                     const ChannelPredicate& forceBoundingBox);
    [[nodiscard]] static std::optional<render::pipeline::SpriteType> hitTestType(
        const std::vector<render::pipeline::RenderSprite>& sprites,
        int stageX,
        int stageY);
    [[nodiscard]] static std::optional<render::pipeline::SpriteType> hitTestType(
        render::pipeline::StageRenderer& renderer,
        int frame,
        int stageX,
        int stageY);
    [[nodiscard]] static std::optional<render::pipeline::SpriteType> hitTestType(
        const std::vector<render::pipeline::RenderSprite>& sprites,
        int stageX,
        int stageY,
        const ChannelPredicate& forceBoundingBox);
    [[nodiscard]] static std::optional<render::pipeline::SpriteType> hitTestType(
        render::pipeline::StageRenderer& renderer,
        int frame,
        int stageX,
        int stageY,
        const ChannelPredicate& forceBoundingBox);
    [[nodiscard]] static std::vector<int> hitTestAll(
        const std::vector<render::pipeline::RenderSprite>& sprites,
        int stageX,
        int stageY,
        const ChannelPredicate& filter);
    [[nodiscard]] static std::vector<int> hitTestAll(
        render::pipeline::StageRenderer& renderer,
        int frame,
        int stageX,
        int stageY,
        const ChannelPredicate& filter);

    [[nodiscard]] static bool hitTestSpritePixel(const render::pipeline::RenderSprite& sprite,
                                                 int stageX,
                                                 int stageY);
    [[nodiscard]] static AlphaHitRule getAlphaHitRule(const render::pipeline::RenderSprite& sprite);
    [[nodiscard]] static bool usesDynamicInkTransparency(const render::pipeline::RenderSprite& sprite);

private:
    [[nodiscard]] static const render::pipeline::RenderSprite* findHitSprite(
        const std::vector<render::pipeline::RenderSprite>& sprites,
        int stageX,
        int stageY,
        const ChannelPredicate& forceBoundingBox);
};

} // namespace libreshockwave::player::input
