#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/player/render/sprite_registry.hpp"
#include "libreshockwave/player/sprite/sprite_state.hpp"

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
        int reg_x{0};
        int reg_y{0};
        bool bitmap{false};
        int bitmap_width{0};
        int bitmap_height{0};
        int bitmap_reg_x{0};
        int bitmap_reg_y{0};
        bool runtime_dynamic{false};
        bool has_image{false};
        lingo::Datum image{lingo::Datum::voidValue()};
    };

    using MemberInfoResolver = std::function<std::optional<MemberInfo>(int cast_lib, int member_num)>;
    using MemberNameResolver = std::function<std::optional<lingo::Datum::CastMemberRef>(const std::string& name)>;
    using MemberImageSetter = std::function<bool(int cast_lib, int member_num, const lingo::Datum& value)>;

    explicit SpriteProperties(render::SpriteRegistry* registry = nullptr);

    void setSpriteRegistry(render::SpriteRegistry* registry);
    void setMemberInfoResolver(MemberInfoResolver resolver);
    void setMemberNameResolver(MemberNameResolver resolver);
    void setMemberImageSetter(MemberImageSetter setter);
    void setLegacyRoundedRegistrationScale(bool enabled);

    [[nodiscard]] std::optional<std::vector<lingo::Datum>> getScriptInstanceList(int sprite_num) const;
    [[nodiscard]] std::vector<lingo::Datum>* mutableScriptInstanceList(int sprite_num);
    [[nodiscard]] lingo::Datum getSpriteProp(int sprite_num, std::string_view prop_name) const;
    [[nodiscard]] bool setSpriteProp(int sprite_num, std::string_view prop_name, const lingo::Datum& value);
    [[nodiscard]] bool setSpriteMember(int sprite_num, const lingo::Datum& value);
    [[nodiscard]] lingo::Datum callSpriteMethod(int sprite_num,
                                                std::string_view method_name,
                                                const std::vector<lingo::Datum>& args);

    [[nodiscard]] SpriteBounds resolveSpriteBounds(const sprite::SpriteState& sprite) const;

    [[nodiscard]] static int encodeCursorMember(const lingo::Datum& datum);
    [[nodiscard]] static bool hasDirectorHorizontalMirror(double rotation, double skew);

private:
    [[nodiscard]] lingo::Datum defaultProp(int sprite_num, std::string_view prop) const;
    [[nodiscard]] bool assignMember(sprite::SpriteState& sprite, const lingo::Datum& value, bool via_set_member_method);
    void autoSizeSprite(sprite::SpriteState& sprite, int cast_lib, int member_num, bool via_set_member_method) const;
    [[nodiscard]] bool setColorValue(const lingo::Datum& value, const std::function<void(int)>& setter) const;
    [[nodiscard]] int coerceInkCode(const lingo::Datum& value) const;
    [[nodiscard]] bool effectiveFlipH(const sprite::SpriteState& sprite) const;
    [[nodiscard]] int mirrorOffset(int reg, int span, bool flipped) const;
    [[nodiscard]] int scaleRegistrationOffset(int reg, int sprite_span, int bitmap_span) const;
    [[nodiscard]] int constraintChannel(const sprite::SpriteState& sprite) const;
    [[nodiscard]] std::pair<int, int> constrainLoc(const sprite::SpriteState& sprite,
                                                   int loc_h,
                                                   int loc_v) const;
    void setConstrainedLoc(sprite::SpriteState& sprite, int loc_h, int loc_v, bool update_h, bool update_v) const;

    static void applyEmptyMemberOverride(sprite::SpriteState& sprite);
    static void resetReleasedEmptyChannel(sprite::SpriteState& sprite);

    [[nodiscard]] std::vector<lingo::Datum>* brokerScriptList(int sprite_num);
    [[nodiscard]] lingo::Datum* primaryBroker(int sprite_num);

    render::SpriteRegistry* registry_{nullptr};
    MemberInfoResolver member_info_resolver_;
    MemberNameResolver member_name_resolver_;
    MemberImageSetter member_image_setter_;
    bool legacy_rounded_registration_scale_{false};
};

} // namespace libreshockwave::player
