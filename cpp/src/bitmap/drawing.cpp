#include "libreshockwave/bitmap/drawing.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "libreshockwave/bitmap/bitmap.hpp"
#include "bitmap_processing.hpp"

namespace libreshockwave::bitmap::drawing {
namespace {

struct FloodFillMatte {
    std::optional<int> palette_index;
    int color_rgb = 0xFFFFFF;
    int tolerance = 0;

    [[nodiscard]] bool usesPaletteIndex() const { return palette_index.has_value(); }
};

using detail::cornerContainsPaletteIndex;
using detail::cornerIndices;
using detail::edgeContainsOpaquePaletteIndex;
using detail::edgeIndices;
using detail::hasOpaqueNonPaletteIndexContent;
using detail::hasPaletteIndices;
using detail::isNearWhiteGrayscale;
using detail::isUniformPaletteIndex;
using detail::maskAlphaFromPixel;
using detail::matchesRgb;
using detail::packArgb;
using detail::paletteIndicesMatchPixels;
using detail::resolvePaletteIndexRgb;

bool defaultIndexedMatteRgb(int rgb) {
    return rgb == 0x000000 || rgb == 0xFFFFFF;
}

std::optional<int> inferDominantEdgePaletteIndex(const std::vector<std::uint32_t>& pixels,
                                                 const std::vector<std::uint8_t>& palette_indices,
                                                 int width,
                                                 int height) {
    if (width <= 0 || height <= 0) return std::nullopt;

    std::array<int, 256> counts{};
    int opaque_edge_count = 0;
    int dominant_index = -1;
    int dominant_count = 0;
    for (const int index : edgeIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        const int palette_index = static_cast<int>(palette_indices[static_cast<std::size_t>(index)]);
        const int count = ++counts[static_cast<std::size_t>(palette_index)];
        ++opaque_edge_count;
        if (count > dominant_count) {
            dominant_count = count;
            dominant_index = palette_index;
        }
    }
    if (opaque_edge_count == 0 || dominant_index < 0 || isUniformPaletteIndex(palette_indices, dominant_index)) {
        return std::nullopt;
    }

    int opaque_corner_count = 0;
    for (const int index : cornerIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        ++opaque_corner_count;
        if (static_cast<int>(palette_indices[static_cast<std::size_t>(index)]) != dominant_index) {
            return std::nullopt;
        }
    }
    if (opaque_corner_count == 0 || dominant_count * 4 < opaque_edge_count * 3) {
        return std::nullopt;
    }
    return dominant_index;
}

std::optional<int> inferWhiteEdgePaletteIndex(const std::vector<std::uint32_t>& pixels,
                                              const std::vector<std::uint8_t>& palette_indices,
                                              int width,
                                              int height) {
    if (width <= 0 || height <= 0) return std::nullopt;

    std::array<int, 256> counts{};
    int best_index = -1;
    int best_count = 0;
    for (const int index : edgeIndices(width, height)) {
        const auto offset = static_cast<std::size_t>(index);
        if (offset >= pixels.size() || offset >= palette_indices.size() ||
            ((pixels[offset] >> 24) & 0xFFU) == 0) {
            continue;
        }
        const int palette_index = static_cast<int>(palette_indices[offset]);
        const int rgb = resolvePaletteIndexRgb(pixels, palette_indices, palette_index);
        if (rgb != 0xFFFFFF) {
            continue;
        }
        const int count = ++counts[static_cast<std::size_t>(palette_index)];
        if (count > best_count) {
            best_count = count;
            best_index = palette_index;
        }
    }
    return best_index >= 0 && !isUniformPaletteIndex(palette_indices, best_index)
        ? std::optional<int>(best_index)
        : std::nullopt;
}

std::optional<FloodFillMatte> resolveIndexedFloodFillMatte(
    const std::vector<std::uint32_t>& pixels,
    const std::vector<std::uint8_t>& palette_indices,
    int width,
    int height) {
    if (const auto white_edge = inferWhiteEdgePaletteIndex(pixels, palette_indices, width, height)) {
        return FloodFillMatte{*white_edge, resolvePaletteIndexRgb(pixels, palette_indices, *white_edge), 0};
    }
    if (const auto dominant = inferDominantEdgePaletteIndex(pixels, palette_indices, width, height)) {
        const int matte_rgb = resolvePaletteIndexRgb(pixels, palette_indices, *dominant);
        if (matte_rgb == 0xFFFFFF || (*dominant == 0 && defaultIndexedMatteRgb(matte_rgb))) {
            return FloodFillMatte{*dominant, matte_rgb, 0};
        }
    }
    if (cornerContainsPaletteIndex(palette_indices, width, height, 0)) {
        const int index_zero_rgb = resolvePaletteIndexRgb(pixels, palette_indices, 0);
        if (defaultIndexedMatteRgb(index_zero_rgb)) {
            return FloodFillMatte{0, index_zero_rgb, 0};
        }
    }
    return std::nullopt;
}

bool isUniformRgb(const std::vector<std::uint32_t>& pixels, int rgb) {
    for (const auto pixel : pixels) {
        if (((pixel >> 24) & 0xFFU) != 0 && static_cast<int>(pixel & 0x00FFFFFFU) != rgb) {
            return false;
        }
    }
    return true;
}

std::optional<int> inferDominantEdgeRgb(const std::vector<std::uint32_t>& pixels, int width, int height) {
    if (width <= 0 || height <= 0) return std::nullopt;

    std::unordered_map<int, int> counts;
    int opaque_edge_count = 0;
    int dominant_rgb = -1;
    int dominant_count = 0;
    for (const int index : edgeIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        const int rgb = static_cast<int>(pixel & 0x00FFFFFFU);
        const int count = ++counts[rgb];
        ++opaque_edge_count;
        if (count > dominant_count) {
            dominant_count = count;
            dominant_rgb = rgb;
        }
    }
    if (opaque_edge_count == 0 || dominant_rgb < 0 || isUniformRgb(pixels, dominant_rgb)) {
        return std::nullopt;
    }

    int opaque_corner_count = 0;
    for (const int index : cornerIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        ++opaque_corner_count;
        if (static_cast<int>(pixel & 0x00FFFFFFU) != dominant_rgb) {
            return std::nullopt;
        }
    }
    if (opaque_corner_count == 0 || dominant_count * 4 < opaque_edge_count * 3) {
        return std::nullopt;
    }
    return dominant_rgb;
}

bool cornerContainsOpaqueRgb(const std::vector<std::uint32_t>& pixels, int width, int height, int rgb) {
    for (const int index : cornerIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) != 0 && static_cast<int>(pixel & 0x00FFFFFFU) == rgb) {
            return true;
        }
    }
    return false;
}

bool edgeContainsOpaqueRgb(const std::vector<std::uint32_t>& pixels, int width, int height, int rgb) {
    for (const int index : edgeIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) != 0 && static_cast<int>(pixel & 0x00FFFFFFU) == rgb) {
            return true;
        }
    }
    return false;
}

std::optional<FloodFillMatte> resolveRgbFloodFillMatte(const std::vector<std::uint32_t>& pixels,
                                                       int width,
                                                       int height) {
    if (edgeContainsOpaqueRgb(pixels, width, height, 0xFFFFFF)) {
        return FloodFillMatte{std::nullopt, 0xFFFFFF, 0};
    }
    if (const auto dominant = inferDominantEdgeRgb(pixels, width, height)) {
        if (isNearWhiteGrayscale(*dominant, 232, 16) && *dominant != 0xFFFFFF) {
            return std::nullopt;
        }
        return FloodFillMatte{std::nullopt, *dominant, 0};
    }
    return std::nullopt;
}

std::optional<FloodFillMatte> resolveFloodFillMatte(
    const std::vector<std::uint32_t>& pixels,
    const std::optional<std::vector<std::uint8_t>>& palette_indices,
    int width,
    int height) {
    if (hasPaletteIndices(palette_indices, width, height) &&
        paletteIndicesMatchPixels(pixels, *palette_indices)) {
        return resolveIndexedFloodFillMatte(pixels, *palette_indices, width, height);
    }
    return resolveRgbFloodFillMatte(pixels, width, height);
}

bool cornersAreNearWhite(const std::vector<std::uint32_t>& pixels,
                         int width,
                         int height,
                         int min_channel,
                         int max_delta) {
    bool saw_opaque_corner = false;
    for (const int index : cornerIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        saw_opaque_corner = true;
        if (!isNearWhiteGrayscale(static_cast<int>(pixel & 0x00FFFFFFU), min_channel, max_delta)) {
            return false;
        }
    }
    return saw_opaque_corner;
}

bool hasOpaqueNonNearWhiteContent(const std::vector<std::uint32_t>& pixels,
                                  const std::optional<std::vector<std::uint8_t>>& palette_indices,
                                  int width,
                                  int height,
                                  int min_channel,
                                  int max_delta) {
    int content_pixels = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto index = static_cast<std::size_t>(y * width + x);
            const auto pixel = pixels[index];
            if (((pixel >> 24) & 0xFFU) == 0) {
                continue;
            }
            int rgb = static_cast<int>(pixel & 0x00FFFFFFU);
            if (palette_indices.has_value() && index < palette_indices->size()) {
                rgb = resolvePaletteIndexRgb(pixels, *palette_indices, static_cast<int>((*palette_indices)[index]));
            }
            if (!isNearWhiteGrayscale(rgb, min_channel, max_delta)) {
                ++content_pixels;
                if (content_pixels >= 8) {
                    return true;
                }
            }
        }
    }
    return false;
}

std::optional<FloodFillMatte> resolveBackgroundTransparentMatte(
    const std::vector<std::uint32_t>& pixels,
    const std::optional<std::vector<std::uint8_t>>& palette_indices,
    int width,
    int height) {
    if (hasPaletteIndices(palette_indices, width, height)) {
        const auto matte_index = inferDominantEdgePaletteIndex(pixels, *palette_indices, width, height);
        if (!matte_index.has_value()) {
            return std::nullopt;
        }
        const int matte_rgb = resolvePaletteIndexRgb(pixels, *palette_indices, *matte_index);
        if (!isNearWhiteGrayscale(matte_rgb, 232, 16) ||
            !hasOpaqueNonNearWhiteContent(pixels, palette_indices, width, height, 232, 16)) {
            return std::nullopt;
        }
        return FloodFillMatte{*matte_index, matte_rgb, 0};
    }

    if (!cornersAreNearWhite(pixels, width, height, 232, 16)) {
        return std::nullopt;
    }

    int opaque_edge_count = 0;
    int near_white_edge_count = 0;
    bool exact_white_edge = false;
    for (const int index : edgeIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        ++opaque_edge_count;
        exact_white_edge |= (pixel & 0x00FFFFFFU) == 0x00FFFFFFU;
        if (isNearWhiteGrayscale(static_cast<int>(pixel & 0x00FFFFFFU), 232, 16)) {
            ++near_white_edge_count;
        }
    }
    // If the declared background colour reaches the edge, use the exact key.
    // Near-white edge pixels may be authored foreground outlines (for example
    // the catalogue arrows), and must not be absorbed by the antialias matte
    // tolerance in that case.
    if (exact_white_edge) {
        return std::nullopt;
    }
    if (opaque_edge_count == 0 || near_white_edge_count * 4 < opaque_edge_count * 3) {
        return std::nullopt;
    }
    if (!hasOpaqueNonNearWhiteContent(pixels, std::nullopt, width, height, 232, 16)) {
        return std::nullopt;
    }
    return FloodFillMatte{std::nullopt, 0xFFFFFF, 24};
}

bool isTransparentOrMatte(const std::vector<std::uint32_t>& pixels,
                          const std::optional<std::vector<std::uint8_t>>& palette_indices,
                          int index,
                          const FloodFillMatte& matte) {
    const auto pixel = pixels[static_cast<std::size_t>(index)];
    if (((pixel >> 24) & 0xFFU) == 0) {
        return true;
    }
    if (matte.usesPaletteIndex() && palette_indices.has_value() &&
        static_cast<std::size_t>(index) < palette_indices->size()) {
        return static_cast<int>((*palette_indices)[static_cast<std::size_t>(index)]) == *matte.palette_index;
    }
    return matchesRgb(pixel, matte.color_rgb, matte.tolerance);
}

std::shared_ptr<Bitmap> createAlphaMatte(const Bitmap& src, int alpha_threshold) {
    const int width = src.width();
    const int height = src.height();
    const int threshold = std::clamp(alpha_threshold, 0, 255);
    std::vector<std::uint32_t> mask(static_cast<std::size_t>(width * height), 0x00FFFFFFU);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int alpha = static_cast<int>((src.getPixel(x, y) >> 24) & 0xFFU);
            if (alpha < threshold) {
                alpha = 0;
            }
            mask[static_cast<std::size_t>(y * width + x)] =
                (static_cast<std::uint32_t>(alpha) << 24) | 0x00FFFFFFU;
        }
    }
    auto matte = std::make_shared<Bitmap>(width, height, 32, std::move(mask));
    matte->setNativeAlpha(true);
    return matte;
}

std::shared_ptr<Bitmap> createFloodFillMatte(const Bitmap& src) {
    const int width = src.width();
    const int height = src.height();
    const auto pixels = src.pixels();
    const auto palette_indices = src.paletteIndices();
    const auto matte = resolveFloodFillMatte(pixels, palette_indices, width, height);
    const auto transparent = matte
        ? detail::computeEdgeConnectedMask(width, height, [&](int index) {
              return isTransparentOrMatte(pixels, palette_indices, index, *matte);
          })
        : std::vector<bool>(static_cast<std::size_t>(width * height), false);

    std::vector<std::uint32_t> mask(static_cast<std::size_t>(width * height), 0x00FFFFFFU);
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        if (transparent[index]) {
            mask[index] = 0x00FFFFFFU;
        } else {
            auto alpha = static_cast<std::uint32_t>((pixels[index] >> 24) & 0xFFU);
            if (alpha == 0) {
                alpha = 0xFFU;
            }
            mask[index] = (alpha << 24) | 0x00FFFFFFU;
        }
    }

    auto matte_bitmap = std::make_shared<Bitmap>(width, height, 32, std::move(mask));
    matte_bitmap->setNativeAlpha(true);
    return matte_bitmap;
}

bool isGrayscaleMaskSource(const std::vector<std::uint32_t>& pixels, const std::vector<bool>& transparent) {
    int opaque_pixels = 0;
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        if (transparent[index] || ((pixels[index] >> 24) & 0xFFU) == 0) {
            continue;
        }
        const auto r = (pixels[index] >> 16) & 0xFFU;
        const auto g = (pixels[index] >> 8) & 0xFFU;
        const auto b = pixels[index] & 0xFFU;
        if (r != g || g != b) {
            return false;
        }
        ++opaque_pixels;
    }
    return opaque_pixels > 0 && opaque_pixels * 4 <= static_cast<int>(pixels.size()) * 3;
}

bool isWhiteBackedMaskSource(const std::vector<std::uint32_t>& pixels,
                             const std::vector<bool>& transparent,
                             const FloodFillMatte& matte) {
    if (maskAlphaFromPixel(0xFF000000U | static_cast<std::uint32_t>(matte.color_rgb & 0x00FFFFFF)) < 250) {
        return false;
    }

    bool has_transparent_matte = false;
    bool has_opaque_ink = false;
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        if (((pixels[index] >> 24) & 0xFFU) == 0) {
            continue;
        }
        if (transparent[index]) {
            has_transparent_matte = true;
        } else {
            has_opaque_ink = true;
        }
        if (has_transparent_matte && has_opaque_ink) {
            return true;
        }
    }
    return false;
}

bool isMaskSource(const std::vector<std::uint32_t>& pixels,
                  const std::vector<bool>& transparent,
                  const FloodFillMatte& matte) {
    return isGrayscaleMaskSource(pixels, transparent) ||
           isWhiteBackedMaskSource(pixels, transparent, matte);
}

bool isMostlyWhiteRegion(const Bitmap& bitmap, int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    int sampled = 0;
    int white = 0;
    const int step = std::max(1, (width * height) / 64);
    for (int index = 0; index < width * height; index += step) {
        const int px = x + (index % width);
        const int py = y + (index / width);
        if (px < 0 || px >= bitmap.width() || py < 0 || py >= bitmap.height()) {
            continue;
        }
        ++sampled;
        if ((bitmap.getPixel(px, py) & 0x00FFFFFFU) == 0x00FFFFFFU) {
            ++white;
        }
    }
    return sampled > 0 && white * 4 >= sampled * 3;
}

bool copyMatteToMaskImage(Bitmap& dest,
                          const Bitmap& src,
                          int dest_x,
                          int dest_y,
                          int src_x,
                          int src_y,
                          int width,
                          int height) {
    if (src.width() <= 0 || src.height() <= 0 ||
        width <= 0 || height <= 0 ||
        !isMostlyWhiteRegion(dest, dest_x, dest_y, width, height)) {
        return false;
    }

    const auto pixels = src.pixels();
    const auto palette_indices = src.paletteIndices();
    const auto matte = resolveFloodFillMatte(pixels, palette_indices, src.width(), src.height());
    if (!matte.has_value()) {
        return false;
    }
    const auto transparent = detail::computeEdgeConnectedMask(src.width(), src.height(), [&](int index) {
        return isTransparentOrMatte(pixels, palette_indices, index, *matte);
    });
    if (!isMaskSource(pixels, transparent, *matte)) {
        return false;
    }

    const int matte_luma = maskAlphaFromPixel(0xFF000000U | static_cast<std::uint32_t>(matte->color_rgb & 0x00FFFFFF));
    const bool light_matte = matte_luma >= 128;
    for (int y = 0; y < height; ++y) {
        const int sy = src_y + y;
        const int dy = dest_y + y;
        if (sy < 0 || sy >= src.height() || dy < 0 || dy >= dest.height()) {
            continue;
        }
        for (int x = 0; x < width; ++x) {
            const int sx = src_x + x;
            const int dx = dest_x + x;
            if (sx < 0 || sx >= src.width() || dx < 0 || dx >= dest.width()) {
                continue;
            }
            const auto src_offset = static_cast<std::size_t>(sy * src.width() + sx);
            if (src_offset >= transparent.size() || transparent[src_offset]) {
                continue;
            }
            int mask_luma = maskAlphaFromPixel(pixels[src_offset]);
            if (!light_matte) {
                mask_luma = 255 - mask_luma;
            }
            const auto luma = static_cast<std::uint32_t>(mask_luma & 0xFF);
            dest.setPixelPreservePaletteIndex(dx, dy, 0xFF000000U | (luma << 16) | (luma << 8) | luma);
        }
    }
    return true;
}

std::shared_ptr<Bitmap> applyBackgroundTransparentToRegion(const Bitmap& src,
                                                           int src_x,
                                                           int src_y,
                                                           int width,
                                                           int height,
                                                           int background_key_rgb) {
    if (width <= 0 ||
        height <= 0 ||
        (background_key_rgb & 0x00FFFFFF) != 0xFFFFFF ||
        src.hasNativeMatteAlpha()) {
        return nullptr;
    }

    auto result = std::make_shared<Bitmap>(src.getRegion(src_x, src_y, width, height));
    const auto pixels = result->pixels();
    const auto palette_indices = result->paletteIndices();
    const auto matte = resolveBackgroundTransparentMatte(pixels, palette_indices, width, height);
    if (!matte.has_value()) {
        return nullptr;
    }

    const auto transparent = detail::computeEdgeConnectedMask(width, height, [&](int index) {
        return isTransparentOrMatte(pixels, palette_indices, index, *matte);
    });
    bool changed = false;
    auto& result_pixels = result->pixels();
    for (std::size_t index = 0; index < result_pixels.size() && index < transparent.size(); ++index) {
        if (!transparent[index]) {
            continue;
        }
        result_pixels[index] &= 0x00FFFFFFU;
        changed = true;
    }
    if (!changed) {
        return nullptr;
    }
    result->setNativeAlpha(true);
    return result;
}

bool isNearWhiteMattePixel(std::uint32_t pixel) {
    if (((pixel >> 24) & 0xFFU) == 0) {
        return false;
    }
    const int r = static_cast<int>((pixel >> 16) & 0xFFU);
    const int g = static_cast<int>((pixel >> 8) & 0xFFU);
    const int b = static_cast<int>(pixel & 0xFFU);
    return r >= 240 && g >= 240 && b >= 240 &&
           std::abs(r - g) <= 2 &&
           std::abs(g - b) <= 2;
}

bool shouldKeyNearWhiteMatte(const Bitmap& src, int src_x, int src_y, int width, int height, int background_key_rgb) {
    if (src.bitDepth() < 32 ||
        !src.hasTransparentPixels() ||
        (background_key_rgb & 0x00FFFFFF) != 0xFFFFFF ||
        width <= 0 ||
        height <= 0 ||
        src.width() <= 0 ||
        src.height() <= 0) {
        return false;
    }

    const int max_x = src.width() - 1;
    const int max_y = src.height() - 1;
    const int left = std::clamp(src_x, 0, max_x);
    const int top = std::clamp(src_y, 0, max_y);
    const int right = std::clamp(src_x + width - 1, 0, max_x);
    const int bottom = std::clamp(src_y + height - 1, 0, max_y);
    for (int x = left; x <= right; ++x) {
        if (isNearWhiteMattePixel(src.getPixel(x, top)) ||
            isNearWhiteMattePixel(src.getPixel(x, bottom))) {
            return true;
        }
    }
    for (int y = top + 1; y < bottom; ++y) {
        if (isNearWhiteMattePixel(src.getPixel(left, y)) ||
            isNearWhiteMattePixel(src.getPixel(right, y))) {
            return true;
        }
    }
    return false;
}

bool shouldKeyDefaultIndexedMatte(const Bitmap& src,
                                  int background_key_rgb,
                                  const std::optional<std::vector<std::uint8_t>>& palette_indices) {
    if (src.bitDepth() > 8 ||
        (background_key_rgb & 0x00FFFFFF) != 0xFFFFFF ||
        !hasPaletteIndices(palette_indices, src.width(), src.height()) ||
        isUniformPaletteIndex(*palette_indices, 0)) {
        return false;
    }

    const auto& pixels = src.pixels();
    if (!edgeContainsOpaquePaletteIndex(pixels, *palette_indices, src.width(), src.height(), 0)) {
        return false;
    }

    const int index_zero_rgb = resolvePaletteIndexRgb(pixels, *palette_indices, 0);
    return isNearWhiteGrayscale(index_zero_rgb, 232, 16) &&
           hasOpaqueNonPaletteIndexContent(pixels, *palette_indices, 0);
}

std::shared_ptr<Bitmap> createDirectMask(const Bitmap& src,
                                         const FloodFillMatte& matte,
                                         int alpha_threshold) {
    const int width = src.width();
    const int height = src.height();
    const int threshold = std::clamp(alpha_threshold, 0, 255);
    const auto pixels = src.pixels();
    const int matte_luma = maskAlphaFromPixel(0xFF000000U | static_cast<std::uint32_t>(matte.color_rgb & 0x00FFFFFF));
    const bool light_matte = matte_luma >= 128;

    std::vector<std::uint32_t> mask(static_cast<std::size_t>(width * height), 0xFFFFFFFFU);
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        const auto pixel = pixels[index];
        if (((pixel >> 24) & 0xFFU) == 0) {
            mask[index] = 0xFFFFFFFFU;
            continue;
        }
        int mask_luma = maskAlphaFromPixel(pixel);
        if (!light_matte) {
            mask_luma = 255 - mask_luma;
        }
        const int opacity = 255 - mask_luma;
        if (opacity < threshold) {
            mask_luma = 255;
        }
        const auto luma = static_cast<std::uint32_t>(mask_luma & 0xFF);
        mask[index] = 0xFF000000U | (luma << 16) | (luma << 8) | luma;
    }
    return std::make_shared<Bitmap>(width, height, src.bitDepth(), std::move(mask));
}

} // namespace

void fillRect(Bitmap& dest, int x, int y, int width, int height, std::uint32_t argb) {
    dest.fillRect(x, y, width, height, argb);
}

void drawRect(Bitmap& dest, int x, int y, int width, int height, std::uint32_t argb) {
    if (width <= 0 || height <= 0) {
        return;
    }
    for (int px = x; px < x + width; ++px) {
        dest.setPixel(px, y, argb);
        dest.setPixel(px, y + height - 1, argb);
    }
    for (int py = y; py < y + height; ++py) {
        dest.setPixel(x, py, argb);
        dest.setPixel(x + width - 1, py, argb);
    }
}

void drawLine(Bitmap& dest, int x0, int y0, int x1, int y1, std::uint32_t argb) {
    const int dx = std::abs(x1 - x0);
    const int dy = std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    while (true) {
        dest.setPixel(x0, y0, argb);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void fillEllipse(Bitmap& dest, int cx, int cy, int rx, int ry, std::uint32_t argb) {
    if (rx < 0 || ry < 0) {
        return;
    }
    const int rx_sq = rx * rx;
    const int ry_sq = ry * ry;
    const int radius_product = rx_sq * ry_sq;
    for (int y = -ry; y <= ry; ++y) {
        for (int x = -rx; x <= rx; ++x) {
            if ((x * x * ry_sq) + (y * y * rx_sq) <= radius_product) {
                dest.setPixel(cx + x, cy + y, argb);
            }
        }
    }
}

void drawEllipse(Bitmap& dest, int cx, int cy, int rx, int ry, std::uint32_t argb) {
    if (rx <= 0 || ry <= 0) {
        return;
    }

    int x = 0;
    int y = ry;
    const int rx_sq = rx * rx;
    const int ry_sq = ry * ry;
    int p = static_cast<int>(static_cast<double>(ry_sq) -
                             static_cast<double>(rx_sq * ry) +
                             0.25 * static_cast<double>(rx_sq));

    while (ry_sq * x < rx_sq * y) {
        dest.setPixel(cx + x, cy + y, argb);
        dest.setPixel(cx - x, cy + y, argb);
        dest.setPixel(cx + x, cy - y, argb);
        dest.setPixel(cx - x, cy - y, argb);
        if (p < 0) {
            ++x;
            p += (2 * ry_sq * x) + ry_sq;
        } else {
            ++x;
            --y;
            p += (2 * ry_sq * x) - (2 * rx_sq * y) + ry_sq;
        }
    }

    p = static_cast<int>(static_cast<double>(ry_sq) * (static_cast<double>(x) + 0.5) *
                             (static_cast<double>(x) + 0.5) +
                         static_cast<double>(rx_sq) * (y - 1) * (y - 1) -
                         static_cast<double>(rx_sq * ry_sq));
    while (y >= 0) {
        dest.setPixel(cx + x, cy + y, argb);
        dest.setPixel(cx - x, cy + y, argb);
        dest.setPixel(cx + x, cy - y, argb);
        dest.setPixel(cx - x, cy - y, argb);
        if (p > 0) {
            --y;
            p -= (2 * rx_sq * y) + rx_sq;
        } else {
            --y;
            ++x;
            p += (2 * ry_sq * x) - (2 * rx_sq * y) + rx_sq;
        }
    }
}

int combineAlpha(int src_alpha, int blend_alpha) {
    if (src_alpha <= 0 || blend_alpha <= 0) return 0;
    if (src_alpha >= 255) return blend_alpha;
    if (blend_alpha >= 255) return src_alpha;
    return (src_alpha * blend_alpha) / 255;
}

std::uint32_t alphaBlend(std::uint32_t fg, std::uint32_t bg, int alpha) {
    if (alpha <= 0) return bg;
    if (alpha >= 255) return fg;

    const int fg_r = static_cast<int>((fg >> 16) & 0xFFU);
    const int fg_g = static_cast<int>((fg >> 8) & 0xFFU);
    const int fg_b = static_cast<int>(fg & 0xFFU);
    const int bg_r = static_cast<int>((bg >> 16) & 0xFFU);
    const int bg_g = static_cast<int>((bg >> 8) & 0xFFU);
    const int bg_b = static_cast<int>(bg & 0xFFU);
    const int inv_alpha = 255 - alpha;
    const int r = (fg_r * alpha + bg_r * inv_alpha) / 255;
    const int g = (fg_g * alpha + bg_g * inv_alpha) / 255;
    const int b = (fg_b * alpha + bg_b * inv_alpha) / 255;
    return packArgb(255, r, g, b);
}

std::uint32_t applyInk(std::uint32_t src,
                       std::uint32_t dest,
                       id::InkMode ink,
                       int blend,
                       int background_key_rgb) {
    const int src_alpha = static_cast<int>((src >> 24) & 0xFFU);
    const int src_r = static_cast<int>((src >> 16) & 0xFFU);
    const int src_g = static_cast<int>((src >> 8) & 0xFFU);
    const int src_b = static_cast<int>(src & 0xFFU);
    const int dest_r = static_cast<int>((dest >> 16) & 0xFFU);
    const int dest_g = static_cast<int>((dest >> 8) & 0xFFU);
    const int dest_b = static_cast<int>(dest & 0xFFU);
    const int src_rgb = static_cast<int>(src & 0x00FFFFFFU);

    if (ink == id::InkMode::TRANSPARENT) {
        return src_rgb == 0xFFFFFF ? dest : src;
    }
    if (ink == id::InkMode::REVERSE) {
        return packArgb(255, dest_r ^ src_r, dest_g ^ src_g, dest_b ^ src_b);
    }
    if (ink == id::InkMode::GHOST) {
        return packArgb(255, (src_r + dest_r) / 2, (src_g + dest_g) / 2, (src_b + dest_b) / 2);
    }
    if (ink == id::InkMode::NOT_COPY) {
        return packArgb(255, 255 - src_r, 255 - src_g, 255 - src_b);
    }
    if (ink == id::InkMode::NOT_TRANSPARENT) {
        return src_rgb == 0 ? dest : packArgb(255, 255 - src_r, 255 - src_g, 255 - src_b);
    }
    if (ink == id::InkMode::NOT_REVERSE) {
        return packArgb(255, dest_r ^ (255 - src_r), dest_g ^ (255 - src_g), dest_b ^ (255 - src_b));
    }
    if (ink == id::InkMode::NOT_GHOST) {
        return packArgb(255,
                        ((255 - src_r) + dest_r) / 2,
                        ((255 - src_g) + dest_g) / 2,
                        ((255 - src_b) + dest_b) / 2);
    }
    if (ink == id::InkMode::MATTE) {
        if (src_alpha == 0) return dest;
        if (blend < 255) {
            const int matte_alpha = (src_alpha * blend) / 255;
            return matte_alpha == 0 ? dest : alphaBlend(src, dest, matte_alpha);
        }
        return alphaBlend(src, dest, src_alpha);
    }
    if (ink == id::InkMode::MASK) {
        const int alpha = combineAlpha(src_alpha, maskAlphaFromPixel(src));
        return alpha == 0 ? dest : alphaBlend(src, dest, alpha);
    }
    if (ink == id::InkMode::BACKGROUND_TRANSPARENT) {
        if (src_alpha == 0 || src_rgb == (background_key_rgb & 0x00FFFFFF)) {
            return dest;
        }
        if (blend < 255 || src_alpha < 255) {
            return alphaBlend(src, dest, combineAlpha(src_alpha, blend));
        }
        return src;
    }
    if (ink == id::InkMode::BLEND) {
        return alphaBlend(src, dest, combineAlpha(src_alpha, blend));
    }
    if (ink == id::InkMode::ADD_PIN) {
        return packArgb(255, std::min(255, src_r + dest_r), std::min(255, src_g + dest_g), std::min(255, src_b + dest_b));
    }
    if (ink == id::InkMode::ADD) {
        return packArgb(255, src_r + dest_r, src_g + dest_g, src_b + dest_b);
    }
    if (ink == id::InkMode::SUBTRACT_PIN) {
        return packArgb(255, std::max(0, dest_r - src_r), std::max(0, dest_g - src_g), std::max(0, dest_b - src_b));
    }
    if (ink == id::InkMode::SUBTRACT) {
        return packArgb(255, dest_r - src_r, dest_g - src_g, dest_b - src_b);
    }
    if (ink == id::InkMode::LIGHTEST) {
        return src_alpha == 0 ? dest : packArgb(255, std::max(src_r, dest_r), std::max(src_g, dest_g), std::max(src_b, dest_b));
    }
    if (ink == id::InkMode::DARKEST) {
        return src_alpha == 0 ? dest : packArgb(255, std::min(src_r, dest_r), std::min(src_g, dest_g), std::min(src_b, dest_b));
    }
    if (ink == id::InkMode::LIGHTEN || ink == id::InkMode::DARKEN) {
        return src_alpha == 0 ? dest : alphaBlend(src, dest, combineAlpha(src_alpha, blend));
    }

    if (blend < 255) {
        return alphaBlend(src, dest, combineAlpha(src_alpha, blend));
    }
    if (src_alpha == 0) {
        return dest;
    }
    if (src_alpha < 255) {
        return alphaBlend(src, dest, src_alpha);
    }
    return src;
}

bool maskAllowsPixel(const Bitmap& mask, int x, int y) {
    if (x < 0 || x >= mask.width() || y < 0 || y >= mask.height()) {
        return false;
    }
    const auto pixel = mask.getPixel(x, y);
    if (mask.hasNativeMatteAlpha()) {
        return ((pixel >> 24) & 0xFFU) != 0;
    }
    return maskAlphaFromPixel(pixel) < 255;
}

std::shared_ptr<Bitmap> preprocessBackgroundTransparent(const Bitmap& src, int background_key_rgb) {
    return applyBackgroundTransparentToRegion(src, 0, 0, src.width(), src.height(), background_key_rgb);
}

void copyPixels(Bitmap& dest,
                const Bitmap& src,
                int dest_x,
                int dest_y,
                int src_x,
                int src_y,
                int width,
                int height,
                id::InkMode ink,
                int blend,
                const Bitmap* mask,
                int background_key_rgb) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (ink == id::InkMode::MATTE &&
        dest.bitDepth() <= 8 &&
        copyMatteToMaskImage(dest, src, dest_x, dest_y, src_x, src_y, width, height)) {
        return;
    }

    std::shared_ptr<Bitmap> matte_source;
    std::shared_ptr<Bitmap> background_transparent_source;
    const Bitmap* effective_source = &src;
    int effective_src_x = src_x;
    int effective_src_y = src_y;
    if (ink == id::InkMode::MATTE) {
        matte_source = std::make_shared<Bitmap>(applyFloodFillTransparency(src));
        effective_source = matte_source.get();
    } else if (ink == id::InkMode::BACKGROUND_TRANSPARENT) {
        background_transparent_source = preprocessBackgroundTransparent(src, background_key_rgb);
        if (background_transparent_source != nullptr) {
            effective_source = background_transparent_source.get();
        }
    }

    const auto effective_palette_indices = effective_source->paletteIndices();
    const bool key_near_white_matte =
        ink == id::InkMode::BACKGROUND_TRANSPARENT &&
        shouldKeyNearWhiteMatte(*effective_source, effective_src_x, effective_src_y, width, height, background_key_rgb);
    const bool key_default_indexed_matte =
        ink == id::InkMode::BACKGROUND_TRANSPARENT &&
        shouldKeyDefaultIndexedMatte(*effective_source, background_key_rgb, effective_palette_indices);

    for (int y = 0; y < height; ++y) {
        const int sy = effective_src_y + y;
        const int dy = dest_y + y;
        if (sy < 0 || sy >= effective_source->height() || dy < 0 || dy >= dest.height()) {
            continue;
        }

        for (int x = 0; x < width; ++x) {
            const int sx = effective_src_x + x;
            const int dx = dest_x + x;
            if (sx < 0 || sx >= effective_source->width() || dx < 0 || dx >= dest.width()) {
                continue;
            }
            if (mask != nullptr && !maskAllowsPixel(*mask, src_x + x, src_y + y)) {
                continue;
            }

            const auto src_pixel = effective_source->getPixel(sx, sy);
            const auto dest_pixel = dest.getPixel(dx, dy);
            const auto src_offset = static_cast<std::size_t>(sy * effective_source->width() + sx);
            const bool skip_source =
                (key_near_white_matte && isNearWhiteMattePixel(src_pixel)) ||
                (key_default_indexed_matte &&
                 effective_palette_indices.has_value() &&
                 src_offset < effective_palette_indices->size() &&
                 static_cast<int>((*effective_palette_indices)[src_offset]) == 0);
            const auto result_pixel = skip_source
                ? dest_pixel
                : applyInk(src_pixel, dest_pixel, ink, blend, background_key_rgb);
            dest.setPixelPreservePaletteIndex(dx, dy, result_pixel);
        }
    }
}

void copyPixels(Bitmap& dest,
                const Bitmap& src,
                int dest_x,
                int dest_y,
                id::InkMode ink,
                int blend) {
    copyPixels(dest, src, dest_x, dest_y, 0, 0, src.width(), src.height(), ink, blend);
}

std::shared_ptr<Bitmap> transformQuadBitmap(const Bitmap& src,
                                            int src_left,
                                            int src_top,
                                            int src_right,
                                            int src_bottom,
                                            const std::array<int, 4>& px,
                                            const std::array<int, 4>& py,
                                            int min_x,
                                            int min_y,
                                            int max_x,
                                            int max_y) {
    const int src_width = src_right - src_left;
    const int src_height = src_bottom - src_top;
    const int dest_width = max_x - min_x;
    const int dest_height = max_y - min_y;
    auto transformed = std::make_shared<Bitmap>(dest_width, dest_height, src.bitDepth());
    transformed->copyPaletteMetadataFrom(&src);

    const auto src_palette_indices = src.paletteIndices();
    std::optional<std::vector<std::uint8_t>> transformed_indices;
    if (src_palette_indices.has_value() && src_palette_indices->size() == src.pixels().size()) {
        transformed_indices = std::vector<std::uint8_t>(static_cast<std::size_t>(dest_width * dest_height), 0);
    } else {
        transformed->clearPaletteIndices();
    }

    const bool axis_aligned =
        (px[0] == min_x || px[0] == max_x) && (py[0] == min_y || py[0] == max_y) &&
        (px[1] == min_x || px[1] == max_x) && (py[1] == min_y || py[1] == max_y) &&
        (px[2] == min_x || px[2] == max_x) && (py[2] == min_y || py[2] == max_y) &&
        (px[3] == min_x || px[3] == max_x) && (py[3] == min_y || py[3] == max_y);

    if (axis_aligned) {
        const double c0x = px[0] == min_x ? 0.0 : 1.0;
        const double c0y = py[0] == min_y ? 0.0 : 1.0;
        const double axis_xx = (px[1] == min_x ? 0.0 : 1.0) - c0x;
        const double axis_xy = (py[1] == min_y ? 0.0 : 1.0) - c0y;
        const double axis_yx = (px[3] == min_x ? 0.0 : 1.0) - c0x;
        const double axis_yy = (py[3] == min_y ? 0.0 : 1.0) - c0y;

        for (int y = 0; y < dest_height; ++y) {
            const double dv = (static_cast<double>(y) + 0.5) / static_cast<double>(dest_height);
            for (int x = 0; x < dest_width; ++x) {
                const double du = (static_cast<double>(x) + 0.5) / static_cast<double>(dest_width);
                const double rel_x = du - c0x;
                const double rel_y = dv - c0y;
                const double src_u = rel_x * axis_xx + rel_y * axis_xy;
                const double src_v = rel_x * axis_yx + rel_y * axis_yy;
                const int src_x = src_left + std::clamp(
                    static_cast<int>(std::floor(src_u * static_cast<double>(src_width))), 0, src_width - 1);
                const int src_y = src_top + std::clamp(
                    static_cast<int>(std::floor(src_v * static_cast<double>(src_height))), 0, src_height - 1);
                if (src_x < 0 || src_x >= src.width() || src_y < 0 || src_y >= src.height()) {
                    continue;
                }
                transformed->setPixelPreservePaletteIndex(x, y, src.getPixel(src_x, src_y));
                if (transformed_indices.has_value() && src_palette_indices.has_value()) {
                    const auto src_offset = static_cast<std::size_t>(src_y * src.width() + src_x);
                    const auto dest_offset = static_cast<std::size_t>(y * dest_width + x);
                    if (src_offset < src_palette_indices->size() && dest_offset < transformed_indices->size()) {
                        (*transformed_indices)[dest_offset] = (*src_palette_indices)[src_offset];
                    }
                }
            }
        }
    } else {
        const bool flip_h = px[0] > px[1];
        const bool flip_v = py[0] > py[3];
        for (int y = 0; y < dest_height; ++y) {
            const int local_y = y * src_height / dest_height;
            const int src_y = src_top + (flip_v ? (src_height - 1 - local_y) : local_y);
            for (int x = 0; x < dest_width; ++x) {
                const int local_x = x * src_width / dest_width;
                const int src_x = src_left + (flip_h ? (src_width - 1 - local_x) : local_x);
                if (src_x < 0 || src_x >= src.width() || src_y < 0 || src_y >= src.height()) {
                    continue;
                }
                transformed->setPixelPreservePaletteIndex(x, y, src.getPixel(src_x, src_y));
                if (transformed_indices.has_value() && src_palette_indices.has_value()) {
                    const auto src_offset = static_cast<std::size_t>(src_y * src.width() + src_x);
                    const auto dest_offset = static_cast<std::size_t>(y * dest_width + x);
                    if (src_offset < src_palette_indices->size() && dest_offset < transformed_indices->size()) {
                        (*transformed_indices)[dest_offset] = (*src_palette_indices)[src_offset];
                    }
                }
            }
        }
    }

    if (transformed_indices.has_value()) {
        transformed->setPaletteIndices(std::move(*transformed_indices));
    }
    return transformed;
}

std::shared_ptr<Bitmap> createMatte(const Bitmap& src, int alpha_threshold) {
    if (src.width() <= 0 || src.height() <= 0) {
        return std::make_shared<Bitmap>(1, 1, 32);
    }
    if (src.hasNativeMatteAlpha()) {
        return createAlphaMatte(src, alpha_threshold);
    }
    return createFloodFillMatte(src);
}

std::shared_ptr<Bitmap> createMask(const Bitmap& src, int alpha_threshold) {
    if (src.width() <= 0 || src.height() <= 0) {
        return std::make_shared<Bitmap>(1, 1, 32);
    }
    if (src.hasNativeMatteAlpha()) {
        return createAlphaMatte(src, alpha_threshold);
    }

    const auto pixels = src.pixels();
    const auto palette_indices = src.paletteIndices();
    const auto matte = resolveFloodFillMatte(pixels, palette_indices, src.width(), src.height());
    if (matte.has_value()) {
        const auto transparent = detail::computeEdgeConnectedMask(src.width(), src.height(), [&](int index) {
            return isTransparentOrMatte(pixels, palette_indices, index, *matte);
        });
        if (isMaskSource(pixels, transparent, *matte)) {
            return createDirectMask(src, *matte, alpha_threshold);
        }
    }
    return createFloodFillMatte(src);
}

Bitmap applyMatteToRegion(const Bitmap& src, int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) {
        return Bitmap(std::max(width, 1), std::max(height, 1), src.bitDepth());
    }
    auto region = src.getRegion(x, y, width, height);
    if (src.hasNativeMatteAlpha()) {
        region.setNativeAlpha(true);
        return region;
    }

    const auto pixels = region.pixels();
    const auto palette_indices = region.paletteIndices();
    const auto matte = resolveFloodFillMatte(pixels, palette_indices, width, height);
    if (!matte.has_value()) {
        return region;
    }

    const auto transparent = detail::computeEdgeConnectedMask(width, height, [&](int index) {
        return isTransparentOrMatte(pixels, palette_indices, index, *matte);
    });
    auto& region_pixels = region.pixels();
    for (std::size_t index = 0; index < region_pixels.size() && index < transparent.size(); ++index) {
        if (transparent[index]) {
            region_pixels[index] &= 0x00FFFFFFU;
        }
    }
    return region;
}

Bitmap applyFloodFillTransparency(const Bitmap& src) {
    return applyMatteToRegion(src, 0, 0, src.width(), src.height());
}

} // namespace libreshockwave::bitmap::drawing
