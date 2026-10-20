#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/font/bitmap_font.hpp"

namespace libreshockwave::font {

class BdfParser {
public:
    [[nodiscard]] static std::shared_ptr<BitmapFont> parse(std::string_view bdf_text,
                                                           std::string font_name);
    [[nodiscard]] static std::shared_ptr<BitmapFont> parse(const std::vector<std::uint8_t>& bytes,
                                                           std::string font_name);
};

} // namespace libreshockwave::font
