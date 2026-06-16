#include "libreshockwave/bitmap/ColorRef.hpp"

#include <algorithm>
#include <stdexcept>

namespace libreshockwave::bitmap {
namespace {

int clampByte(int value) {
    return std::clamp(value, 0, 255);
}

} // namespace

ColorRef::Rgb::Rgb(int r, int g, int b)
    : r(clampByte(r)), g(clampByte(g)), b(clampByte(b)) {}

ColorRef::Rgb ColorRef::Rgb::fromPacked(std::uint32_t rgb) {
    return Rgb(static_cast<int>((rgb >> 16) & 0xFF),
               static_cast<int>((rgb >> 8) & 0xFF),
               static_cast<int>(rgb & 0xFF));
}

ColorRef::Rgb ColorRef::Rgb::fromHex(std::string hex) {
    if (!hex.empty() && hex.front() == '#') {
        hex.erase(hex.begin());
    }
    if (hex.empty() || hex.size() > 6) {
        throw std::invalid_argument("Invalid RGB hex color");
    }
    return fromPacked(static_cast<std::uint32_t>(std::stoul(hex, nullptr, 16)));
}

std::uint32_t ColorRef::Rgb::toPacked() const {
    return (static_cast<std::uint32_t>(r) << 16) |
           (static_cast<std::uint32_t>(g) << 8) |
           static_cast<std::uint32_t>(b);
}

std::uint32_t ColorRef::Rgb::toArgb() const {
    return 0xFF000000U | toPacked();
}

ColorRef::PaletteIndex::PaletteIndex(int index)
    : index(clampByte(index)) {}

ColorRef::Rgb ColorRef::PaletteIndex::resolve(const Palette* palette) const {
    if (palette == nullptr) {
        palette = &Palette::systemMacPalette();
    }
    const auto rgb = palette->getRGB(index);
    return Rgb(rgb[0], rgb[1], rgb[2]);
}

ColorRef::ColorRef(Rgb rgb) : value_(rgb) {}

ColorRef::ColorRef(PaletteIndex index) : value_(index) {}

ColorRef::Rgb ColorRef::toRgb(const Palette* palette) const {
    if (const auto* rgb = std::get_if<Rgb>(&value_)) {
        return *rgb;
    }
    return std::get<PaletteIndex>(value_).resolve(palette);
}

int ColorRef::toNearestPaletteIndex(const Palette* palette) const {
    if (const auto* index = std::get_if<PaletteIndex>(&value_)) {
        return index->index;
    }
    if (palette == nullptr) {
        palette = &Palette::systemMacPalette();
    }
    return palette->nearestIndex(std::get<Rgb>(value_).toPacked());
}

bool ColorRef::isRgb() const {
    return std::holds_alternative<Rgb>(value_);
}

bool ColorRef::isPaletteIndex() const {
    return std::holds_alternative<PaletteIndex>(value_);
}

ColorRef::Rgb ColorRef::black() {
    return Rgb(0, 0, 0);
}

ColorRef::Rgb ColorRef::white() {
    return Rgb(255, 255, 255);
}

} // namespace libreshockwave::bitmap
