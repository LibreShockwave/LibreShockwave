#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/font/BitmapFont.hpp"

namespace libreshockwave::font {

class TtfBitmapRasterizer {
public:
    [[nodiscard]] static std::shared_ptr<BitmapFont> rasterize(const std::vector<std::uint8_t>& ttfBytes,
                                                               int targetSize,
                                                               const std::string& fontName);
};

} // namespace libreshockwave::font
