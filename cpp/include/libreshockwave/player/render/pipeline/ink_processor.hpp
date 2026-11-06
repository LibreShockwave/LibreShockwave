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
    // `palette` is the colour table the source pixels were decoded with, and it
    // is what matte keying looks up. `movie_palette` is `the palette of movie`,
    // and it is what a sprite background colour in the 0..255 range is resolved
    // against, because Director stores such a colour as a palette number and
    // resolves it in the movie palette, never in the member's own table. Pass
    // nullptr to fall back to `palette`.
    [[nodiscard]] static bitmap::Bitmap applyInk(const bitmap::Bitmap& src,
                                                 int ink,
                                                 int back_color,
                                                 bool use_alpha,
                                                 const bitmap::Palette* palette,
                                                 const bitmap::Palette* movie_palette = nullptr);
    [[nodiscard]] static bitmap::Bitmap applyInk(const bitmap::Bitmap& src,
                                                 id::InkMode ink,
                                                 int back_color,
                                                 bool use_alpha,
                                                 const bitmap::Palette* palette,
                                                 const bitmap::Palette* movie_palette = nullptr);
    // Variant of applyInk for sources whose white pixels are artwork rather than
    // backing. It keeps a white body that an outline encloses, and it keys the
    // white backing that reaches the sprite edge. Use it for authored low-depth
    // art and for the script-built surfaces the rendering rules scope it to.
    [[nodiscard]] static bitmap::Bitmap applyInkPreservingOutlinedWhiteBody(const bitmap::Bitmap& src,
                                                                            int ink,
                                                                            int back_color,
                                                                            bool use_alpha,
                                                                            const bitmap::Palette* palette,
                                                                            const bitmap::Palette* movie_palette = nullptr);
    [[nodiscard]] static bitmap::Bitmap applyInkPreservingOutlinedWhiteBody(const bitmap::Bitmap& src,
                                                                            id::InkMode ink,
                                                                            int back_color,
                                                                            bool use_alpha,
                                                                            const bitmap::Palette* palette,
                                                                            const bitmap::Palette* movie_palette = nullptr);
    // Resolves a sprite background colour to the packed RGB of that colour's
    // transparency key. back_color at or below 255 is a palette number that
    // movie_palette resolves, matching `the palette of movie`; a null
    // movie_palette falls back to palette. Above 255 the value is already
    // packed RGB, and a source with native matte alpha returns -1 unless the
    // ink keys by colour.
    // A 32-bit source without alpha has white backing, so its key is white for
    // every ink but Copy. Use resolveBackColorIgnoringAlpha where the value is
    // a colour in its own right, such as the Darken ink colour filter.
    [[nodiscard]] static int resolveBackColor(const bitmap::Bitmap& src,
                                              id::InkMode ink,
                                              int back_color,
                                              bool use_alpha,
                                              const bitmap::Palette* palette,
                                              const bitmap::Palette* movie_palette = nullptr);
    [[nodiscard]] static int resolveBackColorIgnoringAlpha(int back_color,
                                                           const bitmap::Palette* palette,
                                                           const bitmap::Palette* movie_palette = nullptr);

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
