#pragma once

#include <array>
#include <functional>
#include <optional>
#include <vector>

#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/cast/MemberType.hpp"
#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/player/input/InputState.hpp"
#include "libreshockwave/player/render/SpriteRegistry.hpp"
#include "libreshockwave/player/render/pipeline/RenderSprite.hpp"

namespace libreshockwave::player {

class CursorManager {
public:
    struct MemberInfo {
        ::libreshockwave::cast::MemberType memberType{::libreshockwave::cast::MemberType::Unknown};
        bool editable{false};
        int regX{0};
        int regY{0};
    };

    using SpriteProvider = std::function<std::vector<render::pipeline::RenderSprite>()>;
    using MemberInfoResolver = std::function<std::optional<MemberInfo>(int castLib, int memberNum)>;
    using BitmapResolver = std::function<std::optional<bitmap::Bitmap>(int castLib, int memberNum)>;
    using ChannelPredicate = std::function<bool(int channel)>;
    using GlobalCursorSupplier = std::function<lingo::Datum()>;

    static constexpr int ARROW_CURSOR = -1;
    static constexpr int DEFAULT_CURSOR = 0;
    static constexpr int IBEAM_CURSOR = 1;
    static constexpr int WAIT_CURSOR = 4;
    static constexpr int CUSTOM_BITMAP_CURSOR = 5;
    static constexpr int POINTER_CURSOR = 6;

    CursorManager(input::InputState* inputState, render::SpriteRegistry* spriteRegistry);

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
                                    int stageX,
                                    int stageY) const;
    [[nodiscard]] int hitTestInteractive(const std::vector<render::pipeline::RenderSprite>& sprites,
                                         int stageX,
                                         int stageY) const;
    [[nodiscard]] std::optional<render::pipeline::RenderSprite> findSpriteByChannel(
        const std::vector<render::pipeline::RenderSprite>& sprites,
        int channel) const;
    [[nodiscard]] bool isNavigatorWhitespace(const render::pipeline::RenderSprite* sprite, int stageX, int stageY) const;
    [[nodiscard]] lingo::Datum getGlobalCursorDatum() const;
    [[nodiscard]] int getGlobalCursorCode() const;
    [[nodiscard]] std::optional<bitmap::Bitmap> resolveCursorBitmap(int encodedMember, int encodedMask) const;
    [[nodiscard]] std::optional<std::array<int, 2>> resolveCursorRegPoint(int encodedMember) const;
    [[nodiscard]] bool isInteractive(int channel) const;

    input::InputState* inputState_{nullptr};
    render::SpriteRegistry* spriteRegistry_{nullptr};
    SpriteProvider spriteProvider_;
    MemberInfoResolver memberInfoResolver_;
    BitmapResolver bitmapResolver_;
    ChannelPredicate interactivePredicate_;
    GlobalCursorSupplier globalCursorSupplier_;
};

} // namespace libreshockwave::player
