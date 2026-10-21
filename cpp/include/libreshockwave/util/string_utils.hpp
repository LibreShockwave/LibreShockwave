#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace libreshockwave::util {

[[nodiscard]] bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs);
[[nodiscard]] std::string truncate(std::string_view value, int max_length);
[[nodiscard]] std::string escapeForDisplay(std::string_view value);
[[nodiscard]] std::string escapeHtml(std::string_view value);

} // namespace libreshockwave::util
