#pragma once

#include <cstdint>

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/bitmap/palette.hpp"
#include "libreshockwave/id/ids.hpp"

namespace libreshockwave::player::render::pipeline {

class InkProcessor {
public:
    [[nodiscard]] static bool shouldProcessInk(int ink);
    [[nodiscard]] static bool shouldProcessInk(id::InkMode ink);
    [[nodiscard]] static bool allowsColorize(int ink);
    [[nodiscard]] static bool allowsColorize(id::InkMode ink);

    [[nodiscard]] static bitmap::Bitmap applyInk(const bitmap::Bitmap& src,
                                                 int ink,
                                                 int back_color,
                                                 bool use_alpha,
                                                 const bitmap::Palette* palette);
    [[nodiscard]] static bitmap::Bitmap applyInk(const bitmap::Bitmap& src,
                                                 id::InkMode ink,
                                                 int back_color,
                                                 bool use_alpha,
                                                 const bitmap::Palette* palette);
    [[nodiscard]] static bitmap::Bitmap applyInkPreservingOutlinedWhiteBody(const bitmap::Bitmap& src,
                                                                            int ink,
                                                                            int back_color,
                                                                            bool use_alpha,
                                                                            const bitmap::Palette* palette);
    [[nodiscard]] static bitmap::Bitmap applyInkPreservingOutlinedWhiteBody(const bitmap::Bitmap& src,
                                                                            id::InkMode ink,
                                                                            int back_color,
                                                                            bool use_alpha,
                                                                            const bitmap::Palette* palette);
    [[nodiscard]] static int resolveBackColor(const bitmap::Bitmap& src,
                                              id::InkMode ink,
                                              int back_color,
                                              bool use_alpha,
                                              const bitmap::Palette* palette);
    [[nodiscard]] static int resolveBackColorIgnoringAlpha(int back_color, const bitmap::Palette* palette);

    [[nodiscard]] static bitmap::Bitmap applyForeColorRemap(const bitmap::Bitmap& src,
                                                            std::uint32_t fore_color,
                                                            std::uint32_t back_color);
    [[nodiscard]] static bitmap::Bitmap applyIndexedColorRemap(const bitmap::Bitmap& indexed_source,
                                                               const bitmap::Bitmap& masked_source,
                                                               std::uint32_t fore_color,
                                                               std::uint32_t back_color);
    [[nodiscard]] static bitmap::Bitmap applyDarkenForeColorOffset(const bitmap::Bitmap& src,
                                                                   std::uint32_t fore_color);
    [[nodiscard]] static bitmap::Bitmap remapExactColor(const bitmap::Bitmap& src,
                                                        std::uint32_t from_rgb,
                                                        std::uint32_t to_rgb);
    [[nodiscard]] static bitmap::Bitmap applyBackgroundTransparent(const bitmap::Bitmap& src,
                                                                   int bg_color_rgb);
    [[nodiscard]] static bitmap::Bitmap applyMask(const bitmap::Bitmap& src);
    [[nodiscard]] static bitmap::Bitmap applyMask(const bitmap::Bitmap& src,
                                                  const bitmap::Bitmap& mask);
    [[nodiscard]] static bitmap::Bitmap applyMatte(const bitmap::Bitmap& src,
                                                   int matte_color_rgb,
                                                   int tolerance = 0);
    [[nodiscard]] static bitmap::Bitmap applyFloodFillTransparency(const bitmap::Bitmap& src);
    [[nodiscard]] static bitmap::Bitmap convertOpaqueWhiteToTransparent(const bitmap::Bitmap& src);
    [[nodiscard]] static bitmap::Bitmap multiplyColor(const bitmap::Bitmap& src, int tint_rgb);
};

} // namespace libreshockwave::player::render::pipeline
