#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "libreshockwave/chunks/score_chunk.hpp"
#include "libreshockwave/player/render/sprite_registry.hpp"
#include "libreshockwave/player/render/pipeline/render_sprite.hpp"

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
        int cast_lib,
        int member_num)>;

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
    void collectScoreSprites(int frame, std::vector<RenderSprite>& sprites, std::set<int>& rendered_channels);
    void collectDynamicSprites(std::vector<RenderSprite>& sprites, const std::set<int>& rendered_channels);
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
    [[nodiscard]] std::shared_ptr<const ::libreshockwave::cast::CastMember> resolveCastMember(int cast_lib,
                                                                                              int member_num) const;
    [[nodiscard]] bool hasAnyBehavior(const sprite::SpriteState& state) const;
    [[nodiscard]] static RenderSprite applyLegacyRenderProperties(RenderSprite sprite,
                                                                  const sprite::SpriteState& state);
    [[nodiscard]] SpriteType determineSpriteTypeFromMember(
        const std::shared_ptr<const chunks::CastMemberChunk>& member) const;
    [[nodiscard]] SpriteType determineSpriteTypeFromMember(
        const std::shared_ptr<const ::libreshockwave::cast::CastMember>& member) const;
    [[nodiscard]] RegPoint scaledRegPoint(const chunks::CastMemberChunk& member,
                                          int sprite_width,
                                          int sprite_height,
                                          int pos_x,
                                          int pos_y,
                                          bool flip_h,
                                          bool flip_v) const;
    [[nodiscard]] RegPoint scaledRegPoint(const ::libreshockwave::cast::CastMember& member,
                                          int sprite_width,
                                          int sprite_height,
                                          int pos_x,
                                          int pos_y,
                                          bool flip_h,
                                          bool flip_v) const;
    [[nodiscard]] int resolveScoreColor(int color, bool is_rgb);
    [[nodiscard]] bool usesRgb555ScoreColors() const;
    [[nodiscard]] bool usesLegacyRoundedRegistrationScale() const;
    [[nodiscard]] int directorVersionForParsing() const;

    [[nodiscard]] int scoreSpriteRasterY(int y) const;
    [[nodiscard]] int scoreSpriteRasterYOffset() const;
    [[nodiscard]] static int rasterY(int y);
    [[nodiscard]] static int mirrorOffset(int reg, int span, bool flipped);
    [[nodiscard]] int scaleRegistrationOffset(int reg, int sprite_span, int bitmap_span) const;
    [[nodiscard]] static bool hasDirectorHorizontalMirror(double rotation, double skew);
    [[nodiscard]] static int normalizeTransformAngle(double angle);
    [[nodiscard]] static int expand5Bit(int value);

    DirectorFile* file_{nullptr};
    SpriteRegistry sprite_registry_;
    int background_color_{0xFFFFFF};
    int default_background_color_{0xFFFFFF};
    std::shared_ptr<bitmap::Bitmap> stage_image_;
    std::vector<RenderSprite> last_baked_sprites_;
    CastMemberResolver cast_member_resolver_;
};

} // namespace libreshockwave::player::render::pipeline
