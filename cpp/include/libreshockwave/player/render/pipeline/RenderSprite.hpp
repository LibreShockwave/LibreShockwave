#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "libreshockwave/id/Ids.hpp"

namespace libreshockwave::bitmap {
class Bitmap;
}

namespace libreshockwave::cast {
class CastMember;
}

namespace libreshockwave::chunks {
class CastMemberChunk;
}

namespace libreshockwave::player::render::pipeline {

enum class SpriteType {
    Bitmap,
    Shape,
    Text,
    Button,
    FilmLoop,
    Shockwave3D,
    Unknown
};

[[nodiscard]] std::string_view name(SpriteType type);

class RenderSprite {
public:
    RenderSprite(int channel,
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
                 int blend);

    RenderSprite(int channel,
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
                 bool hasBehaviors);

    RenderSprite(int channel,
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
                 bool hasBehaviors);

    [[nodiscard]] id::ChannelId channelId() const;
    [[nodiscard]] int channel() const;
    [[nodiscard]] int x() const;
    [[nodiscard]] int y() const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] int locZ() const;
    [[nodiscard]] bool isVisible() const;
    [[nodiscard]] SpriteType type() const;
    [[nodiscard]] std::shared_ptr<const chunks::CastMemberChunk> castMember() const;
    [[nodiscard]] std::shared_ptr<const ::libreshockwave::cast::CastMember> dynamicMember() const;
    [[nodiscard]] int foreColor() const;
    [[nodiscard]] int backColor() const;
    [[nodiscard]] bool hasForeColor() const;
    [[nodiscard]] bool hasBackColor() const;
    [[nodiscard]] id::InkMode inkMode() const;
    [[nodiscard]] int ink() const;
    [[nodiscard]] int blend() const;
    [[nodiscard]] bool isFlipH() const;
    [[nodiscard]] bool isFlipV() const;
    [[nodiscard]] double rotation() const;
    [[nodiscard]] double skew() const;
    [[nodiscard]] std::shared_ptr<const bitmap::Bitmap> bakedBitmap() const;
    [[nodiscard]] bool hasBehaviors() const;
    [[nodiscard]] std::optional<int> shapeLineSize() const;
    [[nodiscard]] std::optional<int> shapePattern() const;
    [[nodiscard]] bool hasDirectorHorizontalMirror() const;
    [[nodiscard]] RenderSprite withBakedBitmap(std::shared_ptr<const bitmap::Bitmap> baked) const;
    [[nodiscard]] RenderSprite withBakedBitmapAndSize(std::shared_ptr<const bitmap::Bitmap> baked,
                                                      int newWidth,
                                                      int newHeight) const;
    [[nodiscard]] RenderSprite withShapeLineSize(int lineSize) const;
    [[nodiscard]] RenderSprite withShapePattern(int pattern) const;
    [[nodiscard]] int castMemberId() const;
    [[nodiscard]] std::optional<std::string> memberName() const;

private:
    id::ChannelId channelId_;
    int x_{0};
    int y_{0};
    int width_{0};
    int height_{0};
    int locZ_{0};
    bool visible_{false};
    SpriteType type_{SpriteType::Unknown};
    std::shared_ptr<const chunks::CastMemberChunk> castMember_;
    std::shared_ptr<const ::libreshockwave::cast::CastMember> dynamicMember_;
    int foreColor_{0};
    int backColor_{0};
    bool hasForeColor_{false};
    bool hasBackColor_{false};
    id::InkMode inkMode_{id::InkMode::COPY};
    int blend_{0};
    bool flipH_{false};
    bool flipV_{false};
    double rotation_{0.0};
    double skew_{0.0};
    std::shared_ptr<const bitmap::Bitmap> bakedBitmap_;
    bool hasBehaviors_{false};
    std::optional<int> shapeLineSize_;
    std::optional<int> shapePattern_;
};

} // namespace libreshockwave::player::render::pipeline
