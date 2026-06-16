#pragma once

#include <cstdint>
#include <vector>

#include "libreshockwave/bitmap/Bitmap.hpp"

namespace libreshockwave::bitmap {

class BitmapDecoder {
public:
    [[nodiscard]] static std::vector<std::uint8_t> decompressRLE(const std::vector<std::uint8_t>& compressed,
                                                                 int expectedSize);
    [[nodiscard]] static int calculateScanWidthPixels(int width, int bitDepth, int pitch);
    [[nodiscard]] static int calculateScanWidth(int width, int bitDepth);

    [[nodiscard]] static Bitmap decode1Bit(const std::vector<std::uint8_t>& data,
                                           int width,
                                           int height,
                                           int scanWidth,
                                           const Palette* palette);
    [[nodiscard]] static Bitmap decode2Bit(const std::vector<std::uint8_t>& data,
                                           int width,
                                           int height,
                                           int scanWidth,
                                           const Palette* palette);
    [[nodiscard]] static Bitmap decode4Bit(const std::vector<std::uint8_t>& data,
                                           int width,
                                           int height,
                                           int scanWidth,
                                           const Palette* palette);
    [[nodiscard]] static Bitmap decode8Bit(const std::vector<std::uint8_t>& data,
                                           int width,
                                           int height,
                                           int scanWidth,
                                           const Palette* palette);
    [[nodiscard]] static Bitmap decode16Bit(const std::vector<std::uint8_t>& data,
                                            int width,
                                            int height,
                                            int scanWidth,
                                            bool skipCompression);
    [[nodiscard]] static Bitmap decode32Bit(const std::vector<std::uint8_t>& data,
                                            int width,
                                            int height,
                                            int scanWidth,
                                            bool channelsSeparated);
    [[nodiscard]] static Bitmap decode(const std::vector<std::uint8_t>& data,
                                       int width,
                                       int height,
                                       int bitDepth,
                                       const Palette* palette,
                                       bool bigEndian,
                                       int directorVersion,
                                       int pitch);
    [[nodiscard]] static Bitmap decode(const std::vector<std::uint8_t>& data,
                                       int width,
                                       int height,
                                       int bitDepth,
                                       const Palette* palette);

private:
    [[nodiscard]] static int getAlignmentWidth(int bitDepth);
    [[nodiscard]] static int getNumChannels(int bitDepth);
};

} // namespace libreshockwave::bitmap
