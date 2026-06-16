#include "libreshockwave/bitmap/Drawing.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

#include "libreshockwave/bitmap/Bitmap.hpp"

namespace libreshockwave::bitmap::Drawing {
namespace {

struct FloodFillMatte {
    std::optional<int> paletteIndex;
    int colorRgb = 0xFFFFFF;
    int tolerance = 0;

    [[nodiscard]] bool usesPaletteIndex() const { return paletteIndex.has_value(); }
};

int maskAlphaFromPixel(std::uint32_t pixel) {
    const int r = static_cast<int>((pixel >> 16) & 0xFFU);
    const int g = static_cast<int>((pixel >> 8) & 0xFFU);
    const int b = static_cast<int>(pixel & 0xFFU);
    return ((77 * r) + (150 * g) + (29 * b) + 128) >> 8;
}

std::uint32_t packOpaqueRgb(int r, int g, int b) {
    return 0xFF000000U |
           (static_cast<std::uint32_t>(r & 0xFF) << 16) |
           (static_cast<std::uint32_t>(g & 0xFF) << 8) |
           static_cast<std::uint32_t>(b & 0xFF);
}

std::vector<int> cornerIndices(int width, int height) {
    return {
        0,
        std::max(0, width - 1),
        std::max(0, (height - 1) * width),
        std::max(0, (height - 1) * width + (width - 1)),
    };
}

std::vector<int> edgeIndices(int width, int height) {
    std::vector<int> indices;
    indices.reserve(static_cast<std::size_t>(std::max(1, (width * 2) + std::max(0, height - 2) * 2)));
    for (int x = 0; x < width; ++x) {
        indices.push_back(x);
        if (height > 1) {
            indices.push_back((height - 1) * width + x);
        }
    }
    for (int y = 1; y < height - 1; ++y) {
        indices.push_back(y * width);
        if (width > 1) {
            indices.push_back(y * width + (width - 1));
        }
    }
    return indices;
}

bool hasPaletteIndices(const std::optional<std::vector<std::uint8_t>>& paletteIndices, int width, int height) {
    return paletteIndices.has_value() && paletteIndices->size() >= static_cast<std::size_t>(width * height);
}

bool isUniformPaletteIndex(const std::vector<std::uint8_t>& paletteIndices, int paletteIndex) {
    return std::all_of(paletteIndices.begin(), paletteIndices.end(), [&](std::uint8_t entry) {
        return static_cast<int>(entry) == paletteIndex;
    });
}

int resolvePaletteIndexRgb(const std::vector<std::uint32_t>& pixels,
                           const std::vector<std::uint8_t>& paletteIndices,
                           int paletteIndex) {
    for (std::size_t index = 0; index < pixels.size() && index < paletteIndices.size(); ++index) {
        if (static_cast<int>(paletteIndices[index]) == paletteIndex) {
            return static_cast<int>(pixels[index] & 0x00FFFFFFU);
        }
    }
    return 0xFFFFFF;
}

bool cornerContainsPaletteIndex(const std::vector<std::uint8_t>& paletteIndices,
                                int width,
                                int height,
                                int paletteIndex) {
    for (const int index : cornerIndices(width, height)) {
        if (index >= 0 && static_cast<std::size_t>(index) < paletteIndices.size() &&
            static_cast<int>(paletteIndices[static_cast<std::size_t>(index)]) == paletteIndex) {
            return true;
        }
    }
    return false;
}

bool defaultIndexedMatteRgb(int rgb) {
    return rgb == 0x000000 || rgb == 0xFFFFFF;
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

std::optional<int> inferDominantEdgePaletteIndex(const std::vector<std::uint32_t>& pixels,
                                                 const std::vector<std::uint8_t>& paletteIndices,
                                                 int width,
                                                 int height) {
    if (width <= 0 || height <= 0) return std::nullopt;

    std::array<int, 256> counts{};
    int opaqueEdgeCount = 0;
    int dominantIndex = -1;
    int dominantCount = 0;
    for (const int index : edgeIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        const int paletteIndex = static_cast<int>(paletteIndices[static_cast<std::size_t>(index)]);
        const int count = ++counts[static_cast<std::size_t>(paletteIndex)];
        ++opaqueEdgeCount;
        if (count > dominantCount) {
            dominantCount = count;
            dominantIndex = paletteIndex;
        }
    }
    if (opaqueEdgeCount == 0 || dominantIndex < 0 || isUniformPaletteIndex(paletteIndices, dominantIndex)) {
        return std::nullopt;
    }

    int opaqueCornerCount = 0;
    for (const int index : cornerIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        ++opaqueCornerCount;
        if (static_cast<int>(paletteIndices[static_cast<std::size_t>(index)]) != dominantIndex) {
            return std::nullopt;
        }
    }
    if (opaqueCornerCount == 0 || dominantCount * 4 < opaqueEdgeCount * 3) {
        return std::nullopt;
    }
    return dominantIndex;
}

std::optional<FloodFillMatte> resolveIndexedFloodFillMatte(
    const std::vector<std::uint32_t>& pixels,
    const std::vector<std::uint8_t>& paletteIndices,
    int width,
    int height) {
    if (const auto dominant = inferDominantEdgePaletteIndex(pixels, paletteIndices, width, height)) {
        const int matteRgb = resolvePaletteIndexRgb(pixels, paletteIndices, *dominant);
        if (*dominant == 0 && defaultIndexedMatteRgb(matteRgb)) {
            return FloodFillMatte{*dominant, matteRgb, 0};
        }
    }
    if (cornerContainsPaletteIndex(paletteIndices, width, height, 0)) {
        const int indexZeroRgb = resolvePaletteIndexRgb(pixels, paletteIndices, 0);
        if (defaultIndexedMatteRgb(indexZeroRgb)) {
            return FloodFillMatte{0, indexZeroRgb, 0};
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
    int opaqueEdgeCount = 0;
    int dominantRgb = -1;
    int dominantCount = 0;
    for (const int index : edgeIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        const int rgb = static_cast<int>(pixel & 0x00FFFFFFU);
        const int count = ++counts[rgb];
        ++opaqueEdgeCount;
        if (count > dominantCount) {
            dominantCount = count;
            dominantRgb = rgb;
        }
    }
    if (opaqueEdgeCount == 0 || dominantRgb < 0 || isUniformRgb(pixels, dominantRgb)) {
        return std::nullopt;
    }

    int opaqueCornerCount = 0;
    for (const int index : cornerIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        ++opaqueCornerCount;
        if (static_cast<int>(pixel & 0x00FFFFFFU) != dominantRgb) {
            return std::nullopt;
        }
    }
    if (opaqueCornerCount == 0 || dominantCount * 4 < opaqueEdgeCount * 3) {
        return std::nullopt;
    }
    return dominantRgb;
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

std::optional<FloodFillMatte> resolveRgbFloodFillMatte(const std::vector<std::uint32_t>& pixels,
                                                       int width,
                                                       int height) {
    if (const auto dominant = inferDominantEdgeRgb(pixels, width, height)) {
        return FloodFillMatte{std::nullopt, *dominant, 0};
    }
    if (!cornerContainsOpaqueRgb(pixels, width, height, 0xFFFFFF)) {
        return std::nullopt;
    }
    return FloodFillMatte{std::nullopt, 0xFFFFFF, 0};
}

std::optional<FloodFillMatte> resolveFloodFillMatte(
    const std::vector<std::uint32_t>& pixels,
    const std::optional<std::vector<std::uint8_t>>& paletteIndices,
    int width,
    int height) {
    if (hasPaletteIndices(paletteIndices, width, height)) {
        return resolveIndexedFloodFillMatte(pixels, *paletteIndices, width, height);
    }
    return resolveRgbFloodFillMatte(pixels, width, height);
}

bool cornersAreNearWhite(const std::vector<std::uint32_t>& pixels,
                         int width,
                         int height,
                         int minChannel,
                         int maxDelta) {
    bool sawOpaqueCorner = false;
    for (const int index : cornerIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        sawOpaqueCorner = true;
        if (!isNearWhiteGrayscale(static_cast<int>(pixel & 0x00FFFFFFU), minChannel, maxDelta)) {
            return false;
        }
    }
    return sawOpaqueCorner;
}

bool hasOpaqueNonNearWhiteContent(const std::vector<std::uint32_t>& pixels,
                                  const std::optional<std::vector<std::uint8_t>>& paletteIndices,
                                  int width,
                                  int height,
                                  int minChannel,
                                  int maxDelta) {
    int contentPixels = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto index = static_cast<std::size_t>(y * width + x);
            const auto pixel = pixels[index];
            if (((pixel >> 24) & 0xFFU) == 0) {
                continue;
            }
            int rgb = static_cast<int>(pixel & 0x00FFFFFFU);
            if (paletteIndices.has_value() && index < paletteIndices->size()) {
                rgb = resolvePaletteIndexRgb(pixels, *paletteIndices, static_cast<int>((*paletteIndices)[index]));
            }
            if (!isNearWhiteGrayscale(rgb, minChannel, maxDelta)) {
                ++contentPixels;
                if (contentPixels >= 8) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool edgeContainsOpaquePaletteIndex(const std::vector<std::uint32_t>& pixels,
                                    const std::vector<std::uint8_t>& paletteIndices,
                                    int width,
                                    int height,
                                    int paletteIndex) {
    for (const int index : edgeIndices(width, height)) {
        const auto offset = static_cast<std::size_t>(index);
        if (((pixels[offset] >> 24) & 0xFFU) != 0 &&
            offset < paletteIndices.size() &&
            static_cast<int>(paletteIndices[offset]) == paletteIndex) {
            return true;
        }
    }
    return false;
}

bool hasOpaqueNonPaletteIndexContent(const std::vector<std::uint32_t>& pixels,
                                     const std::vector<std::uint8_t>& paletteIndices,
                                     int paletteIndex) {
    for (std::size_t index = 0; index < pixels.size() && index < paletteIndices.size(); ++index) {
        if (((pixels[index] >> 24) & 0xFFU) != 0 &&
            static_cast<int>(paletteIndices[index]) != paletteIndex) {
            return true;
        }
    }
    return false;
}

std::optional<FloodFillMatte> resolveBackgroundTransparentMatte(
    const std::vector<std::uint32_t>& pixels,
    const std::optional<std::vector<std::uint8_t>>& paletteIndices,
    int width,
    int height) {
    if (hasPaletteIndices(paletteIndices, width, height)) {
        const auto matteIndex = inferDominantEdgePaletteIndex(pixels, *paletteIndices, width, height);
        if (!matteIndex.has_value()) {
            return std::nullopt;
        }
        const int matteRgb = resolvePaletteIndexRgb(pixels, *paletteIndices, *matteIndex);
        if (!isNearWhiteGrayscale(matteRgb, 232, 16) ||
            !hasOpaqueNonNearWhiteContent(pixels, paletteIndices, width, height, 232, 16)) {
            return std::nullopt;
        }
        return FloodFillMatte{*matteIndex, matteRgb, 0};
    }

    if (!cornersAreNearWhite(pixels, width, height, 232, 16)) {
        return std::nullopt;
    }

    int opaqueEdgeCount = 0;
    int nearWhiteEdgeCount = 0;
    for (const int index : edgeIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        ++opaqueEdgeCount;
        if (isNearWhiteGrayscale(static_cast<int>(pixel & 0x00FFFFFFU), 232, 16)) {
            ++nearWhiteEdgeCount;
        }
    }
    if (opaqueEdgeCount == 0 || nearWhiteEdgeCount * 4 < opaqueEdgeCount * 3) {
        return std::nullopt;
    }
    if (!hasOpaqueNonNearWhiteContent(pixels, std::nullopt, width, height, 232, 16)) {
        return std::nullopt;
    }
    return FloodFillMatte{std::nullopt, 0xFFFFFF, 24};
}

bool matchesRgb(std::uint32_t pixel, int matteRgb, int tolerance) {
    const int pr = static_cast<int>((pixel >> 16) & 0xFFU);
    const int pg = static_cast<int>((pixel >> 8) & 0xFFU);
    const int pb = static_cast<int>(pixel & 0xFFU);
    const int mr = (matteRgb >> 16) & 0xFF;
    const int mg = (matteRgb >> 8) & 0xFF;
    const int mb = matteRgb & 0xFF;
    return std::abs(pr - mr) <= tolerance && std::abs(pg - mg) <= tolerance && std::abs(pb - mb) <= tolerance;
}

bool isTransparentOrMatte(const std::vector<std::uint32_t>& pixels,
                          const std::optional<std::vector<std::uint8_t>>& paletteIndices,
                          int index,
                          const FloodFillMatte& matte) {
    const auto pixel = pixels[static_cast<std::size_t>(index)];
    if (((pixel >> 24) & 0xFFU) == 0) {
        return true;
    }
    if (matte.usesPaletteIndex() && paletteIndices.has_value() &&
        static_cast<std::size_t>(index) < paletteIndices->size()) {
        return static_cast<int>((*paletteIndices)[static_cast<std::size_t>(index)]) == *matte.paletteIndex;
    }
    return matchesRgb(pixel, matte.colorRgb, matte.tolerance);
}

std::vector<bool> computeFloodFillTransparency(
    const std::vector<std::uint32_t>& pixels,
    const std::optional<std::vector<std::uint8_t>>& paletteIndices,
    int width,
    int height,
    const FloodFillMatte& matte) {
    std::vector<bool> transparent(static_cast<std::size_t>(width * height), false);
    std::queue<int> queue;
    const auto seed = [&](int x, int y) {
        const int index = y * width + x;
        if (!transparent[static_cast<std::size_t>(index)] &&
            isTransparentOrMatte(pixels, paletteIndices, index, matte)) {
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
    return transparent;
}

std::shared_ptr<Bitmap> createAlphaMatte(const Bitmap& src, int alphaThreshold) {
    const int width = src.width();
    const int height = src.height();
    const int threshold = std::clamp(alphaThreshold, 0, 255);
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
    const auto paletteIndices = src.paletteIndices();
    const auto matte = resolveFloodFillMatte(pixels, paletteIndices, width, height);
    const auto transparent = matte
        ? computeFloodFillTransparency(pixels, paletteIndices, width, height, *matte)
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

    auto matteBitmap = std::make_shared<Bitmap>(width, height, 32, std::move(mask));
    matteBitmap->setNativeAlpha(true);
    return matteBitmap;
}

bool isGrayscaleMaskSource(const std::vector<std::uint32_t>& pixels, const std::vector<bool>& transparent) {
    int opaquePixels = 0;
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
        ++opaquePixels;
    }
    return opaquePixels > 0 && opaquePixels * 4 <= static_cast<int>(pixels.size()) * 3;
}

bool isWhiteBackedMaskSource(const std::vector<std::uint32_t>& pixels,
                             const std::vector<bool>& transparent,
                             const FloodFillMatte& matte) {
    if (maskAlphaFromPixel(0xFF000000U | static_cast<std::uint32_t>(matte.colorRgb & 0x00FFFFFF)) < 250) {
        return false;
    }

    bool hasTransparentMatte = false;
    bool hasOpaqueInk = false;
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        if (((pixels[index] >> 24) & 0xFFU) == 0) {
            continue;
        }
        if (transparent[index]) {
            hasTransparentMatte = true;
        } else {
            hasOpaqueInk = true;
        }
        if (hasTransparentMatte && hasOpaqueInk) {
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
                          int destX,
                          int destY,
                          int srcX,
                          int srcY,
                          int width,
                          int height) {
    if (src.width() <= 0 || src.height() <= 0 ||
        width <= 0 || height <= 0 ||
        !isMostlyWhiteRegion(dest, destX, destY, width, height)) {
        return false;
    }

    const auto pixels = src.pixels();
    const auto paletteIndices = src.paletteIndices();
    const auto matte = resolveFloodFillMatte(pixels, paletteIndices, src.width(), src.height());
    if (!matte.has_value()) {
        return false;
    }
    const auto transparent =
        computeFloodFillTransparency(pixels, paletteIndices, src.width(), src.height(), *matte);
    if (!isMaskSource(pixels, transparent, *matte)) {
        return false;
    }

    const int matteLuma = maskAlphaFromPixel(0xFF000000U | static_cast<std::uint32_t>(matte->colorRgb & 0x00FFFFFF));
    const bool lightMatte = matteLuma >= 128;
    for (int y = 0; y < height; ++y) {
        const int sy = srcY + y;
        const int dy = destY + y;
        if (sy < 0 || sy >= src.height() || dy < 0 || dy >= dest.height()) {
            continue;
        }
        for (int x = 0; x < width; ++x) {
            const int sx = srcX + x;
            const int dx = destX + x;
            if (sx < 0 || sx >= src.width() || dx < 0 || dx >= dest.width()) {
                continue;
            }
            const auto srcOffset = static_cast<std::size_t>(sy * src.width() + sx);
            if (srcOffset >= transparent.size() || transparent[srcOffset]) {
                continue;
            }
            int maskLuma = maskAlphaFromPixel(pixels[srcOffset]);
            if (!lightMatte) {
                maskLuma = 255 - maskLuma;
            }
            const auto luma = static_cast<std::uint32_t>(maskLuma & 0xFF);
            dest.setPixelPreservePaletteIndex(dx, dy, 0xFF000000U | (luma << 16) | (luma << 8) | luma);
        }
    }
    return true;
}

std::shared_ptr<Bitmap> applyBackgroundTransparentToRegion(const Bitmap& src,
                                                           int srcX,
                                                           int srcY,
                                                           int width,
                                                           int height,
                                                           int backgroundKeyRgb) {
    if (width <= 0 ||
        height <= 0 ||
        (backgroundKeyRgb & 0x00FFFFFF) != 0xFFFFFF ||
        src.hasNativeMatteAlpha()) {
        return nullptr;
    }

    auto result = std::make_shared<Bitmap>(src.getRegion(srcX, srcY, width, height));
    const auto pixels = result->pixels();
    const auto paletteIndices = result->paletteIndices();
    const auto matte = resolveBackgroundTransparentMatte(pixels, paletteIndices, width, height);
    if (!matte.has_value()) {
        return nullptr;
    }

    const auto transparent = computeFloodFillTransparency(pixels, paletteIndices, width, height, *matte);
    bool changed = false;
    auto& resultPixels = result->pixels();
    for (std::size_t index = 0; index < resultPixels.size() && index < transparent.size(); ++index) {
        if (!transparent[index]) {
            continue;
        }
        resultPixels[index] &= 0x00FFFFFFU;
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

bool shouldKeyNearWhiteMatte(const Bitmap& src, int srcX, int srcY, int width, int height, int backgroundKeyRgb) {
    if (src.bitDepth() < 32 ||
        !src.hasTransparentPixels() ||
        (backgroundKeyRgb & 0x00FFFFFF) != 0xFFFFFF ||
        width <= 0 ||
        height <= 0 ||
        src.width() <= 0 ||
        src.height() <= 0) {
        return false;
    }

    const int maxX = src.width() - 1;
    const int maxY = src.height() - 1;
    const int left = std::clamp(srcX, 0, maxX);
    const int top = std::clamp(srcY, 0, maxY);
    const int right = std::clamp(srcX + width - 1, 0, maxX);
    const int bottom = std::clamp(srcY + height - 1, 0, maxY);
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
                                  int backgroundKeyRgb,
                                  const std::optional<std::vector<std::uint8_t>>& paletteIndices) {
    if (src.bitDepth() > 8 ||
        (backgroundKeyRgb & 0x00FFFFFF) != 0xFFFFFF ||
        !hasPaletteIndices(paletteIndices, src.width(), src.height()) ||
        isUniformPaletteIndex(*paletteIndices, 0)) {
        return false;
    }

    const auto& pixels = src.pixels();
    if (!edgeContainsOpaquePaletteIndex(pixels, *paletteIndices, src.width(), src.height(), 0)) {
        return false;
    }

    const int indexZeroRgb = resolvePaletteIndexRgb(pixels, *paletteIndices, 0);
    return isNearWhiteGrayscale(indexZeroRgb, 232, 16) &&
           hasOpaqueNonPaletteIndexContent(pixels, *paletteIndices, 0);
}

std::shared_ptr<Bitmap> createDirectMask(const Bitmap& src,
                                         const FloodFillMatte& matte,
                                         int alphaThreshold) {
    const int width = src.width();
    const int height = src.height();
    const int threshold = std::clamp(alphaThreshold, 0, 255);
    const auto pixels = src.pixels();
    const int matteLuma = maskAlphaFromPixel(0xFF000000U | static_cast<std::uint32_t>(matte.colorRgb & 0x00FFFFFF));
    const bool lightMatte = matteLuma >= 128;

    std::vector<std::uint32_t> mask(static_cast<std::size_t>(width * height), 0xFFFFFFFFU);
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        const auto pixel = pixels[index];
        if (((pixel >> 24) & 0xFFU) == 0) {
            mask[index] = 0xFFFFFFFFU;
            continue;
        }
        int maskLuma = maskAlphaFromPixel(pixel);
        if (!lightMatte) {
            maskLuma = 255 - maskLuma;
        }
        const int opacity = 255 - maskLuma;
        if (opacity < threshold) {
            maskLuma = 255;
        }
        const auto luma = static_cast<std::uint32_t>(maskLuma & 0xFF);
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
    const int rxSq = rx * rx;
    const int rySq = ry * ry;
    const int radiusProduct = rxSq * rySq;
    for (int y = -ry; y <= ry; ++y) {
        for (int x = -rx; x <= rx; ++x) {
            if ((x * x * rySq) + (y * y * rxSq) <= radiusProduct) {
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
    const int rxSq = rx * rx;
    const int rySq = ry * ry;
    int p = static_cast<int>(static_cast<double>(rySq) -
                             static_cast<double>(rxSq * ry) +
                             0.25 * static_cast<double>(rxSq));

    while (rySq * x < rxSq * y) {
        dest.setPixel(cx + x, cy + y, argb);
        dest.setPixel(cx - x, cy + y, argb);
        dest.setPixel(cx + x, cy - y, argb);
        dest.setPixel(cx - x, cy - y, argb);
        if (p < 0) {
            ++x;
            p += (2 * rySq * x) + rySq;
        } else {
            ++x;
            --y;
            p += (2 * rySq * x) - (2 * rxSq * y) + rySq;
        }
    }

    p = static_cast<int>(static_cast<double>(rySq) * (static_cast<double>(x) + 0.5) *
                             (static_cast<double>(x) + 0.5) +
                         static_cast<double>(rxSq) * (y - 1) * (y - 1) -
                         static_cast<double>(rxSq * rySq));
    while (y >= 0) {
        dest.setPixel(cx + x, cy + y, argb);
        dest.setPixel(cx - x, cy + y, argb);
        dest.setPixel(cx + x, cy - y, argb);
        dest.setPixel(cx - x, cy - y, argb);
        if (p > 0) {
            --y;
            p -= (2 * rxSq * y) + rxSq;
        } else {
            --y;
            ++x;
            p += (2 * rySq * x) - (2 * rxSq * y) + rxSq;
        }
    }
}

int combineAlpha(int srcAlpha, int blendAlpha) {
    if (srcAlpha <= 0 || blendAlpha <= 0) return 0;
    if (srcAlpha >= 255) return blendAlpha;
    if (blendAlpha >= 255) return srcAlpha;
    return (srcAlpha * blendAlpha) / 255;
}

std::uint32_t alphaBlend(std::uint32_t fg, std::uint32_t bg, int alpha) {
    if (alpha <= 0) return bg;
    if (alpha >= 255) return fg;

    const int fgR = static_cast<int>((fg >> 16) & 0xFFU);
    const int fgG = static_cast<int>((fg >> 8) & 0xFFU);
    const int fgB = static_cast<int>(fg & 0xFFU);
    const int bgR = static_cast<int>((bg >> 16) & 0xFFU);
    const int bgG = static_cast<int>((bg >> 8) & 0xFFU);
    const int bgB = static_cast<int>(bg & 0xFFU);
    const int invAlpha = 255 - alpha;
    const int r = (fgR * alpha + bgR * invAlpha) / 255;
    const int g = (fgG * alpha + bgG * invAlpha) / 255;
    const int b = (fgB * alpha + bgB * invAlpha) / 255;
    return packOpaqueRgb(r, g, b);
}

std::uint32_t applyInk(std::uint32_t src,
                       std::uint32_t dest,
                       id::InkMode ink,
                       int blend,
                       int backgroundKeyRgb) {
    const int srcAlpha = static_cast<int>((src >> 24) & 0xFFU);
    const int srcR = static_cast<int>((src >> 16) & 0xFFU);
    const int srcG = static_cast<int>((src >> 8) & 0xFFU);
    const int srcB = static_cast<int>(src & 0xFFU);
    const int destR = static_cast<int>((dest >> 16) & 0xFFU);
    const int destG = static_cast<int>((dest >> 8) & 0xFFU);
    const int destB = static_cast<int>(dest & 0xFFU);
    const int srcRgb = static_cast<int>(src & 0x00FFFFFFU);

    if (ink == id::InkMode::TRANSPARENT) {
        return srcRgb == 0xFFFFFF ? dest : src;
    }
    if (ink == id::InkMode::REVERSE) {
        return packOpaqueRgb(destR ^ srcR, destG ^ srcG, destB ^ srcB);
    }
    if (ink == id::InkMode::GHOST) {
        return packOpaqueRgb((srcR + destR) / 2, (srcG + destG) / 2, (srcB + destB) / 2);
    }
    if (ink == id::InkMode::NOT_COPY) {
        return packOpaqueRgb(255 - srcR, 255 - srcG, 255 - srcB);
    }
    if (ink == id::InkMode::NOT_TRANSPARENT) {
        return srcRgb == 0 ? dest : packOpaqueRgb(255 - srcR, 255 - srcG, 255 - srcB);
    }
    if (ink == id::InkMode::NOT_REVERSE) {
        return packOpaqueRgb(destR ^ (255 - srcR), destG ^ (255 - srcG), destB ^ (255 - srcB));
    }
    if (ink == id::InkMode::NOT_GHOST) {
        return packOpaqueRgb(((255 - srcR) + destR) / 2,
                             ((255 - srcG) + destG) / 2,
                             ((255 - srcB) + destB) / 2);
    }
    if (ink == id::InkMode::MATTE) {
        if (srcAlpha == 0) return dest;
        if (blend < 255) {
            const int matteAlpha = (srcAlpha * blend) / 255;
            return matteAlpha == 0 ? dest : alphaBlend(src, dest, matteAlpha);
        }
        return alphaBlend(src, dest, srcAlpha);
    }
    if (ink == id::InkMode::MASK) {
        const int alpha = combineAlpha(srcAlpha, maskAlphaFromPixel(src));
        return alpha == 0 ? dest : alphaBlend(src, dest, alpha);
    }
    if (ink == id::InkMode::BACKGROUND_TRANSPARENT) {
        if (srcAlpha == 0 || srcRgb == (backgroundKeyRgb & 0x00FFFFFF)) {
            return dest;
        }
        if (blend < 255 || srcAlpha < 255) {
            return alphaBlend(src, dest, combineAlpha(srcAlpha, blend));
        }
        return src;
    }
    if (ink == id::InkMode::BLEND) {
        return alphaBlend(src, dest, combineAlpha(srcAlpha, blend));
    }
    if (ink == id::InkMode::ADD_PIN) {
        return packOpaqueRgb(std::min(255, srcR + destR), std::min(255, srcG + destG), std::min(255, srcB + destB));
    }
    if (ink == id::InkMode::ADD) {
        return packOpaqueRgb(srcR + destR, srcG + destG, srcB + destB);
    }
    if (ink == id::InkMode::SUBTRACT_PIN) {
        return packOpaqueRgb(std::max(0, destR - srcR), std::max(0, destG - srcG), std::max(0, destB - srcB));
    }
    if (ink == id::InkMode::SUBTRACT) {
        return packOpaqueRgb(destR - srcR, destG - srcG, destB - srcB);
    }
    if (ink == id::InkMode::LIGHTEST) {
        return srcAlpha == 0 ? dest : packOpaqueRgb(std::max(srcR, destR), std::max(srcG, destG), std::max(srcB, destB));
    }
    if (ink == id::InkMode::DARKEST) {
        return srcAlpha == 0 ? dest : packOpaqueRgb(std::min(srcR, destR), std::min(srcG, destG), std::min(srcB, destB));
    }
    if (ink == id::InkMode::LIGHTEN || ink == id::InkMode::DARKEN) {
        return srcAlpha == 0 ? dest : alphaBlend(src, dest, combineAlpha(srcAlpha, blend));
    }

    if (blend < 255) {
        return alphaBlend(src, dest, combineAlpha(srcAlpha, blend));
    }
    if (srcAlpha == 0) {
        return dest;
    }
    if (srcAlpha < 255) {
        return alphaBlend(src, dest, srcAlpha);
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

std::shared_ptr<Bitmap> preprocessBackgroundTransparent(const Bitmap& src, int backgroundKeyRgb) {
    return applyBackgroundTransparentToRegion(src, 0, 0, src.width(), src.height(), backgroundKeyRgb);
}

void copyPixels(Bitmap& dest,
                const Bitmap& src,
                int destX,
                int destY,
                int srcX,
                int srcY,
                int width,
                int height,
                id::InkMode ink,
                int blend,
                const Bitmap* mask,
                int backgroundKeyRgb) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (ink == id::InkMode::MATTE &&
        dest.bitDepth() <= 8 &&
        copyMatteToMaskImage(dest, src, destX, destY, srcX, srcY, width, height)) {
        return;
    }

    std::shared_ptr<Bitmap> matteSource;
    std::shared_ptr<Bitmap> backgroundTransparentSource;
    const Bitmap* effectiveSource = &src;
    int effectiveSrcX = srcX;
    int effectiveSrcY = srcY;
    if (ink == id::InkMode::MATTE) {
        matteSource = std::make_shared<Bitmap>(applyFloodFillTransparency(src));
        effectiveSource = matteSource.get();
    } else if (ink == id::InkMode::BACKGROUND_TRANSPARENT) {
        backgroundTransparentSource = preprocessBackgroundTransparent(src, backgroundKeyRgb);
        if (backgroundTransparentSource != nullptr) {
            effectiveSource = backgroundTransparentSource.get();
        }
    }

    const auto effectivePaletteIndices = effectiveSource->paletteIndices();
    const bool keyNearWhiteMatte =
        ink == id::InkMode::BACKGROUND_TRANSPARENT &&
        shouldKeyNearWhiteMatte(*effectiveSource, effectiveSrcX, effectiveSrcY, width, height, backgroundKeyRgb);
    const bool keyDefaultIndexedMatte =
        ink == id::InkMode::BACKGROUND_TRANSPARENT &&
        shouldKeyDefaultIndexedMatte(*effectiveSource, backgroundKeyRgb, effectivePaletteIndices);

    for (int y = 0; y < height; ++y) {
        const int sy = effectiveSrcY + y;
        const int dy = destY + y;
        if (sy < 0 || sy >= effectiveSource->height() || dy < 0 || dy >= dest.height()) {
            continue;
        }

        for (int x = 0; x < width; ++x) {
            const int sx = effectiveSrcX + x;
            const int dx = destX + x;
            if (sx < 0 || sx >= effectiveSource->width() || dx < 0 || dx >= dest.width()) {
                continue;
            }
            if (mask != nullptr && !maskAllowsPixel(*mask, srcX + x, srcY + y)) {
                continue;
            }

            const auto srcPixel = effectiveSource->getPixel(sx, sy);
            const auto destPixel = dest.getPixel(dx, dy);
            const auto srcOffset = static_cast<std::size_t>(sy * effectiveSource->width() + sx);
            const bool skipSource =
                (keyNearWhiteMatte && isNearWhiteMattePixel(srcPixel)) ||
                (keyDefaultIndexedMatte &&
                 effectivePaletteIndices.has_value() &&
                 srcOffset < effectivePaletteIndices->size() &&
                 static_cast<int>((*effectivePaletteIndices)[srcOffset]) == 0);
            const auto resultPixel = skipSource
                ? destPixel
                : applyInk(srcPixel, destPixel, ink, blend, backgroundKeyRgb);
            dest.setPixelPreservePaletteIndex(dx, dy, resultPixel);
        }
    }
}

void copyPixels(Bitmap& dest,
                const Bitmap& src,
                int destX,
                int destY,
                id::InkMode ink,
                int blend) {
    copyPixels(dest, src, destX, destY, 0, 0, src.width(), src.height(), ink, blend);
}

std::shared_ptr<Bitmap> transformQuadBitmap(const Bitmap& src,
                                            int srcLeft,
                                            int srcTop,
                                            int srcRight,
                                            int srcBottom,
                                            const std::array<int, 4>& px,
                                            const std::array<int, 4>& py,
                                            int minX,
                                            int minY,
                                            int maxX,
                                            int maxY) {
    const int srcWidth = srcRight - srcLeft;
    const int srcHeight = srcBottom - srcTop;
    const int destWidth = maxX - minX;
    const int destHeight = maxY - minY;
    auto transformed = std::make_shared<Bitmap>(destWidth, destHeight, src.bitDepth());
    transformed->copyPaletteMetadataFrom(&src);

    const auto srcPaletteIndices = src.paletteIndices();
    std::optional<std::vector<std::uint8_t>> transformedIndices;
    if (srcPaletteIndices.has_value() && srcPaletteIndices->size() == src.pixels().size()) {
        transformedIndices = std::vector<std::uint8_t>(static_cast<std::size_t>(destWidth * destHeight), 0);
    } else {
        transformed->clearPaletteIndices();
    }

    const bool axisAligned =
        (px[0] == minX || px[0] == maxX) && (py[0] == minY || py[0] == maxY) &&
        (px[1] == minX || px[1] == maxX) && (py[1] == minY || py[1] == maxY) &&
        (px[2] == minX || px[2] == maxX) && (py[2] == minY || py[2] == maxY) &&
        (px[3] == minX || px[3] == maxX) && (py[3] == minY || py[3] == maxY);

    if (axisAligned) {
        const double c0x = px[0] == minX ? 0.0 : 1.0;
        const double c0y = py[0] == minY ? 0.0 : 1.0;
        const double axisXX = (px[1] == minX ? 0.0 : 1.0) - c0x;
        const double axisXY = (py[1] == minY ? 0.0 : 1.0) - c0y;
        const double axisYX = (px[3] == minX ? 0.0 : 1.0) - c0x;
        const double axisYY = (py[3] == minY ? 0.0 : 1.0) - c0y;

        for (int y = 0; y < destHeight; ++y) {
            const double dv = (static_cast<double>(y) + 0.5) / static_cast<double>(destHeight);
            for (int x = 0; x < destWidth; ++x) {
                const double du = (static_cast<double>(x) + 0.5) / static_cast<double>(destWidth);
                const double relX = du - c0x;
                const double relY = dv - c0y;
                const double srcU = relX * axisXX + relY * axisXY;
                const double srcV = relX * axisYX + relY * axisYY;
                const int srcX = srcLeft + std::clamp(
                    static_cast<int>(std::floor(srcU * static_cast<double>(srcWidth))), 0, srcWidth - 1);
                const int srcY = srcTop + std::clamp(
                    static_cast<int>(std::floor(srcV * static_cast<double>(srcHeight))), 0, srcHeight - 1);
                if (srcX < 0 || srcX >= src.width() || srcY < 0 || srcY >= src.height()) {
                    continue;
                }
                transformed->setPixelPreservePaletteIndex(x, y, src.getPixel(srcX, srcY));
                if (transformedIndices.has_value() && srcPaletteIndices.has_value()) {
                    const auto srcOffset = static_cast<std::size_t>(srcY * src.width() + srcX);
                    const auto destOffset = static_cast<std::size_t>(y * destWidth + x);
                    if (srcOffset < srcPaletteIndices->size() && destOffset < transformedIndices->size()) {
                        (*transformedIndices)[destOffset] = (*srcPaletteIndices)[srcOffset];
                    }
                }
            }
        }
    } else {
        const bool flipH = px[0] > px[1];
        const bool flipV = py[0] > py[3];
        for (int y = 0; y < destHeight; ++y) {
            const int localY = y * srcHeight / destHeight;
            const int srcY = srcTop + (flipV ? (srcHeight - 1 - localY) : localY);
            for (int x = 0; x < destWidth; ++x) {
                const int localX = x * srcWidth / destWidth;
                const int srcX = srcLeft + (flipH ? (srcWidth - 1 - localX) : localX);
                if (srcX < 0 || srcX >= src.width() || srcY < 0 || srcY >= src.height()) {
                    continue;
                }
                transformed->setPixelPreservePaletteIndex(x, y, src.getPixel(srcX, srcY));
                if (transformedIndices.has_value() && srcPaletteIndices.has_value()) {
                    const auto srcOffset = static_cast<std::size_t>(srcY * src.width() + srcX);
                    const auto destOffset = static_cast<std::size_t>(y * destWidth + x);
                    if (srcOffset < srcPaletteIndices->size() && destOffset < transformedIndices->size()) {
                        (*transformedIndices)[destOffset] = (*srcPaletteIndices)[srcOffset];
                    }
                }
            }
        }
    }

    if (transformedIndices.has_value()) {
        transformed->setPaletteIndices(std::move(*transformedIndices));
    }
    return transformed;
}

std::shared_ptr<Bitmap> createMatte(const Bitmap& src, int alphaThreshold) {
    if (src.width() <= 0 || src.height() <= 0) {
        return std::make_shared<Bitmap>(1, 1, 32);
    }
    if (src.hasNativeMatteAlpha()) {
        return createAlphaMatte(src, alphaThreshold);
    }
    return createFloodFillMatte(src);
}

std::shared_ptr<Bitmap> createMask(const Bitmap& src, int alphaThreshold) {
    if (src.width() <= 0 || src.height() <= 0) {
        return std::make_shared<Bitmap>(1, 1, 32);
    }
    if (src.hasNativeMatteAlpha()) {
        return createAlphaMatte(src, alphaThreshold);
    }

    const auto pixels = src.pixels();
    const auto paletteIndices = src.paletteIndices();
    const auto matte = resolveFloodFillMatte(pixels, paletteIndices, src.width(), src.height());
    if (matte.has_value()) {
        const auto transparent =
            computeFloodFillTransparency(pixels, paletteIndices, src.width(), src.height(), *matte);
        if (isMaskSource(pixels, transparent, *matte)) {
            return createDirectMask(src, *matte, alphaThreshold);
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
    const auto paletteIndices = region.paletteIndices();
    const auto matte = resolveFloodFillMatte(pixels, paletteIndices, width, height);
    if (!matte.has_value()) {
        return region;
    }

    const auto transparent = computeFloodFillTransparency(pixels, paletteIndices, width, height, *matte);
    auto& regionPixels = region.pixels();
    for (std::size_t index = 0; index < regionPixels.size() && index < transparent.size(); ++index) {
        if (transparent[index]) {
            regionPixels[index] &= 0x00FFFFFFU;
        }
    }
    return region;
}

Bitmap applyFloodFillTransparency(const Bitmap& src) {
    return applyMatteToRegion(src, 0, 0, src.width(), src.height());
}

} // namespace libreshockwave::bitmap::Drawing
