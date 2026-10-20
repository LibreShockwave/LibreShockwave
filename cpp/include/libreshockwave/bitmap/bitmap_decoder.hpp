#pragma once

#include <cstdint>
#include <vector>

#include "libreshockwave/bitmap/bitmap.hpp"

namespace libreshockwave::bitmap {

class BitmapDecoder {
public:
    [[nodiscard]] static std::vector<std::uint8_t> decompressRLE(const std::vector<std::uint8_t>& compressed,
                                                                 int expected_size);
    [[nodiscard]] static int calculateScanWidthPixels(int width, int bit_depth, int pitch);
    [[nodiscard]] static int calculateScanWidth(int width, int bit_depth);

    [[nodiscard]] static Bitmap decode1Bit(const std::vector<std::uint8_t>& data,
                                           int width,
                                           int height,
                                           int scan_width,
                                           const Palette* palette);
    [[nodiscard]] static Bitmap decode2Bit(const std::vector<std::uint8_t>& data,
                                           int width,
                                           int height,
                                           int scan_width,
                                           const Palette* palette);
    [[nodiscard]] static Bitmap decode4Bit(const std::vector<std::uint8_t>& data,
                                           int width,
                                           int height,
                                           int scan_width,
                                           const Palette* palette);
    [[nodiscard]] static Bitmap decode8Bit(const std::vector<std::uint8_t>& data,
                                           int width,
                                           int height,
                                           int scan_width,
                                           const Palette* palette);
    [[nodiscard]] static Bitmap decode16Bit(const std::vector<std::uint8_t>& data,
                                            int width,
                                            int height,
                                            int scan_width,
                                            bool skip_compression);
    [[nodiscard]] static Bitmap decode32Bit(const std::vector<std::uint8_t>& data,
                                            int width,
                                            int height,
                                            int scan_width,
                                            bool channels_separated);
    [[nodiscard]] static Bitmap decode(const std::vector<std::uint8_t>& data,
                                       int width,
                                       int height,
                                       int bit_depth,
                                       const Palette* palette,
                                       bool big_endian,
                                       int director_version,
                                       int pitch);
    [[nodiscard]] static Bitmap decode(const std::vector<std::uint8_t>& data,
                                       int width,
                                       int height,
                                       int bit_depth,
                                       const Palette* palette);

private:
    [[nodiscard]] static int getAlignmentWidth(int bit_depth);
    [[nodiscard]] static int getNumChannels(int bit_depth);
};

} // namespace libreshockwave::bitmap
