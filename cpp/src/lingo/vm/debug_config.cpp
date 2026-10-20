#include "libreshockwave/lingo/vm/debug_config.hpp"

#include <atomic>

namespace libreshockwave::lingo::vm {
namespace {

std::atomic_bool g_debug_playback_enabled{false};

} // namespace

bool DebugConfig::isDebugPlaybackEnabled() {
    return g_debug_playback_enabled.load(std::memory_order_relaxed);
}

void DebugConfig::setDebugPlaybackEnabled(bool enabled) {
    g_debug_playback_enabled.store(enabled, std::memory_order_relaxed);
}

} // namespace libreshockwave::lingo::vm
