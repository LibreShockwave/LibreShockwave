#pragma once

#include <compare>
#include <string>

namespace libreshockwave::player::debug {

struct Breakpoint {
    int script_id{0};
    std::string handler_name;
    int offset{0};
    bool enabled{true};

    [[nodiscard]] static Breakpoint simple(int script_id, std::string handler_name, int offset);
    [[nodiscard]] Breakpoint withEnabled(bool enabled) const;
    [[nodiscard]] std::string key() const;
    [[nodiscard]] std::string toString() const;

    friend bool operator==(const Breakpoint&, const Breakpoint&) = default;
};

struct BreakpointKey {
    int script_id{0};
    std::string handler_name;
    int offset{0};

    [[nodiscard]] static BreakpointKey of(int script_id, std::string handler_name, int offset);
    [[nodiscard]] static BreakpointKey of(const Breakpoint& breakpoint);
    [[nodiscard]] std::string toString() const;

    friend auto operator<=>(const BreakpointKey&, const BreakpointKey&) = default;
};

} // namespace libreshockwave::player::debug
