#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>

#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/bitmap/Palette.hpp"
#include "libreshockwave/chunks/CastMemberChunk.hpp"

namespace libreshockwave::player::render::pipeline {

struct IndexedMatteColorRemap {
    std::uint32_t foreColor{0};
    std::uint32_t backColor{0};

    friend bool operator==(const IndexedMatteColorRemap&, const IndexedMatteColorRemap&) = default;
};

struct BitmapCacheMemberId {
    std::uintptr_t fileIdentity{0};
    int memberId{0};

    friend auto operator<=>(const BitmapCacheMemberId&, const BitmapCacheMemberId&) = default;
};

struct BitmapCacheKey {
    BitmapCacheMemberId member;
    int ink{0};
    int foreColor{0};
    int backColor{0};
    bool hasForeColor{false};
    bool hasBackColor{false};

    friend auto operator<=>(const BitmapCacheKey&, const BitmapCacheKey&) = default;
};

class BitmapCache {
public:
    [[nodiscard]] std::shared_ptr<const bitmap::Bitmap> getCachedProcessed(const chunks::CastMemberChunk& member,
                                                                           int ink,
                                                                           int backColor,
                                                                           int foreColor,
                                                                           bool hasForeColor,
                                                                           bool hasBackColor) const;
    void putProcessed(const chunks::CastMemberChunk& member,
                      int ink,
                      int backColor,
                      int foreColor,
                      bool hasForeColor,
                      bool hasBackColor,
                      std::shared_ptr<const bitmap::Bitmap> bitmap);

    void markDecodeFailed(const chunks::CastMemberChunk& member);
    [[nodiscard]] bool hasDecodeFailed(const chunks::CastMemberChunk& member) const;
    [[nodiscard]] bool invalidateIfPaletteChanged(const chunks::CastMemberChunk& member, int paletteVersion);
    void clear();

    [[nodiscard]] int cachedBitmapCount() const;
    [[nodiscard]] int decodeFailedCount() const;
    [[nodiscard]] int trackedPaletteVersionCount() const;

    [[nodiscard]] static BitmapCacheMemberId memberKey(const chunks::CastMemberChunk* member);
    [[nodiscard]] static BitmapCacheKey cacheKey(const chunks::CastMemberChunk& member,
                                                 int ink,
                                                 int backColor,
                                                 int foreColor,
                                                 bool hasForeColor,
                                                 bool hasBackColor);
    [[nodiscard]] static std::shared_ptr<const bitmap::Bitmap> coerceNonNativeAlphaToOpaque(
        std::shared_ptr<const bitmap::Bitmap> raw,
        bool useAlpha);
    [[nodiscard]] static bitmap::Bitmap coerceNonNativeAlphaToOpaque(const bitmap::Bitmap& raw, bool useAlpha);

    [[nodiscard]] static std::optional<IndexedMatteColorRemap> resolveIndexedMatteColorRemap(
        const bitmap::Bitmap* raw,
        int ink,
        int foreColor,
        int backColor,
        bool hasForeColor,
        bool hasBackColor,
        const bitmap::Palette* palette);
    [[nodiscard]] static bitmap::Bitmap applyIndexedMatteColorRemapIfNeeded(
        const bitmap::Bitmap* raw,
        const bitmap::Bitmap& processed,
        int ink,
        int foreColor,
        int backColor,
        bool hasForeColor,
        bool hasBackColor,
        const bitmap::Palette* palette);
    [[nodiscard]] static bitmap::Bitmap applyIndexedMatteColorRemap(
        const bitmap::Bitmap* raw,
        const bitmap::Bitmap& processed,
        const std::optional<IndexedMatteColorRemap>& remap);

private:
    std::map<BitmapCacheKey, std::shared_ptr<const bitmap::Bitmap>> cache_;
    std::map<BitmapCacheMemberId, bool> decodeFailed_;
    std::map<BitmapCacheMemberId, int> paletteVersions_;
};

} // namespace libreshockwave::player::render::pipeline
