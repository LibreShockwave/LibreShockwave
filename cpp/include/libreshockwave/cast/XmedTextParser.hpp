#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "libreshockwave/cast/XmedStyledText.hpp"

namespace libreshockwave::cast {

class XmedTextParser {
public:
    [[nodiscard]] static bool isTextXtra(const std::vector<std::uint8_t>& specificData);
    [[nodiscard]] static std::optional<XmedStyledText> parseStyled(
        const std::vector<std::uint8_t>& xmedData,
        const std::vector<std::uint8_t>& specificData);
};

} // namespace libreshockwave::cast
