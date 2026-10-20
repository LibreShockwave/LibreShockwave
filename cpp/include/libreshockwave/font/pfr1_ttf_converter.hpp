#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "libreshockwave/font/pfr1_font.hpp"

namespace libreshockwave::font {

class Pfr1TtfConverter {
public:
    [[nodiscard]] static std::vector<std::uint8_t> convert(const Pfr1Font& font,
                                                           const std::string& family_name);
};

} // namespace libreshockwave::font
