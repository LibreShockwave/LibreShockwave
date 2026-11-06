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
    // Non-zero for entries produced from a script-owned runtime bitmap. The
    // revision identifies that bitmap's exact content state, so a mutated
    // image can never reuse an earlier result.
    std::uint64_t source_revision{0};

    friend auto operator<=>(const BitmapCacheKey&, const BitmapCacheKey&) = default;
};

struct BitmapCacheEntry {
    std::shared_ptr<const bitmap::Bitmap> bitmap;
    // Keeps a runtime entry tied to the image it came from. Empty for entries
    // produced from an authored cast member.
    std::weak_ptr<const bitmap::Bitmap> runtime_source;
    // Keeps an entry tied to the exact member object it was produced from.
    // Cache keys hold a file pointer and a member number, and both are reused
    // once a cast file is unloaded. This weak reference rejects an entry whose
    // member object is gone, so a new member cannot inherit old pixels.
    std::weak_ptr<const chunks::CastMemberChunk> member_owner;
};

class BitmapCache {
public:
    /// Look up the processed result of an authored cast member.
    /// Pass the shared owner of the member as member_owner to tie the entry to
    /// that object: an entry whose member died, or that belongs to a different
    /// member object, is dropped instead of served. Omit member_owner when no
    /// shared ownership exists, which keeps the older key-only behaviour.
    /// Example: `if (auto hit = cache.getCachedProcessed(*member, ink, back, fore,
    /// true, true, member)) return hit;`
    [[nodiscard]] std::shared_ptr<const bitmap::Bitmap> getCachedProcessed(const chunks::CastMemberChunk& member,
                                                                           int ink,
                                                                           int back_color,
                                                                           int fore_color,
                                                                           bool has_fore_color,
                                                                           bool has_back_color,
                                                                           const std::shared_ptr<const chunks::CastMemberChunk>& member_owner = {}) const;
    void putProcessed(const chunks::CastMemberChunk& member,
                      int ink,
                      int back_color,
                      int fore_color,
                      bool has_fore_color,
                      bool has_back_color,
                      std::shared_ptr<const bitmap::Bitmap> bitmap,
                      const std::shared_ptr<const chunks::CastMemberChunk>& member_owner = {});

    /// Record that a member could not be decoded, so the renderer stops
    /// retrying it. Pass member_owner to bind the record to that member object:
    /// a later member that reuses the same file pointer and number is not
    /// poisoned by it. Example: `cache.markDecodeFailed(*member, member);`
    void markDecodeFailed(const chunks::CastMemberChunk& member,
                          const std::shared_ptr<const chunks::CastMemberChunk>& member_owner = {});
    [[nodiscard]] bool hasDecodeFailed(const chunks::CastMemberChunk& member,
                                       const std::shared_ptr<const chunks::CastMemberChunk>& member_owner = {}) const;

    /// Look up the processed result of a script-owned runtime image.
    /// The source revision identifies the exact pixel state the result was
    /// produced from, so the caller never sees a stale bitmap. Pass a null
    /// member for images that have no authored cast backing.
    /// Example: `if (auto hit = cache.getCachedRuntimeProcessed(member, live, ...)) return hit;`
    [[nodiscard]] std::shared_ptr<const bitmap::Bitmap> getCachedRuntimeProcessed(
        const chunks::CastMemberChunk* member,
        const std::shared_ptr<const bitmap::Bitmap>& source,
        int ink,
        int back_color,
        int fore_color,
        bool has_fore_color,
        bool has_back_color,
        const std::shared_ptr<const chunks::CastMemberChunk>& member_owner = {}) const;

    /// Store the processed result of a script-owned runtime image.
    /// Earlier results for the same source image are dropped, so a mutating
    /// image holds at most one cached result per ink and colour combination.
    void putRuntimeProcessed(const chunks::CastMemberChunk* member,
                             const std::shared_ptr<const bitmap::Bitmap>& source,
                             int ink,
                             int back_color,
                             int fore_color,
                             bool has_fore_color,
                             bool has_back_color,
                             std::shared_ptr<const bitmap::Bitmap> bitmap,
                             const std::shared_ptr<const chunks::CastMemberChunk>& member_owner = {});

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

    // Builds the colour remap an indexed source needs under Matte or Background
    // Transparent. back_color below 256 is a palette number that movie_palette
    // resolves, matching the movie palette, and falls back to palette. Pass
    // nullptr for movie_palette to keep the source table.
    // Example: `resolveIndexedMatteColorRemap(&raw, sprite.ink(), fore, back,
    // true, true, raw.imagePalette().get(), movie_palette.get())`.
    [[nodiscard]] static std::optional<IndexedMatteColorRemap> resolveIndexedMatteColorRemap(
        const bitmap::Bitmap* raw,
        int ink,
        int fore_color,
        int back_color,
        bool has_fore_color,
        bool has_back_color,
        const bitmap::Palette* palette,
        const bitmap::Palette* movie_palette = nullptr);
    [[nodiscard]] static bitmap::Bitmap applyIndexedMatteColorRemapIfNeeded(
        const bitmap::Bitmap* raw,
        const bitmap::Bitmap& processed,
        int ink,
        int fore_color,
        int back_color,
        bool has_fore_color,
        bool has_back_color,
        const bitmap::Palette* palette,
        const bitmap::Palette* movie_palette = nullptr);
    [[nodiscard]] static bitmap::Bitmap applyIndexedMatteColorRemap(
        const bitmap::Bitmap* raw,
        const bitmap::Bitmap& processed,
        const std::optional<IndexedMatteColorRemap>& remap);

private:
    [[nodiscard]] static BitmapCacheKey runtimeCacheKey(const chunks::CastMemberChunk* member,
                                                        std::uint64_t source_revision,
                                                        int ink,
                                                        int back_color,
                                                        int fore_color,
                                                        bool has_fore_color,
                                                        bool has_back_color);
    // Shared lookup for both cache families. An entry whose owning member or
    // source image is gone is dropped here and reported as a miss, so a reused
    // file pointer, member number, or bitmap address cannot return old pixels.
    [[nodiscard]] std::shared_ptr<const bitmap::Bitmap> lookup(
        const BitmapCacheKey& key,
        const std::shared_ptr<const chunks::CastMemberChunk>& member_owner,
        const std::shared_ptr<const bitmap::Bitmap>& source) const;
    void store(const BitmapCacheKey& key,
               std::shared_ptr<const bitmap::Bitmap> bitmap,
               std::weak_ptr<const bitmap::Bitmap> runtime_source,
               std::weak_ptr<const chunks::CastMemberChunk> member_owner);

    // Lookups prune rejected entries, so the maps change even when a caller
    // only reads. That makes a render cache logically mutable.
    mutable std::map<BitmapCacheKey, BitmapCacheEntry> cache_;
    mutable std::map<BitmapCacheMemberId, std::weak_ptr<const chunks::CastMemberChunk>> decode_failed_;
    std::map<BitmapCacheMemberId, int> palette_versions_;
};

} // namespace libreshockwave::player::render::pipeline
