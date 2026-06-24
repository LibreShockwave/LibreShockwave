#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "libreshockwave/chunks/ScoreChunk.hpp"
#include "libreshockwave/player/render/SpriteRegistry.hpp"
#include "libreshockwave/player/render/pipeline/RenderSprite.hpp"

namespace libreshockwave {
class DirectorFile;
}

namespace libreshockwave::bitmap {
class Bitmap;
}

namespace libreshockwave::cast {
class CastMember;
}

namespace libreshockwave::chunks {
class CastMemberChunk;
}

namespace libreshockwave::player::sprite {
class SpriteState;
}

namespace libreshockwave::player::render::pipeline {

class StageRenderer {
public:
    using CastMemberResolver = std::function<std::shared_ptr<const ::libreshockwave::cast::CastMember>(
        int castLib,
        int memberNum)>;

    explicit StageRenderer(DirectorFile* file = nullptr);

    [[nodiscard]] SpriteRegistry& spriteRegistry();
    [[nodiscard]] const SpriteRegistry& spriteRegistry() const;

    [[nodiscard]] int stageWidth() const;
    [[nodiscard]] int stageHeight() const;
    [[nodiscard]] int backgroundColor() const;
    void setBackgroundColor(int color);
    void setDefaultBackgroundColor(int color);

    [[nodiscard]] std::shared_ptr<bitmap::Bitmap> stageImage();
    [[nodiscard]] std::shared_ptr<bitmap::Bitmap> updateStageImageSnapshot(const bitmap::Bitmap& snapshot);
    [[nodiscard]] bool hasStageImage() const;
    [[nodiscard]] std::shared_ptr<const bitmap::Bitmap> renderableStageImage() const;
    void discardStageImage();
    void resetVisualState();

    void setLastBakedSprites(std::vector<RenderSprite> sprites);
    [[nodiscard]] const std::vector<RenderSprite>& lastBakedSprites() const;
    void setCastMemberResolver(CastMemberResolver resolver);

    [[nodiscard]] std::vector<RenderSprite> getSpritesForFrame(int frame);
    void collectScoreSprites(int frame, std::vector<RenderSprite>& sprites, std::set<int>& renderedChannels);
    void collectDynamicSprites(std::vector<RenderSprite>& sprites, const std::set<int>& renderedChannels);
    static void sortSprites(std::vector<RenderSprite>& sprites);

    void reset();
    void onSpriteEnd(int channel);
    void onFrameEnter(int frame);

    [[nodiscard]] static int expandScoreRgb555(int color);

private:
    struct RegPoint {
        int x{0};
        int y{0};
    };

    [[nodiscard]] std::shared_ptr<chunks::ScoreChunk> scoreChunk() const;
    [[nodiscard]] std::optional<RenderSprite> createRenderSprite(int channel,
                                                                 const chunks::ScoreChunk::ChannelData& data);
    [[nodiscard]] std::optional<RenderSprite> createDynamicRenderSprite(const sprite::SpriteState& state);
    [[nodiscard]] std::shared_ptr<const ::libreshockwave::cast::CastMember> resolveCastMember(int castLib,
                                                                                              int memberNum) const;
    [[nodiscard]] bool hasAnyBehavior(const sprite::SpriteState& state) const;
    [[nodiscard]] static RenderSprite applyLegacyRenderProperties(RenderSprite sprite,
                                                                  const sprite::SpriteState& state);
    [[nodiscard]] SpriteType determineSpriteTypeFromMember(
        const std::shared_ptr<const chunks::CastMemberChunk>& member) const;
    [[nodiscard]] SpriteType determineSpriteTypeFromMember(
        const std::shared_ptr<const ::libreshockwave::cast::CastMember>& member) const;
    [[nodiscard]] RegPoint scaledRegPoint(const chunks::CastMemberChunk& member,
                                          int spriteWidth,
                                          int spriteHeight,
                                          int posX,
                                          int posY,
                                          bool flipH,
                                          bool flipV) const;
    [[nodiscard]] RegPoint scaledRegPoint(const ::libreshockwave::cast::CastMember& member,
                                          int spriteWidth,
                                          int spriteHeight,
                                          int posX,
                                          int posY,
                                          bool flipH,
                                          bool flipV) const;
    [[nodiscard]] int resolveScoreColor(int color, bool isRgb);
    [[nodiscard]] bool usesRgb555ScoreColors() const;
    [[nodiscard]] bool usesLegacyRoundedRegistrationScale() const;
    [[nodiscard]] int directorVersionForParsing() const;

    [[nodiscard]] int scoreSpriteRasterY(int y) const;
    [[nodiscard]] int scoreSpriteRasterYOffset() const;
    [[nodiscard]] static int rasterY(int y);
    [[nodiscard]] static int mirrorOffset(int reg, int span, bool flipped);
    [[nodiscard]] int scaleRegistrationOffset(int reg, int spriteSpan, int bitmapSpan) const;
    [[nodiscard]] static bool hasDirectorHorizontalMirror(double rotation, double skew);
    [[nodiscard]] static int normalizeTransformAngle(double angle);
    [[nodiscard]] static int expand5Bit(int value);

    DirectorFile* file_{nullptr};
    SpriteRegistry spriteRegistry_;
    int backgroundColor_{0xFFFFFF};
    int defaultBackgroundColor_{0xFFFFFF};
    std::shared_ptr<bitmap::Bitmap> stageImage_;
    std::vector<RenderSprite> lastBakedSprites_;
    CastMemberResolver castMemberResolver_;
};

} // namespace libreshockwave::player::render::pipeline
