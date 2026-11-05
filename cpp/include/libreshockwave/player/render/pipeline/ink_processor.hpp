#pragma once

#include <cstdint>

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/bitmap/palette.hpp"
#include "libreshockwave/id/ids.hpp"

namespace libreshockwave::player::render::pipeline {

class InkProcessor {
public:
    // True when the bake stage keys the source background before the stage
    // renderer applies the ink arithmetic. Copy is the only ink that keeps the
    // whole sprite rectangle, so a script-built canvas under Lightest, Add,
    // Matte, or any other processed ink would otherwise print its white
    // backing as an opaque box around the artwork.
    [[nodiscard]] static bool shouldProcessInk(int ink);
    [[nodiscard]] static bool shouldProcessInk(id::InkMode ink);
    [[nodiscard]] static bool allowsColorize(int ink);
    [[nodiscard]] static bool allowsColorize(id::InkMode ink);

    // Prepares a member or sprite image for the stage renderer. The bake stage
    // keys one background colour so the artwork, not the whole source rectangle,
    // reaches the stage. Only Background Transparent keys the sprite background
    // colour from back_color. The other processed inks key the background of the
    // source itself, because a key the source does not contain would leave the
    // rectangle opaque. The renderer applies the ink arithmetic after this step.
    // Returns a copy of src for inks that shouldProcessInk rejects.
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
    // Variant of applyInk for sources whose white pixels are artwork rather than
    // backing. It keeps a white body that an outline encloses, and it keys the
    // white backing that reaches the sprite edge. Use it for authored low-depth
    // art and for the script-built surfaces the rendering rules scope it to.
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
    // Resolves a sprite background colour to packed RGB. back_color below 256 is
    // a palette index in palette, above 255 it is already packed RGB, and a
    // source with native matte alpha returns -1 unless the ink keys by colour.
    // This is a colour lookup only. It does not decide which colour the bake
    // stage keys, because only Background Transparent keys this colour.
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
    // Keys every pixel whose RGB equals bg_color_rgb, plus the authored matte
    // index on indexed sources. Pixels that are already transparent stay as they
    // are. This is exact-match keying, not a flood fill, so a key the source does
    // not contain leaves the whole rectangle opaque.
    [[nodiscard]] static bitmap::Bitmap applyBackgroundTransparent(const bitmap::Bitmap& src,
                                                                   int bg_color_rgb);
    [[nodiscard]] static bitmap::Bitmap applyMask(const bitmap::Bitmap& src);
    [[nodiscard]] static bitmap::Bitmap applyMask(const bitmap::Bitmap& src,
                                                  const bitmap::Bitmap& mask);
    [[nodiscard]] static bitmap::Bitmap applyMatte(const bitmap::Bitmap& src,
                                                   int matte_color_rgb,
                                                   int tolerance = 0);
    [[nodiscard]] static bitmap::Bitmap applyFloodFillTransparency(const bitmap::Bitmap& src);
    // Clears the white backing that reaches the sprite edge and keeps interior
    // white pixels, which are authored art for DARKEN/LIGHTEN tinted sprites.
    // Keyed pixels become 0x00FFFFFF so their RGB stays available downstream.
    [[nodiscard]] static bitmap::Bitmap convertOpaqueWhiteToTransparent(const bitmap::Bitmap& src);
    [[nodiscard]] static bitmap::Bitmap multiplyColor(const bitmap::Bitmap& src, int tint_rgb);
};

} // namespace libreshockwave::player::render::pipeline
