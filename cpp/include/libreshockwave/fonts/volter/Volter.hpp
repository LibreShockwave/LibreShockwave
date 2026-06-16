#pragma once

#include <cstdint>
#include <vector>

namespace libreshockwave::fonts::volter {

[[nodiscard]] const std::vector<std::uint8_t>& regularData();
[[nodiscard]] const std::vector<std::uint8_t>& boldData();

} // namespace libreshockwave::fonts::volter
