#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/bitmap/palette.hpp"
#include "libreshockwave/chunks/cast_member_chunk.hpp"

namespace libreshockwave::player::render::pipeline {

struct IndexedMatteColorRemap {
    std::uint32_t fore_color{0};
    std::uint32_t back_color{0};

    friend bool operator==(const IndexedMatteColorRemap&, const IndexedMatteColorRemap&) = default;
};

struct BitmapCacheMemberId {
    std::uintptr_t file_identity{0};
    int member_id{0};

    friend auto operator<=>(const BitmapCacheMemberId&, const BitmapCacheMemberId&) = default;
};

struct BitmapCacheKey {
    BitmapCacheMemberId member;
    int ink{0};
    int fore_color{0};
    int back_color{0};
    bool has_fore_color{false};
    bool has_back_color{false};

    friend auto operator<=>(const BitmapCacheKey&, const BitmapCacheKey&) = default;
};

class BitmapCache {
public:
    [[nodiscard]] std::shared_ptr<const bitmap::Bitmap> getCachedProcessed(const chunks::CastMemberChunk& member,
                                                                           int ink,
                                                                           int back_color,
                                                                           int fore_color,
                                                                           bool has_fore_color,
                                                                           bool has_back_color) const;
    void putProcessed(const chunks::CastMemberChunk& member,
                      int ink,
                      int back_color,
                      int fore_color,
                      bool has_fore_color,
                      bool has_back_color,
                      std::shared_ptr<const bitmap::Bitmap> bitmap);

    void markDecodeFailed(const chunks::CastMemberChunk& member);
    [[nodiscard]] bool hasDecodeFailed(const chunks::CastMemberChunk& member) const;
    void invalidateMember(const chunks::CastMemberChunk& member);
    [[nodiscard]] bool invalidateIfPaletteChanged(const chunks::CastMemberChunk& member, int palette_version);
    void clear();

    [[nodiscard]] int cachedBitmapCount() const;
    [[nodiscard]] int decodeFailedCount() const;
    [[nodiscard]] int trackedPaletteVersionCount() const;

    [[nodiscard]] static BitmapCacheMemberId memberKey(const chunks::CastMemberChunk* member);
    [[nodiscard]] static BitmapCacheKey cacheKey(const chunks::CastMemberChunk& member,
                                                 int ink,
                                                 int back_color,
                                                 int fore_color,
                                                 bool has_fore_color,
                                                 bool has_back_color);
    [[nodiscard]] static std::shared_ptr<const bitmap::Bitmap> coerceNonNativeAlphaToOpaque(
        std::shared_ptr<const bitmap::Bitmap> raw,
        bool use_alpha);
    [[nodiscard]] static bitmap::Bitmap coerceNonNativeAlphaToOpaque(const bitmap::Bitmap& raw, bool use_alpha);

    [[nodiscard]] static std::optional<IndexedMatteColorRemap> resolveIndexedMatteColorRemap(
        const bitmap::Bitmap* raw,
        int ink,
        int fore_color,
        int back_color,
        bool has_fore_color,
        bool has_back_color,
        const bitmap::Palette* palette);
    [[nodiscard]] static bitmap::Bitmap applyIndexedMatteColorRemapIfNeeded(
        const bitmap::Bitmap* raw,
        const bitmap::Bitmap& processed,
        int ink,
        int fore_color,
        int back_color,
        bool has_fore_color,
        bool has_back_color,
        const bitmap::Palette* palette);
    [[nodiscard]] static bitmap::Bitmap applyIndexedMatteColorRemap(
        const bitmap::Bitmap* raw,
        const bitmap::Bitmap& processed,
        const std::optional<IndexedMatteColorRemap>& remap);

private:
    std::map<BitmapCacheKey, std::shared_ptr<const bitmap::Bitmap>> cache_;
    std::map<BitmapCacheMemberId, bool> decode_failed_;
    std::map<BitmapCacheMemberId, int> palette_versions_;
};

} // namespace libreshockwave::player::render::pipeline
