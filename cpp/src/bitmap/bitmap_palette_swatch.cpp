// Palette swatch construction: the preview images the editor and debugger
// show for a palette. These Bitmap factories build a new image from palette
// colours only, so they live apart from Bitmap's own pixel and index state.
#include "libreshockwave/bitmap/bitmap.hpp"

#include <cmath>

namespace libreshockwave::bitmap {

Bitmap Bitmap::createPaletteSwatch(const std::vector<std::uint32_t>& colors, int swatch_size, int columns) {
    if (colors.empty()) {
        return Bitmap(1, 1, 32);
    }
    const int count = static_cast<int>(colors.size());
    const int cols = columns > 0 ? columns : static_cast<int>(std::ceil(std::sqrt(count)));
    const int rows = static_cast<int>(std::ceil(static_cast<double>(count) / cols));

    Bitmap bitmap(cols * swatch_size, rows * swatch_size, 32);
    bitmap.fill(0xFFFFFFFFU);
    for (int i = 0; i < count; ++i) {
        const int col = i % cols;
        const int row = i / cols;
        bitmap.fillRect(col * swatch_size, row * swatch_size, swatch_size, swatch_size,
                        0xFF000000U | (colors[static_cast<std::size_t>(i)] & 0xFFFFFFU));
    }
    return bitmap;
}

Bitmap Bitmap::createPaletteSwatch(const Palette& palette, int swatch_size) {
    return createPaletteSwatch(palette.colors(), swatch_size, 16);
}

} // namespace libreshockwave::bitmap
