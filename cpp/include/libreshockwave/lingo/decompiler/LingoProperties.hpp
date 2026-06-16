#pragma once

#include <string_view>

#include "libreshockwave/lingo/Opcode.hpp"

namespace libreshockwave::lingo::decompiler {

[[nodiscard]] std::string_view binaryOpName(Opcode opcode);
[[nodiscard]] std::string_view chunkTypeName(int id);
[[nodiscard]] std::string_view putTypeName(int id);
[[nodiscard]] std::string_view moviePropertyName(int id);
[[nodiscard]] std::string_view whenEventName(int id);
[[nodiscard]] std::string_view menuPropertyName(int id);
[[nodiscard]] std::string_view menuItemPropertyName(int id);
[[nodiscard]] std::string_view soundPropertyName(int id);
[[nodiscard]] std::string_view spritePropertyName(int id);
[[nodiscard]] std::string_view animationPropertyName(int id);
[[nodiscard]] std::string_view animation2PropertyName(int id);
[[nodiscard]] std::string_view memberPropertyName(int id);

} // namespace libreshockwave::lingo::decompiler
