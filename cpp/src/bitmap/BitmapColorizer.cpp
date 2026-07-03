#include "libreshockwave/bitmap/BitmapColorizer.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include "BitmapProcessing.hpp"

namespace libreshockwave::bitmap {
namespace {

using detail::interpolateChannel;
using detail::packArgb;

} // namespace

Bitmap BitmapColorizer::colorize(const Bitmap& source,
                                 const ColorRef* foreColor,
                                 const ColorRef* backColor,
                                 const Palette* palette) {
    if (foreColor == nullptr && backColor == nullptr) {
        return source.copy();
    }

    std::optional<ColorRef::Rgb> fg = foreColor != nullptr ? std::optional<ColorRef::Rgb>(foreColor->toRgb(palette)) : std::nullopt;
    std::optional<ColorRef::Rgb> bg = backColor != nullptr ? std::optional<ColorRef::Rgb>(backColor->toRgb(palette)) : std::nullopt;

    Bitmap result(source.width(), source.height(), source.bitDepth());
    const int bitDepth = source.bitDepth();

    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            const auto argb = source.getPixel(x, y);
            const int alpha = static_cast<int>((argb >> 24) & 0xFF);
            const int r = static_cast<int>((argb >> 16) & 0xFF);
            const int g = static_cast<int>((argb >> 8) & 0xFF);
            const int b = static_cast<int>(argb & 0xFF);

            int newR = r;
            int newG = g;
            int newB = b;

            if (bitDepth == 32) {
                const int intensity = (r + g + b) / 3;
                const float t = static_cast<float>(intensity) / 255.0F;

                if (fg.has_value() && bg.has_value()) {
                    newR = interpolateChannel(t, fg->r, bg->r);
                    newG = interpolateChannel(t, fg->g, bg->g);
                    newB = interpolateChannel(t, fg->b, bg->b);
                } else if (fg.has_value() && intensity <= 1) {
                    newR = fg->r;
                    newG = fg->g;
                    newB = fg->b;
                }
            } else {
                const int maxIndex = (1 << bitDepth) - 1;
                const int intensity = (r + g + b) / 3;
                const int estimatedIndex = maxIndex > 0 ? (intensity * maxIndex) / 255 : 0;
                const float t = maxIndex > 0 ? static_cast<float>(estimatedIndex) / static_cast<float>(maxIndex) : 0.0F;

                if (fg.has_value() && bg.has_value()) {
                    newR = interpolateChannel(t, fg->r, bg->r);
                    newG = interpolateChannel(t, fg->g, bg->g);
                    newB = interpolateChannel(t, fg->b, bg->b);
                } else if (fg.has_value() && estimatedIndex == 0) {
                    newR = fg->r;
                    newG = fg->g;
                    newB = fg->b;
                }
            }

            result.setPixelRGBA(x, y, newR, newG, newB, alpha);
        }
    }

    return result;
}

Bitmap BitmapColorizer::colorize(const Bitmap& source, const ColorRef& foreColor, const Palette* palette) {
    return colorize(source, &foreColor, nullptr, palette);
}

Bitmap BitmapColorizer::colorize(const Bitmap& source, std::uint32_t foreColorRgb, std::uint32_t backColorRgb) {
    const ColorRef foreground(ColorRef::Rgb::fromPacked(foreColorRgb));
    const ColorRef background(ColorRef::Rgb::fromPacked(backColorRgb));
    return colorize(source, &foreground, &background, nullptr);
}

Bitmap BitmapColorizer::colorizeWithPaletteIndices(const Bitmap& source,
                                                   int foreColorIndex,
                                                   int backColorIndex,
                                                   const Palette* palette) {
    const ColorRef foreground{ColorRef::PaletteIndex(foreColorIndex)};
    const ColorRef background{ColorRef::PaletteIndex(backColorIndex)};
    return colorize(source, &foreground, &background, palette);
}

bool BitmapColorizer::allowsColorization(int bitDepth, int inkMode) {
    switch (inkMode) {
        case 0:
        case 8:
        case 9:
            return bitDepth == 32 || bitDepth <= 8;
        default:
            return false;
    }
}

bool BitmapColorizer::usesBackColor(int bitDepth, int inkMode) {
    return inkMode == 0 && (bitDepth == 32 || bitDepth <= 8);
}

std::vector<std::uint32_t> BitmapColorizer::colorizeIndexedData(const std::vector<std::uint8_t>& indexedData,
                                                                int bitDepth,
                                                                const ColorRef* foreColor,
                                                                const ColorRef* backColor,
                                                                const Palette* palette) {
    if (indexedData.empty()) {
        return {};
    }

    std::optional<ColorRef::Rgb> fg = foreColor != nullptr ? std::optional<ColorRef::Rgb>(foreColor->toRgb(palette)) : std::nullopt;
    std::optional<ColorRef::Rgb> bg = backColor != nullptr ? std::optional<ColorRef::Rgb>(backColor->toRgb(palette)) : std::nullopt;
    const auto* fgPtr = fg.has_value() ? &*fg : nullptr;
    const auto* bgPtr = bg.has_value() ? &*bg : nullptr;

    const int maxIndex = (1 << bitDepth) - 1;
    if (bitDepth == 8) {
        std::vector<std::uint32_t> result;
        result.reserve(indexedData.size());
        for (const auto byte : indexedData) {
            result.push_back(colorizeIndex(static_cast<int>(byte), maxIndex, fgPtr, bgPtr));
        }
        return result;
    }

    const int pixelsPerByte = bitDepth > 0 ? 8 / bitDepth : 0;
    std::vector<std::uint32_t> result;
    result.reserve(indexedData.size() * static_cast<std::size_t>(std::max(pixelsPerByte, 0)));
    const int mask = (1 << bitDepth) - 1;

    for (const auto byte : indexedData) {
        for (int pixel = 0; pixel < pixelsPerByte; ++pixel) {
            const int shift = (pixelsPerByte - 1 - pixel) * bitDepth;
            const int index = (static_cast<int>(byte) >> shift) & mask;
            result.push_back(colorizeIndex(index, maxIndex, fgPtr, bgPtr));
        }
    }
    return result;
}

std::uint32_t BitmapColorizer::colorizeIndex(int index,
                                             int maxIndex,
                                             const ColorRef::Rgb* foreColor,
                                             const ColorRef::Rgb* backColor) {
    const float t = maxIndex > 0 ? static_cast<float>(index) / static_cast<float>(maxIndex) : 0.0F;

    int r = 0;
    int g = 0;
    int b = 0;
    if (foreColor != nullptr && backColor != nullptr) {
        r = interpolateChannel(t, foreColor->r, backColor->r);
        g = interpolateChannel(t, foreColor->g, backColor->g);
        b = interpolateChannel(t, foreColor->b, backColor->b);
    } else if (foreColor != nullptr && index == 0) {
        r = foreColor->r;
        g = foreColor->g;
        b = foreColor->b;
    } else {
        const int gray = static_cast<int>(std::lround(t * 255.0F));
        r = gray;
        g = gray;
        b = gray;
    }

    return packArgb(255, r, g, b);
}

} // namespace libreshockwave::bitmap
