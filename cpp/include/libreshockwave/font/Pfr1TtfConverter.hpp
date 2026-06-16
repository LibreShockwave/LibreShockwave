#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "libreshockwave/font/Pfr1Font.hpp"

namespace libreshockwave::font {

class Pfr1TtfConverter {
public:
    [[nodiscard]] static std::vector<std::uint8_t> convert(const Pfr1Font& font,
                                                           const std::string& familyName);
};

} // namespace libreshockwave::font
