#pragma once

#include <cstdint>

namespace libreshockwave::bitmap {

/// Identity of one bitmap's content state.
///
/// Every Bitmap owns one. Construction and copying assign a fresh value, and a
/// mutation refreshes the value in place, so two images that report the same
/// value hold identical content. Render caches key on the value to reject
/// results that were produced from an older state of the same image.
/// Example: `if (cached_revision == image.revision()) reuse();`
class BitmapRevision {
public:
    BitmapRevision() noexcept;
    BitmapRevision(const BitmapRevision& other) noexcept;
    BitmapRevision& operator=(const BitmapRevision& other) noexcept;
    BitmapRevision(BitmapRevision&&) = default;
    BitmapRevision& operator=(BitmapRevision&&) = default;

    /// The identity value of this content state. Never zero, because zero
    /// means "not produced by a Bitmap" in cache keys.
    /// Example: `keys.source_revision = revision.value();`
    [[nodiscard]] std::uint64_t value() const noexcept { return value_; }

    /// Replace the identity with a fresh value. Cached results derived from
    /// the previous state stop matching after this call.
    void refresh() noexcept;

private:
    std::uint64_t value_{0};
};

} // namespace libreshockwave::bitmap
