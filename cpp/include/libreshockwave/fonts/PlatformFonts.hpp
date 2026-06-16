#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace libreshockwave::fonts::platform {

[[nodiscard]] const std::vector<std::uint8_t>* macFontData(std::string_view dataKey);
[[nodiscard]] const std::vector<std::uint8_t>* windowsFontData(std::string_view dataKey);

} // namespace libreshockwave::fonts::platform
