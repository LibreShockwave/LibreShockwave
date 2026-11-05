#include "libreshockwave/bitmap/bitmap_revision.hpp"

#include <atomic>

namespace libreshockwave::bitmap {
namespace {

// Monotonic source of content identities. Constant-initialized, so it is valid
// before main() and unique across the whole process.
std::atomic<std::uint64_t> g_bitmap_revision{0};

std::uint64_t nextRevision() noexcept {
    return g_bitmap_revision.fetch_add(1, std::memory_order_relaxed) + 1;
}

} // namespace

BitmapRevision::BitmapRevision() noexcept : value_(nextRevision()) {}

BitmapRevision::BitmapRevision(const BitmapRevision&) noexcept : value_(nextRevision()) {}

BitmapRevision& BitmapRevision::operator=(const BitmapRevision&) noexcept {
    value_ = nextRevision();
    return *this;
}

void BitmapRevision::refresh() noexcept { value_ = nextRevision(); }

} // namespace libreshockwave::bitmap
