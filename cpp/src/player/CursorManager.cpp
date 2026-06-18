#include "libreshockwave/player/CursorManager.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

#include "libreshockwave/player/input/HitTester.hpp"

namespace libreshockwave::player {
namespace {

std::string normalizedCursorName(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char ch : value) {
        if (ch == '#' || ch == ' ' || ch == '_' || ch == '-') {
            continue;
        }
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

std::optional<int> symbolicCursorCode(std::string_view value) {
    const std::string name = normalizedCursorName(value);
    if (name == "arrow") return CursorManager::ARROW_CURSOR;
    if (name == "default" || name == "none") return CursorManager::DEFAULT_CURSOR;
    if (name == "ibeam" || name == "i") return CursorManager::IBEAM_CURSOR;
    if (name == "wait" || name == "watch" || name == "hourglass") return CursorManager::WAIT_CURSOR;
    if (name == "pointer" || name == "hand" || name == "finger" || name == "pointingfinger") {
        return CursorManager::POINTER_CURSOR;
    }
    return std::nullopt;
}

} // namespace

CursorManager::CursorManager(input::InputState* inputState, render::SpriteRegistry* spriteRegistry)
    : inputState_(inputState), spriteRegistry_(spriteRegistry) {}

void CursorManager::setSpriteProvider(SpriteProvider provider) {
    spriteProvider_ = std::move(provider);
}

void CursorManager::setMemberInfoResolver(MemberInfoResolver resolver) {
    memberInfoResolver_ = std::move(resolver);
}

void CursorManager::setBitmapResolver(BitmapResolver resolver) {
    bitmapResolver_ = std::move(resolver);
}

void CursorManager::setInteractivePredicate(ChannelPredicate predicate) {
    interactivePredicate_ = std::move(predicate);
}

void CursorManager::setGlobalCursorSupplier(GlobalCursorSupplier supplier) {
    globalCursorSupplier_ = std::move(supplier);
}

int CursorManager::getCursorAtMouse() const {
    if (inputState_ == nullptr) {
        return ARROW_CURSOR;
    }

    const int mouseH = inputState_->mouseH();
    const int mouseV = inputState_->mouseV();
    auto sprites = currentSprites();
    const int interactiveHitChannel = hitTestInteractive(sprites, mouseH, mouseV);
    const int visualHitChannel = interactiveHitChannel > 0 ? interactiveHitChannel : hitTestVisual(sprites, mouseH, mouseV);
    const int hitChannel = interactiveHitChannel > 0 ? interactiveHitChannel : visualHitChannel;
    const auto hitSprite = findSpriteByChannel(sprites, hitChannel);
    const bool suppressInteractiveCursor = isNavigatorWhitespace(hitSprite ? &*hitSprite : nullptr, mouseH, mouseV);

    if (hitChannel > 0 && spriteRegistry_ != nullptr) {
        auto sprite = spriteRegistry_->get(hitChannel);
        if (sprite) {
            if (!suppressInteractiveCursor && sprite->hasBitmapCursor()) {
                return CUSTOM_BITMAP_CURSOR;
            }

            const int castLib = sprite->effectiveCastLib();
            const int memberNum = sprite->effectiveCastMember();
            if (memberNum > 0 && memberInfoResolver_) {
                const auto info = memberInfoResolver_(castLib, memberNum);
                if (info.has_value()) {
                    if (info->editable && info->memberType == ::libreshockwave::cast::MemberType::Text) {
                        return IBEAM_CURSOR;
                    }
                    if (info->memberType == ::libreshockwave::cast::MemberType::Button) {
                        return POINTER_CURSOR;
                    }
                }
            }

            const int spriteCursor = sprite->cursor();
            if (!suppressInteractiveCursor && spriteCursor != DEFAULT_CURSOR) {
                return spriteCursor;
            }
            if (!suppressInteractiveCursor && hitChannel == interactiveHitChannel) {
                return POINTER_CURSOR;
            }
        }
    }

    if (suppressInteractiveCursor) {
        return ARROW_CURSOR;
    }

    const int globalCursor = getGlobalCursorCode();
    return globalCursor != DEFAULT_CURSOR ? globalCursor : ARROW_CURSOR;
}

std::optional<bitmap::Bitmap> CursorManager::getCursorBitmap() const {
    if (inputState_ == nullptr) {
        return std::nullopt;
    }

    const int mouseH = inputState_->mouseH();
    const int mouseV = inputState_->mouseV();
    auto sprites = currentSprites();
    const int interactiveHitChannel = hitTestInteractive(sprites, mouseH, mouseV);
    const int visualHitChannel = interactiveHitChannel > 0 ? interactiveHitChannel : hitTestVisual(sprites, mouseH, mouseV);
    const int hitChannel = interactiveHitChannel > 0 ? interactiveHitChannel : visualHitChannel;
    const auto hitSprite = findSpriteByChannel(sprites, hitChannel);
    if (isNavigatorWhitespace(hitSprite ? &*hitSprite : nullptr, mouseH, mouseV)) {
        return std::nullopt;
    }

    auto globalCursor = getGlobalCursorDatum();
    if (globalCursor.isList() && globalCursor.listValue().items().size() >= 2) {
        const auto& items = globalCursor.listValue().items();
        return resolveCursorBitmap(encodeCursorMember(items[0]), encodeCursorMember(items[1]));
    }

    if (hitChannel <= 0 || spriteRegistry_ == nullptr) {
        return std::nullopt;
    }

    auto sprite = spriteRegistry_->get(hitChannel);
    if (!sprite || !sprite->hasBitmapCursor()) {
        return std::nullopt;
    }
    return resolveCursorBitmap(sprite->cursorMemberNum(), sprite->cursorMaskNum());
}

std::optional<std::array<int, 2>> CursorManager::getCursorRegPoint() const {
    if (inputState_ == nullptr) {
        return std::nullopt;
    }

    const int mouseH = inputState_->mouseH();
    const int mouseV = inputState_->mouseV();
    auto sprites = currentSprites();
    const int interactiveHitChannel = hitTestInteractive(sprites, mouseH, mouseV);
    const int visualHitChannel = interactiveHitChannel > 0 ? interactiveHitChannel : hitTestVisual(sprites, mouseH, mouseV);
    const int hitChannel = interactiveHitChannel > 0 ? interactiveHitChannel : visualHitChannel;
    const auto hitSprite = findSpriteByChannel(sprites, hitChannel);
    if (isNavigatorWhitespace(hitSprite ? &*hitSprite : nullptr, mouseH, mouseV)) {
        return std::nullopt;
    }

    auto globalCursor = getGlobalCursorDatum();
    if (globalCursor.isList() && !globalCursor.listValue().items().empty()) {
        return resolveCursorRegPoint(encodeCursorMember(globalCursor.listValue().items().front()));
    }

    if (hitChannel <= 0 || spriteRegistry_ == nullptr) {
        return std::nullopt;
    }

    auto sprite = spriteRegistry_->get(hitChannel);
    if (!sprite || !sprite->hasBitmapCursor()) {
        return std::nullopt;
    }
    return resolveCursorRegPoint(sprite->cursorMemberNum());
}

bitmap::Bitmap CursorManager::applyCursorMask(const bitmap::Bitmap& cursor, const bitmap::Bitmap& mask) {
    bitmap::Bitmap result(cursor.width(), cursor.height(), 32);
    for (int y = 0; y < cursor.height(); ++y) {
        for (int x = 0; x < cursor.width(); ++x) {
            const std::uint32_t cursorRgb = cursor.getPixel(x, y) & 0x00FFFFFFU;
            if (x < mask.width() && y < mask.height()) {
                const std::uint32_t maskRgb = mask.getPixel(x, y) & 0x00FFFFFFU;
                result.setPixel(x, y, maskRgb == 0x00FFFFFFU ? 0x00000000U : (0xFF000000U | cursorRgb));
            } else {
                result.setPixel(x, y, 0x00000000U);
            }
        }
    }
    return result;
}

int CursorManager::encodeCursorMember(const lingo::Datum& datum) {
    if (const auto* ref = datum.asCastMemberRef()) {
        return (ref->castLib << 16) | (ref->memberNum() & 0xFFFF);
    }
    return datum.intValue();
}

int CursorManager::cursorCodeFromDatum(const lingo::Datum& datum) {
    if (const auto* symbol = datum.asSymbol()) {
        if (const auto cursor = symbolicCursorCode(symbol->name)) {
            return *cursor;
        }
        return DEFAULT_CURSOR;
    }
    if (datum.isString()) {
        if (const auto cursor = symbolicCursorCode(datum.stringValue())) {
            return *cursor;
        }
    }
    return datum.intValue();
}

bool CursorManager::isNearWhite(std::uint32_t pixel) {
    const int red = static_cast<int>((pixel >> 16) & 0xFFU);
    const int green = static_cast<int>((pixel >> 8) & 0xFFU);
    const int blue = static_cast<int>(pixel & 0xFFU);
    return red >= 250 && green >= 250 && blue >= 250;
}

std::vector<render::pipeline::RenderSprite> CursorManager::currentSprites() const {
    return spriteProvider_ ? spriteProvider_() : std::vector<render::pipeline::RenderSprite>{};
}

int CursorManager::hitTestVisual(const std::vector<render::pipeline::RenderSprite>& sprites,
                                 int stageX,
                                 int stageY) const {
    return input::HitTester::hitTest(sprites, stageX, stageY);
}

int CursorManager::hitTestInteractive(const std::vector<render::pipeline::RenderSprite>& sprites,
                                      int stageX,
                                      int stageY) const {
    auto exactHits = input::HitTester::hitTestAll(sprites, stageX, stageY, [](int) {
        return false;
    });
    auto exact = std::ranges::find_if(exactHits, [this](int channel) {
        return isInteractive(channel);
    });
    if (exact != exactHits.end()) {
        return *exact;
    }

    auto boundingHits = input::HitTester::hitTestAll(sprites, stageX, stageY, [this](int channel) {
        return isInteractive(channel);
    });
    auto bounding = std::ranges::find_if(boundingHits, [this](int channel) {
        return isInteractive(channel);
    });
    return bounding == boundingHits.end() ? 0 : *bounding;
}

std::optional<render::pipeline::RenderSprite> CursorManager::findSpriteByChannel(
    const std::vector<render::pipeline::RenderSprite>& sprites,
    int channel) const {
    if (channel <= 0) {
        return std::nullopt;
    }
    for (auto it = sprites.rbegin(); it != sprites.rend(); ++it) {
        if (it->channel() == channel) {
            return *it;
        }
    }
    return std::nullopt;
}

bool CursorManager::isNavigatorWhitespace(const render::pipeline::RenderSprite* sprite, int stageX, int stageY) const {
    if (sprite == nullptr || !sprite->hasBehaviors() || sprite->inkMode() != id::InkMode::MATTE) {
        return false;
    }

    const auto baked = sprite->bakedBitmap();
    if (!baked) {
        return false;
    }

    const int localX = stageX - sprite->x();
    const int localY = stageY - sprite->y();
    const int bw = baked->width();
    const int bh = baked->height();
    const int sw = sprite->width();
    const int sh = sprite->height();
    const int bx = (sw > 0 && sw != bw) ? (localX * bw / sw) : localX;
    const int by = (sh > 0 && sh != bh) ? (localY * bh / sh) : localY;
    if (bx < 0 || bx >= bw || by < 0 || by >= bh) {
        return false;
    }

    const std::uint32_t pixel = baked->getPixel(bx, by);
    const int alpha = static_cast<int>((pixel >> 24) & 0xFFU);
    return alpha >= 128 && isNearWhite(pixel);
}

lingo::Datum CursorManager::getGlobalCursorDatum() const {
    return globalCursorSupplier_ ? globalCursorSupplier_() : lingo::Datum::voidValue();
}

int CursorManager::getGlobalCursorCode() const {
    auto globalCursor = getGlobalCursorDatum();
    if (globalCursor.isList() && !globalCursor.listValue().items().empty()) {
        return CUSTOM_BITMAP_CURSOR;
    }
    if (!globalCursor.isVoid()) {
        return cursorCodeFromDatum(globalCursor);
    }
    return DEFAULT_CURSOR;
}

std::optional<bitmap::Bitmap> CursorManager::resolveCursorBitmap(int encodedMember, int encodedMask) const {
    if (!bitmapResolver_) {
        return std::nullopt;
    }

    int castLib = (encodedMember >> 16) & 0xFFFF;
    const int memberNum = encodedMember & 0xFFFF;
    if (castLib == 0) {
        castLib = 1;
    }

    auto cursor = bitmapResolver_(castLib, memberNum);
    if (!cursor.has_value()) {
        return std::nullopt;
    }

    if (encodedMask != 0) {
        int maskCastLib = (encodedMask >> 16) & 0xFFFF;
        const int maskMemberNum = encodedMask & 0xFFFF;
        if (maskCastLib == 0) {
            maskCastLib = 1;
        }

        auto mask = bitmapResolver_(maskCastLib, maskMemberNum);
        if (mask.has_value()) {
            cursor = applyCursorMask(*cursor, *mask);
        }
    }

    return cursor;
}

std::optional<std::array<int, 2>> CursorManager::resolveCursorRegPoint(int encodedMember) const {
    if (!memberInfoResolver_) {
        return std::array<int, 2>{0, 0};
    }

    int castLib = (encodedMember >> 16) & 0xFFFF;
    const int memberNum = encodedMember & 0xFFFF;
    if (castLib == 0) {
        castLib = 1;
    }

    const auto info = memberInfoResolver_(castLib, memberNum);
    if (!info.has_value()) {
        return std::array<int, 2>{0, 0};
    }
    return std::array<int, 2>{info->regX, info->regY};
}

bool CursorManager::isInteractive(int channel) const {
    return interactivePredicate_ && interactivePredicate_(channel);
}

} // namespace libreshockwave::player
