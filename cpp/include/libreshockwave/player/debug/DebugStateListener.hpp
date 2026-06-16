#pragma once

#include "libreshockwave/player/debug/DebugSnapshot.hpp"

namespace libreshockwave::player::debug {

class DebugStateListener {
public:
    virtual ~DebugStateListener() = default;

    virtual void onPaused(const DebugSnapshot&) {}
    virtual void onResumed() {}
    virtual void onBreakpointsChanged() {}
    virtual void onWatchExpressionsChanged() {}
};

} // namespace libreshockwave::player::debug
