#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "libreshockwave/player/debug/Breakpoint.hpp"

namespace libreshockwave::player::debug {

class BreakpointManager {
public:
    [[nodiscard]] std::optional<Breakpoint> getBreakpoint(int scriptId,
                                                          const std::string& handlerName,
                                                          int offset) const;
    [[nodiscard]] bool hasBreakpoint(int scriptId, const std::string& handlerName, int offset) const;
    void setBreakpoint(Breakpoint breakpoint);
    [[nodiscard]] Breakpoint addBreakpoint(int scriptId, std::string handlerName, int offset);
    [[nodiscard]] std::optional<Breakpoint> removeBreakpoint(int scriptId,
                                                             const std::string& handlerName,
                                                             int offset);
    [[nodiscard]] std::optional<Breakpoint> toggleBreakpoint(int scriptId,
                                                             std::string handlerName,
                                                             int offset);
    [[nodiscard]] std::optional<Breakpoint> toggleEnabled(int scriptId,
                                                          const std::string& handlerName,
                                                          int offset);

    [[nodiscard]] std::vector<Breakpoint> getAllBreakpoints() const;
    [[nodiscard]] std::vector<Breakpoint> getBreakpointsForScript(int scriptId) const;
    [[nodiscard]] std::set<int> getOffsetsForScript(int scriptId) const;
    void clearAll();

    [[nodiscard]] std::map<int, std::set<int>> toOffsetMap() const;
    void setFromOffsetMap(const std::map<int, std::set<int>>& offsetMap);

    [[nodiscard]] std::string serialize() const;
    [[nodiscard]] std::string serializeLegacy() const;
    void deserialize(std::string data);

private:
    void deserializeJson(const std::string& json);
    void deserializeLegacy(const std::string& data);

    [[nodiscard]] static std::string escapeJson(const std::string& value);
    [[nodiscard]] static std::optional<Breakpoint> parseBreakpointJson(const std::string& json);
    [[nodiscard]] static int parseJsonInt(const std::string& json, const std::string& key, int defaultValue);
    [[nodiscard]] static std::string parseJsonString(const std::string& json,
                                                     const std::string& key,
                                                     const std::string& defaultValue);
    [[nodiscard]] static bool parseJsonBoolean(const std::string& json,
                                               const std::string& key,
                                               bool defaultValue);

    std::map<BreakpointKey, Breakpoint> breakpoints_;
};

} // namespace libreshockwave::player::debug
