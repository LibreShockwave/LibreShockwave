#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "libreshockwave/cast/xmed_styled_text.hpp"

namespace libreshockwave::cast {

class XmedTextParser {
public:
    [[nodiscard]] static bool isTextXtra(const std::vector<std::uint8_t>& specific_data);
    [[nodiscard]] static std::optional<XmedStyledText> parseStyled(
        const std::vector<std::uint8_t>& xmed_data,
        const std::vector<std::uint8_t>& specific_data);
};

} // namespace libreshockwave::cast
