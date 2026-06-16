#include "libreshockwave/player/debug/Breakpoint.hpp"

#include <sstream>
#include <utility>

namespace libreshockwave::player::debug {

Breakpoint Breakpoint::simple(int scriptId, std::string handlerName, int offset) {
    return Breakpoint{scriptId, std::move(handlerName), offset, true};
}

Breakpoint Breakpoint::withEnabled(bool newEnabled) const {
    return Breakpoint{scriptId, handlerName, offset, newEnabled};
}

std::string Breakpoint::key() const {
    return std::to_string(scriptId) + ":" + handlerName + ":" + std::to_string(offset);
}

std::string Breakpoint::toString() const {
    std::ostringstream out;
    out << "Breakpoint[" << scriptId << ":" << handlerName << ":" << offset;
    if (!enabled) {
        out << ", disabled";
    }
    out << "]";
    return out.str();
}

BreakpointKey BreakpointKey::of(int scriptId, std::string handlerName, int offset) {
    return BreakpointKey{scriptId, std::move(handlerName), offset};
}

BreakpointKey BreakpointKey::of(const Breakpoint& breakpoint) {
    return BreakpointKey{breakpoint.scriptId, breakpoint.handlerName, breakpoint.offset};
}

std::string BreakpointKey::toString() const {
    return std::to_string(scriptId) + ":" + handlerName + ":" + std::to_string(offset);
}

} // namespace libreshockwave::player::debug
