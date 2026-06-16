#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "libreshockwave/bitmap/Palette.hpp"

namespace libreshockwave::bitmap {

class ColorRef {
public:
    struct Rgb {
        int r;
        int g;
        int b;

        Rgb(int r, int g, int b);

        [[nodiscard]] static Rgb fromPacked(std::uint32_t rgb);
        [[nodiscard]] static Rgb fromHex(std::string hex);
        [[nodiscard]] std::uint32_t toPacked() const;
        [[nodiscard]] std::uint32_t toArgb() const;

        friend bool operator==(const Rgb&, const Rgb&) = default;
    };

    struct PaletteIndex {
        int index;

        explicit PaletteIndex(int index);
        [[nodiscard]] Rgb resolve(const Palette* palette) const;

        friend bool operator==(const PaletteIndex&, const PaletteIndex&) = default;
    };

    ColorRef(Rgb rgb);
    ColorRef(PaletteIndex index);

    [[nodiscard]] Rgb toRgb(const Palette* palette = nullptr) const;
    [[nodiscard]] int toNearestPaletteIndex(const Palette* palette = nullptr) const;
    [[nodiscard]] bool isRgb() const;
    [[nodiscard]] bool isPaletteIndex() const;

    [[nodiscard]] static Rgb black();
    [[nodiscard]] static Rgb white();

private:
    std::variant<Rgb, PaletteIndex> value_;
};

} // namespace libreshockwave::bitmap
