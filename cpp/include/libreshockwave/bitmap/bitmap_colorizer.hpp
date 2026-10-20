#pragma once

#include <cstdint>
#include <vector>

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/bitmap/color_ref.hpp"

namespace libreshockwave::bitmap {

class BitmapColorizer {
public:
    [[nodiscard]] static Bitmap colorize(const Bitmap& source,
                                         const ColorRef* fore_color,
                                         const ColorRef* back_color,
                                         const Palette* palette);
    [[nodiscard]] static Bitmap colorize(const Bitmap& source,
                                         const ColorRef& fore_color,
                                         const Palette* palette);
    [[nodiscard]] static Bitmap colorize(const Bitmap& source,
                                         std::uint32_t fore_color_rgb,
                                         std::uint32_t back_color_rgb);
    [[nodiscard]] static Bitmap colorizeWithPaletteIndices(const Bitmap& source,
                                                           int fore_color_index,
                                                           int back_color_index,
                                                           const Palette* palette);
    [[nodiscard]] static bool allowsColorization(int bit_depth, int ink_mode);
    [[nodiscard]] static bool usesBackColor(int bit_depth, int ink_mode);
    [[nodiscard]] static std::vector<std::uint32_t> colorizeIndexedData(const std::vector<std::uint8_t>& indexed_data,
                                                                        int bit_depth,
                                                                        const ColorRef* fore_color,
                                                                        const ColorRef* back_color,
                                                                        const Palette* palette);

private:
    [[nodiscard]] static std::uint32_t colorizeIndex(int index,
                                                     int max_index,
                                                     const ColorRef::Rgb* fore_color,
                                                     const ColorRef::Rgb* back_color);
};

} // namespace libreshockwave::bitmap
