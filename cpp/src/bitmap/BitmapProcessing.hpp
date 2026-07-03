#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <queue>
#include <vector>

#include "libreshockwave/bitmap/Bitmap.hpp"

namespace libreshockwave::bitmap::detail {

inline int channel(std::uint32_t argb, int shift) {
    return static_cast<int>((argb >> shift) & 0xFFU);
}

inline std::uint32_t packArgb(int alpha, int r, int g, int b) {
    return (static_cast<std::uint32_t>(alpha & 0xFF) << 24) |
           (static_cast<std::uint32_t>(r & 0xFF) << 16) |
           (static_cast<std::uint32_t>(g & 0xFF) << 8) |
           static_cast<std::uint32_t>(b & 0xFF);
}

inline int interpolateChannel(float t, int foreground, int background) {
    return static_cast<int>(std::lround((1.0F - t) * static_cast<float>(foreground) +
                                        t * static_cast<float>(background)));
}

inline int maskAlphaFromPixel(std::uint32_t pixel) {
    const int r = channel(pixel, 16);
    const int g = channel(pixel, 8);
    const int b = channel(pixel, 0);
    return ((77 * r) + (150 * g) + (29 * b) + 128) >> 8;
}

inline bool matchesRgb(std::uint32_t pixel, int matteRgb, int tolerance) {
    const int pr = channel(pixel, 16);
    const int pg = channel(pixel, 8);
    const int pb = channel(pixel, 0);
    const int mr = (matteRgb >> 16) & 0xFF;
    const int mg = (matteRgb >> 8) & 0xFF;
    const int mb = matteRgb & 0xFF;
    return std::abs(pr - mr) <= tolerance &&
           std::abs(pg - mg) <= tolerance &&
           std::abs(pb - mb) <= tolerance;
}

inline bool isNearWhiteGrayscale(int colorRgb, int minChannel, int maxDelta) {
    const int r = (colorRgb >> 16) & 0xFF;
    const int g = (colorRgb >> 8) & 0xFF;
    const int b = colorRgb & 0xFF;
    return r >= minChannel && g >= minChannel && b >= minChannel &&
           std::abs(r - g) <= maxDelta &&
           std::abs(g - b) <= maxDelta &&
           std::abs(r - b) <= maxDelta;
}

inline bool isOpaqueColor(std::uint32_t argb, int colorRgb) {
    return channel(argb, 24) == 0xFF && static_cast<int>(argb & 0x00FFFFFFU) == (colorRgb & 0x00FFFFFF);
}

inline Bitmap derivedBitmap(const Bitmap& src, std::vector<std::uint32_t> pixels) {
    Bitmap derived(src.width(), src.height(), src.bitDepth(), std::move(pixels));
    derived.copyPaletteMetadataFrom(&src);
    return derived;
}

inline bool hasPaletteIndices(const std::optional<std::vector<std::uint8_t>>& paletteIndices,
                              int width,
                              int height) {
    return paletteIndices.has_value() &&
           paletteIndices->size() >= static_cast<std::size_t>(std::max(0, width) * std::max(0, height));
}

inline std::vector<int> cornerIndices(int width, int height) {
    return {
        0,
        std::max(0, width - 1),
        std::max(0, height - 1) * width,
        std::max(0, height - 1) * width + std::max(0, width - 1),
    };
}

inline std::vector<int> edgeIndices(int width, int height) {
    std::vector<int> indices;
    if (width <= 0 || height <= 0) {
        return indices;
    }
    indices.reserve(static_cast<std::size_t>((width * 2) + std::max(0, height - 2) * 2));
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

inline bool isUniformPaletteIndex(const std::vector<std::uint8_t>& paletteIndices, int paletteIndex) {
    return std::all_of(paletteIndices.begin(), paletteIndices.end(), [&](std::uint8_t entry) {
        return static_cast<int>(entry & 0xFFU) == paletteIndex;
    });
}

inline int resolvePaletteIndexRgb(const std::vector<std::uint32_t>& pixels,
                                  const std::vector<std::uint8_t>& paletteIndices,
                                  int paletteIndex) {
    for (std::size_t index = 0; index < pixels.size() && index < paletteIndices.size(); ++index) {
        if (static_cast<int>(paletteIndices[index] & 0xFFU) == paletteIndex) {
            return static_cast<int>(pixels[index] & 0x00FFFFFFU);
        }
    }
    return 0xFFFFFF;
}

inline bool cornerContainsPaletteIndex(const std::vector<std::uint8_t>& paletteIndices,
                                       int width,
                                       int height,
                                       int paletteIndex) {
    for (const int index : cornerIndices(width, height)) {
        if (index >= 0 && static_cast<std::size_t>(index) < paletteIndices.size() &&
            static_cast<int>(paletteIndices[static_cast<std::size_t>(index)] & 0xFFU) == paletteIndex) {
            return true;
        }
    }
    return false;
}

inline bool edgeContainsOpaquePaletteIndex(const std::vector<std::uint32_t>& pixels,
                                           const std::vector<std::uint8_t>& paletteIndices,
                                           int width,
                                           int height,
                                           int paletteIndex) {
    for (const int index : edgeIndices(width, height)) {
        const auto offset = static_cast<std::size_t>(index);
        if (offset < pixels.size() &&
            offset < paletteIndices.size() &&
            channel(pixels[offset], 24) != 0 &&
            static_cast<int>(paletteIndices[offset] & 0xFFU) == paletteIndex) {
            return true;
        }
    }
    return false;
}

inline bool hasOpaqueNonPaletteIndexContent(const std::vector<std::uint32_t>& pixels,
                                            const std::vector<std::uint8_t>& paletteIndices,
                                            int paletteIndex) {
    for (std::size_t index = 0; index < pixels.size() && index < paletteIndices.size(); ++index) {
        if (channel(pixels[index], 24) != 0 &&
            static_cast<int>(paletteIndices[index] & 0xFFU) != paletteIndex) {
            return true;
        }
    }
    return false;
}

template <typename Predicate>
std::vector<bool> computeEdgeConnectedMask(int width, int height, Predicate&& shouldInclude) {
    std::vector<bool> connected(static_cast<std::size_t>(std::max(0, width) * std::max(0, height)), false);
    if (width <= 0 || height <= 0) {
        return connected;
    }

    std::queue<int> queue;
    const auto seed = [&](int x, int y) {
        const int index = y * width + x;
        const auto offset = static_cast<std::size_t>(index);
        if (!connected[offset] && shouldInclude(index)) {
            connected[offset] = true;
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

    return connected;
}

} // namespace libreshockwave::bitmap::detail
