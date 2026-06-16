#include "libreshockwave/cast/ScriptType.hpp"

namespace libreshockwave::cast {

int code(ScriptType type) {
    return static_cast<int>(type);
}

std::string_view name(ScriptType type) {
    switch (type) {
        case ScriptType::Invalid: return "invalid";
        case ScriptType::Score: return "score";
        case ScriptType::Movie: return "movie";
        case ScriptType::Parent: return "parent";
        case ScriptType::Unknown: return "unknown";
    }
    return "unknown";
}

ScriptType scriptTypeFromCode(int value) {
    switch (value) {
        case 0: return ScriptType::Invalid;
        case 1: return ScriptType::Score;
        case 3: return ScriptType::Movie;
        case 7: return ScriptType::Parent;
        default: return ScriptType::Unknown;
    }
}

bool isBehavior(ScriptType type) { return type == ScriptType::Score; }
bool isMovieScript(ScriptType type) { return type == ScriptType::Movie; }
bool isParentScript(ScriptType type) { return type == ScriptType::Parent; }

} // namespace libreshockwave::cast
