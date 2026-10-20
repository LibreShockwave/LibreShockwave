#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "libreshockwave/id/ids.hpp"

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
                 std::shared_ptr<const chunks::CastMemberChunk> cast_member,
                 int fore_color,
                 int back_color,
                 int ink,
                 int blend);

    RenderSprite(int channel,
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
                 bool has_behaviors);

    RenderSprite(int channel,
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
                 bool has_behaviors);

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
                                                      int new_width,
                                                      int new_height) const;
    [[nodiscard]] RenderSprite withShapeLineSize(int line_size) const;
    [[nodiscard]] RenderSprite withShapePattern(int pattern) const;
    [[nodiscard]] int castMemberId() const;
    [[nodiscard]] std::optional<std::string> memberName() const;

private:
    id::ChannelId channel_id_;
    int x_{0};
    int y_{0};
    int width_{0};
    int height_{0};
    int loc_z_{0};
    bool visible_{false};
    SpriteType type_{SpriteType::Unknown};
    std::shared_ptr<const chunks::CastMemberChunk> cast_member_;
    std::shared_ptr<const ::libreshockwave::cast::CastMember> dynamic_member_;
    int fore_color_{0};
    int back_color_{0};
    bool has_fore_color_{false};
    bool has_back_color_{false};
    id::InkMode ink_mode_{id::InkMode::COPY};
    int blend_{0};
    bool flip_h_{false};
    bool flip_v_{false};
    double rotation_{0.0};
    double skew_{0.0};
    std::shared_ptr<const bitmap::Bitmap> baked_bitmap_;
    bool has_behaviors_{false};
    std::optional<int> shape_line_size_;
    std::optional<int> shape_pattern_;
};

} // namespace libreshockwave::player::render::pipeline
