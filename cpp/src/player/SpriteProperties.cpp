#include "libreshockwave/player/SpriteProperties.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>

#include "libreshockwave/id/Ids.hpp"
#include "libreshockwave/player/CursorManager.hpp"

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
    const int castLib = sprite.effectiveCastLib();
    return castLib > 0 ? castLib : 1;
}

lingo::Datum rgbColorDatum(int rgb) {
    return lingo::Datum::colorRef((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

lingo::Datum normalizedColorDatum(const lingo::Datum& value, int rgb) {
    return value.asColorRef() != nullptr ? value : rgbColorDatum(rgb);
}

std::optional<int> spriteChannelFromDatum(const lingo::Datum& datum) {
    if (const auto* spriteRef = datum.asSpriteRef()) {
        return spriteRef->spriteNum();
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
    memberInfoResolver_ = std::move(resolver);
}

void SpriteProperties::setMemberNameResolver(MemberNameResolver resolver) {
    memberNameResolver_ = std::move(resolver);
}

void SpriteProperties::setMemberImageSetter(MemberImageSetter setter) {
    memberImageSetter_ = std::move(setter);
}

void SpriteProperties::setLegacyRoundedRegistrationScale(bool enabled) {
    legacyRoundedRegistrationScale_ = enabled;
}

std::optional<std::vector<lingo::Datum>> SpriteProperties::getScriptInstanceList(int spriteNum) const {
    if (registry_ == nullptr) {
        return std::nullopt;
    }
    auto sprite = registry_->get(spriteNum);
    if (sprite == nullptr || sprite->scriptInstanceList().empty()) {
        return std::nullopt;
    }
    return sprite->scriptInstanceList();
}

std::vector<lingo::Datum>* SpriteProperties::mutableScriptInstanceList(int spriteNum) {
    if (registry_ == nullptr) {
        return nullptr;
    }
    auto sprite = registry_->get(spriteNum);
    if (sprite == nullptr || sprite->scriptInstanceList().empty()) {
        return nullptr;
    }
    return &sprite->scriptInstanceList();
}

lingo::Datum SpriteProperties::getSpriteProp(int spriteNum, std::string_view propName) const {
    const std::string prop = lowerProp(propName);
    if (registry_ == nullptr) {
        return defaultProp(spriteNum, prop);
    }

    auto sprite = registry_->get(spriteNum);
    if (sprite == nullptr) {
        return defaultProp(spriteNum, prop);
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
    if (prop == "spritenum") return lingo::Datum::of(spriteNum);
    if (prop == "type") return lingo::Datum::of(1);
    if (prop == "castnum" || prop == "membernum") return lingo::Datum::of(sprite->effectiveCastMember());
    if (prop == "castlibnum") return lingo::Datum::of(sprite->effectiveCastLib());
    if (prop == "member") {
        const int castLib = sprite->effectiveCastLib();
        const int member = sprite->effectiveCastMember();
        return member > 0 ? lingo::Datum::castMemberRef(lingo::Datum::CastMemberRef{castLib, member})
                          : lingo::Datum::voidValue();
    }
    if (prop == "image") {
        if (memberInfoResolver_ != nullptr) {
            if (auto info = memberInfoResolver_(sprite->effectiveCastLib(), sprite->effectiveCastMember());
                info.has_value() && info->hasImage) {
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
    const std::string legacyProp = normalizeLegacySpriteProp(prop);
    if (auto stored = sprite->legacyProperty(legacyProp)) {
        return *stored;
    }
    if (auto legacyDefault = defaultLegacySpriteProp(legacyProp)) {
        return *legacyDefault;
    }
    return lingo::Datum::voidValue();
}

bool SpriteProperties::setSpriteProp(int spriteNum, std::string_view propName, const lingo::Datum& value) {
    if (registry_ == nullptr) {
        return false;
    }

    const std::string prop = lowerProp(propName);
    auto sprite = registry_->getOrCreateDynamic(spriteNum);
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
        if (memberImageSetter_ != nullptr && sprite->effectiveCastMember() > 0) {
            return memberImageSetter_(sprite->effectiveCastLib(), sprite->effectiveCastMember(), value);
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
        const int encodedCast = (slot >> 16) & 0xFFFF;
        const int encodedMember = slot & 0xFFFF;
        if (encodedCast > 0 && encodedMember > 0) {
            sprite->setDynamicMember(encodedCast, encodedMember);
            autoSizeSprite(*sprite, encodedCast, encodedMember, false);
        } else {
            const int castLib = castLibForPlainMemberAssignment(*sprite);
            sprite->setDynamicMember(castLib, slot);
            autoSizeSprite(*sprite, castLib, slot, false);
        }
        return true;
    }
    if (prop == "castlibnum") {
        const int castLib = value.intValue();
        const int memberNum = sprite->effectiveCastMember();
        sprite->setDynamicMember(castLib, memberNum);
        if (memberNum > 0) {
            autoSizeSprite(*sprite, castLib, memberNum, false);
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
                    item.scriptInstanceValue().setProperty("spritenum", lingo::Datum::of(spriteNum));
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
        const std::string legacyProp = normalizeLegacySpriteProp(prop);
        sprite->setLegacyProperty(legacyProp, value);
        if (legacyProp == "constraint") {
            setConstrainedLoc(*sprite, sprite->locH(), sprite->locV(), false, false);
        }
        return true;
    }
    if (prop == "type" || prop == "id") {
        return true;
    }
    return false;
}

bool SpriteProperties::setSpriteMember(int spriteNum, const lingo::Datum& value) {
    if (registry_ == nullptr) {
        return false;
    }
    auto sprite = registry_->getOrCreateDynamic(spriteNum);
    registry_->bumpRevision();
    return assignMember(*sprite, value, true);
}

lingo::Datum SpriteProperties::callSpriteMethod(int spriteNum,
                                                std::string_view methodName,
                                                const std::vector<lingo::Datum>& args) {
    if (registry_ == nullptr || spriteNum <= 0) {
        return lingo::Datum::voidValue();
    }

    const std::string method = lowerProp(methodName);
    if (method == "setcursor") {
        if (args.empty()) {
            return lingo::Datum::FALSE;
        }
        (void)setSpriteProp(spriteNum, "cursor", args[0]);
        return lingo::Datum::TRUE;
    }
    if (method == "getcursor") {
        return getSpriteProp(spriteNum, "cursor");
    }
    if (method == "setmember") {
        if (args.empty()) {
            return lingo::Datum::FALSE;
        }
        (void)setSpriteMember(spriteNum, args[0]);
        return lingo::Datum::TRUE;
    }
    if (method == "getmember") {
        return getSpriteProp(spriteNum, "member");
    }
    if (method == "intersects" || method == "within") {
        if (args.empty()) {
            return lingo::Datum::FALSE;
        }
        const auto otherSpriteNum = spriteChannelFromDatum(args.front());
        if (!otherSpriteNum.has_value() || *otherSpriteNum <= 0) {
            return lingo::Datum::FALSE;
        }
        auto first = registry_->get(spriteNum);
        auto second = registry_->get(*otherSpriteNum);
        if (first == nullptr || second == nullptr) {
            return lingo::Datum::FALSE;
        }
        const auto firstBounds = resolveSpriteBounds(*first);
        const auto secondBounds = resolveSpriteBounds(*second);
        const bool result = method == "intersects" ? boundsIntersect(firstBounds, secondBounds)
                                                   : boundsWithin(firstBounds, secondBounds);
        return result ? lingo::Datum::TRUE : lingo::Datum::FALSE;
    }

    if (method != "setid" && method != "getid" &&
        method != "setlink" && method != "getlink") {
        return lingo::Datum::voidValue();
    }

    lingo::Datum* broker = primaryBroker(spriteNum);
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
    int regX = 0;
    int regY = 0;

    if (memberInfoResolver_ != nullptr && sprite.effectiveCastMember() > 0) {
        auto info = memberInfoResolver_(sprite.effectiveCastLib(), sprite.effectiveCastMember());
        if (info.has_value()) {
            const bool flipH = effectiveFlipH(sprite);
            const bool flipV = sprite.isFlipV();
            if (info->bitmap) {
                const int bitmapWidth = info->bitmapWidth > 0 ? info->bitmapWidth : info->width;
                const int bitmapHeight = info->bitmapHeight > 0 ? info->bitmapHeight : info->height;
                regX = info->bitmapRegX;
                regY = info->bitmapRegY;
                if (width > 0 && bitmapWidth > 0 && bitmapWidth != width) {
                    regX = scaleRegistrationOffset(regX, width, bitmapWidth);
                }
                if (height > 0 && bitmapHeight > 0 && bitmapHeight != height) {
                    regY = scaleRegistrationOffset(regY, height, bitmapHeight);
                }
                regX = mirrorOffset(regX, width > 0 ? width : bitmapWidth, flipH);
                regY = mirrorOffset(regY, height > 0 ? height : bitmapHeight, flipV);
            } else {
                const int spanWidth = info->runtimeDynamic && width <= 0 ? info->width : width;
                const int spanHeight = info->runtimeDynamic && height <= 0 ? info->height : height;
                regX = mirrorOffset(info->regX, spanWidth, flipH);
                regY = mirrorOffset(info->regY, spanHeight, flipV);
            }
        }
    }

    const int left = sprite.locH() - regX;
    const int top = sprite.locV() - regY;
    return SpriteBounds{left, top, left + width, top + height};
}

int SpriteProperties::encodeCursorMember(const lingo::Datum& datum) {
    if (const auto* ref = datum.asCastMemberRef()) {
        return (ref->castLib << 16) | (ref->memberNum() & 0xFFFF);
    }
    return datum.intValue();
}

bool SpriteProperties::hasDirectorHorizontalMirror(double rotation, double skew) {
    return normalizeTransformAngle(rotation) == 180 && normalizeTransformAngle(skew) == 180;
}

lingo::Datum SpriteProperties::defaultProp(int spriteNum, std::string_view prop) const {
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
    if (prop == "spritenum") return lingo::Datum::of(spriteNum);
    if (prop == "type") return lingo::Datum::of(0);
    if (prop == "member") return lingo::Datum::voidValue();
    if (prop == "ilk") return lingo::Datum::symbol("sprite");
    if (auto legacyDefault = defaultLegacySpriteProp(prop)) return *legacyDefault;
    return lingo::Datum::voidValue();
}

bool SpriteProperties::assignMember(sprite::SpriteState& sprite, const lingo::Datum& value, bool viaSetMemberMethod) {
    if (const auto* memberRef = value.asCastMemberRef()) {
        if (memberRef->memberNum() <= 0) {
            applyEmptyMemberOverride(sprite);
        } else {
            sprite.setDynamicMember(memberRef->castLib, memberRef->memberNum());
            autoSizeSprite(sprite, memberRef->castLib, memberRef->memberNum(), viaSetMemberMethod);
        }
        return true;
    }

    if (value.isString() && memberNameResolver_ != nullptr) {
        if (auto resolved = memberNameResolver_(value.stringValue())) {
            if (resolved->memberNum() <= 0) {
                applyEmptyMemberOverride(sprite);
            } else {
                sprite.setDynamicMember(resolved->castLib, resolved->memberNum());
                autoSizeSprite(sprite, resolved->castLib, resolved->memberNum(), viaSetMemberMethod);
            }
        }
        return true;
    }

    const int memberNum = value.intValue();
    if (memberNum <= 0) {
        applyEmptyMemberOverride(sprite);
        return true;
    }

    const int encodedCast = (memberNum >> 16) & 0xFFFF;
    const int encodedMember = memberNum & 0xFFFF;
    if (encodedCast > 0 && encodedMember > 0) {
        sprite.setDynamicMember(encodedCast, encodedMember);
        autoSizeSprite(sprite, encodedCast, encodedMember, viaSetMemberMethod);
    } else {
        const int castLib = castLibForPlainMemberAssignment(sprite);
        sprite.setDynamicMember(castLib, memberNum);
        autoSizeSprite(sprite, castLib, memberNum, viaSetMemberMethod);
    }
    return true;
}

void SpriteProperties::autoSizeSprite(sprite::SpriteState& sprite,
                                      int castLib,
                                      int memberNum,
                                      bool viaSetMemberMethod) const {
    if (memberInfoResolver_ == nullptr || memberNum <= 0) {
        return;
    }
    auto info = memberInfoResolver_(castLib, memberNum);
    if (!info.has_value()) {
        return;
    }

    if (info->bitmap) {
        const int width = info->bitmapWidth > 0 ? info->bitmapWidth : info->width;
        const int height = info->bitmapHeight > 0 ? info->bitmapHeight : info->height;
        sprite.applyIntrinsicSize(width, height);
        return;
    }

    if (viaSetMemberMethod && info->runtimeDynamic) {
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

int SpriteProperties::scaleRegistrationOffset(int reg, int spriteSpan, int bitmapSpan) const {
    if (bitmapSpan == 0) {
        return reg;
    }
    if (legacyRoundedRegistrationScale_) {
        return static_cast<int>(std::lround(static_cast<float>(reg) *
                                            static_cast<float>(spriteSpan) /
                                            static_cast<float>(bitmapSpan)));
    }
    return reg * spriteSpan / bitmapSpan;
}

int SpriteProperties::constraintChannel(const sprite::SpriteState& sprite) const {
    const auto constraint = sprite.legacyProperty("constraint");
    if (!constraint.has_value() || constraint->isVoid()) {
        return 0;
    }
    return constraint->intValue();
}

std::pair<int, int> SpriteProperties::constrainLoc(const sprite::SpriteState& sprite,
                                                   int locH,
                                                   int locV) const {
    if (registry_ == nullptr) {
        return {locH, locV};
    }

    const int channel = constraintChannel(sprite);
    if (channel <= 0) {
        return {locH, locV};
    }

    const auto constraintSprite = registry_->get(channel);
    if (constraintSprite == nullptr) {
        return {locH, locV};
    }

    const auto bounds = resolveSpriteBounds(*constraintSprite);
    const int minH = std::min(bounds.left, bounds.right);
    const int maxH = std::max(bounds.left, bounds.right);
    const int minV = std::min(bounds.top, bounds.bottom);
    const int maxV = std::max(bounds.top, bounds.bottom);
    return {std::clamp(locH, minH, maxH), std::clamp(locV, minV, maxV)};
}

void SpriteProperties::setConstrainedLoc(sprite::SpriteState& sprite,
                                         int locH,
                                         int locV,
                                         bool updateH,
                                         bool updateV) const {
    const auto constrained = constrainLoc(sprite, locH, locV);
    if (updateH || constrained.first != sprite.locH()) {
        sprite.setLocH(constrained.first);
    }
    if (updateV || constrained.second != sprite.locV()) {
        sprite.setLocV(constrained.second);
    }
}

void SpriteProperties::applyEmptyMemberOverride(sprite::SpriteState& sprite) {
    sprite.setDynamicMember(0, 0);
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

std::vector<lingo::Datum>* SpriteProperties::brokerScriptList(int spriteNum) {
    if (registry_ == nullptr) {
        return nullptr;
    }
    auto sprite = registry_->get(spriteNum);
    if (sprite == nullptr || sprite->scriptInstanceList().empty()) {
        return nullptr;
    }
    return &sprite->scriptInstanceList();
}

lingo::Datum* SpriteProperties::primaryBroker(int spriteNum) {
    auto* scripts = brokerScriptList(spriteNum);
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
