#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/player/render/SpriteRegistry.hpp"
#include "libreshockwave/player/sprite/SpriteState.hpp"

namespace libreshockwave::player {

class SpriteProperties {
public:
    struct SpriteBounds {
        int left{0};
        int top{0};
        int right{0};
        int bottom{0};

        friend bool operator==(const SpriteBounds&, const SpriteBounds&) = default;
    };

    struct MemberInfo {
        int width{0};
        int height{0};
        int regX{0};
        int regY{0};
        bool bitmap{false};
        int bitmapWidth{0};
        int bitmapHeight{0};
        int bitmapRegX{0};
        int bitmapRegY{0};
        bool runtimeDynamic{false};
        bool hasImage{false};
        lingo::Datum image{lingo::Datum::voidValue()};
    };

    using MemberInfoResolver = std::function<std::optional<MemberInfo>(int castLib, int memberNum)>;
    using MemberNameResolver = std::function<std::optional<lingo::Datum::CastMemberRef>(const std::string& name)>;
    using MemberImageSetter = std::function<bool(int castLib, int memberNum, const lingo::Datum& value)>;

    explicit SpriteProperties(render::SpriteRegistry* registry = nullptr);

    void setSpriteRegistry(render::SpriteRegistry* registry);
    void setMemberInfoResolver(MemberInfoResolver resolver);
    void setMemberNameResolver(MemberNameResolver resolver);
    void setMemberImageSetter(MemberImageSetter setter);
    void setLegacyRoundedRegistrationScale(bool enabled);

    [[nodiscard]] std::optional<std::vector<lingo::Datum>> getScriptInstanceList(int spriteNum) const;
    [[nodiscard]] std::vector<lingo::Datum>* mutableScriptInstanceList(int spriteNum);
    [[nodiscard]] lingo::Datum getSpriteProp(int spriteNum, std::string_view propName) const;
    [[nodiscard]] bool setSpriteProp(int spriteNum, std::string_view propName, const lingo::Datum& value);
    [[nodiscard]] bool setSpriteMember(int spriteNum, const lingo::Datum& value);
    [[nodiscard]] lingo::Datum callSpriteMethod(int spriteNum,
                                                std::string_view methodName,
                                                const std::vector<lingo::Datum>& args);

    [[nodiscard]] SpriteBounds resolveSpriteBounds(const sprite::SpriteState& sprite) const;

    [[nodiscard]] static int encodeCursorMember(const lingo::Datum& datum);
    [[nodiscard]] static bool hasDirectorHorizontalMirror(double rotation, double skew);

private:
    [[nodiscard]] lingo::Datum defaultProp(int spriteNum, std::string_view prop) const;
    [[nodiscard]] bool assignMember(sprite::SpriteState& sprite, const lingo::Datum& value, bool viaSetMemberMethod);
    void autoSizeSprite(sprite::SpriteState& sprite, int castLib, int memberNum, bool viaSetMemberMethod) const;
    [[nodiscard]] bool setColorValue(const lingo::Datum& value, const std::function<void(int)>& setter) const;
    [[nodiscard]] int coerceInkCode(const lingo::Datum& value) const;
    [[nodiscard]] bool effectiveFlipH(const sprite::SpriteState& sprite) const;
    [[nodiscard]] int mirrorOffset(int reg, int span, bool flipped) const;
    [[nodiscard]] int scaleRegistrationOffset(int reg, int spriteSpan, int bitmapSpan) const;
    [[nodiscard]] int constraintChannel(const sprite::SpriteState& sprite) const;
    [[nodiscard]] std::pair<int, int> constrainLoc(const sprite::SpriteState& sprite,
                                                   int locH,
                                                   int locV) const;
    void setConstrainedLoc(sprite::SpriteState& sprite, int locH, int locV, bool updateH, bool updateV) const;

    static void applyEmptyMemberOverride(sprite::SpriteState& sprite);
    static void resetReleasedEmptyChannel(sprite::SpriteState& sprite);

    [[nodiscard]] std::vector<lingo::Datum>* brokerScriptList(int spriteNum);
    [[nodiscard]] lingo::Datum* primaryBroker(int spriteNum);

    render::SpriteRegistry* registry_{nullptr};
    MemberInfoResolver memberInfoResolver_;
    MemberNameResolver memberNameResolver_;
    MemberImageSetter memberImageSetter_;
    bool legacyRoundedRegistrationScale_{false};
};

} // namespace libreshockwave::player
