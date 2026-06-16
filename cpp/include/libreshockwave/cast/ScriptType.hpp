#pragma once

#include <string_view>

namespace libreshockwave::cast {

enum class ScriptType {
    Invalid = 0,
    Score = 1,
    Movie = 3,
    Parent = 7,
    Unknown = 255
};

[[nodiscard]] int code(ScriptType type);
[[nodiscard]] std::string_view name(ScriptType type);
[[nodiscard]] ScriptType scriptTypeFromCode(int code);
[[nodiscard]] bool isBehavior(ScriptType type);
[[nodiscard]] bool isMovieScript(ScriptType type);
[[nodiscard]] bool isParentScript(ScriptType type);

} // namespace libreshockwave::cast
