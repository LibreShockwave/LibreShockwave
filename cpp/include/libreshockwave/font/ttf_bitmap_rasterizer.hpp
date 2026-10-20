#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/font/bitmap_font.hpp"

namespace libreshockwave::font {

class TtfBitmapRasterizer {
public:
    [[nodiscard]] static std::shared_ptr<BitmapFont> rasterize(const std::vector<std::uint8_t>& ttf_bytes,
                                                               int target_size,
                                                               const std::string& font_name);
};

} // namespace libreshockwave::font
