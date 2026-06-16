#pragma once

#include <cstdint>

#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/bitmap/Palette.hpp"
#include "libreshockwave/id/Ids.hpp"

namespace libreshockwave::player::render::pipeline {

class InkProcessor {
public:
    [[nodiscard]] static bool shouldProcessInk(int ink);
    [[nodiscard]] static bool shouldProcessInk(id::InkMode ink);
    [[nodiscard]] static bool allowsColorize(int ink);
    [[nodiscard]] static bool allowsColorize(id::InkMode ink);

    [[nodiscard]] static bitmap::Bitmap applyInk(const bitmap::Bitmap& src,
                                                 int ink,
                                                 int backColor,
                                                 bool useAlpha,
                                                 const bitmap::Palette* palette);
    [[nodiscard]] static bitmap::Bitmap applyInk(const bitmap::Bitmap& src,
                                                 id::InkMode ink,
                                                 int backColor,
                                                 bool useAlpha,
                                                 const bitmap::Palette* palette);
    [[nodiscard]] static bitmap::Bitmap applyInkPreservingOutlinedWhiteBody(const bitmap::Bitmap& src,
                                                                            int ink,
                                                                            int backColor,
                                                                            bool useAlpha,
                                                                            const bitmap::Palette* palette);
    [[nodiscard]] static bitmap::Bitmap applyInkPreservingOutlinedWhiteBody(const bitmap::Bitmap& src,
                                                                            id::InkMode ink,
                                                                            int backColor,
                                                                            bool useAlpha,
                                                                            const bitmap::Palette* palette);
    [[nodiscard]] static int resolveBackColor(const bitmap::Bitmap& src,
                                              id::InkMode ink,
                                              int backColor,
                                              bool useAlpha,
                                              const bitmap::Palette* palette);
    [[nodiscard]] static int resolveBackColorIgnoringAlpha(int backColor, const bitmap::Palette* palette);

    [[nodiscard]] static bitmap::Bitmap applyForeColorRemap(const bitmap::Bitmap& src,
                                                            std::uint32_t foreColor,
                                                            std::uint32_t backColor);
    [[nodiscard]] static bitmap::Bitmap applyIndexedColorRemap(const bitmap::Bitmap& indexedSource,
                                                               const bitmap::Bitmap& maskedSource,
                                                               std::uint32_t foreColor,
                                                               std::uint32_t backColor);
    [[nodiscard]] static bitmap::Bitmap applyDarkenForeColorOffset(const bitmap::Bitmap& src,
                                                                   std::uint32_t foreColor);
    [[nodiscard]] static bitmap::Bitmap remapExactColor(const bitmap::Bitmap& src,
                                                        std::uint32_t fromRgb,
                                                        std::uint32_t toRgb);
    [[nodiscard]] static bitmap::Bitmap applyBackgroundTransparent(const bitmap::Bitmap& src,
                                                                   int bgColorRgb);
    [[nodiscard]] static bitmap::Bitmap applyMask(const bitmap::Bitmap& src);
    [[nodiscard]] static bitmap::Bitmap applyMatte(const bitmap::Bitmap& src,
                                                   int matteColorRgb,
                                                   int tolerance = 0);
    [[nodiscard]] static bitmap::Bitmap applyFloodFillTransparency(const bitmap::Bitmap& src);
    [[nodiscard]] static bitmap::Bitmap convertOpaqueWhiteToTransparent(const bitmap::Bitmap& src);
    [[nodiscard]] static bitmap::Bitmap multiplyColor(const bitmap::Bitmap& src, int tintRgb);
};

} // namespace libreshockwave::player::render::pipeline
