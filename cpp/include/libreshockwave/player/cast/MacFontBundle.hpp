#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/font/BitmapFont.hpp"

namespace libreshockwave::player::cast {

class MacFontBundle {
public:
    static void initialize();
    static void registerTtfData(const std::string& dataKey, std::vector<std::uint8_t> ttfBytes);
    static void clearTtfData();

    [[nodiscard]] static std::shared_ptr<font::BitmapFont> getFont(const std::string& fontName,
                                                                   int fontSize,
                                                                   bool bold = false,
                                                                   bool italic = false);
    [[nodiscard]] static bool hasMacFont(const std::string& fontName);
    [[nodiscard]] static bool hasBoldVariant(const std::string& fontName);
};

} // namespace libreshockwave::player::cast
