#include "libreshockwave/bitmap/bitmap_colorizer.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include "bitmap_processing.hpp"

namespace libreshockwave::bitmap {
namespace {

using detail::interpolateChannel;
using detail::packArgb;

} // namespace

Bitmap BitmapColorizer::colorize(const Bitmap& source,
                                 const ColorRef* fore_color,
                                 const ColorRef* back_color,
                                 const Palette* palette) {
    if (fore_color == nullptr && back_color == nullptr) {
        return source.copy();
    }

    std::optional<ColorRef::Rgb> fg = fore_color != nullptr ? std::optional<ColorRef::Rgb>(fore_color->toRgb(palette)) : std::nullopt;
    std::optional<ColorRef::Rgb> bg = back_color != nullptr ? std::optional<ColorRef::Rgb>(back_color->toRgb(palette)) : std::nullopt;

    Bitmap result(source.width(), source.height(), source.bitDepth());
    const int bit_depth = source.bitDepth();

    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            const auto argb = source.getPixel(x, y);
            const int alpha = static_cast<int>((argb >> 24) & 0xFF);
            const int r = static_cast<int>((argb >> 16) & 0xFF);
            const int g = static_cast<int>((argb >> 8) & 0xFF);
            const int b = static_cast<int>(argb & 0xFF);

            int new_r = r;
            int new_g = g;
            int new_b = b;

            if (bit_depth == 32) {
                const int intensity = (r + g + b) / 3;
                const float t = static_cast<float>(intensity) / 255.0F;

                if (fg.has_value() && bg.has_value()) {
                    new_r = interpolateChannel(t, fg->r, bg->r);
                    new_g = interpolateChannel(t, fg->g, bg->g);
                    new_b = interpolateChannel(t, fg->b, bg->b);
                } else if (fg.has_value() && intensity <= 1) {
                    new_r = fg->r;
                    new_g = fg->g;
                    new_b = fg->b;
                }
            } else {
                const int max_index = (1 << bit_depth) - 1;
                const int intensity = (r + g + b) / 3;
                const int estimated_index = max_index > 0 ? (intensity * max_index) / 255 : 0;
                const float t = max_index > 0 ? static_cast<float>(estimated_index) / static_cast<float>(max_index) : 0.0F;

                if (fg.has_value() && bg.has_value()) {
                    new_r = interpolateChannel(t, fg->r, bg->r);
                    new_g = interpolateChannel(t, fg->g, bg->g);
                    new_b = interpolateChannel(t, fg->b, bg->b);
                } else if (fg.has_value() && estimated_index == 0) {
                    new_r = fg->r;
                    new_g = fg->g;
                    new_b = fg->b;
                }
            }

            result.setPixelRGBA(x, y, new_r, new_g, new_b, alpha);
        }
    }

    return result;
}

Bitmap BitmapColorizer::colorize(const Bitmap& source, const ColorRef& fore_color, const Palette* palette) {
    return colorize(source, &fore_color, nullptr, palette);
}

Bitmap BitmapColorizer::colorize(const Bitmap& source, std::uint32_t fore_color_rgb, std::uint32_t back_color_rgb) {
    const ColorRef foreground(ColorRef::Rgb::fromPacked(fore_color_rgb));
    const ColorRef background(ColorRef::Rgb::fromPacked(back_color_rgb));
    return colorize(source, &foreground, &background, nullptr);
}

Bitmap BitmapColorizer::colorizeWithPaletteIndices(const Bitmap& source,
                                                   int fore_color_index,
                                                   int back_color_index,
                                                   const Palette* palette) {
    const ColorRef foreground{ColorRef::PaletteIndex(fore_color_index)};
    const ColorRef background{ColorRef::PaletteIndex(back_color_index)};
    return colorize(source, &foreground, &background, palette);
}

bool BitmapColorizer::allowsColorization(int bit_depth, int ink_mode) {
    switch (ink_mode) {
        case 0:
        case 8:
        case 9:
            return bit_depth == 32 || bit_depth <= 8;
        default:
            return false;
    }
}

bool BitmapColorizer::usesBackColor(int bit_depth, int ink_mode) {
    return ink_mode == 0 && (bit_depth == 32 || bit_depth <= 8);
}

std::vector<std::uint32_t> BitmapColorizer::colorizeIndexedData(const std::vector<std::uint8_t>& indexed_data,
                                                                int bit_depth,
                                                                const ColorRef* fore_color,
                                                                const ColorRef* back_color,
                                                                const Palette* palette) {
    if (indexed_data.empty()) {
        return {};
    }

    std::optional<ColorRef::Rgb> fg = fore_color != nullptr ? std::optional<ColorRef::Rgb>(fore_color->toRgb(palette)) : std::nullopt;
    std::optional<ColorRef::Rgb> bg = back_color != nullptr ? std::optional<ColorRef::Rgb>(back_color->toRgb(palette)) : std::nullopt;
    const auto* fg_ptr = fg.has_value() ? &*fg : nullptr;
    const auto* bg_ptr = bg.has_value() ? &*bg : nullptr;

    const int max_index = (1 << bit_depth) - 1;
    if (bit_depth == 8) {
        std::vector<std::uint32_t> result;
        result.reserve(indexed_data.size());
        for (const auto byte : indexed_data) {
            result.push_back(colorizeIndex(static_cast<int>(byte), max_index, fg_ptr, bg_ptr));
        }
        return result;
    }

    const int pixels_per_byte = bit_depth > 0 ? 8 / bit_depth : 0;
    std::vector<std::uint32_t> result;
    result.reserve(indexed_data.size() * static_cast<std::size_t>(std::max(pixels_per_byte, 0)));
    const int mask = (1 << bit_depth) - 1;

    for (const auto byte : indexed_data) {
        for (int pixel = 0; pixel < pixels_per_byte; ++pixel) {
            const int shift = (pixels_per_byte - 1 - pixel) * bit_depth;
            const int index = (static_cast<int>(byte) >> shift) & mask;
            result.push_back(colorizeIndex(index, max_index, fg_ptr, bg_ptr));
        }
    }
    return result;
}

std::uint32_t BitmapColorizer::colorizeIndex(int index,
                                             int max_index,
                                             const ColorRef::Rgb* fore_color,
                                             const ColorRef::Rgb* back_color) {
    const float t = max_index > 0 ? static_cast<float>(index) / static_cast<float>(max_index) : 0.0F;

    int r = 0;
    int g = 0;
    int b = 0;
    if (fore_color != nullptr && back_color != nullptr) {
        r = interpolateChannel(t, fore_color->r, back_color->r);
        g = interpolateChannel(t, fore_color->g, back_color->g);
        b = interpolateChannel(t, fore_color->b, back_color->b);
    } else if (fore_color != nullptr && index == 0) {
        r = fore_color->r;
        g = fore_color->g;
        b = fore_color->b;
    } else {
        const int gray = static_cast<int>(std::lround(t * 255.0F));
        r = gray;
        g = gray;
        b = gray;
    }

    return packArgb(255, r, g, b);
}

} // namespace libreshockwave::bitmap
