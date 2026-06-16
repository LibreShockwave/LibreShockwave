#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/font/BitmapFont.hpp"

namespace libreshockwave::font {

class BdfParser {
public:
    [[nodiscard]] static std::shared_ptr<BitmapFont> parse(std::string_view bdfText,
                                                           std::string fontName);
    [[nodiscard]] static std::shared_ptr<BitmapFont> parse(const std::vector<std::uint8_t>& bytes,
                                                           std::string fontName);
};

} // namespace libreshockwave::font
