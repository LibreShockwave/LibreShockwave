#include "libreshockwave/lingo/vm/DebugConfig.hpp"

#include <atomic>

namespace libreshockwave::lingo::vm {
namespace {

std::atomic_bool gDebugPlaybackEnabled{false};

} // namespace

bool DebugConfig::isDebugPlaybackEnabled() {
    return gDebugPlaybackEnabled.load(std::memory_order_relaxed);
}

void DebugConfig::setDebugPlaybackEnabled(bool enabled) {
    gDebugPlaybackEnabled.store(enabled, std::memory_order_relaxed);
}

} // namespace libreshockwave::lingo::vm
