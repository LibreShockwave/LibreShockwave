#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace libreshockwave::bitmap {

class Palette {
public:
    static constexpr int SYSTEM_MAC = -1;
    static constexpr int RAINBOW = -2;
    static constexpr int GRAYSCALE = -3;
    static constexpr int PASTELS = -4;
    static constexpr int VIVID = -5;
    static constexpr int NTSC = -6;
    static constexpr int METALLIC = -7;
    static constexpr int SYSTEM_WIN = -101;
    static constexpr int SYSTEM_WIN_DIR4 = -102;

    Palette(std::vector<std::uint32_t> colors, std::string name);

    [[nodiscard]] std::uint32_t getColor(int index) const;
    [[nodiscard]] std::array<int, 3> getRGB(int index) const;
    [[nodiscard]] int size() const;
    [[nodiscard]] int nearestIndex(std::uint32_t rgb) const;
    [[nodiscard]] const std::string& name() const;
    [[nodiscard]] const std::vector<std::uint32_t>& colors() const;

    [[nodiscard]] static std::optional<std::string> builtInSymbolName(int paletteId);
    [[nodiscard]] static const Palette* builtInBySymbolName(std::string_view symbolName);
    [[nodiscard]] static std::optional<std::string> normalizeBuiltInSymbolName(std::string_view symbolName);
    [[nodiscard]] static const Palette& builtIn(int paletteId);

    [[nodiscard]] static const Palette& systemMacPalette();
    [[nodiscard]] static const Palette& rainbowPalette();
    [[nodiscard]] static const Palette& grayscalePalette();
    [[nodiscard]] static const Palette& metallicPalette();
    [[nodiscard]] static const Palette& systemWinPalette();

private:
    std::vector<std::uint32_t> colors_;
    std::string name_;
    mutable std::unordered_map<std::uint32_t, int> nearestCache_;
};

} // namespace libreshockwave::bitmap
