#include "libreshockwave/player/sprite_properties.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>

#include "libreshockwave/id/ids.hpp"
#include "libreshockwave/player/cursor_manager.hpp"

namespace libreshockwave::player {
namespace {

std::string lowerProp(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

int normalizeTransformAngle(double angle) {
    int normalized = static_cast<int>(std::lround(angle)) % 360;
    if (normalized < 0) {
        normalized += 360;
    }
    return normalized;
}

std::string normalizeLegacySpriteProp(std::string_view prop) {
    const std::string lower = lowerProp(prop);
    if (lower == "moveable") return "moveablesprite";
    if (lower == "editable") return "editabletext";
    if (lower == "currenttime") return "movietime";
    return lower;
}

std::optional<lingo::Datum> defaultLegacySpriteProp(std::string_view prop) {
    const std::string normalized = normalizeLegacySpriteProp(prop);
    if (normalized == "constraint" ||
        normalized == "immediate" ||
        normalized == "movierate" ||
        normalized == "movietime" ||
        normalized == "scorecolor" ||
        normalized == "scriptnum" ||
        normalized == "starttime" ||
        normalized == "stoptime" ||
        normalized == "mostrecentcuepoint" ||
        normalized == "tweened" ||
        normalized == "moveablesprite" ||
        normalized == "editabletext") {
        return lingo::Datum::of(0);
    }
    if (normalized == "linesize" || normalized == "pattern") {
        return lingo::Datum::of(1);
    }
    if (normalized == "volume") {
        return lingo::Datum::of(256);
    }
    if (normalized == "name") {
        return lingo::Datum::of(std::string());
    }
    return std::nullopt;
}

bool isLegacySpriteRuntimeProp(std::string_view prop) {
    return defaultLegacySpriteProp(prop).has_value();
}

int castLibForPlainMemberAssignment(const sprite::SpriteState& sprite) {
    const int cast_lib = sprite.effectiveCastLib();
    return cast_lib > 0 ? cast_lib : 1;
}

lingo::Datum rgbColorDatum(int rgb) {
    return lingo::Datum::colorRef((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

lingo::Datum normalizedColorDatum(const lingo::Datum& value, int rgb) {
    return value.asColorRef() != nullptr ? value : rgbColorDatum(rgb);
}

std::optional<int> spriteChannelFromDatum(const lingo::Datum& datum) {
    if (const auto* sprite_ref = datum.asSpriteRef()) {
        return sprite_ref->spriteNum();
    }
    if (datum.asInt() != nullptr || datum.asFloat() != nullptr) {
        return datum.intValue();
    }
    return std::nullopt;
}

SpriteProperties::SpriteBounds normalizedBounds(SpriteProperties::SpriteBounds bounds) {
    if (bounds.left > bounds.right) {
        std::swap(bounds.left, bounds.right);
    }
    if (bounds.top > bounds.bottom) {
        std::swap(bounds.top, bounds.bottom);
    }
    return bounds;
}

bool hasPositiveArea(const SpriteProperties::SpriteBounds& bounds) {
    return bounds.right > bounds.left && bounds.bottom > bounds.top;
}

bool boundsIntersect(SpriteProperties::SpriteBounds first, SpriteProperties::SpriteBounds second) {
    first = normalizedBounds(first);
    second = normalizedBounds(second);
    return hasPositiveArea(first) && hasPositiveArea(second) &&
           first.right >= second.left && second.right >= first.left &&
           first.bottom >= second.top && second.bottom >= first.top;
}

bool boundsWithin(SpriteProperties::SpriteBounds first, SpriteProperties::SpriteBounds second) {
    first = normalizedBounds(first);
    second = normalizedBounds(second);
    return hasPositiveArea(first) && hasPositiveArea(second) &&
           first.left >= second.left && first.top >= second.top &&
           first.right <= second.right && first.bottom <= second.bottom;
}

} // namespace

SpriteProperties::SpriteProperties(render::SpriteRegistry* registry)
    : registry_(registry) {}

void SpriteProperties::setSpriteRegistry(render::SpriteRegistry* registry) {
    registry_ = registry;
}

void SpriteProperties::setMemberInfoResolver(MemberInfoResolver resolver) {
    member_info_resolver_ = std::move(resolver);
}

void SpriteProperties::setMemberNameResolver(MemberNameResolver resolver) {
    member_name_resolver_ = std::move(resolver);
}

void SpriteProperties::setMemberImageSetter(MemberImageSetter setter) {
    member_image_setter_ = std::move(setter);
}

void SpriteProperties::setLegacyRoundedRegistrationScale(bool enabled) {
    legacy_rounded_registration_scale_ = enabled;
}

std::optional<std::vector<lingo::Datum>> SpriteProperties::getScriptInstanceList(int sprite_num) const {
    if (registry_ == nullptr) {
        return std::nullopt;
    }
    auto sprite = registry_->get(sprite_num);
    if (sprite == nullptr || sprite->scriptInstanceList().empty()) {
        return std::nullopt;
    }
    return sprite->scriptInstanceList();
}

std::vector<lingo::Datum>* SpriteProperties::mutableScriptInstanceList(int sprite_num) {
    if (registry_ == nullptr) {
        return nullptr;
    }
    auto sprite = registry_->get(sprite_num);
    if (sprite == nullptr || sprite->scriptInstanceList().empty()) {
        return nullptr;
    }
    return &sprite->scriptInstanceList();
}

lingo::Datum SpriteProperties::getSpriteProp(int sprite_num, std::string_view prop_name) const {
    const std::string prop = lowerProp(prop_name);
    if (registry_ == nullptr) {
        return defaultProp(sprite_num, prop);
    }

    auto sprite = registry_->get(sprite_num);
    if (sprite == nullptr) {
        return defaultProp(sprite_num, prop);
    }

    if (prop == "loch") return lingo::Datum::of(sprite->locH());
    if (prop == "locv") return lingo::Datum::of(sprite->locV());
    if (prop == "locz") return lingo::Datum::of(sprite->locZ());
    if (prop == "loc") return lingo::Datum::intPoint(sprite->locH(), sprite->locV());
    if (prop == "width") return lingo::Datum::of(sprite->width());
    if (prop == "height") return lingo::Datum::of(sprite->height());
    if (prop == "visible") return lingo::Datum::of(sprite->isVisible() ? 1 : 0);
    if (prop == "puppet") return lingo::Datum::of(sprite->isPuppet() ? 1 : 0);
    if (prop == "ink") return lingo::Datum::of(sprite->ink());
    if (prop == "blend") return lingo::Datum::of(sprite->blend());
    if (prop == "stretch") return lingo::Datum::of(sprite->stretch());
    if (prop == "forecolor") return lingo::Datum::of(sprite->foreColor());
    if (prop == "backcolor") return lingo::Datum::of(sprite->backColor());
    if (prop == "color") return sprite->colorDatum().value_or(rgbColorDatum(sprite->foreColor()));
    if (prop == "bgcolor") return sprite->bgColorDatum().value_or(rgbColorDatum(sprite->backColor()));
    if (prop == "left") return lingo::Datum::of(resolveSpriteBounds(*sprite).left);
    if (prop == "top") return lingo::Datum::of(resolveSpriteBounds(*sprite).top);
    if (prop == "right") return lingo::Datum::of(resolveSpriteBounds(*sprite).right);
    if (prop == "bottom") return lingo::Datum::of(resolveSpriteBounds(*sprite).bottom);
    if (prop == "rect") {
        const auto bounds = resolveSpriteBounds(*sprite);
        return lingo::Datum::intRect(bounds.left, bounds.top, bounds.right, bounds.bottom);
    }
    if (prop == "spritenum") return lingo::Datum::of(sprite_num);
    if (prop == "type") return lingo::Datum::of(1);
    if (prop == "castnum" || prop == "membernum") return lingo::Datum::of(sprite->effectiveCastMember());
    if (prop == "castlibnum") return lingo::Datum::of(sprite->effectiveCastLib());
    if (prop == "member") {
        const int cast_lib = sprite->effectiveCastLib();
        const int member = sprite->effectiveCastMember();
        return member > 0 ? lingo::Datum::castMemberRef(lingo::Datum::CastMemberRef{cast_lib, member})
                          : lingo::Datum::voidValue();
    }
    if (prop == "image") {
        if (member_info_resolver_ != nullptr) {
            if (auto info = member_info_resolver_(sprite->effectiveCastLib(), sprite->effectiveCastMember());
                info.has_value() && info->has_image) {
                return info->image;
            }
        }
        return lingo::Datum::voidValue();
    }
    if (prop == "ilk") return lingo::Datum::symbol("sprite");
    if (prop == "rotation") return lingo::Datum::of(sprite->rotation());
    if (prop == "skew") return lingo::Datum::of(sprite->skew());
    if (prop == "fliph") return lingo::Datum::of(sprite->isFlipH() ? 1 : 0);
    if (prop == "flipv") return lingo::Datum::of(sprite->isFlipV() ? 1 : 0);
    if (prop == "trails") return lingo::Datum::of(sprite->trails());
    if (prop == "cursor") return lingo::Datum::of(sprite->cursor());
    if (prop == "scriptinstancelist") return lingo::Datum::list(sprite->scriptInstanceList());
    const std::string legacy_prop = normalizeLegacySpriteProp(prop);
    if (auto stored = sprite->legacyProperty(legacy_prop)) {
        return *stored;
    }
    if (auto legacy_default = defaultLegacySpriteProp(legacy_prop)) {
        return *legacy_default;
    }
    return lingo::Datum::voidValue();
}

bool SpriteProperties::setSpriteProp(int sprite_num, std::string_view prop_name, const lingo::Datum& value) {
    if (registry_ == nullptr) {
        return false;
    }

    const std::string prop = lowerProp(prop_name);
    auto sprite = registry_->getOrCreateDynamic(sprite_num);
    registry_->bumpRevision();

    if (prop == "loch") {
        setConstrainedLoc(*sprite, value.intValue(), sprite->locV(), true, false);
        return true;
    }
    if (prop == "locv") {
        setConstrainedLoc(*sprite, sprite->locH(), value.intValue(), false, true);
        return true;
    }
    if (prop == "locz") {
        sprite->setLocZ(value.intValue());
        return true;
    }
    if (prop == "loc") {
        const auto* point = value.asIntPoint();
        if (point == nullptr) {
            return false;
        }
        setConstrainedLoc(*sprite, point->x, point->y, true, true);
        return true;
    }
    if (prop == "rect") {
        const auto* rect = value.asIntRect();
        if (rect == nullptr) {
            return false;
        }
        sprite->setWidth(rect->width());
        sprite->setHeight(rect->height());
        setConstrainedLoc(*sprite, rect->left, rect->top, true, true);
        return true;
    }
    if (prop == "left") {
        const int left = value.intValue();
        const auto bounds = resolveSpriteBounds(*sprite);
        sprite->setWidth(bounds.right - left);
        const auto updated = resolveSpriteBounds(*sprite);
        setConstrainedLoc(*sprite, sprite->locH() + left - updated.left, sprite->locV(), true, false);
        return true;
    }
    if (prop == "top") {
        const int top = value.intValue();
        const auto bounds = resolveSpriteBounds(*sprite);
        sprite->setHeight(bounds.bottom - top);
        const auto updated = resolveSpriteBounds(*sprite);
        setConstrainedLoc(*sprite, sprite->locH(), sprite->locV() + top - updated.top, false, true);
        return true;
    }
    if (prop == "right") {
        const int right = value.intValue();
        const auto bounds = resolveSpriteBounds(*sprite);
        sprite->setWidth(right - bounds.left);
        const auto updated = resolveSpriteBounds(*sprite);
        setConstrainedLoc(*sprite, sprite->locH() + bounds.left - updated.left, sprite->locV(), true, false);
        return true;
    }
    if (prop == "bottom") {
        const int bottom = value.intValue();
        const auto bounds = resolveSpriteBounds(*sprite);
        sprite->setHeight(bottom - bounds.top);
        const auto updated = resolveSpriteBounds(*sprite);
        setConstrainedLoc(*sprite, sprite->locH(), sprite->locV() + bounds.top - updated.top, false, true);
        return true;
    }
    if (prop == "visible") {
        sprite->setVisible(value.boolValue());
        return true;
    }
    if (prop == "puppet") {
        sprite->setPuppet(value.boolValue());
        if (!sprite->isPuppet() && sprite->effectiveCastMember() <= 0) {
            resetReleasedEmptyChannel(*sprite);
        }
        return true;
    }
    if (prop == "ink") {
        if (!value.isVoid()) {
            sprite->setInk(coerceInkCode(value));
        }
        return true;
    }
    if (prop == "blend") {
        if (!value.isVoid()) {
            sprite->setBlend(value.intValue());
        }
        return true;
    }
    if (prop == "stretch") {
        sprite->setStretch(value.intValue());
        return true;
    }
    if (prop == "trails") {
        sprite->setTrails(value.intValue());
        return true;
    }
    if (prop == "width") {
        sprite->setWidth(value.intValue());
        return true;
    }
    if (prop == "height") {
        sprite->setHeight(value.intValue());
        return true;
    }
    if (prop == "image") {
        if (member_image_setter_ != nullptr && sprite->effectiveCastMember() > 0) {
            return member_image_setter_(sprite->effectiveCastLib(), sprite->effectiveCastMember(), value);
        }
        return true;
    }
    if (prop == "forecolor") {
        if (!value.isVoid()) {
            sprite->setForeColor(value.intValue());
        }
        return true;
    }
    if (prop == "backcolor") {
        if (!value.isVoid()) {
            sprite->setBackColor(value.intValue());
        }
        return true;
    }
    if (prop == "member") {
        return assignMember(*sprite, value, false);
    }
    if (prop == "castnum" || prop == "membernum") {
        const int slot = value.intValue();
        if (slot <= 0) {
            applyEmptyMemberOverride(*sprite);
            return true;
        }
        const int encoded_cast = (slot >> 16) & 0xFFFF;
        const int encoded_member = slot & 0xFFFF;
        if (encoded_cast > 0 && encoded_member > 0) {
            sprite->setDynamicMember(encoded_cast, encoded_member);
            autoSizeSprite(*sprite, encoded_cast, encoded_member, false);
        } else {
            const int cast_lib = castLibForPlainMemberAssignment(*sprite);
            sprite->setDynamicMember(cast_lib, slot);
            autoSizeSprite(*sprite, cast_lib, slot, false);
        }
        return true;
    }
    if (prop == "castlibnum") {
        const int cast_lib = value.intValue();
        const int member_num = sprite->effectiveCastMember();
        sprite->setDynamicMember(cast_lib, member_num);
        if (member_num > 0) {
            autoSizeSprite(*sprite, cast_lib, member_num, false);
        }
        return true;
    }
    if (prop == "color") {
        return setColorValue(value, [sprite, value](int color) {
            sprite->setColorDatum(color, normalizedColorDatum(value, color));
        });
    }
    if (prop == "bgcolor") {
        return setColorValue(value, [sprite, value](int color) {
            sprite->setBgColorDatum(color, normalizedColorDatum(value, color));
        });
    }
    if (prop == "rotation") {
        sprite->setRotation(value.floatValue());
        return true;
    }
    if (prop == "skew") {
        sprite->setSkew(value.floatValue());
        return true;
    }
    if (prop == "fliph") {
        sprite->setFlipH(value.boolValue());
        return true;
    }
    if (prop == "flipv") {
        sprite->setFlipV(value.boolValue());
        return true;
    }
    if (prop == "scriptinstancelist") {
        if (value.isList()) {
            auto items = value.listValue().items();
            for (auto& item : items) {
                if (item.type() == lingo::DatumType::ScriptInstanceRef) {
                    item.scriptInstanceValue().setProperty("spritenum", lingo::Datum::of(sprite_num));
                }
            }
            sprite->setScriptInstanceList(std::move(items));
        } else {
            sprite->setScriptInstanceList({});
        }
        return true;
    }
    if (prop == "cursor") {
        if (value.isList() && value.listValue().items().size() >= 2) {
            const auto& items = value.listValue().items();
            sprite->setCursorMembers(encodeCursorMember(items[0]), encodeCursorMember(items[1]));
        } else {
            sprite->setCursor(CursorManager::cursorCodeFromDatum(value));
        }
        return true;
    }
    if (isLegacySpriteRuntimeProp(prop)) {
        const std::string legacy_prop = normalizeLegacySpriteProp(prop);
        sprite->setLegacyProperty(legacy_prop, value);
        if (legacy_prop == "constraint") {
            setConstrainedLoc(*sprite, sprite->locH(), sprite->locV(), false, false);
        }
        return true;
    }
    if (prop == "type" || prop == "id") {
        return true;
    }
    return false;
}

bool SpriteProperties::setSpriteMember(int sprite_num, const lingo::Datum& value) {
    if (registry_ == nullptr) {
        return false;
    }
    auto sprite = registry_->getOrCreateDynamic(sprite_num);
    registry_->bumpRevision();
    return assignMember(*sprite, value, true);
}

lingo::Datum SpriteProperties::callSpriteMethod(int sprite_num,
                                                std::string_view method_name,
                                                const std::vector<lingo::Datum>& args) {
    if (registry_ == nullptr || sprite_num <= 0) {
        return lingo::Datum::voidValue();
    }

    const std::string method = lowerProp(method_name);
    if (method == "setcursor") {
        if (args.empty()) {
            return lingo::Datum::FALSE;
        }
        (void)setSpriteProp(sprite_num, "cursor", args[0]);
        return lingo::Datum::TRUE;
    }
    if (method == "getcursor") {
        return getSpriteProp(sprite_num, "cursor");
    }
    if (method == "setmember") {
        if (args.empty()) {
            return lingo::Datum::FALSE;
        }
        (void)setSpriteMember(sprite_num, args[0]);
        return lingo::Datum::TRUE;
    }
    if (method == "getmember") {
        return getSpriteProp(sprite_num, "member");
    }
    if (method == "intersects" || method == "within") {
        if (args.empty()) {
            return lingo::Datum::FALSE;
        }
        const auto other_sprite_num = spriteChannelFromDatum(args.front());
        if (!other_sprite_num.has_value() || *other_sprite_num <= 0) {
            return lingo::Datum::FALSE;
        }
        auto first = registry_->get(sprite_num);
        auto second = registry_->get(*other_sprite_num);
        if (first == nullptr || second == nullptr) {
            return lingo::Datum::FALSE;
        }
        const auto first_bounds = resolveSpriteBounds(*first);
        const auto second_bounds = resolveSpriteBounds(*second);
        const bool result = method == "intersects" ? boundsIntersect(first_bounds, second_bounds)
                                                   : boundsWithin(first_bounds, second_bounds);
        return result ? lingo::Datum::TRUE : lingo::Datum::FALSE;
    }

    if (method != "setid" && method != "getid" &&
        method != "setlink" && method != "getlink") {
        return lingo::Datum::voidValue();
    }

    lingo::Datum* broker = primaryBroker(sprite_num);
    if (broker == nullptr) {
        return lingo::Datum::FALSE;
    }

    if (method == "setid") {
        if (args.empty()) {
            return lingo::Datum::FALSE;
        }
        broker->scriptInstanceValue().setProperty("id", args[0]);
        return lingo::Datum::TRUE;
    }
    if (method == "getid") {
        return broker->scriptInstanceValue().getProperty("id");
    }
    if (method == "setlink") {
        if (args.empty()) {
            return lingo::Datum::FALSE;
        }
        broker->scriptInstanceValue().setProperty("pLink", args[0]);
        return lingo::Datum::TRUE;
    }
    if (method == "getlink") {
        return broker->scriptInstanceValue().getProperty("pLink");
    }

    return lingo::Datum::voidValue();
}

SpriteProperties::SpriteBounds SpriteProperties::resolveSpriteBounds(const sprite::SpriteState& sprite) const {
    const int width = sprite.width();
    const int height = sprite.height();
    int reg_x = 0;
    int reg_y = 0;

    if (member_info_resolver_ != nullptr && sprite.effectiveCastMember() > 0) {
        auto info = member_info_resolver_(sprite.effectiveCastLib(), sprite.effectiveCastMember());
        if (info.has_value()) {
            const bool flip_h = effectiveFlipH(sprite);
            const bool flip_v = sprite.isFlipV();
            if (info->bitmap) {
                const int bitmap_width = info->bitmap_width > 0 ? info->bitmap_width : info->width;
                const int bitmap_height = info->bitmap_height > 0 ? info->bitmap_height : info->height;
                reg_x = info->bitmap_reg_x;
                reg_y = info->bitmap_reg_y;
                if (width > 0 && bitmap_width > 0 && bitmap_width != width) {
                    reg_x = scaleRegistrationOffset(reg_x, width, bitmap_width);
                }
                if (height > 0 && bitmap_height > 0 && bitmap_height != height) {
                    reg_y = scaleRegistrationOffset(reg_y, height, bitmap_height);
                }
                reg_x = mirrorOffset(reg_x, width > 0 ? width : bitmap_width, flip_h);
                reg_y = mirrorOffset(reg_y, height > 0 ? height : bitmap_height, flip_v);
            } else {
                const int span_width = info->runtime_dynamic && width <= 0 ? info->width : width;
                const int span_height = info->runtime_dynamic && height <= 0 ? info->height : height;
                reg_x = mirrorOffset(info->reg_x, span_width, flip_h);
                reg_y = mirrorOffset(info->reg_y, span_height, flip_v);
            }
        }
    }

    const int left = sprite.locH() - reg_x;
    const int top = sprite.locV() - reg_y;
    return SpriteBounds{left, top, left + width, top + height};
}

int SpriteProperties::encodeCursorMember(const lingo::Datum& datum) {
    if (const auto* ref = datum.asCastMemberRef()) {
        return (ref->cast_lib << 16) | (ref->memberNum() & 0xFFFF);
    }
    return datum.intValue();
}

bool SpriteProperties::hasDirectorHorizontalMirror(double rotation, double skew) {
    return normalizeTransformAngle(rotation) == 180 && normalizeTransformAngle(skew) == 180;
}

lingo::Datum SpriteProperties::defaultProp(int sprite_num, std::string_view prop) const {
    if (prop == "puppet" || prop == "visible") return lingo::Datum::of(0);
    if (prop == "loch" || prop == "locv" || prop == "width" || prop == "height" ||
        prop == "left" || prop == "top" || prop == "ink" || prop == "castnum" ||
        prop == "membernum" || prop == "castlibnum" || prop == "blend" || prop == "stretch" || prop == "locz" ||
        prop == "forecolor" || prop == "backcolor") {
        return lingo::Datum::of(0);
    }
    if (prop == "color" || prop == "bgcolor") return rgbColorDatum(0);
    if (prop == "loc") return lingo::Datum::intPoint(0, 0);
    if (prop == "rect") return lingo::Datum::intRect(0, 0, 0, 0);
    if (prop == "spritenum") return lingo::Datum::of(sprite_num);
    if (prop == "type") return lingo::Datum::of(0);
    if (prop == "member") return lingo::Datum::voidValue();
    if (prop == "ilk") return lingo::Datum::symbol("sprite");
    if (auto legacy_default = defaultLegacySpriteProp(prop)) return *legacy_default;
    return lingo::Datum::voidValue();
}

bool SpriteProperties::assignMember(sprite::SpriteState& sprite, const lingo::Datum& value, bool via_set_member_method) {
    if (const auto* member_ref = value.asCastMemberRef()) {
        if (member_ref->memberNum() <= 0) {
            applyEmptyMemberOverride(sprite);
        } else {
            sprite.setDynamicMember(member_ref->cast_lib, member_ref->memberNum());
            autoSizeSprite(sprite, member_ref->cast_lib, member_ref->memberNum(), via_set_member_method);
        }
        return true;
    }

    if (value.isString() && member_name_resolver_ != nullptr) {
        if (auto resolved = member_name_resolver_(value.stringValue())) {
            if (resolved->memberNum() <= 0) {
                applyEmptyMemberOverride(sprite);
            } else {
                sprite.setDynamicMember(resolved->cast_lib, resolved->memberNum());
                autoSizeSprite(sprite, resolved->cast_lib, resolved->memberNum(), via_set_member_method);
            }
        }
        return true;
    }

    const int member_num = value.intValue();
    if (member_num <= 0) {
        applyEmptyMemberOverride(sprite);
        return true;
    }

    const int encoded_cast = (member_num >> 16) & 0xFFFF;
    const int encoded_member = member_num & 0xFFFF;
    if (encoded_cast > 0 && encoded_member > 0) {
        sprite.setDynamicMember(encoded_cast, encoded_member);
        autoSizeSprite(sprite, encoded_cast, encoded_member, via_set_member_method);
    } else {
        const int cast_lib = castLibForPlainMemberAssignment(sprite);
        sprite.setDynamicMember(cast_lib, member_num);
        autoSizeSprite(sprite, cast_lib, member_num, via_set_member_method);
    }
    return true;
}

void SpriteProperties::autoSizeSprite(sprite::SpriteState& sprite,
                                      int cast_lib,
                                      int member_num,
                                      bool via_set_member_method) const {
    if (member_info_resolver_ == nullptr || member_num <= 0) {
        return;
    }
    auto info = member_info_resolver_(cast_lib, member_num);
    if (!info.has_value()) {
        return;
    }

    if (info->bitmap) {
        const int width = info->bitmap_width > 0 ? info->bitmap_width : info->width;
        const int height = info->bitmap_height > 0 ? info->bitmap_height : info->height;
        sprite.applyIntrinsicSize(width, height);
        return;
    }

    if (via_set_member_method && info->runtime_dynamic) {
        sprite.applyMemberAssignmentSize(info->width, info->height);
    } else {
        sprite.applyIntrinsicSize(info->width, info->height);
    }
}

bool SpriteProperties::setColorValue(const lingo::Datum& value, const std::function<void(int)>& setter) const {
    if (value.isVoid()) {
        return true;
    }
    if (const auto* color = value.asColorRef()) {
        setter((color->r << 16) | (color->g << 8) | color->b);
    } else {
        setter(value.intValue());
    }
    return true;
}

int SpriteProperties::coerceInkCode(const lingo::Datum& value) const {
    if (const auto* symbol = value.asSymbol()) {
        if (auto ink = id::inkModeFromName(symbol->name)) {
            return id::code(*ink);
        }
    }
    if (value.isString()) {
        if (auto ink = id::inkModeFromName(value.stringValue())) {
            return id::code(*ink);
        }
    }
    return value.intValue();
}

bool SpriteProperties::effectiveFlipH(const sprite::SpriteState& sprite) const {
    return sprite.isFlipH() ^ hasDirectorHorizontalMirror(sprite.rotation(), sprite.skew());
}

int SpriteProperties::mirrorOffset(int reg, int span, bool flipped) const {
    if (!flipped || span <= 0) {
        return reg;
    }
    return span - reg;
}

int SpriteProperties::scaleRegistrationOffset(int reg, int sprite_span, int bitmap_span) const {
    if (bitmap_span == 0) {
        return reg;
    }
    if (legacy_rounded_registration_scale_) {
        return static_cast<int>(std::lround(static_cast<float>(reg) *
                                            static_cast<float>(sprite_span) /
                                            static_cast<float>(bitmap_span)));
    }
    return reg * sprite_span / bitmap_span;
}

int SpriteProperties::constraintChannel(const sprite::SpriteState& sprite) const {
    const auto constraint = sprite.legacyProperty("constraint");
    if (!constraint.has_value() || constraint->isVoid()) {
        return 0;
    }
    return constraint->intValue();
}

std::pair<int, int> SpriteProperties::constrainLoc(const sprite::SpriteState& sprite,
                                                   int loc_h,
                                                   int loc_v) const {
    if (registry_ == nullptr) {
        return {loc_h, loc_v};
    }

    const int channel = constraintChannel(sprite);
    if (channel <= 0) {
        return {loc_h, loc_v};
    }

    const auto constraint_sprite = registry_->get(channel);
    if (constraint_sprite == nullptr) {
        return {loc_h, loc_v};
    }

    const auto bounds = resolveSpriteBounds(*constraint_sprite);
    const int min_h = std::min(bounds.left, bounds.right);
    const int max_h = std::max(bounds.left, bounds.right);
    const int min_v = std::min(bounds.top, bounds.bottom);
    const int max_v = std::max(bounds.top, bounds.bottom);
    return {std::clamp(loc_h, min_h, max_h), std::clamp(loc_v, min_v, max_v)};
}

void SpriteProperties::setConstrainedLoc(sprite::SpriteState& sprite,
                                         int loc_h,
                                         int loc_v,
                                         bool update_h,
                                         bool update_v) const {
    const auto constrained = constrainLoc(sprite, loc_h, loc_v);
    if (update_h || constrained.first != sprite.locH()) {
        sprite.setLocH(constrained.first);
    }
    if (update_v || constrained.second != sprite.locV()) {
        sprite.setLocV(constrained.second);
    }
}

void SpriteProperties::applyEmptyMemberOverride(sprite::SpriteState& sprite) {
    // A sprite that loses a real member must draw nothing. Remember that
    // transition so the renderer does not turn it into a bgColor fill.
    const bool had_member = sprite.effectiveCastMember() > 0;
    sprite.setDynamicMember(0, 0);
    sprite.setMemberCleared(had_member);
    sprite.resetReleasedSpriteTransforms();
}

void SpriteProperties::resetReleasedEmptyChannel(sprite::SpriteState& sprite) {
    sprite.setScriptInstanceList({});
    sprite.setVisible(false);
    sprite.setCursor(0);
    sprite.setBlend(100);
    sprite.setStretch(0);
    sprite.resetReleasedChannelGeometry();
    sprite.resetReleasedSpriteTransforms();
}

std::vector<lingo::Datum>* SpriteProperties::brokerScriptList(int sprite_num) {
    if (registry_ == nullptr) {
        return nullptr;
    }
    auto sprite = registry_->get(sprite_num);
    if (sprite == nullptr || sprite->scriptInstanceList().empty()) {
        return nullptr;
    }
    return &sprite->scriptInstanceList();
}

lingo::Datum* SpriteProperties::primaryBroker(int sprite_num) {
    auto* scripts = brokerScriptList(sprite_num);
    if (scripts == nullptr || scripts->empty()) {
        return nullptr;
    }
    for (auto& script : *scripts) {
        if (script.type() == lingo::DatumType::ScriptInstanceRef) {
            return &script;
        }
    }
    return nullptr;
}

} // namespace libreshockwave::player
