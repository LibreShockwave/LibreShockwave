#include "libreshockwave/player/render/pipeline/BitmapCache.hpp"

#include <utility>

#include "libreshockwave/id/Ids.hpp"
#include "libreshockwave/player/render/pipeline/InkProcessor.hpp"

namespace libreshockwave::player::render::pipeline {

namespace {

bool isTransparentOrBlackMask(const bitmap::Bitmap& bitmap) {
    bool hasOpaqueBlack = false;
    for (const auto pixel : bitmap.pixels()) {
        const auto alpha = (pixel >> 24U) & 0xFFU;
        if (alpha == 0) {
            continue;
        }
        if ((pixel & 0x00FFFFFFU) != 0x000000U) {
            return false;
        }
        hasOpaqueBlack = true;
    }
    return hasOpaqueBlack;
}

} // namespace

std::shared_ptr<const bitmap::Bitmap> BitmapCache::getCachedProcessed(const chunks::CastMemberChunk& member,
                                                                      int ink,
                                                                      int backColor,
                                                                      int foreColor,
                                                                      bool hasForeColor,
                                                                      bool hasBackColor) const {
    const auto key = cacheKey(member, ink, backColor, foreColor, hasForeColor, hasBackColor);
    const auto it = cache_.find(key);
    return it == cache_.end() ? nullptr : it->second;
}

void BitmapCache::putProcessed(const chunks::CastMemberChunk& member,
                               int ink,
                               int backColor,
                               int foreColor,
                               bool hasForeColor,
                               bool hasBackColor,
                               std::shared_ptr<const bitmap::Bitmap> bitmap) {
    const auto key = cacheKey(member, ink, backColor, foreColor, hasForeColor, hasBackColor);
    if (bitmap) {
        cache_[key] = std::move(bitmap);
    } else {
        cache_.erase(key);
    }
}

void BitmapCache::markDecodeFailed(const chunks::CastMemberChunk& member) {
    decodeFailed_[memberKey(&member)] = true;
}

bool BitmapCache::hasDecodeFailed(const chunks::CastMemberChunk& member) const {
    return decodeFailed_.contains(memberKey(&member));
}

bool BitmapCache::invalidateIfPaletteChanged(const chunks::CastMemberChunk& member, int paletteVersion) {
    const auto memberId = memberKey(&member);
    const auto it = paletteVersions_.find(memberId);
    if (it != paletteVersions_.end() && it->second == paletteVersion) {
        return false;
    }

    paletteVersions_[memberId] = paletteVersion;
    for (auto iter = cache_.begin(); iter != cache_.end();) {
        if (iter->first.member == memberId) {
            iter = cache_.erase(iter);
        } else {
            ++iter;
        }
    }
    decodeFailed_.erase(memberId);
    return true;
}

void BitmapCache::clear() {
    cache_.clear();
    decodeFailed_.clear();
    paletteVersions_.clear();
}

int BitmapCache::cachedBitmapCount() const {
    return static_cast<int>(cache_.size());
}

int BitmapCache::decodeFailedCount() const {
    return static_cast<int>(decodeFailed_.size());
}

int BitmapCache::trackedPaletteVersionCount() const {
    return static_cast<int>(paletteVersions_.size());
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
                                     int backColor,
                                     int foreColor,
                                     bool hasForeColor,
                                     bool hasBackColor) {
    return BitmapCacheKey{
        memberKey(&member),
        ink,
        hasForeColor ? foreColor : 0,
        backColor,
        hasForeColor,
        hasBackColor
    };
}

std::shared_ptr<const bitmap::Bitmap> BitmapCache::coerceNonNativeAlphaToOpaque(
    std::shared_ptr<const bitmap::Bitmap> raw,
    bool useAlpha) {
    if (!raw || raw->bitDepth() != 32 || useAlpha || !raw->hasTransparentPixels()) {
        return raw;
    }
    return std::make_shared<bitmap::Bitmap>(raw->copyWithNonNativeAlphaOpaque());
}

bitmap::Bitmap BitmapCache::coerceNonNativeAlphaToOpaque(const bitmap::Bitmap& raw, bool useAlpha) {
    if (raw.bitDepth() != 32 || useAlpha || !raw.hasTransparentPixels()) {
        return raw.copy();
    }
    return raw.copyWithNonNativeAlphaOpaque();
}

std::optional<IndexedMatteColorRemap> BitmapCache::resolveIndexedMatteColorRemap(
    const bitmap::Bitmap* raw,
    int ink,
    int foreColor,
    int backColor,
    bool hasForeColor,
    bool hasBackColor,
    const bitmap::Palette* palette) {
    if (raw == nullptr || raw->bitDepth() <= 1 || !raw->paletteIndices().has_value()) {
        return std::nullopt;
    }

    const auto inkMode = id::inkModeFromCode(ink);
    if (inkMode != id::InkMode::MATTE && inkMode != id::InkMode::BACKGROUND_TRANSPARENT) {
        return std::nullopt;
    }
    if (!hasForeColor && !hasBackColor) {
        return std::nullopt;
    }

    const auto effectiveForeColor = static_cast<std::uint32_t>(hasForeColor ? foreColor : 0x000000) & 0x00FFFFFFU;
    const auto effectiveBackColor = static_cast<std::uint32_t>(
        hasBackColor ? InkProcessor::resolveBackColor(*raw, id::InkMode::COPY, backColor, false, palette) : 0xFFFFFF
    ) & 0x00FFFFFFU;

    if (effectiveForeColor == 0x000000U && effectiveBackColor == 0xFFFFFFU) {
        return std::nullopt;
    }

    return IndexedMatteColorRemap{effectiveForeColor, effectiveBackColor};
}

bitmap::Bitmap BitmapCache::applyIndexedMatteColorRemapIfNeeded(const bitmap::Bitmap* raw,
                                                                const bitmap::Bitmap& processed,
                                                                int ink,
                                                                int foreColor,
                                                                int backColor,
                                                                bool hasForeColor,
                                                                bool hasBackColor,
                                                                const bitmap::Palette* palette) {
    const auto inkMode = id::inkModeFromCode(ink);
    if (inkMode == id::InkMode::DARKEN &&
        hasForeColor &&
        raw != nullptr &&
        raw->bitDepth() > 1 &&
        raw->paletteIndices().has_value()) {
        return InkProcessor::applyDarkenForeColorOffset(processed, static_cast<std::uint32_t>(foreColor));
    }

    return applyIndexedMatteColorRemap(
        raw,
        processed,
        resolveIndexedMatteColorRemap(raw, ink, foreColor, backColor, hasForeColor, hasBackColor, palette));
}

bitmap::Bitmap BitmapCache::applyIndexedMatteColorRemap(
    const bitmap::Bitmap* raw,
    const bitmap::Bitmap& processed,
    const std::optional<IndexedMatteColorRemap>& remap) {
    if (raw == nullptr || !remap.has_value()) {
        return processed.copy();
    }
    if (remap->foreColor == 0x0000FFU && remap->backColor == 0xFFFFFFU && isTransparentOrBlackMask(processed)) {
        return processed.copy();
    }
    return InkProcessor::applyIndexedColorRemap(*raw, processed, remap->foreColor, remap->backColor);
}

} // namespace libreshockwave::player::render::pipeline
