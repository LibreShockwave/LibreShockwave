#include "libreshockwave/bitmap/png_decoder.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string_view>

#ifdef LIBRESHOCKWAVE_HAVE_ZLIB
#include <zlib.h>
#endif

namespace libreshockwave::bitmap {
namespace {

constexpr std::array<std::uint8_t, 8> PNG_SIGNATURE{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
constexpr std::size_t MAX_INFLATED_BYTES = 256'000'000U;

struct PngInfo {
    int width = 0;
    int height = 0;
    int bit_depth = 0;
    int color_type = 0;
    int compression = 0;
    int filter = 0;
    int interlace = 0;
};

std::optional<std::uint32_t> readU32BE(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4U > data.size()) {
        return std::nullopt;
    }
    return (static_cast<std::uint32_t>(data[offset]) << 24U) |
           (static_cast<std::uint32_t>(data[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(data[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(data[offset + 3U]);
}

std::uint16_t readU16BE(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[offset]) << 8U) |
                                      static_cast<std::uint16_t>(data[offset + 1U]));
}

bool hasPngSignature(const std::vector<std::uint8_t>& data) {
    return data.size() >= PNG_SIGNATURE.size() &&
           std::equal(PNG_SIGNATURE.begin(), PNG_SIGNATURE.end(), data.begin());
}

int channelCount(int color_type) {
    switch (color_type) {
        case 0: return 1;
        case 2: return 3;
        case 3: return 1;
        case 4: return 2;
        case 6: return 4;
        default: return 0;
    }
}

std::uint8_t paethPredictor(std::uint8_t left, std::uint8_t up, std::uint8_t up_left) {
    const int a = left;
    const int b = up;
    const int c = up_left;
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) {
        return left;
    }
    if (pb <= pc) {
        return up;
    }
    return up_left;
}

std::optional<std::vector<std::uint8_t>> inflateZlib(const std::vector<std::uint8_t>& compressed,
                                                     std::size_t expected_size) {
#ifdef LIBRESHOCKWAVE_HAVE_ZLIB
    if (expected_size == 0 || expected_size > MAX_INFLATED_BYTES ||
        expected_size > std::numeric_limits<uLongf>::max()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> output(expected_size);
    uLongf output_size = static_cast<uLongf>(output.size());
    const int status = uncompress(output.data(),
                                  &output_size,
                                  compressed.data(),
                                  static_cast<uLong>(compressed.size()));
    if (status != Z_OK || output_size != static_cast<uLongf>(expected_size)) {
        return std::nullopt;
    }
    return output;
#else
    (void)compressed;
    (void)expected_size;
    return std::nullopt;
#endif
}

bool unfilterRows(const std::vector<std::uint8_t>& filtered,
                  int width,
                  int height,
                  int channels,
                  std::vector<std::uint8_t>& rows) {
    const std::size_t row_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
    rows.assign(static_cast<std::size_t>(height) * row_bytes, 0);
    const std::size_t filtered_row_bytes = row_bytes + 1U;
    if (filtered.size() != filtered_row_bytes * static_cast<std::size_t>(height)) {
        return false;
    }

    for (int y = 0; y < height; ++y) {
        const auto row_index = static_cast<std::size_t>(y);
        const auto filter = filtered[row_index * filtered_row_bytes];
        const auto* src = filtered.data() + row_index * filtered_row_bytes + 1U;
        auto* dst = rows.data() + row_index * row_bytes;
        const auto* prev = y > 0 ? rows.data() + (row_index - 1U) * row_bytes : nullptr;

        for (std::size_t x = 0; x < row_bytes; ++x) {
            const std::uint8_t left = x >= static_cast<std::size_t>(channels) ? dst[x - channels] : 0;
            const std::uint8_t up = prev != nullptr ? prev[x] : 0;
            const std::uint8_t up_left = prev != nullptr && x >= static_cast<std::size_t>(channels)
                ? prev[x - channels]
                : 0;
            int value = src[x];
            switch (filter) {
                case 0: break;
                case 1: value += left; break;
                case 2: value += up; break;
                case 3: value += (static_cast<int>(left) + static_cast<int>(up)) / 2; break;
                case 4: value += paethPredictor(left, up, up_left); break;
                default: return false;
            }
            dst[x] = static_cast<std::uint8_t>(value & 0xFF);
        }
    }
    return true;
}

std::optional<Bitmap> rowsToBitmap(const PngInfo& info,
                                   const std::vector<std::uint8_t>& rows,
                                   const std::vector<std::uint32_t>& palette,
                                   const std::vector<std::uint8_t>& palette_alpha,
                                   const std::optional<std::uint16_t>& transparent_gray,
                                   const std::optional<std::array<std::uint16_t, 3>>& transparent_rgb) {
    Bitmap bitmap(info.width, info.height, info.color_type == 3 ? 8 : 32);
    std::vector<std::uint8_t> indices;
    std::shared_ptr<const Palette> image_palette;
    if (info.color_type == 3) {
        if (palette.empty()) {
            return std::nullopt;
        }
        indices.assign(static_cast<std::size_t>(info.width) * static_cast<std::size_t>(info.height), 0);
        image_palette = std::make_shared<Palette>(palette, "PNG Palette");
    }

    bool has_alpha = false;
    const int channels = channelCount(info.color_type);
    const std::size_t row_bytes = static_cast<std::size_t>(info.width) * static_cast<std::size_t>(channels);
    for (int y = 0; y < info.height; ++y) {
        const auto* row = rows.data() + static_cast<std::size_t>(y) * row_bytes;
        for (int x = 0; x < info.width; ++x) {
            const auto pos = static_cast<std::size_t>(y * info.width + x);
            const auto offset = static_cast<std::size_t>(x) * static_cast<std::size_t>(channels);
            std::uint32_t rgb = 0;
            std::uint8_t alpha = 0xFF;

            switch (info.color_type) {
                case 0: {
                    const auto gray = row[offset];
                    rgb = (static_cast<std::uint32_t>(gray) << 16U) |
                          (static_cast<std::uint32_t>(gray) << 8U) |
                          static_cast<std::uint32_t>(gray);
                    if (transparent_gray.has_value() && *transparent_gray == gray) {
                        alpha = 0;
                    }
                    break;
                }
                case 2: {
                    const auto r = row[offset];
                    const auto g = row[offset + 1U];
                    const auto b = row[offset + 2U];
                    rgb = (static_cast<std::uint32_t>(r) << 16U) |
                          (static_cast<std::uint32_t>(g) << 8U) |
                          static_cast<std::uint32_t>(b);
                    if (transparent_rgb.has_value() &&
                        (*transparent_rgb)[0] == r &&
                        (*transparent_rgb)[1] == g &&
                        (*transparent_rgb)[2] == b) {
                        alpha = 0;
                    }
                    break;
                }
                case 3: {
                    const auto index = row[offset];
                    if (index >= palette.size()) {
                        return std::nullopt;
                    }
                    rgb = palette[index] & 0x00FFFFFFU;
                    if (index < palette_alpha.size()) {
                        alpha = palette_alpha[index];
                    }
                    indices[pos] = index;
                    break;
                }
                case 4: {
                    const auto gray = row[offset];
                    rgb = (static_cast<std::uint32_t>(gray) << 16U) |
                          (static_cast<std::uint32_t>(gray) << 8U) |
                          static_cast<std::uint32_t>(gray);
                    alpha = row[offset + 1U];
                    break;
                }
                case 6: {
                    alpha = row[offset + 3U];
                    rgb = (static_cast<std::uint32_t>(row[offset]) << 16U) |
                          (static_cast<std::uint32_t>(row[offset + 1U]) << 8U) |
                          static_cast<std::uint32_t>(row[offset + 2U]);
                    break;
                }
                default:
                    return std::nullopt;
            }

            if (alpha < 0xFF) {
                has_alpha = true;
            }
            bitmap.setPixel(x, y, (static_cast<std::uint32_t>(alpha) << 24U) | rgb);
        }
    }

    if (info.color_type == 3) {
        bitmap.setPaletteIndices(std::move(indices));
        bitmap.setImagePalette(std::move(image_palette));
    }
    if (has_alpha) {
        bitmap.setNativeAlpha(true);
    }
    return bitmap;
}

} // namespace

std::optional<Bitmap> PngDecoder::decode(const std::vector<std::uint8_t>& data) {
    if (!hasPngSignature(data)) {
        return std::nullopt;
    }

    PngInfo info;
    bool have_ihdr = false;
    bool have_iend = false;
    std::vector<std::uint8_t> idat;
    std::vector<std::uint32_t> palette;
    std::vector<std::uint8_t> palette_alpha;
    std::optional<std::uint16_t> transparent_gray;
    std::optional<std::array<std::uint16_t, 3>> transparent_rgb;

    std::size_t offset = PNG_SIGNATURE.size();
    while (offset + 12U <= data.size()) {
        const auto raw_length = readU32BE(data, offset);
        if (!raw_length.has_value()) {
            return std::nullopt;
        }
        offset += 4U;
        if (*raw_length > data.size() - offset - 8U) {
            return std::nullopt;
        }
        const auto chunk_length = static_cast<std::size_t>(*raw_length);
        const std::string_view type(reinterpret_cast<const char*>(data.data() + offset), 4);
        offset += 4U;
        const std::size_t chunk_offset = offset;
        offset += chunk_length;
        offset += 4U;

        if (type == "IHDR") {
            if (have_ihdr || chunk_length != 13U) {
                return std::nullopt;
            }
            const auto raw_width = readU32BE(data, chunk_offset);
            const auto raw_height = readU32BE(data, chunk_offset + 4U);
            if (!raw_width.has_value() || !raw_height.has_value() ||
                *raw_width == 0 || *raw_height == 0 ||
                *raw_width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
                *raw_height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
                return std::nullopt;
            }
            info.width = static_cast<int>(*raw_width);
            info.height = static_cast<int>(*raw_height);
            info.bit_depth = data[chunk_offset + 8U];
            info.color_type = data[chunk_offset + 9U];
            info.compression = data[chunk_offset + 10U];
            info.filter = data[chunk_offset + 11U];
            info.interlace = data[chunk_offset + 12U];
            have_ihdr = true;
        } else if (type == "PLTE") {
            if (chunk_length % 3U != 0) {
                return std::nullopt;
            }
            palette.clear();
            palette.reserve(chunk_length / 3U);
            for (std::size_t i = 0; i < chunk_length; i += 3U) {
                const auto r = data[chunk_offset + i];
                const auto g = data[chunk_offset + i + 1U];
                const auto b = data[chunk_offset + i + 2U];
                palette.push_back((static_cast<std::uint32_t>(r) << 16U) |
                                  (static_cast<std::uint32_t>(g) << 8U) |
                                  static_cast<std::uint32_t>(b));
            }
        } else if (type == "tRNS") {
            if (!have_ihdr) {
                return std::nullopt;
            }
            if (info.color_type == 3) {
                palette_alpha.assign(data.begin() + static_cast<std::ptrdiff_t>(chunk_offset),
                                    data.begin() + static_cast<std::ptrdiff_t>(chunk_offset + chunk_length));
            } else if (info.color_type == 0 && chunk_length >= 2U) {
                transparent_gray = readU16BE(data, chunk_offset);
            } else if (info.color_type == 2 && chunk_length >= 6U) {
                transparent_rgb = std::array<std::uint16_t, 3>{
                    readU16BE(data, chunk_offset),
                    readU16BE(data, chunk_offset + 2U),
                    readU16BE(data, chunk_offset + 4U)
                };
            }
        } else if (type == "IDAT") {
            idat.insert(idat.end(),
                        data.begin() + static_cast<std::ptrdiff_t>(chunk_offset),
                        data.begin() + static_cast<std::ptrdiff_t>(chunk_offset + chunk_length));
        } else if (type == "IEND") {
            have_iend = true;
            break;
        }
    }

    const int channels = channelCount(info.color_type);
    if (!have_ihdr || !have_iend || idat.empty() ||
        info.bit_depth != 8 ||
        channels <= 0 ||
        info.compression != 0 ||
        info.filter != 0 ||
        info.interlace != 0) {
        return std::nullopt;
    }

    const auto width = static_cast<std::size_t>(info.width);
    const auto height = static_cast<std::size_t>(info.height);
    if (width > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(channels)) {
        return std::nullopt;
    }
    const auto row_bytes = width * static_cast<std::size_t>(channels);
    if (height > std::numeric_limits<std::size_t>::max() / (row_bytes + 1U)) {
        return std::nullopt;
    }
    const auto expected_inflated_size = (row_bytes + 1U) * height;
    auto inflated = inflateZlib(idat, expected_inflated_size);
    if (!inflated.has_value()) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> rows;
    if (!unfilterRows(*inflated, info.width, info.height, channels, rows)) {
        return std::nullopt;
    }
    return rowsToBitmap(info, rows, palette, palette_alpha, transparent_gray, transparent_rgb);
}

} // namespace libreshockwave::bitmap
