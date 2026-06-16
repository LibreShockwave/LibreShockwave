#pragma once

#include <cstdint>
#include <vector>

namespace libreshockwave::util {

[[nodiscard]] bool containsMp3SyncFrame(const std::vector<std::uint8_t>& data, int searchLimit);

} // namespace libreshockwave::util
