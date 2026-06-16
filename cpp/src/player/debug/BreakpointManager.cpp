#include "libreshockwave/player/debug/BreakpointManager.hpp"

#include <cctype>
#include <sstream>
#include <utility>

namespace libreshockwave::player::debug {
namespace {

std::string trim(std::string value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(begin, end);
}

std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> parts;
    std::string current;
    std::istringstream in(value);
    while (std::getline(in, current, delimiter)) {
        parts.push_back(current);
    }
    return parts;
}

} // namespace

std::optional<Breakpoint> BreakpointManager::getBreakpoint(int scriptId,
                                                           const std::string& handlerName,
                                                           int offset) const {
    const auto it = breakpoints_.find(BreakpointKey::of(scriptId, handlerName, offset));
    return it == breakpoints_.end() ? std::nullopt : std::optional<Breakpoint>(it->second);
}

bool BreakpointManager::hasBreakpoint(int scriptId, const std::string& handlerName, int offset) const {
    return breakpoints_.contains(BreakpointKey::of(scriptId, handlerName, offset));
}

void BreakpointManager::setBreakpoint(Breakpoint breakpoint) {
    breakpoints_[BreakpointKey::of(breakpoint)] = std::move(breakpoint);
}

Breakpoint BreakpointManager::addBreakpoint(int scriptId, std::string handlerName, int offset) {
    Breakpoint breakpoint = Breakpoint::simple(scriptId, std::move(handlerName), offset);
    setBreakpoint(breakpoint);
    return breakpoint;
}

std::optional<Breakpoint> BreakpointManager::removeBreakpoint(int scriptId,
                                                              const std::string& handlerName,
                                                              int offset) {
    const auto key = BreakpointKey::of(scriptId, handlerName, offset);
    const auto it = breakpoints_.find(key);
    if (it == breakpoints_.end()) {
        return std::nullopt;
    }
    Breakpoint removed = it->second;
    breakpoints_.erase(it);
    return removed;
}

std::optional<Breakpoint> BreakpointManager::toggleBreakpoint(int scriptId,
                                                              std::string handlerName,
                                                              int offset) {
    const auto key = BreakpointKey::of(scriptId, handlerName, offset);
    const auto it = breakpoints_.find(key);
    if (it != breakpoints_.end()) {
        breakpoints_.erase(it);
        return std::nullopt;
    }
    Breakpoint breakpoint = Breakpoint::simple(scriptId, std::move(handlerName), offset);
    breakpoints_[BreakpointKey::of(breakpoint)] = breakpoint;
    return breakpoint;
}

std::optional<Breakpoint> BreakpointManager::toggleEnabled(int scriptId,
                                                           const std::string& handlerName,
                                                           int offset) {
    const auto key = BreakpointKey::of(scriptId, handlerName, offset);
    const auto it = breakpoints_.find(key);
    if (it == breakpoints_.end()) {
        return std::nullopt;
    }
    it->second = it->second.withEnabled(!it->second.enabled);
    return it->second;
}

std::vector<Breakpoint> BreakpointManager::getAllBreakpoints() const {
    std::vector<Breakpoint> result;
    for (const auto& [key, breakpoint] : breakpoints_) {
        (void)key;
        result.push_back(breakpoint);
    }
    return result;
}

std::vector<Breakpoint> BreakpointManager::getBreakpointsForScript(int scriptId) const {
    std::vector<Breakpoint> result;
    for (const auto& [key, breakpoint] : breakpoints_) {
        (void)key;
        if (breakpoint.scriptId == scriptId) {
            result.push_back(breakpoint);
        }
    }
    return result;
}

std::set<int> BreakpointManager::getOffsetsForScript(int scriptId) const {
    std::set<int> offsets;
    for (const auto& [key, breakpoint] : breakpoints_) {
        (void)key;
        if (breakpoint.scriptId == scriptId) {
            offsets.insert(breakpoint.offset);
        }
    }
    return offsets;
}

void BreakpointManager::clearAll() {
    breakpoints_.clear();
}

std::map<int, std::set<int>> BreakpointManager::toOffsetMap() const {
    std::map<int, std::set<int>> result;
    for (const auto& [key, breakpoint] : breakpoints_) {
        (void)key;
        result[breakpoint.scriptId].insert(breakpoint.offset);
    }
    return result;
}

void BreakpointManager::setFromOffsetMap(const std::map<int, std::set<int>>& offsetMap) {
    clearAll();
    for (const auto& [scriptId, offsets] : offsetMap) {
        for (const int offset : offsets) {
            setBreakpoint(Breakpoint::simple(scriptId, "", offset));
        }
    }
}

std::string BreakpointManager::serialize() const {
    if (breakpoints_.empty()) {
        return "";
    }

    std::ostringstream out;
    out << "{\"version\":3,\"breakpoints\":[";
    bool first = true;
    for (const auto& [key, breakpoint] : breakpoints_) {
        (void)key;
        if (!first) {
            out << ",";
        }
        first = false;
        out << "{\"scriptId\":" << breakpoint.scriptId
            << ",\"handlerName\":\"" << escapeJson(breakpoint.handlerName)
            << "\",\"offset\":" << breakpoint.offset
            << ",\"enabled\":" << (breakpoint.enabled ? "true" : "false")
            << "}";
    }
    out << "]}";
    return out.str();
}

std::string BreakpointManager::escapeJson(const std::string& value) {
    std::string result;
    for (const char ch : value) {
        switch (ch) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += ch; break;
        }
    }
    return result;
}

void BreakpointManager::deserialize(std::string data) {
    clearAll();
    data = trim(std::move(data));
    if (data.empty()) {
        return;
    }
    if (data.front() == '{') {
        deserializeJson(data);
    } else {
        deserializeLegacy(data);
    }
}

void BreakpointManager::deserializeJson(const std::string& json) {
    const std::string marker = "\"breakpoints\":[";
    const auto breakpointsStart = json.find(marker);
    if (breakpointsStart == std::string::npos) {
        return;
    }
    const auto arrayStart = json.find('[', breakpointsStart);
    const auto arrayEnd = json.rfind(']');
    if (arrayStart == std::string::npos || arrayEnd == std::string::npos || arrayEnd <= arrayStart) {
        return;
    }

    const std::string arrayContent = json.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
    if (trim(arrayContent).empty()) {
        return;
    }

    int depth = 0;
    std::size_t objectStart = std::string::npos;
    for (std::size_t index = 0; index < arrayContent.size(); ++index) {
        const char ch = arrayContent[index];
        if (ch == '{') {
            if (depth == 0) {
                objectStart = index;
            }
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0 && objectStart != std::string::npos) {
                auto breakpoint = parseBreakpointJson(arrayContent.substr(objectStart, index - objectStart + 1));
                if (breakpoint.has_value()) {
                    setBreakpoint(*breakpoint);
                }
                objectStart = std::string::npos;
            }
        }
    }
}

std::optional<Breakpoint> BreakpointManager::parseBreakpointJson(const std::string& json) {
    const int scriptId = parseJsonInt(json, "scriptId", 0);
    const std::string handlerName = parseJsonString(json, "handlerName", "");
    const int offset = parseJsonInt(json, "offset", 0);
    const bool enabled = parseJsonBoolean(json, "enabled", true);
    return Breakpoint{scriptId, handlerName, offset, enabled};
}

int BreakpointManager::parseJsonInt(const std::string& json, const std::string& key, int defaultValue) {
    const std::string pattern = "\"" + key + "\":";
    const auto index = json.find(pattern);
    if (index == std::string::npos) {
        return defaultValue;
    }

    std::size_t start = index + pattern.size();
    std::size_t end = start;
    while (end < json.size() && (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '-')) {
        ++end;
    }
    if (end == start) {
        return defaultValue;
    }

    try {
        return std::stoi(json.substr(start, end - start));
    } catch (...) {
        return defaultValue;
    }
}

std::string BreakpointManager::parseJsonString(const std::string& json,
                                               const std::string& key,
                                               const std::string& defaultValue) {
    const std::string pattern = "\"" + key + "\":\"";
    const auto index = json.find(pattern);
    if (index == std::string::npos) {
        return defaultValue;
    }

    std::string result;
    bool escaped = false;
    for (std::size_t pos = index + pattern.size(); pos < json.size(); ++pos) {
        const char ch = json[pos];
        if (escaped) {
            switch (ch) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                default: result += ch; break;
            }
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else if (ch == '"') {
            return result;
        } else {
            result += ch;
        }
    }
    return defaultValue;
}

bool BreakpointManager::parseJsonBoolean(const std::string& json,
                                         const std::string& key,
                                         bool defaultValue) {
    const std::string pattern = "\"" + key + "\":";
    const auto index = json.find(pattern);
    if (index == std::string::npos) {
        return defaultValue;
    }
    const std::size_t start = index + pattern.size();
    if (json.compare(start, 4, "true") == 0) {
        return true;
    }
    if (json.compare(start, 5, "false") == 0) {
        return false;
    }
    return defaultValue;
}

void BreakpointManager::deserializeLegacy(const std::string& data) {
    try {
        for (const auto& script : split(data, ';')) {
            if (script.empty()) {
                continue;
            }
            const auto parts = split(script, ':');
            if (parts.size() != 2) {
                continue;
            }
            const int scriptId = std::stoi(parts[0]);
            for (const auto& offsetText : split(parts[1], ',')) {
                if (!offsetText.empty()) {
                    setBreakpoint(Breakpoint::simple(scriptId, "", std::stoi(offsetText)));
                }
            }
        }
    } catch (...) {
    }
}

std::string BreakpointManager::serializeLegacy() const {
    const auto offsetMap = toOffsetMap();
    std::ostringstream out;
    bool firstScript = true;
    for (const auto& [scriptId, offsets] : offsetMap) {
        if (offsets.empty()) {
            continue;
        }
        if (!firstScript) {
            out << ";";
        }
        firstScript = false;
        out << scriptId << ":";
        bool firstOffset = true;
        for (const int offset : offsets) {
            if (!firstOffset) {
                out << ",";
            }
            firstOffset = false;
            out << offset;
        }
    }
    return out.str();
}

} // namespace libreshockwave::player::debug
