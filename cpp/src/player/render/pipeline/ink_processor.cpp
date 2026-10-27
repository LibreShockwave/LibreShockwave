#include "libreshockwave/player/render/pipeline/ink_processor.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <queue>
#include <vector>

#include "../../../bitmap/bitmap_processing.hpp"

namespace libreshockwave::player::render::pipeline {
namespace {

using bitmap::detail::derivedBitmap;
using bitmap::detail::interpolateChannel;
using bitmap::detail::isNearWhiteGrayscale;
using bitmap::detail::isOpaqueColor;
using bitmap::detail::isUniformPaletteIndex;
using bitmap::detail::maskAlphaFromPixel;
using bitmap::detail::matchesRgb;
using bitmap::detail::packArgb;

bool hasOpaqueBorderColor(const bitmap::Bitmap& src, int color_rgb) {
    const int width = src.width();
    const int height = src.height();
    if (width <= 0 || height <= 0) {
        return false;
    }

    const auto& pixels = src.pixels();
    for (const int index : bitmap::detail::edgeIndices(width, height)) {
        if (static_cast<std::size_t>(index) < pixels.size() && isOpaqueColor(pixels[index], color_rgb)) {
            return true;
        }
    }
    return false;
}

bool shouldKeyDefaultIndexedMatte(const bitmap::Bitmap& src,
                                  int background_key_rgb,
                                  const std::vector<std::uint8_t>* palette_indices) {
    if (src.bitDepth() > 8 || (background_key_rgb & 0x00FFFFFF) != 0xFFFFFF ||
        palette_indices == nullptr || palette_indices->size() < src.pixels().size()) {
        return false;
    }
    if (isUniformPaletteIndex(*palette_indices, 0)) {
        return false;
    }
    if (!bitmap::detail::edgeContainsOpaquePaletteIndex(src.pixels(), *palette_indices, src.width(), src.height(), 0)) {
        return false;
    }

    const int index_zero_rgb = bitmap::detail::resolvePaletteIndexRgb(src.pixels(), *palette_indices, 0);
    return isNearWhiteGrayscale(index_zero_rgb, 232, 16) &&
           bitmap::detail::hasOpaqueNonPaletteIndexContent(src.pixels(), *palette_indices, 0);
}

int multiplyDirectorChannel(int src, int tint) {
    if (tint >= 255) {
        return src;
    }
    if (src >= 255) {
        return tint;
    }
    return (src * tint) >> 8;
}

bool cornerContainsOpaqueRgb(const bitmap::Bitmap& src, int rgb) {
    const int width = src.width();
    const int height = src.height();
    if (width <= 0 || height <= 0) {
        return false;
    }
    const auto& pixels = src.pixels();
    for (const int index : bitmap::detail::cornerIndices(width, height)) {
        if (static_cast<std::size_t>(index) < pixels.size() && isOpaqueColor(pixels[index], rgb & 0x00FFFFFF)) {
            return true;
        }
    }
    return false;
}

bool isDefaultIndexedMatteRgb(int rgb) {
    return (rgb & 0x00FFFFFF) == 0x000000 || (rgb & 0x00FFFFFF) == 0xFFFFFF;
}

bool indexMatchesRgb(const bitmap::Bitmap& src,
                     const std::vector<std::uint8_t>& palette_indices,
                     const bitmap::Palette* palette,
                     int palette_index,
                     int rgb) {
    const int index_rgb = palette != nullptr && palette_index >= 0 && palette_index < palette->size()
        ? static_cast<int>(palette->getColor(palette_index) & 0x00FFFFFFU)
        : bitmap::detail::resolvePaletteIndexRgb(src.pixels(), palette_indices, palette_index);
    return index_rgb == (rgb & 0x00FFFFFF);
}

std::optional<int> explicitIndexedMatteIndex(const bitmap::Bitmap& src,
                                             const std::vector<std::uint8_t>& palette_indices,
                                             int matte_rgb,
                                             const bitmap::Palette* palette) {
    if (indexMatchesRgb(src, palette_indices, palette, 0, matte_rgb) &&
        bitmap::detail::edgeContainsOpaquePaletteIndex(src.pixels(), palette_indices, src.width(), src.height(), 0)) {
        return 0;
    }

    std::vector<int> counts(256, 0);
    int best_index = -1;
    int best_count = 0;
    const int width = src.width();
    const int height = src.height();
    const auto count_edge_index = [&](int x, int y) {
        const auto index = static_cast<std::size_t>(y * width + x);
        if (index >= src.pixels().size() || index >= palette_indices.size() ||
            ((src.pixels()[index] >> 24) & 0xFFU) == 0) {
            return;
        }
        const int palette_index = static_cast<int>(palette_indices[index] & 0xFFU);
        if (!indexMatchesRgb(src, palette_indices, palette, palette_index, matte_rgb)) {
            return;
        }
        const int count = ++counts[static_cast<std::size_t>(palette_index)];
        if (count > best_count) {
            best_count = count;
            best_index = palette_index;
        }
    };

    for (int x = 0; x < width; ++x) {
        count_edge_index(x, 0);
        if (height > 1) {
            count_edge_index(x, height - 1);
        }
    }
    for (int y = 1; y < height - 1; ++y) {
        count_edge_index(0, y);
        if (width > 1) {
            count_edge_index(width - 1, y);
        }
    }
    return best_index >= 0 ? std::optional<int>(best_index) : std::nullopt;
}

std::optional<int> resolveExplicitIndexedMatteIndex(const bitmap::Bitmap& src,
                                                    int back_color,
                                                    const bitmap::Palette* palette) {
    if (src.bitDepth() > 8) {
        return std::nullopt;
    }
    const auto palette_indices = src.paletteIndices();
    if (!palette_indices.has_value() || palette_indices->size() < src.pixels().size()) {
        return std::nullopt;
    }

    const auto image_palette = src.imagePalette();
    const auto* effective_palette = palette != nullptr ? palette : image_palette.get();
    const int matte_rgb = InkProcessor::resolveBackColorIgnoringAlpha(back_color, effective_palette) & 0x00FFFFFF;
    if (matte_rgb != 0xFFFFFF) {
        return std::nullopt;
    }

    const auto matte_index = explicitIndexedMatteIndex(src, *palette_indices, matte_rgb, effective_palette);
    if (!matte_index.has_value() || isUniformPaletteIndex(*palette_indices, *matte_index)) {
        return std::nullopt;
    }
    if (!bitmap::detail::hasOpaqueNonPaletteIndexContent(src.pixels(), *palette_indices, *matte_index)) {
        return std::nullopt;
    }
    return matte_index;
}

std::optional<int> resolveDefaultIndexedFloodFillMatteIndex(const bitmap::Bitmap& src,
                                                            const std::vector<std::uint8_t>& palette_indices) {
    if (palette_indices.size() < src.pixels().size()) {
        return std::nullopt;
    }
    const int index_zero_rgb = bitmap::detail::resolvePaletteIndexRgb(src.pixels(), palette_indices, 0);
    if (!isDefaultIndexedMatteRgb(index_zero_rgb)) {
        return std::nullopt;
    }
    if (bitmap::detail::edgeContainsOpaquePaletteIndex(src.pixels(), palette_indices, src.width(), src.height(), 0) ||
        bitmap::detail::cornerContainsPaletteIndex(palette_indices, src.width(), src.height(), 0)) {
        return 0;
    }
    return std::nullopt;
}

bool shouldPreferDefaultWhiteIndexedMatte(const bitmap::Bitmap& src,
                                          const std::vector<std::uint8_t>& palette_indices,
                                          int default_matte_index,
                                          std::optional<int> explicit_matte_index) {
    if (src.isScriptModified() || !explicit_matte_index.has_value() || *explicit_matte_index == default_matte_index) {
        return false;
    }
    if (default_matte_index != 0 || palette_indices.size() < src.pixels().size()) {
        return false;
    }
    return (bitmap::detail::resolvePaletteIndexRgb(src.pixels(), palette_indices, default_matte_index) & 0x00FFFFFF) ==
           0xFFFFFF;
}

bitmap::Bitmap applyIndexedMatteInternal(const bitmap::Bitmap& src, int matte_index, bool clear_transparent_rgb) {
    const int width = src.width();
    const int height = src.height();
    const auto palette_indices = src.paletteIndices();
    if (width <= 0 || height <= 0 || !palette_indices.has_value() ||
        palette_indices->size() < src.pixels().size()) {
        return src.copy();
    }

    const auto& pixels = src.pixels();
    const auto transparent = bitmap::detail::computeEdgeConnectedMask(width, height, [&](int index) {
        const auto offset = static_cast<std::size_t>(index);
        const auto pixel = pixels[offset];
        return ((pixel >> 24) & 0xFFU) == 0 ||
               static_cast<int>((*palette_indices)[offset] & 0xFFU) == matte_index;
    });

    std::vector<std::uint32_t> result = pixels;
    for (std::size_t index = 0; index < result.size() && index < transparent.size(); ++index) {
        if (transparent[index]) {
            result[index] = clear_transparent_rgb ? 0x00000000U : (result[index] & 0x00FFFFFFU);
        }
    }
    return derivedBitmap(src, std::move(result));
}

bool isOpaqueNonWhite(std::uint32_t pixel) {
    return ((pixel >> 24) & 0xFFU) != 0 && (pixel & 0x00FFFFFFU) != 0x00FFFFFFU;
}

bool outlineTouchesMultipleEdges(const bitmap::Bitmap& src) {
    const int width = src.width();
    const int height = src.height();
    if (width <= 0 || height <= 0) {
        return false;
    }

    bool top = false;
    bool bottom = false;
    bool left = false;
    bool right = false;
    for (int x = 0; x < width; ++x) {
        top = top || isOpaqueNonWhite(src.getPixel(x, 0));
        bottom = bottom || isOpaqueNonWhite(src.getPixel(x, height - 1));
    }
    for (int y = 0; y < height; ++y) {
        left = left || isOpaqueNonWhite(src.getPixel(0, y));
        right = right || isOpaqueNonWhite(src.getPixel(width - 1, y));
    }

    const int touched_edges = (top ? 1 : 0) + (bottom ? 1 : 0) + (left ? 1 : 0) + (right ? 1 : 0);
    return touched_edges >= 3 ||
           (width == 1 && top && bottom) ||
           (height == 1 && left && right);
}

void seedOutlinedBackground(const std::vector<std::uint32_t>& pixels,
                            const std::vector<bool>& dilated_barrier,
                            std::vector<bool>& outside,
                            std::queue<int>& queue,
                            int x,
                            int y,
                            int width) {
    const int index = y * width + x;
    const auto size_index = static_cast<std::size_t>(index);
    if (outside[size_index] || dilated_barrier[size_index] ||
        ((pixels[size_index] >> 24) & 0xFFU) == 0) {
        return;
    }
    outside[size_index] = true;
    queue.push(index);
}

bitmap::Bitmap applyOutlinedWhiteBodyMatte(const bitmap::Bitmap& src) {
    const int width = src.width();
    const int height = src.height();
    const auto& pixels = src.pixels();
    std::vector<bool> barrier(pixels.size(), false);
    std::vector<bool> dilated(pixels.size(), false);
    std::vector<bool> outside(pixels.size(), false);
    std::queue<int> queue;

    for (std::size_t index = 0; index < pixels.size(); ++index) {
        barrier[index] = isOpaqueNonWhite(pixels[index]);
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto index = static_cast<std::size_t>(y * width + x);
            if (!barrier[index]) {
                continue;
            }
            for (int ny = std::max(0, y - 1); ny <= std::min(height - 1, y + 1); ++ny) {
                for (int nx = std::max(0, x - 1); nx <= std::min(width - 1, x + 1); ++nx) {
                    dilated[static_cast<std::size_t>(ny * width + nx)] = true;
                }
            }
        }
    }

    for (int x = 0; x < width; ++x) {
        seedOutlinedBackground(pixels, dilated, outside, queue, x, 0, width);
        seedOutlinedBackground(pixels, dilated, outside, queue, x, height - 1, width);
    }
    for (int y = 1; y < height - 1; ++y) {
        seedOutlinedBackground(pixels, dilated, outside, queue, 0, y, width);
        seedOutlinedBackground(pixels, dilated, outside, queue, width - 1, y, width);
    }

    while (!queue.empty()) {
        const int index = queue.front();
        queue.pop();
        const int x = index % width;
        const int y = index / width;
        if (x > 0) seedOutlinedBackground(pixels, dilated, outside, queue, x - 1, y, width);
        if (x < width - 1) seedOutlinedBackground(pixels, dilated, outside, queue, x + 1, y, width);
        if (y > 0) seedOutlinedBackground(pixels, dilated, outside, queue, x, y - 1, width);
        if (y < height - 1) seedOutlinedBackground(pixels, dilated, outside, queue, x, y + 1, width);
    }

    std::vector<std::uint32_t> result = pixels;
    for (std::size_t index = 0; index < result.size(); ++index) {
        if (!barrier[index] && outside[index]) {
            result[index] = 0x00000000U;
        }
    }
    return derivedBitmap(src, std::move(result));
}

std::optional<bitmap::Bitmap> applyOutlinedWhiteBodyMatteIfNeeded(const bitmap::Bitmap& src,
                                                                  int matte_color_rgb,
                                                                  int tolerance,
                                                                  bool allow_script_built32_bit,
                                                                  std::optional<int> matte_palette_index) {
    if ((matte_color_rgb & 0x00FFFFFF) != 0xFFFFFF) {
        return std::nullopt;
    }

    const int bit_depth = src.bitDepth();
    const bool low_depth_asset = bit_depth >= 1 && bit_depth <= 8 && !src.isScriptModified();
    const bool script_built32_bit = allow_script_built32_bit &&
                                  bit_depth == 32 &&
                                  src.isScriptModified() &&
                                  !src.isNativeAlpha();
    if (!low_depth_asset && !script_built32_bit) {
        return std::nullopt;
    }

    int white = 0;
    int dark = 0;
    int gray = 0;
    int colored = 0;
    for (const auto pixel : src.pixels()) {
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        const int rgb = static_cast<int>(pixel & 0x00FFFFFFU);
        const int r = (rgb >> 16) & 0xFF;
        const int g = (rgb >> 8) & 0xFF;
        const int b = rgb & 0xFF;
        if (std::abs(r - g) > 4 || std::abs(g - b) > 4) {
            if (!script_built32_bit) {
                return std::nullopt;
            }
            ++colored;
            continue;
        }
        if (rgb == 0xFFFFFF) {
            ++white;
        } else if (rgb <= 0x303030) {
            ++dark;
        } else {
            ++gray;
        }
    }

    const int non_white = dark + gray + colored;
    if (white == 0 || non_white == 0 || !outlineTouchesMultipleEdges(src)) {
        return std::nullopt;
    }
    if (script_built32_bit && white < non_white) {
        return std::nullopt;
    }

    const auto plain = matte_palette_index.has_value()
        ? applyIndexedMatteInternal(src, *matte_palette_index, true)
        : InkProcessor::applyMatte(src, matte_color_rgb, tolerance);
    int remaining_white = 0;
    for (const auto pixel : plain.pixels()) {
        if (((pixel >> 24) & 0xFFU) != 0 && (pixel & 0x00FFFFFFU) == 0xFFFFFFU) {
            ++remaining_white;
        }
    }
    if ((low_depth_asset && remaining_white == 0) || remaining_white * 20 > white) {
        return std::nullopt;
    }

    return applyOutlinedWhiteBodyMatte(src);
}

int resolveMatteColorForInk(const bitmap::Bitmap& src,
                            id::InkMode ink,
                            int back_color,
                            bool use_alpha,
                            const bitmap::Palette* palette,
                            bool preserve_script_outlined_white_body) {
    if (src.hasNativeMatteAlpha() && use_alpha) {
        return -1;
    }
    if (preserve_script_outlined_white_body &&
        ink == id::InkMode::MATTE &&
        src.bitDepth() == 32 &&
        src.isScriptModified() &&
        !src.isNativeAlpha() &&
        hasOpaqueBorderColor(src, 0xFFFFFF)) {
        return 0xFFFFFF;
    }
    if (src.bitDepth() == 32 && !src.isScriptModified()) {
        return 0xFFFFFF;
    }
    if ((ink == id::InkMode::DARKEN || ink == id::InkMode::LIGHTEN) && src.isRectangularMedia()) {
        return -1;
    }
    if (!src.paletteIndices().has_value() && cornerContainsOpaqueRgb(src, 0xFFFFFF)) {
        return 0xFFFFFF;
    }
    return InkProcessor::resolveBackColor(src, ink, back_color, use_alpha, palette);
}

bitmap::Bitmap applyInkInternal(const bitmap::Bitmap& src,
                                id::InkMode ink,
                                int back_color,
                                bool use_alpha,
                                const bitmap::Palette* palette,
                                bool preserve_script_outlined_white_body) {
    if (src.width() == 0 || src.height() == 0 || !InkProcessor::shouldProcessInk(ink)) {
        return src.copy();
    }

    std::optional<bitmap::Bitmap> isolated;
    const bitmap::Bitmap* working = &src;
    if ((ink == id::InkMode::ADD_PIN || ink == id::InkMode::ADD) && src.paletteIndices().has_value()) {
        isolated = InkProcessor::applyFloodFillTransparency(src);
        working = &*isolated;
    }
    const auto& input = *working;

    if (ink == id::InkMode::MATTE) {
        const auto explicit_matte_index = resolveExplicitIndexedMatteIndex(input, back_color, palette);
        const auto palette_indices = input.paletteIndices();
        const auto default_matte_index =
            explicit_matte_index.has_value() || !palette_indices.has_value()
                ? std::optional<int>{}
                : resolveDefaultIndexedFloodFillMatteIndex(input, *palette_indices);
        const auto fallback_matte_index = explicit_matte_index.has_value() && palette_indices.has_value()
            ? resolveDefaultIndexedFloodFillMatteIndex(input, *palette_indices)
            : std::optional<int>{};
        const auto matte_palette_index =
            fallback_matte_index.has_value() &&
                    palette_indices.has_value() &&
                    shouldPreferDefaultWhiteIndexedMatte(input, *palette_indices, *fallback_matte_index, explicit_matte_index)
                ? fallback_matte_index
                : (explicit_matte_index.has_value() ? explicit_matte_index : default_matte_index);
        const int matte_color = matte_palette_index.has_value() && palette_indices.has_value()
            ? bitmap::detail::resolvePaletteIndexRgb(input.pixels(), *palette_indices, *matte_palette_index)
            : resolveMatteColorForInk(input, ink, back_color, use_alpha, palette, preserve_script_outlined_white_body);
        if (matte_color < 0) {
            return input.copy();
        }
        auto outlined = applyOutlinedWhiteBodyMatteIfNeeded(
            input, matte_color, 0, preserve_script_outlined_white_body, matte_palette_index);
        if (outlined.has_value()) {
            return std::move(*outlined);
        }
        if (matte_palette_index.has_value()) {
            return applyIndexedMatteInternal(input, *matte_palette_index, true);
        }
        return InkProcessor::applyMatte(input, matte_color);
    }

    if (ink == id::InkMode::MASK) {
        return InkProcessor::applyMask(input);
    }

    if (ink == id::InkMode::DARKEN || ink == id::InkMode::LIGHTEN) {
        bitmap::Bitmap masked = input.copy();
        if (!(input.bitDepth() == 32 && !use_alpha) && !input.isRectangularMedia()) {
            const int matte_color = InkProcessor::resolveBackColor(input, ink, back_color, use_alpha, palette);
            if (matte_color >= 0) {
                masked = input.bitDepth() >= 16
                    ? InkProcessor::applyBackgroundTransparent(input, matte_color)
                    : InkProcessor::applyMatte(input, matte_color);
            }
        }

        if (ink == id::InkMode::DARKEN) {
            const int tint_rgb = InkProcessor::resolveBackColor(input, ink, back_color, false, palette);
            if (tint_rgb >= 0 && tint_rgb != 0xFFFFFF) {
                masked = InkProcessor::multiplyColor(masked, tint_rgb);
            }
        }
        return masked;
    }

    const int bg_color = InkProcessor::resolveBackColor(input, ink, back_color, use_alpha, palette);
    if (bg_color < 0) {
        return input.copy();
    }
    return InkProcessor::applyBackgroundTransparent(input, bg_color);
}

} // namespace

bool InkProcessor::shouldProcessInk(int ink) {
    return shouldProcessInk(id::inkModeFromCode(ink));
}

bool InkProcessor::shouldProcessInk(id::InkMode ink) {
    return ink == id::InkMode::TRANSPARENT ||
           ink == id::InkMode::REVERSE ||
           ink == id::InkMode::GHOST ||
           ink == id::InkMode::NOT_COPY ||
           ink == id::InkMode::NOT_TRANSPARENT ||
           ink == id::InkMode::NOT_REVERSE ||
           ink == id::InkMode::NOT_GHOST ||
           ink == id::InkMode::MATTE ||
           ink == id::InkMode::MASK ||
           ink == id::InkMode::ADD_PIN ||
           ink == id::InkMode::ADD ||
           ink == id::InkMode::SUBTRACT_PIN ||
           ink == id::InkMode::SUBTRACT ||
           ink == id::InkMode::BACKGROUND_TRANSPARENT ||
           ink == id::InkMode::BLEND ||
           ink == id::InkMode::LIGHTEN ||
           ink == id::InkMode::DARKEN;
}

bool InkProcessor::allowsColorize(int ink) {
    return allowsColorize(id::inkModeFromCode(ink));
}

bool InkProcessor::allowsColorize(id::InkMode ink) {
    return ink == id::InkMode::COPY;
}

bitmap::Bitmap InkProcessor::applyInk(const bitmap::Bitmap& src,
                                      int ink,
                                      int back_color,
                                      bool use_alpha,
                                      const bitmap::Palette* palette) {
    return applyInkInternal(src, id::inkModeFromCode(ink), back_color, use_alpha, palette, false);
}

bitmap::Bitmap InkProcessor::applyInk(const bitmap::Bitmap& src,
                                      id::InkMode ink,
                                      int back_color,
                                      bool use_alpha,
                                      const bitmap::Palette* palette) {
    return applyInkInternal(src, ink, back_color, use_alpha, palette, false);
}

bitmap::Bitmap InkProcessor::applyInkPreservingOutlinedWhiteBody(const bitmap::Bitmap& src,
                                                                 int ink,
                                                                 int back_color,
                                                                 bool use_alpha,
                                                                 const bitmap::Palette* palette) {
    return applyInkInternal(src, id::inkModeFromCode(ink), back_color, use_alpha, palette, true);
}

bitmap::Bitmap InkProcessor::applyInkPreservingOutlinedWhiteBody(const bitmap::Bitmap& src,
                                                                 id::InkMode ink,
                                                                 int back_color,
                                                                 bool use_alpha,
                                                                 const bitmap::Palette* palette) {
    return applyInkInternal(src, ink, back_color, use_alpha, palette, true);
}

int InkProcessor::resolveBackColor(const bitmap::Bitmap& src,
                                   id::InkMode ink,
                                   int back_color,
                                   bool use_alpha,
                                   const bitmap::Palette* palette) {
    if (src.hasNativeMatteAlpha() && use_alpha) {
        const int alpha_back_color = resolveBackColorIgnoringAlpha(back_color, palette);
        if (ink == id::InkMode::BACKGROUND_TRANSPARENT && hasOpaqueBorderColor(src, alpha_back_color)) {
            return alpha_back_color;
        }
        return -1;
    }

    if (back_color > 255) {
        return back_color & 0x00FFFFFF;
    }

    if (src.bitDepth() == 32 && !use_alpha && ink != id::InkMode::COPY) {
        return 0xFFFFFF;
    }

    if (palette != nullptr && back_color >= 0 && back_color < palette->size()) {
        return static_cast<int>(palette->getColor(back_color) & 0x00FFFFFFU);
    }

    const int gray = 255 - back_color;
    return (gray << 16) | (gray << 8) | gray;
}

int InkProcessor::resolveBackColorIgnoringAlpha(int back_color, const bitmap::Palette* palette) {
    if (back_color > 255) {
        return back_color & 0x00FFFFFF;
    }
    if (palette != nullptr && back_color >= 0 && back_color < palette->size()) {
        return static_cast<int>(palette->getColor(back_color) & 0x00FFFFFFU);
    }

    const int gray = 255 - back_color;
    return (gray << 16) | (gray << 8) | gray;
}

bitmap::Bitmap InkProcessor::applyForeColorRemap(const bitmap::Bitmap& src,
                                                 std::uint32_t fore_color,
                                                 std::uint32_t back_color) {
    if (src.width() == 0 || src.height() == 0) {
        return src.copy();
    }

    const int fr = static_cast<int>((fore_color >> 16) & 0xFFU);
    const int fg = static_cast<int>((fore_color >> 8) & 0xFFU);
    const int fb = static_cast<int>(fore_color & 0xFFU);
    const int br = static_cast<int>((back_color >> 16) & 0xFFU);
    const int bg = static_cast<int>((back_color >> 8) & 0xFFU);
    const int bb = static_cast<int>(back_color & 0xFFU);

    std::vector<std::uint32_t> result;
    result.reserve(src.pixels().size());
    for (const auto pixel : src.pixels()) {
        const int alpha = static_cast<int>((pixel >> 24) & 0xFFU);
        if (alpha == 0) {
            result.push_back(0);
            continue;
        }

        const int r = static_cast<int>((pixel >> 16) & 0xFFU);
        const int g = static_cast<int>((pixel >> 8) & 0xFFU);
        const int b = static_cast<int>(pixel & 0xFFU);
        const float t = static_cast<float>((r + g + b) / 3) / 255.0F;
        result.push_back(packArgb(alpha,
                                  interpolateChannel(t, fr, br),
                                  interpolateChannel(t, fg, bg),
                                  interpolateChannel(t, fb, bb)));
    }

    return derivedBitmap(src, std::move(result));
}

bitmap::Bitmap InkProcessor::applyIndexedColorRemap(const bitmap::Bitmap& indexed_source,
                                                    const bitmap::Bitmap& masked_source,
                                                    std::uint32_t fore_color,
                                                    std::uint32_t back_color) {
    if (indexed_source.width() != masked_source.width() || indexed_source.height() != masked_source.height()) {
        return masked_source.copy();
    }
    const auto palette_indices = indexed_source.paletteIndices();
    if (!palette_indices.has_value() || palette_indices->size() != masked_source.pixels().size()) {
        return masked_source.copy();
    }

    const int fr = static_cast<int>((fore_color >> 16) & 0xFFU);
    const int fg = static_cast<int>((fore_color >> 8) & 0xFFU);
    const int fb = static_cast<int>(fore_color & 0xFFU);
    const int br = static_cast<int>((back_color >> 16) & 0xFFU);
    const int bg = static_cast<int>((back_color >> 8) & 0xFFU);
    const int bb = static_cast<int>(back_color & 0xFFU);

    std::vector<std::uint32_t> result;
    result.reserve(masked_source.pixels().size());
    for (std::size_t i = 0; i < masked_source.pixels().size(); ++i) {
        const auto pixel = masked_source.pixels()[i];
        const int alpha = static_cast<int>((pixel >> 24) & 0xFFU);
        if (alpha == 0) {
            result.push_back(0);
            continue;
        }

        const int palette_index = static_cast<int>((*palette_indices)[i] & 0xFFU);
        const float t = static_cast<float>(255 - palette_index) / 255.0F;
        result.push_back(packArgb(alpha,
                                  interpolateChannel(t, fr, br),
                                  interpolateChannel(t, fg, bg),
                                  interpolateChannel(t, fb, bb)));
    }

    return derivedBitmap(masked_source, std::move(result));
}

bitmap::Bitmap InkProcessor::applyDarkenForeColorOffset(const bitmap::Bitmap& src, std::uint32_t fore_color) {
    const int fore_r = static_cast<int>((fore_color >> 16) & 0xFFU);
    const int fore_g = static_cast<int>((fore_color >> 8) & 0xFFU);
    const int fore_b = static_cast<int>(fore_color & 0xFFU);

    std::vector<std::uint32_t> result;
    result.reserve(src.pixels().size());
    for (const auto pixel : src.pixels()) {
        const int alpha = static_cast<int>((pixel >> 24) & 0xFFU);
        if (alpha == 0) {
            result.push_back(0);
            continue;
        }

        const int r = std::min(255, static_cast<int>((pixel >> 16) & 0xFFU) + fore_r);
        const int g = std::min(255, static_cast<int>((pixel >> 8) & 0xFFU) + fore_g);
        const int b = std::min(255, static_cast<int>(pixel & 0xFFU) + fore_b);
        result.push_back(packArgb(alpha, r, g, b));
    }

    return derivedBitmap(src, std::move(result));
}

bitmap::Bitmap InkProcessor::remapExactColor(const bitmap::Bitmap& src,
                                             std::uint32_t from_rgb,
                                             std::uint32_t to_rgb) {
    std::vector<std::uint32_t> result;
    result.reserve(src.pixels().size());
    for (const auto pixel : src.pixels()) {
        if ((pixel & 0x00FFFFFFU) == (from_rgb & 0x00FFFFFFU)) {
            result.push_back((pixel & 0xFF000000U) | (to_rgb & 0x00FFFFFFU));
        } else {
            result.push_back(pixel);
        }
    }
    return derivedBitmap(src, std::move(result));
}

bitmap::Bitmap InkProcessor::applyBackgroundTransparent(const bitmap::Bitmap& src, int bg_color_rgb) {
    std::vector<std::uint32_t> result;
    result.reserve(src.pixels().size());

    const auto palette_indices = src.paletteIndices();
    const auto* palette_index_ptr =
        (palette_indices.has_value() && palette_indices->size() >= src.pixels().size()) ? &*palette_indices : nullptr;
    const bool key_default_indexed_matte = shouldKeyDefaultIndexedMatte(src, bg_color_rgb, palette_index_ptr);

    for (std::size_t index = 0; index < src.pixels().size(); ++index) {
        const auto pixel = src.pixels()[index];
        const int alpha = static_cast<int>((pixel >> 24) & 0xFFU);
        if (alpha == 0) {
            result.push_back(0);
            continue;
        }

        const auto rgb = pixel & 0x00FFFFFFU;
        if (key_default_indexed_matte &&
            palette_index_ptr != nullptr &&
            index < palette_index_ptr->size() &&
            static_cast<int>((*palette_index_ptr)[index] & 0xFFU) == 0) {
            result.push_back(0);
        } else if (rgb == static_cast<std::uint32_t>(bg_color_rgb & 0x00FFFFFF)) {
            result.push_back(0);
        } else {
            result.push_back(pixel | 0xFF000000U);
        }
    }

    return derivedBitmap(src, std::move(result));
}

bitmap::Bitmap InkProcessor::applyMask(const bitmap::Bitmap& src) {
    std::vector<std::uint32_t> result;
    result.reserve(src.pixels().size());
    for (const auto pixel : src.pixels()) {
        const int src_alpha = static_cast<int>((pixel >> 24) & 0xFFU);
        if (src_alpha == 0) {
            result.push_back(0);
            continue;
        }

        const int mask_alpha = maskAlphaFromPixel(pixel);
        const int combined_alpha = (src_alpha * mask_alpha) / 255;
        result.push_back((static_cast<std::uint32_t>(combined_alpha & 0xFF) << 24) |
                         (pixel & 0x00FFFFFFU));
    }
    return derivedBitmap(src, std::move(result));
}

bitmap::Bitmap InkProcessor::applyMask(const bitmap::Bitmap& src, const bitmap::Bitmap& mask) {
    std::vector<std::uint32_t> result;
    result.reserve(src.pixels().size());
    for (int y = 0; y < src.height(); ++y) {
        for (int x = 0; x < src.width(); ++x) {
            const auto pixel = src.getPixel(x, y);
            const int src_alpha = static_cast<int>((pixel >> 24) & 0xFFU);
            if (src_alpha == 0 || x >= mask.width() || y >= mask.height()) {
                result.push_back(0);
                continue;
            }

            // Director MASK ink receives a separate cast member immediately
            // after the source image. Black mask pixels reveal the source;
            // white pixels hide it, with greys providing partial opacity.
            const int mask_alpha = 255 - maskAlphaFromPixel(mask.getPixel(x, y));
            const int combined_alpha = (src_alpha * mask_alpha) / 255;
            result.push_back((static_cast<std::uint32_t>(combined_alpha & 0xFF) << 24) |
                             (pixel & 0x00FFFFFFU));
        }
    }
    return derivedBitmap(src, std::move(result));
}

bitmap::Bitmap InkProcessor::applyMatte(const bitmap::Bitmap& src, int matte_color_rgb, int tolerance) {
    const int width = src.width();
    const int height = src.height();
    if (width <= 0 || height <= 0) {
        return src.copy();
    }

    const auto& pixels = src.pixels();
    const auto transparent = bitmap::detail::computeEdgeConnectedMask(width, height, [&](int index) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        return ((pixel >> 24) & 0xFFU) == 0 || matchesRgb(pixel, matte_color_rgb, tolerance);
    });

    std::vector<std::uint32_t> result = pixels;
    for (std::size_t index = 0; index < result.size() && index < transparent.size(); ++index) {
        if (transparent[index]) {
            result[index] = 0;
        }
    }
    return derivedBitmap(src, std::move(result));
}

bitmap::Bitmap InkProcessor::applyFloodFillTransparency(const bitmap::Bitmap& src) {
    const auto palette_indices = src.paletteIndices();
    if (!palette_indices.has_value()) {
        return src.copy();
    }
    const auto matte_index = resolveDefaultIndexedFloodFillMatteIndex(src, *palette_indices);
    if (!matte_index.has_value()) {
        return src.copy();
    }
    return applyIndexedMatteInternal(src, *matte_index, false);
}

bitmap::Bitmap InkProcessor::convertOpaqueWhiteToTransparent(const bitmap::Bitmap& src) {
    const int width = src.width();
    const int height = src.height();
    if (width <= 0 || height <= 0) {
        return src.copy();
    }

    // Only border-connected white is the sprite's backing. Interior white
    // pixels are authored art that a DARKEN/LIGHTEN tint must keep, so a
    // global white match would punch holes through the artwork. Walk out from
    // the edges and clear only the white region that reaches them.
    const auto& pixels = src.pixels();
    const auto is_backing = [&](int index) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        return ((pixel >> 24U) & 0xFFU) == 0 || (pixel & 0x00FFFFFFU) == 0x00FFFFFFU;
    };
    const auto transparent = bitmap::detail::computeEdgeConnectedMask(width, height, is_backing);

    std::vector<std::uint32_t> result = pixels;
    bool changed = false;
    for (std::size_t index = 0; index < result.size() && index < transparent.size(); ++index) {
        if (transparent[index] && ((result[index] >> 24U) & 0xFFU) != 0U) {
            result[index] = 0x00FFFFFFU;
            changed = true;
        }
    }
    return changed ? derivedBitmap(src, std::move(result)) : src.copy();
}

bitmap::Bitmap InkProcessor::multiplyColor(const bitmap::Bitmap& src, int tint_rgb) {
    const int tint_r = (tint_rgb >> 16) & 0xFF;
    const int tint_g = (tint_rgb >> 8) & 0xFF;
    const int tint_b = tint_rgb & 0xFF;

    std::vector<std::uint32_t> result;
    result.reserve(src.pixels().size());
    for (const auto pixel : src.pixels()) {
        const int alpha = static_cast<int>((pixel >> 24) & 0xFFU);
        if (alpha == 0) {
            result.push_back(0);
            continue;
        }

        const int r = static_cast<int>((pixel >> 16) & 0xFFU);
        const int g = static_cast<int>((pixel >> 8) & 0xFFU);
        const int b = static_cast<int>(pixel & 0xFFU);
        result.push_back(packArgb(alpha,
                                  multiplyDirectorChannel(r, tint_r),
                                  multiplyDirectorChannel(g, tint_g),
                                  multiplyDirectorChannel(b, tint_b)));
    }
    return derivedBitmap(src, std::move(result));
}

} // namespace libreshockwave::player::render::pipeline
