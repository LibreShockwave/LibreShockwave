#include "libreshockwave/player/cursor_manager.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

#include "libreshockwave/player/input/hit_tester.hpp"

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

CursorManager::CursorManager(input::InputState* input_state, render::SpriteRegistry* sprite_registry)
    : input_state_(input_state), sprite_registry_(sprite_registry) {}

void CursorManager::setSpriteProvider(SpriteProvider provider) {
    sprite_provider_ = std::move(provider);
}

void CursorManager::setMemberInfoResolver(MemberInfoResolver resolver) {
    member_info_resolver_ = std::move(resolver);
}

void CursorManager::setBitmapResolver(BitmapResolver resolver) {
    bitmap_resolver_ = std::move(resolver);
}

void CursorManager::setInteractivePredicate(ChannelPredicate predicate) {
    interactive_predicate_ = std::move(predicate);
}

void CursorManager::setGlobalCursorSupplier(GlobalCursorSupplier supplier) {
    global_cursor_supplier_ = std::move(supplier);
}

int CursorManager::getCursorAtMouse() const {
    if (input_state_ == nullptr) {
        return ARROW_CURSOR;
    }

    const int mouse_h = input_state_->mouseH();
    const int mouse_v = input_state_->mouseV();
    auto sprites = currentSprites();
    const int interactive_hit_channel = hitTestInteractive(sprites, mouse_h, mouse_v);
    const int visual_hit_channel = interactive_hit_channel > 0 ? interactive_hit_channel : hitTestVisual(sprites, mouse_h, mouse_v);
    const int hit_channel = interactive_hit_channel > 0 ? interactive_hit_channel : visual_hit_channel;
    const auto hit_sprite = findSpriteByChannel(sprites, hit_channel);
    const bool suppress_interactive_cursor = isNavigatorWhitespace(hit_sprite ? &*hit_sprite : nullptr, mouse_h, mouse_v);

    if (hit_channel > 0 && sprite_registry_ != nullptr) {
        auto sprite = sprite_registry_->get(hit_channel);
        if (sprite) {
            if (!suppress_interactive_cursor && sprite->hasBitmapCursor()) {
                return CUSTOM_BITMAP_CURSOR;
            }

            const int cast_lib = sprite->effectiveCastLib();
            const int member_num = sprite->effectiveCastMember();
            if (member_num > 0 && member_info_resolver_) {
                const auto info = member_info_resolver_(cast_lib, member_num);
                if (info.has_value()) {
                    if (info->editable && info->member_type == ::libreshockwave::cast::MemberType::Text) {
                        return IBEAM_CURSOR;
                    }
                    if (info->member_type == ::libreshockwave::cast::MemberType::Button) {
                        return POINTER_CURSOR;
                    }
                }
            }

            const int sprite_cursor = sprite->cursor();
            if (!suppress_interactive_cursor && sprite_cursor != DEFAULT_CURSOR) {
                return sprite_cursor;
            }
            if (!suppress_interactive_cursor && hit_channel == interactive_hit_channel) {
                return POINTER_CURSOR;
            }
        }
    }

    if (suppress_interactive_cursor) {
        return ARROW_CURSOR;
    }

    const int global_cursor = getGlobalCursorCode();
    return global_cursor != DEFAULT_CURSOR ? global_cursor : ARROW_CURSOR;
}

std::optional<bitmap::Bitmap> CursorManager::getCursorBitmap() const {
    if (input_state_ == nullptr) {
        return std::nullopt;
    }

    const int mouse_h = input_state_->mouseH();
    const int mouse_v = input_state_->mouseV();
    auto sprites = currentSprites();
    const int interactive_hit_channel = hitTestInteractive(sprites, mouse_h, mouse_v);
    const int visual_hit_channel = interactive_hit_channel > 0 ? interactive_hit_channel : hitTestVisual(sprites, mouse_h, mouse_v);
    const int hit_channel = interactive_hit_channel > 0 ? interactive_hit_channel : visual_hit_channel;
    const auto hit_sprite = findSpriteByChannel(sprites, hit_channel);
    if (isNavigatorWhitespace(hit_sprite ? &*hit_sprite : nullptr, mouse_h, mouse_v)) {
        return std::nullopt;
    }

    auto global_cursor = getGlobalCursorDatum();
    if (global_cursor.isList() && global_cursor.listValue().items().size() >= 2) {
        const auto& items = global_cursor.listValue().items();
        return resolveCursorBitmap(encodeCursorMember(items[0]), encodeCursorMember(items[1]));
    }

    if (hit_channel <= 0 || sprite_registry_ == nullptr) {
        return std::nullopt;
    }

    auto sprite = sprite_registry_->get(hit_channel);
    if (!sprite || !sprite->hasBitmapCursor()) {
        return std::nullopt;
    }
    return resolveCursorBitmap(sprite->cursorMemberNum(), sprite->cursorMaskNum());
}

std::optional<std::array<int, 2>> CursorManager::getCursorRegPoint() const {
    if (input_state_ == nullptr) {
        return std::nullopt;
    }

    const int mouse_h = input_state_->mouseH();
    const int mouse_v = input_state_->mouseV();
    auto sprites = currentSprites();
    const int interactive_hit_channel = hitTestInteractive(sprites, mouse_h, mouse_v);
    const int visual_hit_channel = interactive_hit_channel > 0 ? interactive_hit_channel : hitTestVisual(sprites, mouse_h, mouse_v);
    const int hit_channel = interactive_hit_channel > 0 ? interactive_hit_channel : visual_hit_channel;
    const auto hit_sprite = findSpriteByChannel(sprites, hit_channel);
    if (isNavigatorWhitespace(hit_sprite ? &*hit_sprite : nullptr, mouse_h, mouse_v)) {
        return std::nullopt;
    }

    auto global_cursor = getGlobalCursorDatum();
    if (global_cursor.isList() && !global_cursor.listValue().items().empty()) {
        return resolveCursorRegPoint(encodeCursorMember(global_cursor.listValue().items().front()));
    }

    if (hit_channel <= 0 || sprite_registry_ == nullptr) {
        return std::nullopt;
    }

    auto sprite = sprite_registry_->get(hit_channel);
    if (!sprite || !sprite->hasBitmapCursor()) {
        return std::nullopt;
    }
    return resolveCursorRegPoint(sprite->cursorMemberNum());
}

bitmap::Bitmap CursorManager::applyCursorMask(const bitmap::Bitmap& cursor, const bitmap::Bitmap& mask) {
    bitmap::Bitmap result(cursor.width(), cursor.height(), 32);
    for (int y = 0; y < cursor.height(); ++y) {
        for (int x = 0; x < cursor.width(); ++x) {
            const std::uint32_t cursor_rgb = cursor.getPixel(x, y) & 0x00FFFFFFU;
            if (x < mask.width() && y < mask.height()) {
                const std::uint32_t mask_rgb = mask.getPixel(x, y) & 0x00FFFFFFU;
                result.setPixel(x, y, mask_rgb == 0x00FFFFFFU ? 0x00000000U : (0xFF000000U | cursor_rgb));
            } else {
                result.setPixel(x, y, 0x00000000U);
            }
        }
    }
    return result;
}

int CursorManager::encodeCursorMember(const lingo::Datum& datum) {
    if (const auto* ref = datum.asCastMemberRef()) {
        return (ref->cast_lib << 16) | (ref->memberNum() & 0xFFFF);
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
    return sprite_provider_ ? sprite_provider_() : std::vector<render::pipeline::RenderSprite>{};
}

int CursorManager::hitTestVisual(const std::vector<render::pipeline::RenderSprite>& sprites,
                                 int stage_x,
                                 int stage_y) const {
    return input::HitTester::hitTest(sprites, stage_x, stage_y);
}

int CursorManager::hitTestInteractive(const std::vector<render::pipeline::RenderSprite>& sprites,
                                      int stage_x,
                                      int stage_y) const {
    auto exact_hits = input::HitTester::hitTestAll(sprites, stage_x, stage_y, [](int) {
        return false;
    });
    auto exact = std::ranges::find_if(exact_hits, [this](int channel) {
        return isInteractive(channel);
    });
    if (exact != exact_hits.end()) {
        return *exact;
    }

    auto bounding_hits = input::HitTester::hitTestAll(sprites, stage_x, stage_y, [this](int channel) {
        return isInteractive(channel);
    });
    auto bounding = std::ranges::find_if(bounding_hits, [this](int channel) {
        return isInteractive(channel);
    });
    return bounding == bounding_hits.end() ? 0 : *bounding;
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

bool CursorManager::isNavigatorWhitespace(const render::pipeline::RenderSprite* sprite, int stage_x, int stage_y) const {
    if (sprite == nullptr || !sprite->hasBehaviors() || sprite->inkMode() != id::InkMode::MATTE) {
        return false;
    }

    const auto baked = sprite->bakedBitmap();
    if (!baked) {
        return false;
    }

    const int local_x = stage_x - sprite->x();
    const int local_y = stage_y - sprite->y();
    const int bw = baked->width();
    const int bh = baked->height();
    const int sw = sprite->width();
    const int sh = sprite->height();
    const int bx = (sw > 0 && sw != bw) ? (local_x * bw / sw) : local_x;
    const int by = (sh > 0 && sh != bh) ? (local_y * bh / sh) : local_y;
    if (bx < 0 || bx >= bw || by < 0 || by >= bh) {
        return false;
    }

    const std::uint32_t pixel = baked->getPixel(bx, by);
    const int alpha = static_cast<int>((pixel >> 24) & 0xFFU);
    return alpha >= 128 && isNearWhite(pixel);
}

lingo::Datum CursorManager::getGlobalCursorDatum() const {
    return global_cursor_supplier_ ? global_cursor_supplier_() : lingo::Datum::voidValue();
}

int CursorManager::getGlobalCursorCode() const {
    auto global_cursor = getGlobalCursorDatum();
    if (global_cursor.isList() && !global_cursor.listValue().items().empty()) {
        return CUSTOM_BITMAP_CURSOR;
    }
    if (!global_cursor.isVoid()) {
        return cursorCodeFromDatum(global_cursor);
    }
    return DEFAULT_CURSOR;
}

std::optional<bitmap::Bitmap> CursorManager::resolveCursorBitmap(int encoded_member, int encoded_mask) const {
    if (!bitmap_resolver_) {
        return std::nullopt;
    }

    int cast_lib = (encoded_member >> 16) & 0xFFFF;
    const int member_num = encoded_member & 0xFFFF;
    if (cast_lib == 0) {
        cast_lib = 1;
    }

    auto cursor = bitmap_resolver_(cast_lib, member_num);
    if (!cursor.has_value()) {
        return std::nullopt;
    }

    if (encoded_mask != 0) {
        int mask_cast_lib = (encoded_mask >> 16) & 0xFFFF;
        const int mask_member_num = encoded_mask & 0xFFFF;
        if (mask_cast_lib == 0) {
            mask_cast_lib = 1;
        }

        auto mask = bitmap_resolver_(mask_cast_lib, mask_member_num);
        if (mask.has_value()) {
            cursor = applyCursorMask(*cursor, *mask);
        }
    }

    return cursor;
}

std::optional<std::array<int, 2>> CursorManager::resolveCursorRegPoint(int encoded_member) const {
    if (!member_info_resolver_) {
        return std::array<int, 2>{0, 0};
    }

    int cast_lib = (encoded_member >> 16) & 0xFFFF;
    const int member_num = encoded_member & 0xFFFF;
    if (cast_lib == 0) {
        cast_lib = 1;
    }

    const auto info = member_info_resolver_(cast_lib, member_num);
    if (!info.has_value()) {
        return std::array<int, 2>{0, 0};
    }
    return std::array<int, 2>{info->reg_x, info->reg_y};
}

bool CursorManager::isInteractive(int channel) const {
    return interactive_predicate_ && interactive_predicate_(channel);
}

} // namespace libreshockwave::player
