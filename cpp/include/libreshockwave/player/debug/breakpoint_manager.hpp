#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "libreshockwave/player/debug/breakpoint.hpp"

namespace libreshockwave::player::debug {

class BreakpointManager {
public:
    [[nodiscard]] std::optional<Breakpoint> getBreakpoint(int script_id,
                                                          const std::string& handler_name,
                                                          int offset) const;
    [[nodiscard]] bool hasBreakpoint(int script_id, const std::string& handler_name, int offset) const;
    void setBreakpoint(Breakpoint breakpoint);
    [[nodiscard]] Breakpoint addBreakpoint(int script_id, std::string handler_name, int offset);
    [[nodiscard]] std::optional<Breakpoint> removeBreakpoint(int script_id,
                                                             const std::string& handler_name,
                                                             int offset);
    [[nodiscard]] std::optional<Breakpoint> toggleBreakpoint(int script_id,
                                                             std::string handler_name,
                                                             int offset);
    [[nodiscard]] std::optional<Breakpoint> toggleEnabled(int script_id,
                                                          const std::string& handler_name,
                                                          int offset);

    [[nodiscard]] std::vector<Breakpoint> getAllBreakpoints() const;
    [[nodiscard]] std::vector<Breakpoint> getBreakpointsForScript(int script_id) const;
    [[nodiscard]] std::set<int> getOffsetsForScript(int script_id) const;
    void clearAll();

    [[nodiscard]] std::map<int, std::set<int>> toOffsetMap() const;
    void setFromOffsetMap(const std::map<int, std::set<int>>& offset_map);

    [[nodiscard]] std::string serialize() const;
    [[nodiscard]] std::string serializeLegacy() const;
    void deserialize(std::string data);

private:
    void deserializeJson(const std::string& json);
    void deserializeLegacy(const std::string& data);

    [[nodiscard]] static std::string escapeJson(const std::string& value);
    [[nodiscard]] static std::optional<Breakpoint> parseBreakpointJson(const std::string& json);
    [[nodiscard]] static int parseJsonInt(const std::string& json, const std::string& key, int default_value);
    [[nodiscard]] static std::string parseJsonString(const std::string& json,
                                                     const std::string& key,
                                                     const std::string& default_value);
    [[nodiscard]] static bool parseJsonBoolean(const std::string& json,
                                               const std::string& key,
                                               bool default_value);

    std::map<BreakpointKey, Breakpoint> breakpoints_;
};

} // namespace libreshockwave::player::debug
