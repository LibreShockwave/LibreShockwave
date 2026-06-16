#pragma once

#include <cstdint>
#include <vector>

#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/bitmap/ColorRef.hpp"

namespace libreshockwave::bitmap {

class BitmapColorizer {
public:
    [[nodiscard]] static Bitmap colorize(const Bitmap& source,
                                         const ColorRef* foreColor,
                                         const ColorRef* backColor,
                                         const Palette* palette);
    [[nodiscard]] static Bitmap colorize(const Bitmap& source,
                                         const ColorRef& foreColor,
                                         const Palette* palette);
    [[nodiscard]] static Bitmap colorize(const Bitmap& source,
                                         std::uint32_t foreColorRgb,
                                         std::uint32_t backColorRgb);
    [[nodiscard]] static Bitmap colorizeWithPaletteIndices(const Bitmap& source,
                                                           int foreColorIndex,
                                                           int backColorIndex,
                                                           const Palette* palette);
    [[nodiscard]] static bool allowsColorization(int bitDepth, int inkMode);
    [[nodiscard]] static bool usesBackColor(int bitDepth, int inkMode);
    [[nodiscard]] static std::vector<std::uint32_t> colorizeIndexedData(const std::vector<std::uint8_t>& indexedData,
                                                                        int bitDepth,
                                                                        const ColorRef* foreColor,
                                                                        const ColorRef* backColor,
                                                                        const Palette* palette);

private:
    [[nodiscard]] static std::uint32_t colorizeIndex(int index,
                                                     int maxIndex,
                                                     const ColorRef::Rgb* foreColor,
                                                     const ColorRef::Rgb* backColor);
};

} // namespace libreshockwave::bitmap
