#pragma once

#include <string>
#include <string_view>

namespace libreshockwave::util {

[[nodiscard]] std::string truncate(std::string_view value, int maxLength);
[[nodiscard]] std::string escapeForDisplay(std::string_view value);
[[nodiscard]] std::string escapeHtml(std::string_view value);

} // namespace libreshockwave::util
