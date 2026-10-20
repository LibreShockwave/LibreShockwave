#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <queue>
#include <vector>

#include "libreshockwave/bitmap/bitmap.hpp"

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

inline bool matchesRgb(std::uint32_t pixel, int matte_rgb, int tolerance) {
    const int pr = channel(pixel, 16);
    const int pg = channel(pixel, 8);
    const int pb = channel(pixel, 0);
    const int mr = (matte_rgb >> 16) & 0xFF;
    const int mg = (matte_rgb >> 8) & 0xFF;
    const int mb = matte_rgb & 0xFF;
    return std::abs(pr - mr) <= tolerance &&
           std::abs(pg - mg) <= tolerance &&
           std::abs(pb - mb) <= tolerance;
}

inline bool isNearWhiteGrayscale(int color_rgb, int min_channel, int max_delta) {
    const int r = (color_rgb >> 16) & 0xFF;
    const int g = (color_rgb >> 8) & 0xFF;
    const int b = color_rgb & 0xFF;
    return r >= min_channel && g >= min_channel && b >= min_channel &&
           std::abs(r - g) <= max_delta &&
           std::abs(g - b) <= max_delta &&
           std::abs(r - b) <= max_delta;
}

inline bool isOpaqueColor(std::uint32_t argb, int color_rgb) {
    return channel(argb, 24) == 0xFF && static_cast<int>(argb & 0x00FFFFFFU) == (color_rgb & 0x00FFFFFF);
}

inline Bitmap derivedBitmap(const Bitmap& src, std::vector<std::uint32_t> pixels) {
    Bitmap derived(src.width(), src.height(), src.bitDepth(), std::move(pixels));
    derived.copyPaletteMetadataFrom(&src);
    return derived;
}

inline bool hasPaletteIndices(const std::optional<std::vector<std::uint8_t>>& palette_indices,
                              int width,
                              int height) {
    return palette_indices.has_value() &&
           palette_indices->size() >= static_cast<std::size_t>(std::max(0, width) * std::max(0, height));
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

inline bool isUniformPaletteIndex(const std::vector<std::uint8_t>& palette_indices, int palette_index) {
    return std::all_of(palette_indices.begin(), palette_indices.end(), [&](std::uint8_t entry) {
        return static_cast<int>(entry & 0xFFU) == palette_index;
    });
}

inline int resolvePaletteIndexRgb(const std::vector<std::uint32_t>& pixels,
                                  const std::vector<std::uint8_t>& palette_indices,
                                  int palette_index) {
    for (std::size_t index = 0; index < pixels.size() && index < palette_indices.size(); ++index) {
        if (static_cast<int>(palette_indices[index] & 0xFFU) == palette_index) {
            return static_cast<int>(pixels[index] & 0x00FFFFFFU);
        }
    }
    return 0xFFFFFF;
}

inline bool paletteIndicesMatchPixels(const std::vector<std::uint32_t>& pixels,
                                      const std::vector<std::uint8_t>& palette_indices) {
    std::array<std::optional<std::uint32_t>, 256> colors{};
    for (std::size_t index = 0; index < pixels.size() && index < palette_indices.size(); ++index) {
        if (channel(pixels[index], 24) == 0) {
            continue;
        }
        const auto palette_index = static_cast<std::size_t>(palette_indices[index] & 0xFFU);
        const auto rgb = pixels[index] & 0x00FFFFFFU;
        if (colors[palette_index].has_value() && *colors[palette_index] != rgb) {
            return false;
        }
        colors[palette_index] = rgb;
    }
    return true;
}

inline bool cornerContainsPaletteIndex(const std::vector<std::uint8_t>& palette_indices,
                                       int width,
                                       int height,
                                       int palette_index) {
    for (const int index : cornerIndices(width, height)) {
        if (index >= 0 && static_cast<std::size_t>(index) < palette_indices.size() &&
            static_cast<int>(palette_indices[static_cast<std::size_t>(index)] & 0xFFU) == palette_index) {
            return true;
        }
    }
    return false;
}

inline bool edgeContainsOpaquePaletteIndex(const std::vector<std::uint32_t>& pixels,
                                           const std::vector<std::uint8_t>& palette_indices,
                                           int width,
                                           int height,
                                           int palette_index) {
    for (const int index : edgeIndices(width, height)) {
        const auto offset = static_cast<std::size_t>(index);
        if (offset < pixels.size() &&
            offset < palette_indices.size() &&
            channel(pixels[offset], 24) != 0 &&
            static_cast<int>(palette_indices[offset] & 0xFFU) == palette_index) {
            return true;
        }
    }
    return false;
}

inline bool hasOpaqueNonPaletteIndexContent(const std::vector<std::uint32_t>& pixels,
                                            const std::vector<std::uint8_t>& palette_indices,
                                            int palette_index) {
    for (std::size_t index = 0; index < pixels.size() && index < palette_indices.size(); ++index) {
        if (channel(pixels[index], 24) != 0 &&
            static_cast<int>(palette_indices[index] & 0xFFU) != palette_index) {
            return true;
        }
    }
    return false;
}

template <typename Predicate>
std::vector<bool> computeEdgeConnectedMask(int width, int height, Predicate&& should_include) {
    std::vector<bool> connected(static_cast<std::size_t>(std::max(0, width) * std::max(0, height)), false);
    if (width <= 0 || height <= 0) {
        return connected;
    }

    std::queue<int> queue;
    const auto seed = [&](int x, int y) {
        const int index = y * width + x;
        const auto offset = static_cast<std::size_t>(index);
        if (!connected[offset] && should_include(index)) {
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
