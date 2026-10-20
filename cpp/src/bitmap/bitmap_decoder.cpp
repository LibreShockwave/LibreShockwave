#include "libreshockwave/bitmap/bitmap_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>

#include "libreshockwave/io/binary_reader.hpp"

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
                                                       int expected_size) {
    if (expected_size <= 0) {
        return {};
    }
    expected_size = std::min(expected_size, 100'000'000);

    std::vector<std::uint8_t> result(static_cast<std::size_t>(expected_size), 0);
    std::size_t pos = 0;
    std::size_t out_pos = 0;

    while (pos < compressed.size() && out_pos < result.size()) {
        const int control = compressed[pos++] & 0xFF;
        if (control < 0x80) {
            const int count = control + 1;
            for (int index = 0; index < count && pos < compressed.size() && out_pos < result.size(); ++index) {
                result[out_pos++] = compressed[pos++];
            }
        } else if (control == 0x80) {
            continue;
        } else {
            const int count = 257 - control;
            if (pos < compressed.size()) {
                const auto value = compressed[pos++];
                for (int index = 0; index < count && out_pos < result.size(); ++index) {
                    result[out_pos++] = value;
                }
            }
        }
    }

    if (out_pos < result.size()) {
        result.resize(out_pos);
    }
    return result;
}

int BitmapDecoder::calculateScanWidthPixels(int width, int bit_depth, int pitch) {
    if (pitch > 0 && bit_depth > 0) {
        return (pitch * 8) / bit_depth;
    }

    const int alignment_width = getAlignmentWidth(bit_depth);
    if (width % alignment_width == 0) {
        return width;
    }
    return alignment_width * ((width + alignment_width - 1) / alignment_width);
}

int BitmapDecoder::calculateScanWidth(int width, int bit_depth) {
    const int bits_per_row = width * bit_depth;
    return ((bits_per_row + 15) / 16) * 2;
}

Bitmap BitmapDecoder::decode1Bit(const std::vector<std::uint8_t>& data,
                                 int width,
                                 int height,
                                 int scan_width,
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

    const auto& mac_palette = Palette::systemMacPalette();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int scan_index = y * scan_width + x;
            if (scan_index >= static_cast<int>(expanded.size())) {
                break;
            }
            const int color_index = expanded[static_cast<std::size_t>(scan_index)] & 0xFF;
            const auto rgb = mac_palette.getRGB(color_index);
            bitmap.setPixelRGB(x, y, rgb[0], rgb[1], rgb[2]);
            indices[static_cast<std::size_t>(y * width + x)] = static_cast<std::uint8_t>(color_index);
        }
    }
    bitmap.setPaletteIndices(std::move(indices));
    return bitmap;
}

Bitmap BitmapDecoder::decode2Bit(const std::vector<std::uint8_t>& data,
                                 int width,
                                 int height,
                                 int scan_width,
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
            const int scan_index = y * scan_width + x;
            if (scan_index >= static_cast<int>(expanded.size())) {
                break;
            }
            const int color_index = expanded[static_cast<std::size_t>(scan_index)] & 0xFF;
            const auto rgb = palette ? palette->getRGB(color_index) : std::array<int, 3>{color_index, color_index, color_index};
            bitmap.setPixelRGB(x, y, rgb[0], rgb[1], rgb[2]);
            indices[static_cast<std::size_t>(y * width + x)] = static_cast<std::uint8_t>(color_index);
        }
    }
    bitmap.setPaletteIndices(std::move(indices));
    return bitmap;
}

Bitmap BitmapDecoder::decode4Bit(const std::vector<std::uint8_t>& data,
                                 int width,
                                 int height,
                                 int scan_width,
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
            const int scan_index = y * scan_width + x;
            if (scan_index >= static_cast<int>(expanded.size())) {
                break;
            }
            const int color_index = expanded[static_cast<std::size_t>(scan_index)] & 0xFF;
            const auto rgb = palette ? palette->getRGB(color_index) : std::array<int, 3>{color_index, color_index, color_index};
            bitmap.setPixelRGB(x, y, rgb[0], rgb[1], rgb[2]);
            indices[static_cast<std::size_t>(y * width + x)] = static_cast<std::uint8_t>(color_index);
        }
    }
    bitmap.setPaletteIndices(std::move(indices));
    return bitmap;
}

Bitmap BitmapDecoder::decode8Bit(const std::vector<std::uint8_t>& data,
                                 int width,
                                 int height,
                                 int scan_width,
                                 const Palette* palette) {
    Bitmap bitmap(width, height, 8);
    std::vector<std::uint8_t> indices(static_cast<std::size_t>(width * height), 0);
    const Palette* resolved_palette = palette ? palette : &Palette::systemMacPalette();

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int scan_index = y * scan_width + x;
            if (scan_index >= static_cast<int>(data.size())) {
                break;
            }
            const int color_index = data[static_cast<std::size_t>(scan_index)] & 0xFF;
            const auto rgb = resolved_palette->getRGB(color_index);
            bitmap.setPixelRGB(x, y, rgb[0], rgb[1], rgb[2]);
            indices[static_cast<std::size_t>(y * width + x)] = static_cast<std::uint8_t>(color_index);
        }
    }
    bitmap.setPaletteIndices(std::move(indices));
    return bitmap;
}

Bitmap BitmapDecoder::decode16Bit(const std::vector<std::uint8_t>& data,
                                  int width,
                                  int height,
                                  int scan_width,
                                  bool skip_compression) {
    Bitmap bitmap(width, height, 16);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int pixel16 = 0;
            if (skip_compression) {
                const int offset = (y * scan_width + x) * 2;
                if (offset + 1 >= static_cast<int>(data.size())) {
                    continue;
                }
                pixel16 = ((data[static_cast<std::size_t>(offset)] & 0xFF) << 8) |
                          (data[static_cast<std::size_t>(offset + 1)] & 0xFF);
            } else {
                const int row_offset = y * scan_width * 2;
                const int high_index = row_offset + x;
                const int low_index = row_offset + scan_width + x;
                if (high_index >= static_cast<int>(data.size()) || low_index >= static_cast<int>(data.size())) {
                    continue;
                }
                pixel16 = ((data[static_cast<std::size_t>(high_index)] & 0xFF) << 8) |
                          (data[static_cast<std::size_t>(low_index)] & 0xFF);
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
                                  int scan_width,
                                  bool channels_separated) {
    Bitmap bitmap(width, height, 32);
    if (channels_separated) {
        for (int y = 0; y < height; ++y) {
            const int line_offset = y * scan_width * 4;
            for (int x = 0; x < width; ++x) {
                const int a_index = line_offset + x;
                const int r_index = line_offset + x + scan_width;
                const int g_index = line_offset + x + (scan_width * 2);
                const int b_index = line_offset + x + (scan_width * 3);
                if (b_index >= static_cast<int>(data.size())) {
                    continue;
                }
                bitmap.setPixelRGBA(x,
                                    y,
                                    data[static_cast<std::size_t>(r_index)] & 0xFF,
                                    data[static_cast<std::size_t>(g_index)] & 0xFF,
                                    data[static_cast<std::size_t>(b_index)] & 0xFF,
                                    data[static_cast<std::size_t>(a_index)] & 0xFF);
            }
        }
    } else {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int byte_index = (y * scan_width + x) * 4;
                if (byte_index + 3 >= static_cast<int>(data.size())) {
                    continue;
                }
                bitmap.setPixelRGBA(x,
                                    y,
                                    data[static_cast<std::size_t>(byte_index + 1)] & 0xFF,
                                    data[static_cast<std::size_t>(byte_index + 2)] & 0xFF,
                                    data[static_cast<std::size_t>(byte_index + 3)] & 0xFF,
                                    data[static_cast<std::size_t>(byte_index)] & 0xFF);
            }
        }
    }
    return bitmap;
}

Bitmap BitmapDecoder::decode(const std::vector<std::uint8_t>& data,
                             int width,
                             int height,
                             int bit_depth,
                             const Palette* palette,
                             bool big_endian,
                             int director_version,
                             int pitch) {
    (void)big_endian;
    if (width <= 0 || height <= 0 || data.empty()) {
        return Bitmap(std::max(1, width), std::max(1, height), bit_depth);
    }

    const int num_channels = getNumChannels(bit_depth);
    const int alignment_width = getAlignmentWidth(bit_depth);
    int scan_width = calculateScanWidthPixels(width, bit_depth, pitch);

    int expected_len = 0;
    if (bit_depth == 32 && director_version >= 1000) {
        expected_len = scan_width * height * num_channels;
    } else if (bit_depth == 1) {
        expected_len = (scan_width / 8) * height;
    } else if (bit_depth == 2) {
        expected_len = (scan_width / 4) * height;
    } else if (bit_depth == 4) {
        expected_len = (scan_width / 2) * height;
    } else {
        expected_len = scan_width * height * num_channels;
    }

    bool skip_compression = static_cast<int>(data.size()) >= expected_len;
    const bool zlib = isZlibData(data);
    if (zlib) {
        skip_compression = false;
    }

    std::vector<std::uint8_t> decompressed;
    if (skip_compression) {
        if (static_cast<int>(data.size()) > expected_len) {
            decompressed.assign(data.begin(), data.begin() + expected_len);
        } else {
            decompressed = data;
        }
    } else if (zlib) {
#ifdef LIBRESHOCKWAVE_HAVE_ZLIB
        try {
            decompressed = io::BinaryReader::decompressZlib(data);
            if (expected_len >= 0 && decompressed.size() > static_cast<std::size_t>(expected_len)) {
                decompressed.resize(static_cast<std::size_t>(expected_len));
            }
        } catch (const std::exception&) {
            decompressed = decompressRLE(data, expected_len);
        }
#else
        decompressed = decompressRLE(data, expected_len);
#endif
    } else {
        decompressed = decompressRLE(data, expected_len);
    }

    if (pitch > 0) {
        // Keep pitch-derived scan width.
    } else if (static_cast<int>(decompressed.size()) == width * height * num_channels) {
        scan_width = width;
    } else if (bit_depth == 32 && director_version >= 1000) {
        scan_width = width;
    } else if (width % alignment_width == 0) {
        scan_width = width;
    } else {
        scan_width = alignment_width * ((width + alignment_width - 1) / alignment_width);
    }

    switch (bit_depth) {
        case 1: return decode1Bit(decompressed, width, height, scan_width, palette);
        case 2: return decode2Bit(decompressed, width, height, scan_width, palette);
        case 4: return decode4Bit(decompressed, width, height, scan_width, palette);
        case 8: return decode8Bit(decompressed, width, height, scan_width, palette);
        case 16: return decode16Bit(decompressed, width, height, scan_width, skip_compression);
        case 32: return decode32Bit(decompressed, width, height, scan_width, director_version >= 1000);
        default: return decode8Bit(decompressed, width, height, scan_width, palette);
    }
}

Bitmap BitmapDecoder::decode(const std::vector<std::uint8_t>& data,
                             int width,
                             int height,
                             int bit_depth,
                             const Palette* palette) {
    return decode(data, width, height, bit_depth, palette, true, 1200, 0);
}

int BitmapDecoder::getAlignmentWidth(int bit_depth) {
    switch (bit_depth) {
        case 1: return 16;
        case 4:
        case 32: return 4;
        case 2:
        case 8: return 2;
        case 16: return 1;
        default: return 2;
    }
}

int BitmapDecoder::getNumChannels(int bit_depth) {
    switch (bit_depth) {
        case 32: return 4;
        case 16: return 2;
        default: return 1;
    }
}

} // namespace libreshockwave::bitmap
