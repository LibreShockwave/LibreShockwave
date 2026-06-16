#include "libreshockwave/bitmap/BitmapDecoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::bitmap {
namespace {

bool isZlibData(const std::vector<std::uint8_t>& data) {
    return data.size() >= 2 &&
           data[0] == 0x78 &&
           (data[1] == 0x01 || data[1] == 0x5E || data[1] == 0x9C || data[1] == 0xDA);
}

int scaledIndex(int value, int max) {
    return static_cast<int>(std::lround(static_cast<float>(value) / static_cast<float>(max) * 255.0F));
}

} // namespace

std::vector<std::uint8_t> BitmapDecoder::decompressRLE(const std::vector<std::uint8_t>& compressed,
                                                       int expectedSize) {
    if (expectedSize <= 0) {
        return {};
    }
    expectedSize = std::min(expectedSize, 100'000'000);

    std::vector<std::uint8_t> result(static_cast<std::size_t>(expectedSize), 0);
    std::size_t pos = 0;
    std::size_t outPos = 0;

    while (pos < compressed.size() && outPos < result.size()) {
        const int control = compressed[pos++] & 0xFF;
        if (control < 0x80) {
            const int count = control + 1;
            for (int index = 0; index < count && pos < compressed.size() && outPos < result.size(); ++index) {
                result[outPos++] = compressed[pos++];
            }
        } else if (control == 0x80) {
            continue;
        } else {
            const int count = 257 - control;
            if (pos < compressed.size()) {
                const auto value = compressed[pos++];
                for (int index = 0; index < count && outPos < result.size(); ++index) {
                    result[outPos++] = value;
                }
            }
        }
    }

    if (outPos < result.size()) {
        result.resize(outPos);
    }
    return result;
}

int BitmapDecoder::calculateScanWidthPixels(int width, int bitDepth, int pitch) {
    if (pitch > 0 && bitDepth > 0) {
        return (pitch * 8) / bitDepth;
    }

    const int alignmentWidth = getAlignmentWidth(bitDepth);
    if (width % alignmentWidth == 0) {
        return width;
    }
    return alignmentWidth * ((width + alignmentWidth - 1) / alignmentWidth);
}

int BitmapDecoder::calculateScanWidth(int width, int bitDepth) {
    const int bitsPerRow = width * bitDepth;
    return ((bitsPerRow + 15) / 16) * 2;
}

Bitmap BitmapDecoder::decode1Bit(const std::vector<std::uint8_t>& data,
                                 int width,
                                 int height,
                                 int scanWidth,
                                 const Palette* palette) {
    (void)palette;
    Bitmap bitmap(width, height, 1);
    std::vector<std::uint8_t> indices(static_cast<std::size_t>(width * height), 0);
    std::vector<std::uint8_t> expanded(data.size() * 8U, 0);

    std::size_t out = 0;
    for (const auto datum : data) {
        const int value = datum & 0xFF;
        for (int bit = 1; bit <= 8; ++bit) {
            expanded[out++] = ((value & (0x1 << (8 - bit))) >> (8 - bit)) == 1 ? 0xFF : 0x00;
        }
    }

    const auto& macPalette = Palette::systemMacPalette();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int scanIndex = y * scanWidth + x;
            if (scanIndex >= static_cast<int>(expanded.size())) {
                break;
            }
            const int colorIndex = expanded[static_cast<std::size_t>(scanIndex)] & 0xFF;
            const auto rgb = macPalette.getRGB(colorIndex);
            bitmap.setPixelRGB(x, y, rgb[0], rgb[1], rgb[2]);
            indices[static_cast<std::size_t>(y * width + x)] = static_cast<std::uint8_t>(colorIndex);
        }
    }
    bitmap.setPaletteIndices(std::move(indices));
    return bitmap;
}

Bitmap BitmapDecoder::decode2Bit(const std::vector<std::uint8_t>& data,
                                 int width,
                                 int height,
                                 int scanWidth,
                                 const Palette* palette) {
    Bitmap bitmap(width, height, 2);
    std::vector<std::uint8_t> indices(static_cast<std::size_t>(width * height), 0);
    std::vector<std::uint8_t> expanded(data.size() * 4U, 0);

    for (std::size_t index = 0; index < data.size(); ++index) {
        const int value = data[index] & 0xFF;
        expanded[index * 4U] = static_cast<std::uint8_t>(scaledIndex((value & 0xC0) >> 6, 3));
        expanded[index * 4U + 1U] = static_cast<std::uint8_t>(scaledIndex((value & 0x30) >> 4, 3));
        expanded[index * 4U + 2U] = static_cast<std::uint8_t>(scaledIndex((value & 0x0C) >> 2, 3));
        expanded[index * 4U + 3U] = static_cast<std::uint8_t>(scaledIndex(value & 0x03, 3));
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int scanIndex = y * scanWidth + x;
            if (scanIndex >= static_cast<int>(expanded.size())) {
                break;
            }
            const int colorIndex = expanded[static_cast<std::size_t>(scanIndex)] & 0xFF;
            const auto rgb = palette ? palette->getRGB(colorIndex) : std::array<int, 3>{colorIndex, colorIndex, colorIndex};
            bitmap.setPixelRGB(x, y, rgb[0], rgb[1], rgb[2]);
            indices[static_cast<std::size_t>(y * width + x)] = static_cast<std::uint8_t>(colorIndex);
        }
    }
    bitmap.setPaletteIndices(std::move(indices));
    return bitmap;
}

Bitmap BitmapDecoder::decode4Bit(const std::vector<std::uint8_t>& data,
                                 int width,
                                 int height,
                                 int scanWidth,
                                 const Palette* palette) {
    Bitmap bitmap(width, height, 4);
    std::vector<std::uint8_t> indices(static_cast<std::size_t>(width * height), 0);
    std::vector<std::uint8_t> expanded(data.size() * 2U, 0);

    for (std::size_t index = 0; index < data.size(); ++index) {
        const int value = data[index] & 0xFF;
        expanded[index * 2U] = static_cast<std::uint8_t>(scaledIndex((value & 0xF0) >> 4, 15));
        expanded[index * 2U + 1U] = static_cast<std::uint8_t>(scaledIndex(value & 0x0F, 15));
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int scanIndex = y * scanWidth + x;
            if (scanIndex >= static_cast<int>(expanded.size())) {
                break;
            }
            const int colorIndex = expanded[static_cast<std::size_t>(scanIndex)] & 0xFF;
            const auto rgb = palette ? palette->getRGB(colorIndex) : std::array<int, 3>{colorIndex, colorIndex, colorIndex};
            bitmap.setPixelRGB(x, y, rgb[0], rgb[1], rgb[2]);
            indices[static_cast<std::size_t>(y * width + x)] = static_cast<std::uint8_t>(colorIndex);
        }
    }
    bitmap.setPaletteIndices(std::move(indices));
    return bitmap;
}

Bitmap BitmapDecoder::decode8Bit(const std::vector<std::uint8_t>& data,
                                 int width,
                                 int height,
                                 int scanWidth,
                                 const Palette* palette) {
    Bitmap bitmap(width, height, 8);
    std::vector<std::uint8_t> indices(static_cast<std::size_t>(width * height), 0);
    const Palette* resolvedPalette = palette ? palette : &Palette::systemMacPalette();

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int scanIndex = y * scanWidth + x;
            if (scanIndex >= static_cast<int>(data.size())) {
                break;
            }
            const int colorIndex = data[static_cast<std::size_t>(scanIndex)] & 0xFF;
            const auto rgb = resolvedPalette->getRGB(colorIndex);
            bitmap.setPixelRGB(x, y, rgb[0], rgb[1], rgb[2]);
            indices[static_cast<std::size_t>(y * width + x)] = static_cast<std::uint8_t>(colorIndex);
        }
    }
    bitmap.setPaletteIndices(std::move(indices));
    return bitmap;
}

Bitmap BitmapDecoder::decode16Bit(const std::vector<std::uint8_t>& data,
                                  int width,
                                  int height,
                                  int scanWidth,
                                  bool skipCompression) {
    Bitmap bitmap(width, height, 16);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int pixel16 = 0;
            if (skipCompression) {
                const int offset = (y * scanWidth + x) * 2;
                if (offset + 1 >= static_cast<int>(data.size())) {
                    continue;
                }
                pixel16 = ((data[static_cast<std::size_t>(offset)] & 0xFF) << 8) |
                          (data[static_cast<std::size_t>(offset + 1)] & 0xFF);
            } else {
                const int rowOffset = y * scanWidth * 2;
                const int highIndex = rowOffset + x;
                const int lowIndex = rowOffset + scanWidth + x;
                if (highIndex >= static_cast<int>(data.size()) || lowIndex >= static_cast<int>(data.size())) {
                    continue;
                }
                pixel16 = ((data[static_cast<std::size_t>(highIndex)] & 0xFF) << 8) |
                          (data[static_cast<std::size_t>(lowIndex)] & 0xFF);
            }

            const int r5 = (pixel16 >> 10) & 0x1F;
            const int g5 = (pixel16 >> 5) & 0x1F;
            const int b5 = pixel16 & 0x1F;
            bitmap.setPixelRGB(x, y, (r5 << 3) | (r5 >> 2), (g5 << 3) | (g5 >> 2), (b5 << 3) | (b5 >> 2));
        }
    }
    return bitmap;
}

Bitmap BitmapDecoder::decode32Bit(const std::vector<std::uint8_t>& data,
                                  int width,
                                  int height,
                                  int scanWidth,
                                  bool channelsSeparated) {
    Bitmap bitmap(width, height, 32);
    if (channelsSeparated) {
        for (int y = 0; y < height; ++y) {
            const int lineOffset = y * scanWidth * 4;
            for (int x = 0; x < width; ++x) {
                const int aIndex = lineOffset + x;
                const int rIndex = lineOffset + x + scanWidth;
                const int gIndex = lineOffset + x + (scanWidth * 2);
                const int bIndex = lineOffset + x + (scanWidth * 3);
                if (bIndex >= static_cast<int>(data.size())) {
                    continue;
                }
                bitmap.setPixelRGBA(x,
                                    y,
                                    data[static_cast<std::size_t>(rIndex)] & 0xFF,
                                    data[static_cast<std::size_t>(gIndex)] & 0xFF,
                                    data[static_cast<std::size_t>(bIndex)] & 0xFF,
                                    data[static_cast<std::size_t>(aIndex)] & 0xFF);
            }
        }
    } else {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int byteIndex = (y * scanWidth + x) * 4;
                if (byteIndex + 3 >= static_cast<int>(data.size())) {
                    continue;
                }
                bitmap.setPixelRGBA(x,
                                    y,
                                    data[static_cast<std::size_t>(byteIndex + 1)] & 0xFF,
                                    data[static_cast<std::size_t>(byteIndex + 2)] & 0xFF,
                                    data[static_cast<std::size_t>(byteIndex + 3)] & 0xFF,
                                    data[static_cast<std::size_t>(byteIndex)] & 0xFF);
            }
        }
    }
    return bitmap;
}

Bitmap BitmapDecoder::decode(const std::vector<std::uint8_t>& data,
                             int width,
                             int height,
                             int bitDepth,
                             const Palette* palette,
                             bool bigEndian,
                             int directorVersion,
                             int pitch) {
    (void)bigEndian;
    if (width <= 0 || height <= 0 || data.empty()) {
        return Bitmap(std::max(1, width), std::max(1, height), bitDepth);
    }

    const int numChannels = getNumChannels(bitDepth);
    const int alignmentWidth = getAlignmentWidth(bitDepth);
    int scanWidth = calculateScanWidthPixels(width, bitDepth, pitch);

    int expectedLen = 0;
    if (bitDepth == 32 && directorVersion >= 1000) {
        expectedLen = scanWidth * height * numChannels;
    } else if (bitDepth == 1) {
        expectedLen = (scanWidth / 8) * height;
    } else if (bitDepth == 2) {
        expectedLen = (scanWidth / 4) * height;
    } else if (bitDepth == 4) {
        expectedLen = (scanWidth / 2) * height;
    } else {
        expectedLen = scanWidth * height * numChannels;
    }

    bool skipCompression = static_cast<int>(data.size()) >= expectedLen;
    const bool zlib = isZlibData(data);
    if (zlib) {
        skipCompression = false;
    }

    std::vector<std::uint8_t> decompressed;
    if (skipCompression) {
        if (static_cast<int>(data.size()) > expectedLen) {
            decompressed.assign(data.begin(), data.begin() + expectedLen);
        } else {
            decompressed = data;
        }
    } else if (zlib) {
#ifdef LIBRESHOCKWAVE_HAVE_ZLIB
        try {
            decompressed = io::BinaryReader::decompressZlib(data);
            if (expectedLen >= 0 && decompressed.size() > static_cast<std::size_t>(expectedLen)) {
                decompressed.resize(static_cast<std::size_t>(expectedLen));
            }
        } catch (const std::exception&) {
            decompressed = decompressRLE(data, expectedLen);
        }
#else
        decompressed = decompressRLE(data, expectedLen);
#endif
    } else {
        decompressed = decompressRLE(data, expectedLen);
    }

    if (pitch > 0) {
        // Keep pitch-derived scan width.
    } else if (static_cast<int>(decompressed.size()) == width * height * numChannels) {
        scanWidth = width;
    } else if (bitDepth == 32 && directorVersion >= 1000) {
        scanWidth = width;
    } else if (width % alignmentWidth == 0) {
        scanWidth = width;
    } else {
        scanWidth = alignmentWidth * ((width + alignmentWidth - 1) / alignmentWidth);
    }

    switch (bitDepth) {
        case 1: return decode1Bit(decompressed, width, height, scanWidth, palette);
        case 2: return decode2Bit(decompressed, width, height, scanWidth, palette);
        case 4: return decode4Bit(decompressed, width, height, scanWidth, palette);
        case 8: return decode8Bit(decompressed, width, height, scanWidth, palette);
        case 16: return decode16Bit(decompressed, width, height, scanWidth, skipCompression);
        case 32: return decode32Bit(decompressed, width, height, scanWidth, directorVersion >= 1000);
        default: return decode8Bit(decompressed, width, height, scanWidth, palette);
    }
}

Bitmap BitmapDecoder::decode(const std::vector<std::uint8_t>& data,
                             int width,
                             int height,
                             int bitDepth,
                             const Palette* palette) {
    return decode(data, width, height, bitDepth, palette, true, 1200, 0);
}

int BitmapDecoder::getAlignmentWidth(int bitDepth) {
    switch (bitDepth) {
        case 1: return 16;
        case 4:
        case 32: return 4;
        case 2:
        case 8: return 2;
        case 16: return 1;
        default: return 2;
    }
}

int BitmapDecoder::getNumChannels(int bitDepth) {
    switch (bitDepth) {
        case 32: return 4;
        case 16: return 2;
        default: return 1;
    }
}

} // namespace libreshockwave::bitmap
