#include "libreshockwave/player/render/pipeline/InkProcessor.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <queue>
#include <vector>

namespace libreshockwave::player::render::pipeline {
namespace {

std::uint32_t packArgb(int alpha, int r, int g, int b) {
    return (static_cast<std::uint32_t>(alpha & 0xFF) << 24) |
           (static_cast<std::uint32_t>(r & 0xFF) << 16) |
           (static_cast<std::uint32_t>(g & 0xFF) << 8) |
           static_cast<std::uint32_t>(b & 0xFF);
}

bool isOpaqueColor(std::uint32_t argb, int colorRgb) {
    return ((argb >> 24) & 0xFFU) == 0xFFU && static_cast<int>(argb & 0x00FFFFFFU) == colorRgb;
}

bool hasOpaqueBorderColor(const bitmap::Bitmap& src, int colorRgb) {
    const int w = src.width();
    const int h = src.height();
    if (w <= 0 || h <= 0) {
        return false;
    }

    for (int x = 0; x < w; ++x) {
        if (isOpaqueColor(src.getPixel(x, 0), colorRgb)) return true;
        if (isOpaqueColor(src.getPixel(x, h - 1), colorRgb)) return true;
    }
    for (int y = 1; y < h - 1; ++y) {
        if (isOpaqueColor(src.getPixel(0, y), colorRgb)) return true;
        if (isOpaqueColor(src.getPixel(w - 1, y), colorRgb)) return true;
    }
    return false;
}

bitmap::Bitmap derivedBitmap(const bitmap::Bitmap& src, std::vector<std::uint32_t> pixels) {
    bitmap::Bitmap derived(src.width(), src.height(), src.bitDepth(), std::move(pixels));
    derived.copyPaletteMetadataFrom(&src);
    return derived;
}

int interpolate(float t, int fore, int back) {
    return static_cast<int>(std::lround((1.0F - t) * static_cast<float>(fore) +
                                        t * static_cast<float>(back)));
}

int maskAlphaFromPixel(std::uint32_t pixel) {
    const int r = static_cast<int>((pixel >> 16) & 0xFFU);
    const int g = static_cast<int>((pixel >> 8) & 0xFFU);
    const int b = static_cast<int>(pixel & 0xFFU);
    return ((77 * r) + (150 * g) + (29 * b) + 128) >> 8;
}

bool matchesRgb(std::uint32_t pixel, int matteRgb, int tolerance) {
    const int pr = static_cast<int>((pixel >> 16) & 0xFFU);
    const int pg = static_cast<int>((pixel >> 8) & 0xFFU);
    const int pb = static_cast<int>(pixel & 0xFFU);
    const int mr = (matteRgb >> 16) & 0xFF;
    const int mg = (matteRgb >> 8) & 0xFF;
    const int mb = matteRgb & 0xFF;
    return std::abs(pr - mr) <= tolerance &&
           std::abs(pg - mg) <= tolerance &&
           std::abs(pb - mb) <= tolerance;
}

bool isUniformPaletteIndex(const std::vector<std::uint8_t>& paletteIndices, int paletteIndex) {
    return std::all_of(paletteIndices.begin(), paletteIndices.end(), [&](std::uint8_t entry) {
        return static_cast<int>(entry & 0xFFU) == paletteIndex;
    });
}

bool edgeContainsOpaquePaletteIndex(const bitmap::Bitmap& src,
                                    const std::vector<std::uint8_t>& paletteIndices,
                                    int paletteIndex) {
    const int w = src.width();
    const int h = src.height();
    if (w <= 0 || h <= 0) {
        return false;
    }

    const auto isEdgeIndex = [&](int x, int y) {
        const auto index = static_cast<std::size_t>(y * w + x);
        return index < src.pixels().size() &&
               index < paletteIndices.size() &&
               ((src.pixels()[index] >> 24) & 0xFFU) != 0 &&
               static_cast<int>(paletteIndices[index] & 0xFFU) == paletteIndex;
    };

    for (int x = 0; x < w; ++x) {
        if (isEdgeIndex(x, 0) || isEdgeIndex(x, h - 1)) {
            return true;
        }
    }
    for (int y = 1; y < h - 1; ++y) {
        if (isEdgeIndex(0, y) || isEdgeIndex(w - 1, y)) {
            return true;
        }
    }
    return false;
}

int resolvePaletteIndexRgb(const bitmap::Bitmap& src,
                           const std::vector<std::uint8_t>& paletteIndices,
                           int paletteIndex) {
    const auto& pixels = src.pixels();
    for (std::size_t index = 0; index < pixels.size() && index < paletteIndices.size(); ++index) {
        if (static_cast<int>(paletteIndices[index] & 0xFFU) == paletteIndex) {
            return static_cast<int>(pixels[index] & 0x00FFFFFFU);
        }
    }
    return 0xFFFFFF;
}

bool hasOpaqueNonPaletteIndexContent(const bitmap::Bitmap& src,
                                     const std::vector<std::uint8_t>& paletteIndices,
                                     int paletteIndex) {
    const auto& pixels = src.pixels();
    for (std::size_t index = 0; index < pixels.size() && index < paletteIndices.size(); ++index) {
        if (((pixels[index] >> 24) & 0xFFU) != 0 &&
            static_cast<int>(paletteIndices[index] & 0xFFU) != paletteIndex) {
            return true;
        }
    }
    return false;
}

bool isNearWhiteGrayscale(int rgb, int minChannel, int maxDelta) {
    const int r = (rgb >> 16) & 0xFF;
    const int g = (rgb >> 8) & 0xFF;
    const int b = rgb & 0xFF;
    return r >= minChannel && g >= minChannel && b >= minChannel &&
           std::abs(r - g) <= maxDelta &&
           std::abs(g - b) <= maxDelta &&
           std::abs(r - b) <= maxDelta;
}

bool shouldKeyDefaultIndexedMatte(const bitmap::Bitmap& src,
                                  int backgroundKeyRgb,
                                  const std::vector<std::uint8_t>* paletteIndices) {
    if (src.bitDepth() > 8 || (backgroundKeyRgb & 0x00FFFFFF) != 0xFFFFFF ||
        paletteIndices == nullptr || paletteIndices->size() < src.pixels().size()) {
        return false;
    }
    if (isUniformPaletteIndex(*paletteIndices, 0)) {
        return false;
    }
    if (!edgeContainsOpaquePaletteIndex(src, *paletteIndices, 0)) {
        return false;
    }

    const int indexZeroRgb = resolvePaletteIndexRgb(src, *paletteIndices, 0);
    return isNearWhiteGrayscale(indexZeroRgb, 232, 16) &&
           hasOpaqueNonPaletteIndexContent(src, *paletteIndices, 0);
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

bool cornerContainsPaletteIndex(const std::vector<std::uint8_t>& paletteIndices,
                                int width,
                                int height,
                                int paletteIndex) {
    if (width <= 0 || height <= 0 || paletteIndices.empty()) {
        return false;
    }
    const std::size_t corners[] = {
        0U,
        static_cast<std::size_t>(std::max(0, width - 1)),
        static_cast<std::size_t>(std::max(0, height - 1) * width),
        static_cast<std::size_t>(std::max(0, height - 1) * width + std::max(0, width - 1))
    };
    for (const auto index : corners) {
        if (index < paletteIndices.size() &&
            static_cast<int>(paletteIndices[index] & 0xFFU) == paletteIndex) {
            return true;
        }
    }
    return false;
}

bool cornerContainsOpaqueRgb(const bitmap::Bitmap& src, int rgb) {
    const int width = src.width();
    const int height = src.height();
    if (width <= 0 || height <= 0) {
        return false;
    }
    const std::size_t corners[] = {
        0U,
        static_cast<std::size_t>(std::max(0, width - 1)),
        static_cast<std::size_t>(std::max(0, height - 1) * width),
        static_cast<std::size_t>(std::max(0, height - 1) * width + std::max(0, width - 1))
    };
    for (const auto index : corners) {
        if (index < src.pixels().size() && isOpaqueColor(src.pixels()[index], rgb & 0x00FFFFFF)) {
            return true;
        }
    }
    return false;
}

bool isDefaultIndexedMatteRgb(int rgb) {
    return (rgb & 0x00FFFFFF) == 0x000000 || (rgb & 0x00FFFFFF) == 0xFFFFFF;
}

bool indexMatchesRgb(const bitmap::Bitmap& src,
                     const std::vector<std::uint8_t>& paletteIndices,
                     const bitmap::Palette* palette,
                     int paletteIndex,
                     int rgb) {
    const int indexRgb = palette != nullptr && paletteIndex >= 0 && paletteIndex < palette->size()
        ? static_cast<int>(palette->getColor(paletteIndex) & 0x00FFFFFFU)
        : resolvePaletteIndexRgb(src, paletteIndices, paletteIndex);
    return indexRgb == (rgb & 0x00FFFFFF);
}

std::optional<int> explicitIndexedMatteIndex(const bitmap::Bitmap& src,
                                             const std::vector<std::uint8_t>& paletteIndices,
                                             int matteRgb,
                                             const bitmap::Palette* palette) {
    if (indexMatchesRgb(src, paletteIndices, palette, 0, matteRgb) &&
        edgeContainsOpaquePaletteIndex(src, paletteIndices, 0)) {
        return 0;
    }

    std::vector<int> counts(256, 0);
    int bestIndex = -1;
    int bestCount = 0;
    const int width = src.width();
    const int height = src.height();
    const auto countEdgeIndex = [&](int x, int y) {
        const auto index = static_cast<std::size_t>(y * width + x);
        if (index >= src.pixels().size() || index >= paletteIndices.size() ||
            ((src.pixels()[index] >> 24) & 0xFFU) == 0) {
            return;
        }
        const int paletteIndex = static_cast<int>(paletteIndices[index] & 0xFFU);
        if (!indexMatchesRgb(src, paletteIndices, palette, paletteIndex, matteRgb)) {
            return;
        }
        const int count = ++counts[static_cast<std::size_t>(paletteIndex)];
        if (count > bestCount) {
            bestCount = count;
            bestIndex = paletteIndex;
        }
    };

    for (int x = 0; x < width; ++x) {
        countEdgeIndex(x, 0);
        if (height > 1) {
            countEdgeIndex(x, height - 1);
        }
    }
    for (int y = 1; y < height - 1; ++y) {
        countEdgeIndex(0, y);
        if (width > 1) {
            countEdgeIndex(width - 1, y);
        }
    }
    return bestIndex >= 0 ? std::optional<int>(bestIndex) : std::nullopt;
}

std::optional<int> resolveExplicitIndexedMatteIndex(const bitmap::Bitmap& src,
                                                    int backColor,
                                                    const bitmap::Palette* palette) {
    if (src.bitDepth() > 8) {
        return std::nullopt;
    }
    const auto paletteIndices = src.paletteIndices();
    if (!paletteIndices.has_value() || paletteIndices->size() < src.pixels().size()) {
        return std::nullopt;
    }

    const auto imagePalette = src.imagePalette();
    const auto* effectivePalette = palette != nullptr ? palette : imagePalette.get();
    const int matteRgb = InkProcessor::resolveBackColorIgnoringAlpha(backColor, effectivePalette) & 0x00FFFFFF;
    if (matteRgb != 0xFFFFFF) {
        return std::nullopt;
    }

    const auto matteIndex = explicitIndexedMatteIndex(src, *paletteIndices, matteRgb, effectivePalette);
    if (!matteIndex.has_value() || isUniformPaletteIndex(*paletteIndices, *matteIndex)) {
        return std::nullopt;
    }
    if (!hasOpaqueNonPaletteIndexContent(src, *paletteIndices, *matteIndex)) {
        return std::nullopt;
    }
    return matteIndex;
}

std::optional<int> resolveDefaultIndexedFloodFillMatteIndex(const bitmap::Bitmap& src,
                                                            const std::vector<std::uint8_t>& paletteIndices) {
    if (paletteIndices.size() < src.pixels().size()) {
        return std::nullopt;
    }
    const int indexZeroRgb = resolvePaletteIndexRgb(src, paletteIndices, 0);
    if (!isDefaultIndexedMatteRgb(indexZeroRgb)) {
        return std::nullopt;
    }
    if (edgeContainsOpaquePaletteIndex(src, paletteIndices, 0) ||
        cornerContainsPaletteIndex(paletteIndices, src.width(), src.height(), 0)) {
        return 0;
    }
    return std::nullopt;
}

bitmap::Bitmap applyIndexedMatteInternal(const bitmap::Bitmap& src, int matteIndex, bool clearTransparentRgb) {
    const int width = src.width();
    const int height = src.height();
    const auto paletteIndices = src.paletteIndices();
    if (width <= 0 || height <= 0 || !paletteIndices.has_value() ||
        paletteIndices->size() < src.pixels().size()) {
        return src.copy();
    }

    std::vector<bool> transparent(static_cast<std::size_t>(width * height), false);
    std::queue<int> queue;
    const auto seed = [&](int x, int y) {
        const int index = y * width + x;
        const auto sizeIndex = static_cast<std::size_t>(index);
        if (transparent[sizeIndex]) {
            return;
        }
        const auto pixel = src.pixels()[sizeIndex];
        if (((pixel >> 24) & 0xFFU) == 0 ||
            static_cast<int>((*paletteIndices)[sizeIndex] & 0xFFU) == matteIndex) {
            transparent[sizeIndex] = true;
            queue.push(index);
        }
    };

    for (int x = 0; x < width; ++x) {
        seed(x, 0);
        seed(x, height - 1);
    }
    for (int y = 1; y < height - 1; ++y) {
        seed(0, y);
        seed(width - 1, y);
    }

    while (!queue.empty()) {
        const int index = queue.front();
        queue.pop();
        const int x = index % width;
        const int y = index / width;
        if (x > 0) seed(x - 1, y);
        if (x < width - 1) seed(x + 1, y);
        if (y > 0) seed(x, y - 1);
        if (y < height - 1) seed(x, y + 1);
    }

    std::vector<std::uint32_t> result = src.pixels();
    for (std::size_t index = 0; index < result.size() && index < transparent.size(); ++index) {
        if (transparent[index]) {
            result[index] = clearTransparentRgb ? 0x00000000U : (result[index] & 0x00FFFFFFU);
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

    const int touchedEdges = (top ? 1 : 0) + (bottom ? 1 : 0) + (left ? 1 : 0) + (right ? 1 : 0);
    return touchedEdges >= 3 ||
           (width == 1 && top && bottom) ||
           (height == 1 && left && right);
}

void seedOutlinedBackground(const std::vector<std::uint32_t>& pixels,
                            const std::vector<bool>& dilatedBarrier,
                            std::vector<bool>& outside,
                            std::queue<int>& queue,
                            int x,
                            int y,
                            int width) {
    const int index = y * width + x;
    const auto sizeIndex = static_cast<std::size_t>(index);
    if (outside[sizeIndex] || dilatedBarrier[sizeIndex] ||
        ((pixels[sizeIndex] >> 24) & 0xFFU) == 0) {
        return;
    }
    outside[sizeIndex] = true;
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
                                                                  int matteColorRgb,
                                                                  int tolerance,
                                                                  bool allowScriptBuilt32Bit,
                                                                  std::optional<int> mattePaletteIndex) {
    if ((matteColorRgb & 0x00FFFFFF) != 0xFFFFFF) {
        return std::nullopt;
    }

    const int bitDepth = src.bitDepth();
    const bool lowDepthAsset = bitDepth >= 1 && bitDepth <= 8 && !src.isScriptModified();
    const bool scriptBuilt32Bit = allowScriptBuilt32Bit &&
                                  bitDepth == 32 &&
                                  src.isScriptModified() &&
                                  !src.isNativeAlpha();
    if (!lowDepthAsset && !scriptBuilt32Bit) {
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
            if (!scriptBuilt32Bit) {
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

    const int nonWhite = dark + gray + colored;
    if (white == 0 || nonWhite == 0 || !outlineTouchesMultipleEdges(src)) {
        return std::nullopt;
    }
    if (scriptBuilt32Bit && white < nonWhite) {
        return std::nullopt;
    }

    const auto plain = mattePaletteIndex.has_value()
        ? applyIndexedMatteInternal(src, *mattePaletteIndex, true)
        : InkProcessor::applyMatte(src, matteColorRgb, tolerance);
    int remainingWhite = 0;
    for (const auto pixel : plain.pixels()) {
        if (((pixel >> 24) & 0xFFU) != 0 && (pixel & 0x00FFFFFFU) == 0xFFFFFFU) {
            ++remainingWhite;
        }
    }
    if (remainingWhite * 20 > white) {
        return std::nullopt;
    }

    return applyOutlinedWhiteBodyMatte(src);
}

int resolveMatteColorForInk(const bitmap::Bitmap& src,
                            id::InkMode ink,
                            int backColor,
                            bool useAlpha,
                            const bitmap::Palette* palette,
                            bool preserveScriptOutlinedWhiteBody) {
    if (src.hasNativeMatteAlpha() && useAlpha) {
        return -1;
    }
    if (preserveScriptOutlinedWhiteBody &&
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
    return InkProcessor::resolveBackColor(src, ink, backColor, useAlpha, palette);
}

bitmap::Bitmap applyInkInternal(const bitmap::Bitmap& src,
                                id::InkMode ink,
                                int backColor,
                                bool useAlpha,
                                const bitmap::Palette* palette,
                                bool preserveScriptOutlinedWhiteBody) {
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
        const auto explicitMatteIndex = resolveExplicitIndexedMatteIndex(input, backColor, palette);
        const auto paletteIndices = input.paletteIndices();
        const auto defaultMatteIndex =
            explicitMatteIndex.has_value() || !paletteIndices.has_value()
                ? std::optional<int>{}
                : resolveDefaultIndexedFloodFillMatteIndex(input, *paletteIndices);
        const auto mattePaletteIndex = explicitMatteIndex.has_value() ? explicitMatteIndex : defaultMatteIndex;
        const int matteColor = mattePaletteIndex.has_value() && paletteIndices.has_value()
            ? resolvePaletteIndexRgb(input, *paletteIndices, *mattePaletteIndex)
            : resolveMatteColorForInk(input, ink, backColor, useAlpha, palette, preserveScriptOutlinedWhiteBody);
        if (matteColor < 0) {
            return input.copy();
        }
        auto outlined = applyOutlinedWhiteBodyMatteIfNeeded(
            input, matteColor, 0, preserveScriptOutlinedWhiteBody, mattePaletteIndex);
        if (outlined.has_value()) {
            return std::move(*outlined);
        }
        if (mattePaletteIndex.has_value()) {
            return applyIndexedMatteInternal(input, *mattePaletteIndex, true);
        }
        return InkProcessor::applyMatte(input, matteColor);
    }

    if (ink == id::InkMode::MASK) {
        return InkProcessor::applyMask(input);
    }

    if (ink == id::InkMode::DARKEN || ink == id::InkMode::LIGHTEN) {
        bitmap::Bitmap masked = input.copy();
        if (!(input.bitDepth() == 32 && !useAlpha) && !input.isRectangularMedia()) {
            const int matteColor = InkProcessor::resolveBackColor(input, ink, backColor, useAlpha, palette);
            if (matteColor >= 0) {
                masked = input.bitDepth() >= 16
                    ? InkProcessor::applyBackgroundTransparent(input, matteColor)
                    : InkProcessor::applyMatte(input, matteColor);
            }
        }

        if (ink == id::InkMode::DARKEN) {
            const int tintRgb = InkProcessor::resolveBackColor(input, ink, backColor, false, palette);
            if (tintRgb >= 0 && tintRgb != 0xFFFFFF) {
                masked = InkProcessor::multiplyColor(masked, tintRgb);
            }
        }
        return masked;
    }

    const int bgColor = InkProcessor::resolveBackColor(input, ink, backColor, useAlpha, palette);
    if (bgColor < 0) {
        return input.copy();
    }
    return InkProcessor::applyBackgroundTransparent(input, bgColor);
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
                                      int backColor,
                                      bool useAlpha,
                                      const bitmap::Palette* palette) {
    return applyInkInternal(src, id::inkModeFromCode(ink), backColor, useAlpha, palette, false);
}

bitmap::Bitmap InkProcessor::applyInk(const bitmap::Bitmap& src,
                                      id::InkMode ink,
                                      int backColor,
                                      bool useAlpha,
                                      const bitmap::Palette* palette) {
    return applyInkInternal(src, ink, backColor, useAlpha, palette, false);
}

bitmap::Bitmap InkProcessor::applyInkPreservingOutlinedWhiteBody(const bitmap::Bitmap& src,
                                                                 int ink,
                                                                 int backColor,
                                                                 bool useAlpha,
                                                                 const bitmap::Palette* palette) {
    return applyInkInternal(src, id::inkModeFromCode(ink), backColor, useAlpha, palette, true);
}

bitmap::Bitmap InkProcessor::applyInkPreservingOutlinedWhiteBody(const bitmap::Bitmap& src,
                                                                 id::InkMode ink,
                                                                 int backColor,
                                                                 bool useAlpha,
                                                                 const bitmap::Palette* palette) {
    return applyInkInternal(src, ink, backColor, useAlpha, palette, true);
}

int InkProcessor::resolveBackColor(const bitmap::Bitmap& src,
                                   id::InkMode ink,
                                   int backColor,
                                   bool useAlpha,
                                   const bitmap::Palette* palette) {
    if (src.hasNativeMatteAlpha() && useAlpha) {
        const int alphaBackColor = resolveBackColorIgnoringAlpha(backColor, palette);
        if (ink == id::InkMode::BACKGROUND_TRANSPARENT && hasOpaqueBorderColor(src, alphaBackColor)) {
            return alphaBackColor;
        }
        return -1;
    }

    if (backColor > 255) {
        return backColor & 0x00FFFFFF;
    }

    if (src.bitDepth() == 32 && !useAlpha && ink != id::InkMode::COPY) {
        return 0xFFFFFF;
    }

    if (palette != nullptr && backColor >= 0 && backColor < palette->size()) {
        return static_cast<int>(palette->getColor(backColor) & 0x00FFFFFFU);
    }

    const int gray = 255 - backColor;
    return (gray << 16) | (gray << 8) | gray;
}

int InkProcessor::resolveBackColorIgnoringAlpha(int backColor, const bitmap::Palette* palette) {
    if (backColor > 255) {
        return backColor & 0x00FFFFFF;
    }
    if (palette != nullptr && backColor >= 0 && backColor < palette->size()) {
        return static_cast<int>(palette->getColor(backColor) & 0x00FFFFFFU);
    }

    const int gray = 255 - backColor;
    return (gray << 16) | (gray << 8) | gray;
}

bitmap::Bitmap InkProcessor::applyForeColorRemap(const bitmap::Bitmap& src,
                                                 std::uint32_t foreColor,
                                                 std::uint32_t backColor) {
    if (src.width() == 0 || src.height() == 0) {
        return src.copy();
    }

    const int fr = static_cast<int>((foreColor >> 16) & 0xFFU);
    const int fg = static_cast<int>((foreColor >> 8) & 0xFFU);
    const int fb = static_cast<int>(foreColor & 0xFFU);
    const int br = static_cast<int>((backColor >> 16) & 0xFFU);
    const int bg = static_cast<int>((backColor >> 8) & 0xFFU);
    const int bb = static_cast<int>(backColor & 0xFFU);

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
        result.push_back(packArgb(alpha, interpolate(t, fr, br), interpolate(t, fg, bg), interpolate(t, fb, bb)));
    }

    return derivedBitmap(src, std::move(result));
}

bitmap::Bitmap InkProcessor::applyIndexedColorRemap(const bitmap::Bitmap& indexedSource,
                                                    const bitmap::Bitmap& maskedSource,
                                                    std::uint32_t foreColor,
                                                    std::uint32_t backColor) {
    if (indexedSource.width() != maskedSource.width() || indexedSource.height() != maskedSource.height()) {
        return maskedSource.copy();
    }
    const auto paletteIndices = indexedSource.paletteIndices();
    if (!paletteIndices.has_value() || paletteIndices->size() != maskedSource.pixels().size()) {
        return maskedSource.copy();
    }

    const int fr = static_cast<int>((foreColor >> 16) & 0xFFU);
    const int fg = static_cast<int>((foreColor >> 8) & 0xFFU);
    const int fb = static_cast<int>(foreColor & 0xFFU);
    const int br = static_cast<int>((backColor >> 16) & 0xFFU);
    const int bg = static_cast<int>((backColor >> 8) & 0xFFU);
    const int bb = static_cast<int>(backColor & 0xFFU);

    std::vector<std::uint32_t> result;
    result.reserve(maskedSource.pixels().size());
    for (std::size_t i = 0; i < maskedSource.pixels().size(); ++i) {
        const auto pixel = maskedSource.pixels()[i];
        const int alpha = static_cast<int>((pixel >> 24) & 0xFFU);
        if (alpha == 0) {
            result.push_back(0);
            continue;
        }

        const int paletteIndex = static_cast<int>((*paletteIndices)[i] & 0xFFU);
        const float t = static_cast<float>(255 - paletteIndex) / 255.0F;
        result.push_back(packArgb(alpha, interpolate(t, fr, br), interpolate(t, fg, bg), interpolate(t, fb, bb)));
    }

    return derivedBitmap(maskedSource, std::move(result));
}

bitmap::Bitmap InkProcessor::applyDarkenForeColorOffset(const bitmap::Bitmap& src, std::uint32_t foreColor) {
    const int foreR = static_cast<int>((foreColor >> 16) & 0xFFU);
    const int foreG = static_cast<int>((foreColor >> 8) & 0xFFU);
    const int foreB = static_cast<int>(foreColor & 0xFFU);

    std::vector<std::uint32_t> result;
    result.reserve(src.pixels().size());
    for (const auto pixel : src.pixels()) {
        const int alpha = static_cast<int>((pixel >> 24) & 0xFFU);
        if (alpha == 0) {
            result.push_back(0);
            continue;
        }

        const int r = std::min(255, static_cast<int>((pixel >> 16) & 0xFFU) + foreR);
        const int g = std::min(255, static_cast<int>((pixel >> 8) & 0xFFU) + foreG);
        const int b = std::min(255, static_cast<int>(pixel & 0xFFU) + foreB);
        result.push_back(packArgb(alpha, r, g, b));
    }

    return derivedBitmap(src, std::move(result));
}

bitmap::Bitmap InkProcessor::remapExactColor(const bitmap::Bitmap& src,
                                             std::uint32_t fromRgb,
                                             std::uint32_t toRgb) {
    std::vector<std::uint32_t> result;
    result.reserve(src.pixels().size());
    for (const auto pixel : src.pixels()) {
        if ((pixel & 0x00FFFFFFU) == (fromRgb & 0x00FFFFFFU)) {
            result.push_back((pixel & 0xFF000000U) | (toRgb & 0x00FFFFFFU));
        } else {
            result.push_back(pixel);
        }
    }
    return derivedBitmap(src, std::move(result));
}

bitmap::Bitmap InkProcessor::applyBackgroundTransparent(const bitmap::Bitmap& src, int bgColorRgb) {
    std::vector<std::uint32_t> result;
    result.reserve(src.pixels().size());

    const auto paletteIndices = src.paletteIndices();
    const auto* paletteIndexPtr =
        (paletteIndices.has_value() && paletteIndices->size() >= src.pixels().size()) ? &*paletteIndices : nullptr;
    const bool keyDefaultIndexedMatte = shouldKeyDefaultIndexedMatte(src, bgColorRgb, paletteIndexPtr);

    for (std::size_t index = 0; index < src.pixels().size(); ++index) {
        const auto pixel = src.pixels()[index];
        const int alpha = static_cast<int>((pixel >> 24) & 0xFFU);
        if (alpha == 0) {
            result.push_back(0);
            continue;
        }

        const auto rgb = pixel & 0x00FFFFFFU;
        if (keyDefaultIndexedMatte &&
            paletteIndexPtr != nullptr &&
            index < paletteIndexPtr->size() &&
            static_cast<int>((*paletteIndexPtr)[index] & 0xFFU) == 0) {
            result.push_back(0);
        } else if (rgb == static_cast<std::uint32_t>(bgColorRgb & 0x00FFFFFF)) {
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
        const int srcAlpha = static_cast<int>((pixel >> 24) & 0xFFU);
        if (srcAlpha == 0) {
            result.push_back(0);
            continue;
        }

        const int maskAlpha = maskAlphaFromPixel(pixel);
        const int combinedAlpha = (srcAlpha * maskAlpha) / 255;
        result.push_back((static_cast<std::uint32_t>(combinedAlpha & 0xFF) << 24) |
                         (pixel & 0x00FFFFFFU));
    }
    return derivedBitmap(src, std::move(result));
}

bitmap::Bitmap InkProcessor::applyMatte(const bitmap::Bitmap& src, int matteColorRgb, int tolerance) {
    const int width = src.width();
    const int height = src.height();
    if (width <= 0 || height <= 0) {
        return src.copy();
    }

    std::vector<bool> transparent(static_cast<std::size_t>(width * height), false);
    std::queue<int> queue;
    const auto seed = [&](int x, int y) {
        const int index = y * width + x;
        if (transparent[static_cast<std::size_t>(index)]) {
            return;
        }
        const auto pixel = src.pixels()[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0 || matchesRgb(pixel, matteColorRgb, tolerance)) {
            transparent[static_cast<std::size_t>(index)] = true;
            queue.push(index);
        }
    };

    for (int x = 0; x < width; ++x) {
        seed(x, 0);
        seed(x, height - 1);
    }
    for (int y = 1; y < height - 1; ++y) {
        seed(0, y);
        seed(width - 1, y);
    }

    while (!queue.empty()) {
        const int index = queue.front();
        queue.pop();
        const int x = index % width;
        const int y = index / width;
        if (x > 0) seed(x - 1, y);
        if (x < width - 1) seed(x + 1, y);
        if (y > 0) seed(x, y - 1);
        if (y < height - 1) seed(x, y + 1);
    }

    std::vector<std::uint32_t> result = src.pixels();
    for (std::size_t index = 0; index < result.size() && index < transparent.size(); ++index) {
        if (transparent[index]) {
            result[index] = 0;
        }
    }
    return derivedBitmap(src, std::move(result));
}

bitmap::Bitmap InkProcessor::applyFloodFillTransparency(const bitmap::Bitmap& src) {
    const auto paletteIndices = src.paletteIndices();
    if (!paletteIndices.has_value()) {
        return src.copy();
    }
    const auto matteIndex = resolveDefaultIndexedFloodFillMatteIndex(src, *paletteIndices);
    if (!matteIndex.has_value()) {
        return src.copy();
    }
    return applyIndexedMatteInternal(src, *matteIndex, false);
}

bitmap::Bitmap InkProcessor::convertOpaqueWhiteToTransparent(const bitmap::Bitmap& src) {
    bool changed = false;
    std::vector<std::uint32_t> result;
    result.reserve(src.pixels().size());
    for (const auto pixel : src.pixels()) {
        if (pixel == 0xFFFFFFFFU) {
            result.push_back(0x00FFFFFFU);
            changed = true;
        } else {
            result.push_back(pixel);
        }
    }
    return changed ? derivedBitmap(src, std::move(result)) : src.copy();
}

bitmap::Bitmap InkProcessor::multiplyColor(const bitmap::Bitmap& src, int tintRgb) {
    const int tintR = (tintRgb >> 16) & 0xFF;
    const int tintG = (tintRgb >> 8) & 0xFF;
    const int tintB = tintRgb & 0xFF;

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
                                  multiplyDirectorChannel(r, tintR),
                                  multiplyDirectorChannel(g, tintG),
                                  multiplyDirectorChannel(b, tintB)));
    }
    return derivedBitmap(src, std::move(result));
}

} // namespace libreshockwave::player::render::pipeline
