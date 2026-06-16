#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/font/BitmapFont.hpp"

namespace libreshockwave::player::cast {

class WindowsFontBundle {
public:
    static void registerFontData(const std::string& fontName,
                                 std::vector<std::uint8_t> regular,
                                 std::vector<std::uint8_t> bold = {},
                                 std::vector<std::uint8_t> italic = {},
                                 std::vector<std::uint8_t> boldItalic = {});
    static void clearFontData();

    [[nodiscard]] static std::shared_ptr<font::BitmapFont> getFont(const std::string& fontName,
                                                                   int fontSize,
                                                                   bool bold,
                                                                   bool italic);
    [[nodiscard]] static bool hasWindowsFont(const std::string& fontName);
    [[nodiscard]] static bool hasBoldVariant(const std::string& fontName);
};

} // namespace libreshockwave::player::cast
