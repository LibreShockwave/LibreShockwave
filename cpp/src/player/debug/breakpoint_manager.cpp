#include "libreshockwave/player/debug/breakpoint_manager.hpp"

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

std::optional<Breakpoint> BreakpointManager::getBreakpoint(int script_id,
                                                           const std::string& handler_name,
                                                           int offset) const {
    const auto it = breakpoints_.find(BreakpointKey::of(script_id, handler_name, offset));
    return it == breakpoints_.end() ? std::nullopt : std::optional<Breakpoint>(it->second);
}

bool BreakpointManager::hasBreakpoint(int script_id, const std::string& handler_name, int offset) const {
    return breakpoints_.contains(BreakpointKey::of(script_id, handler_name, offset));
}

void BreakpointManager::setBreakpoint(Breakpoint breakpoint) {
    breakpoints_[BreakpointKey::of(breakpoint)] = std::move(breakpoint);
}

Breakpoint BreakpointManager::addBreakpoint(int script_id, std::string handler_name, int offset) {
    Breakpoint breakpoint = Breakpoint::simple(script_id, std::move(handler_name), offset);
    setBreakpoint(breakpoint);
    return breakpoint;
}

std::optional<Breakpoint> BreakpointManager::removeBreakpoint(int script_id,
                                                              const std::string& handler_name,
                                                              int offset) {
    const auto key = BreakpointKey::of(script_id, handler_name, offset);
    const auto it = breakpoints_.find(key);
    if (it == breakpoints_.end()) {
        return std::nullopt;
    }
    Breakpoint removed = it->second;
    breakpoints_.erase(it);
    return removed;
}

std::optional<Breakpoint> BreakpointManager::toggleBreakpoint(int script_id,
                                                              std::string handler_name,
                                                              int offset) {
    const auto key = BreakpointKey::of(script_id, handler_name, offset);
    const auto it = breakpoints_.find(key);
    if (it != breakpoints_.end()) {
        breakpoints_.erase(it);
        return std::nullopt;
    }
    Breakpoint breakpoint = Breakpoint::simple(script_id, std::move(handler_name), offset);
    breakpoints_[BreakpointKey::of(breakpoint)] = breakpoint;
    return breakpoint;
}

std::optional<Breakpoint> BreakpointManager::toggleEnabled(int script_id,
                                                           const std::string& handler_name,
                                                           int offset) {
    const auto key = BreakpointKey::of(script_id, handler_name, offset);
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

std::vector<Breakpoint> BreakpointManager::getBreakpointsForScript(int script_id) const {
    std::vector<Breakpoint> result;
    for (const auto& [key, breakpoint] : breakpoints_) {
        (void)key;
        if (breakpoint.script_id == script_id) {
            result.push_back(breakpoint);
        }
    }
    return result;
}

std::set<int> BreakpointManager::getOffsetsForScript(int script_id) const {
    std::set<int> offsets;
    for (const auto& [key, breakpoint] : breakpoints_) {
        (void)key;
        if (breakpoint.script_id == script_id) {
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
        result[breakpoint.script_id].insert(breakpoint.offset);
    }
    return result;
}

void BreakpointManager::setFromOffsetMap(const std::map<int, std::set<int>>& offset_map) {
    clearAll();
    for (const auto& [script_id, offsets] : offset_map) {
        for (const int offset : offsets) {
            setBreakpoint(Breakpoint::simple(script_id, "", offset));
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
        out << "{\"scriptId\":" << breakpoint.script_id
            << ",\"handlerName\":\"" << escapeJson(breakpoint.handler_name)
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
    const auto breakpoints_start = json.find(marker);
    if (breakpoints_start == std::string::npos) {
        return;
    }
    const auto array_start = json.find('[', breakpoints_start);
    const auto array_end = json.rfind(']');
    if (array_start == std::string::npos || array_end == std::string::npos || array_end <= array_start) {
        return;
    }

    const std::string array_content = json.substr(array_start + 1, array_end - array_start - 1);
    if (trim(array_content).empty()) {
        return;
    }

    int depth = 0;
    std::size_t object_start = std::string::npos;
    for (std::size_t index = 0; index < array_content.size(); ++index) {
        const char ch = array_content[index];
        if (ch == '{') {
            if (depth == 0) {
                object_start = index;
            }
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0 && object_start != std::string::npos) {
                auto breakpoint = parseBreakpointJson(array_content.substr(object_start, index - object_start + 1));
                if (breakpoint.has_value()) {
                    setBreakpoint(*breakpoint);
                }
                object_start = std::string::npos;
            }
        }
    }
}

std::optional<Breakpoint> BreakpointManager::parseBreakpointJson(const std::string& json) {
    const int script_id = parseJsonInt(json, "scriptId", 0);
    const std::string handler_name = parseJsonString(json, "handlerName", "");
    const int offset = parseJsonInt(json, "offset", 0);
    const bool enabled = parseJsonBoolean(json, "enabled", true);
    return Breakpoint{script_id, handler_name, offset, enabled};
}

int BreakpointManager::parseJsonInt(const std::string& json, const std::string& key, int default_value) {
    const std::string pattern = "\"" + key + "\":";
    const auto index = json.find(pattern);
    if (index == std::string::npos) {
        return default_value;
    }

    std::size_t start = index + pattern.size();
    std::size_t end = start;
    while (end < json.size() && (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '-')) {
        ++end;
    }
    if (end == start) {
        return default_value;
    }

    try {
        return std::stoi(json.substr(start, end - start));
    } catch (...) {
        return default_value;
    }
}

std::string BreakpointManager::parseJsonString(const std::string& json,
                                               const std::string& key,
                                               const std::string& default_value) {
    const std::string pattern = "\"" + key + "\":\"";
    const auto index = json.find(pattern);
    if (index == std::string::npos) {
        return default_value;
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
    return default_value;
}

bool BreakpointManager::parseJsonBoolean(const std::string& json,
                                         const std::string& key,
                                         bool default_value) {
    const std::string pattern = "\"" + key + "\":";
    const auto index = json.find(pattern);
    if (index == std::string::npos) {
        return default_value;
    }
    const std::size_t start = index + pattern.size();
    if (json.compare(start, 4, "true") == 0) {
        return true;
    }
    if (json.compare(start, 5, "false") == 0) {
        return false;
    }
    return default_value;
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
            const int script_id = std::stoi(parts[0]);
            for (const auto& offset_text : split(parts[1], ',')) {
                if (!offset_text.empty()) {
                    setBreakpoint(Breakpoint::simple(script_id, "", std::stoi(offset_text)));
                }
            }
        }
    } catch (...) {
    }
}

std::string BreakpointManager::serializeLegacy() const {
    const auto offset_map = toOffsetMap();
    std::ostringstream out;
    bool first_script = true;
    for (const auto& [script_id, offsets] : offset_map) {
        if (offsets.empty()) {
            continue;
        }
        if (!first_script) {
            out << ";";
        }
        first_script = false;
        out << script_id << ":";
        bool first_offset = true;
        for (const int offset : offsets) {
            if (!first_offset) {
                out << ",";
            }
            first_offset = false;
            out << offset;
        }
    }
    return out.str();
}

} // namespace libreshockwave::player::debug
