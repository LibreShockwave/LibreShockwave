#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/font/bitmap_font.hpp"

namespace libreshockwave::player::cast {

class WindowsFontBundle {
public:
    static void registerFontData(const std::string& font_name,
                                 std::vector<std::uint8_t> regular,
                                 std::vector<std::uint8_t> bold = {},
                                 std::vector<std::uint8_t> italic = {},
                                 std::vector<std::uint8_t> bold_italic = {});
    static void clearFontData();

    [[nodiscard]] static std::shared_ptr<font::BitmapFont> getFont(const std::string& font_name,
                                                                   int font_size,
                                                                   bool bold,
                                                                   bool italic);
    [[nodiscard]] static bool hasWindowsFont(const std::string& font_name);
    [[nodiscard]] static bool hasBoldVariant(const std::string& font_name);
};

} // namespace libreshockwave::player::cast
