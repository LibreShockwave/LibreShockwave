#include "libreshockwave/player/debug/breakpoint.hpp"

#include <sstream>
#include <utility>

namespace libreshockwave::player::debug {

Breakpoint Breakpoint::simple(int script_id, std::string handler_name, int offset) {
    return Breakpoint{script_id, std::move(handler_name), offset, true};
}

Breakpoint Breakpoint::withEnabled(bool new_enabled) const {
    return Breakpoint{script_id, handler_name, offset, new_enabled};
}

std::string Breakpoint::key() const {
    return std::to_string(script_id) + ":" + handler_name + ":" + std::to_string(offset);
}

std::string Breakpoint::toString() const {
    std::ostringstream out;
    out << "Breakpoint[" << script_id << ":" << handler_name << ":" << offset;
    if (!enabled) {
        out << ", disabled";
    }
    out << "]";
    return out.str();
}

BreakpointKey BreakpointKey::of(int script_id, std::string handler_name, int offset) {
    return BreakpointKey{script_id, std::move(handler_name), offset};
}

BreakpointKey BreakpointKey::of(const Breakpoint& breakpoint) {
    return BreakpointKey{breakpoint.script_id, breakpoint.handler_name, breakpoint.offset};
}

std::string BreakpointKey::toString() const {
    return std::to_string(script_id) + ":" + handler_name + ":" + std::to_string(offset);
}

} // namespace libreshockwave::player::debug
