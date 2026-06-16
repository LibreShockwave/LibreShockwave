#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "libreshockwave/bitmap/Bitmap.hpp"

namespace libreshockwave::bitmap {

class PngDecoder {
public:
    [[nodiscard]] static std::optional<Bitmap> decode(const std::vector<std::uint8_t>& data);
};

} // namespace libreshockwave::bitmap
