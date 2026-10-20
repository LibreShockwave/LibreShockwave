#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/font/bitmap_font.hpp"

namespace libreshockwave::player::cast {

class MacFontBundle {
public:
    static void initialize();
    static void registerTtfData(const std::string& data_key, std::vector<std::uint8_t> ttf_bytes);
    static void clearTtfData();

    [[nodiscard]] static std::shared_ptr<font::BitmapFont> getFont(const std::string& font_name,
                                                                   int font_size,
                                                                   bool bold = false,
                                                                   bool italic = false);
    [[nodiscard]] static bool hasMacFont(const std::string& font_name);
    [[nodiscard]] static bool hasBoldVariant(const std::string& font_name);
};

} // namespace libreshockwave::player::cast
