#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "libreshockwave/bitmap/bitmap.hpp"

namespace libreshockwave::apps {

/// Decodes a baseline JPEG image into a 32-bit opaque bitmap. Returns nullopt
/// when libjpeg is unavailable or the image cannot be decoded.
[[nodiscard]] std::optional<bitmap::Bitmap> decodeJpeg(const std::vector<std::uint8_t>& data);

} // namespace libreshockwave::apps
