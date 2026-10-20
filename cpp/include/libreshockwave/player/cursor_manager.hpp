#pragma once

#include <array>
#include <functional>
#include <optional>
#include <vector>

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/cast/member_type.hpp"
#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/player/input/input_state.hpp"
#include "libreshockwave/player/render/sprite_registry.hpp"
#include "libreshockwave/player/render/pipeline/render_sprite.hpp"

namespace libreshockwave::player {

class CursorManager {
public:
    struct MemberInfo {
        ::libreshockwave::cast::MemberType member_type{::libreshockwave::cast::MemberType::Unknown};
        bool editable{false};
        int reg_x{0};
        int reg_y{0};
    };

    using SpriteProvider = std::function<std::vector<render::pipeline::RenderSprite>()>;
    using MemberInfoResolver = std::function<std::optional<MemberInfo>(int cast_lib, int member_num)>;
    using BitmapResolver = std::function<std::optional<bitmap::Bitmap>(int cast_lib, int member_num)>;
    using ChannelPredicate = std::function<bool(int channel)>;
    using GlobalCursorSupplier = std::function<lingo::Datum()>;

    static constexpr int ARROW_CURSOR = -1;
    static constexpr int DEFAULT_CURSOR = 0;
    static constexpr int IBEAM_CURSOR = 1;
    static constexpr int WAIT_CURSOR = 4;
    static constexpr int CUSTOM_BITMAP_CURSOR = 5;
    static constexpr int POINTER_CURSOR = 6;

    CursorManager(input::InputState* input_state, render::SpriteRegistry* sprite_registry);

    void setSpriteProvider(SpriteProvider provider);
    void setMemberInfoResolver(MemberInfoResolver resolver);
    void setBitmapResolver(BitmapResolver resolver);
    void setInteractivePredicate(ChannelPredicate predicate);
    void setGlobalCursorSupplier(GlobalCursorSupplier supplier);

    [[nodiscard]] int getCursorAtMouse() const;
    [[nodiscard]] std::optional<bitmap::Bitmap> getCursorBitmap() const;
    [[nodiscard]] std::optional<std::array<int, 2>> getCursorRegPoint() const;

    [[nodiscard]] static bitmap::Bitmap applyCursorMask(const bitmap::Bitmap& cursor, const bitmap::Bitmap& mask);
    [[nodiscard]] static int cursorCodeFromDatum(const lingo::Datum& datum);
    [[nodiscard]] static int encodeCursorMember(const lingo::Datum& datum);
    [[nodiscard]] static bool isNearWhite(std::uint32_t pixel);

private:
    [[nodiscard]] std::vector<render::pipeline::RenderSprite> currentSprites() const;
    [[nodiscard]] int hitTestVisual(const std::vector<render::pipeline::RenderSprite>& sprites,
                                    int stage_x,
                                    int stage_y) const;
    [[nodiscard]] int hitTestInteractive(const std::vector<render::pipeline::RenderSprite>& sprites,
                                         int stage_x,
                                         int stage_y) const;
    [[nodiscard]] std::optional<render::pipeline::RenderSprite> findSpriteByChannel(
        const std::vector<render::pipeline::RenderSprite>& sprites,
        int channel) const;
    [[nodiscard]] bool isNavigatorWhitespace(const render::pipeline::RenderSprite* sprite, int stage_x, int stage_y) const;
    [[nodiscard]] lingo::Datum getGlobalCursorDatum() const;
    [[nodiscard]] int getGlobalCursorCode() const;
    [[nodiscard]] std::optional<bitmap::Bitmap> resolveCursorBitmap(int encoded_member, int encoded_mask) const;
    [[nodiscard]] std::optional<std::array<int, 2>> resolveCursorRegPoint(int encoded_member) const;
    [[nodiscard]] bool isInteractive(int channel) const;

    input::InputState* input_state_{nullptr};
    render::SpriteRegistry* sprite_registry_{nullptr};
    SpriteProvider sprite_provider_;
    MemberInfoResolver member_info_resolver_;
    BitmapResolver bitmap_resolver_;
    ChannelPredicate interactive_predicate_;
    GlobalCursorSupplier global_cursor_supplier_;
};

} // namespace libreshockwave::player
