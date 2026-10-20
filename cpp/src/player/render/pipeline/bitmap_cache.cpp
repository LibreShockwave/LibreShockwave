#include "libreshockwave/player/render/pipeline/bitmap_cache.hpp"

#include <utility>

#include "libreshockwave/id/ids.hpp"
#include "libreshockwave/player/render/pipeline/ink_processor.hpp"

namespace libreshockwave::player::render::pipeline {

namespace {

bool isTransparentOrBlackMask(const bitmap::Bitmap& bitmap) {
    bool has_opaque_black = false;
    for (const auto pixel : bitmap.pixels()) {
        const auto alpha = (pixel >> 24U) & 0xFFU;
        if (alpha == 0) {
            continue;
        }
        if ((pixel & 0x00FFFFFFU) != 0x000000U) {
            return false;
        }
        has_opaque_black = true;
    }
    return has_opaque_black;
}

} // namespace

std::shared_ptr<const bitmap::Bitmap> BitmapCache::getCachedProcessed(const chunks::CastMemberChunk& member,
                                                                      int ink,
                                                                      int back_color,
                                                                      int fore_color,
                                                                      bool has_fore_color,
                                                                      bool has_back_color) const {
    const auto key = cacheKey(member, ink, back_color, fore_color, has_fore_color, has_back_color);
    const auto it = cache_.find(key);
    return it == cache_.end() ? nullptr : it->second;
}

void BitmapCache::putProcessed(const chunks::CastMemberChunk& member,
                               int ink,
                               int back_color,
                               int fore_color,
                               bool has_fore_color,
                               bool has_back_color,
                               std::shared_ptr<const bitmap::Bitmap> bitmap) {
    const auto key = cacheKey(member, ink, back_color, fore_color, has_fore_color, has_back_color);
    if (bitmap) {
        cache_[key] = std::move(bitmap);
    } else {
        cache_.erase(key);
    }
}

void BitmapCache::markDecodeFailed(const chunks::CastMemberChunk& member) {
    decode_failed_[memberKey(&member)] = true;
}

bool BitmapCache::hasDecodeFailed(const chunks::CastMemberChunk& member) const {
    return decode_failed_.contains(memberKey(&member));
}

void BitmapCache::invalidateMember(const chunks::CastMemberChunk& member) {
    const auto member_id = memberKey(&member);
    for (auto iter = cache_.begin(); iter != cache_.end();) {
        if (iter->first.member == member_id) {
            iter = cache_.erase(iter);
        } else {
            ++iter;
        }
    }
    decode_failed_.erase(member_id);
}

bool BitmapCache::invalidateIfPaletteChanged(const chunks::CastMemberChunk& member, int palette_version) {
    const auto member_id = memberKey(&member);
    const auto it = palette_versions_.find(member_id);
    if (it != palette_versions_.end() && it->second == palette_version) {
        return false;
    }

    palette_versions_[member_id] = palette_version;
    for (auto iter = cache_.begin(); iter != cache_.end();) {
        if (iter->first.member == member_id) {
            iter = cache_.erase(iter);
        } else {
            ++iter;
        }
    }
    decode_failed_.erase(member_id);
    return true;
}

void BitmapCache::clear() {
    cache_.clear();
    decode_failed_.clear();
    palette_versions_.clear();
}

int BitmapCache::cachedBitmapCount() const {
    return static_cast<int>(cache_.size());
}

int BitmapCache::decodeFailedCount() const {
    return static_cast<int>(decode_failed_.size());
}

int BitmapCache::trackedPaletteVersionCount() const {
    return static_cast<int>(palette_versions_.size());
}

BitmapCacheMemberId BitmapCache::memberKey(const chunks::CastMemberChunk* member) {
    if (member == nullptr) {
        return {};
    }

    return BitmapCacheMemberId{
        reinterpret_cast<std::uintptr_t>(member->file()),
        member->id().value()
    };
}

BitmapCacheKey BitmapCache::cacheKey(const chunks::CastMemberChunk& member,
                                     int ink,
                                     int back_color,
                                     int fore_color,
                                     bool has_fore_color,
                                     bool has_back_color) {
    return BitmapCacheKey{
        memberKey(&member),
        ink,
        has_fore_color ? fore_color : 0,
        back_color,
        has_fore_color,
        has_back_color
    };
}

std::shared_ptr<const bitmap::Bitmap> BitmapCache::coerceNonNativeAlphaToOpaque(
    std::shared_ptr<const bitmap::Bitmap> raw,
    bool use_alpha) {
    if (!raw || raw->bitDepth() != 32 || use_alpha || !raw->hasTransparentPixels()) {
        return raw;
    }
    return std::make_shared<bitmap::Bitmap>(coerceNonNativeAlphaToOpaque(*raw, use_alpha));
}

bitmap::Bitmap BitmapCache::coerceNonNativeAlphaToOpaque(const bitmap::Bitmap& raw, bool use_alpha) {
    if (raw.bitDepth() != 32 || use_alpha || !raw.hasTransparentPixels()) {
        return raw.copy();
    }

    // Most non-native 32-bit Director records use the alpha byte as padding,
    // but an empty 0x00000000 pixel is also used as a real transparent
    // placeholder (notably by 1x1 click-area members). Preserve that sentinel
    // so special inks cannot turn an invisible hit area into a visible block.
    auto normalized = raw.copy();
    for (auto& pixel : normalized.pixels()) {
        const auto rgb = pixel & 0x00FFFFFFU;
        if (((pixel >> 24U) & 0xFFU) != 0U || rgb != 0U) {
            pixel = 0xFF000000U | rgb;
        }
    }
    normalized.setNativeAlpha(false);
    return normalized;
}

std::optional<IndexedMatteColorRemap> BitmapCache::resolveIndexedMatteColorRemap(
    const bitmap::Bitmap* raw,
    int ink,
    int fore_color,
    int back_color,
    bool has_fore_color,
    bool has_back_color,
    const bitmap::Palette* palette) {
    if (raw == nullptr || raw->bitDepth() <= 1 || !raw->paletteIndices().has_value()) {
        return std::nullopt;
    }

    const auto ink_mode = id::inkModeFromCode(ink);
    if (ink_mode != id::InkMode::MATTE && ink_mode != id::InkMode::BACKGROUND_TRANSPARENT) {
        return std::nullopt;
    }
    if (!has_fore_color && !has_back_color) {
        return std::nullopt;
    }

    const auto effective_fore_color = static_cast<std::uint32_t>(has_fore_color ? fore_color : 0x000000) & 0x00FFFFFFU;
    const auto effective_back_color = static_cast<std::uint32_t>(
        has_back_color ? InkProcessor::resolveBackColor(*raw, id::InkMode::COPY, back_color, false, palette) : 0xFFFFFF
    ) & 0x00FFFFFFU;

    if (effective_fore_color == 0x000000U && effective_back_color == 0xFFFFFFU) {
        return std::nullopt;
    }

    return IndexedMatteColorRemap{effective_fore_color, effective_back_color};
}

bitmap::Bitmap BitmapCache::applyIndexedMatteColorRemapIfNeeded(const bitmap::Bitmap* raw,
                                                                const bitmap::Bitmap& processed,
                                                                int ink,
                                                                int fore_color,
                                                                int back_color,
                                                                bool has_fore_color,
                                                                bool has_back_color,
                                                                const bitmap::Palette* palette) {
    const auto ink_mode = id::inkModeFromCode(ink);
    if (ink_mode == id::InkMode::DARKEN &&
        has_fore_color &&
        raw != nullptr &&
        raw->bitDepth() > 1 &&
        raw->paletteIndices().has_value()) {
        return InkProcessor::applyDarkenForeColorOffset(processed, static_cast<std::uint32_t>(fore_color));
    }

    return applyIndexedMatteColorRemap(
        raw,
        processed,
        resolveIndexedMatteColorRemap(raw, ink, fore_color, back_color, has_fore_color, has_back_color, palette));
}

bitmap::Bitmap BitmapCache::applyIndexedMatteColorRemap(
    const bitmap::Bitmap* raw,
    const bitmap::Bitmap& processed,
    const std::optional<IndexedMatteColorRemap>& remap) {
    if (raw == nullptr || !remap.has_value()) {
        return processed.copy();
    }
    if (remap->fore_color == 0x0000FFU && remap->back_color == 0xFFFFFFU && isTransparentOrBlackMask(processed)) {
        return processed.copy();
    }
    return InkProcessor::applyIndexedColorRemap(*raw, processed, remap->fore_color, remap->back_color);
}

} // namespace libreshockwave::player::render::pipeline
