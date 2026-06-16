#include "libreshockwave/bitmap/PngDecoder.hpp"

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
    int bitDepth = 0;
    int colorType = 0;
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

int channelCount(int colorType) {
    switch (colorType) {
        case 0: return 1;
        case 2: return 3;
        case 3: return 1;
        case 4: return 2;
        case 6: return 4;
        default: return 0;
    }
}

std::uint8_t paethPredictor(std::uint8_t left, std::uint8_t up, std::uint8_t upLeft) {
    const int a = left;
    const int b = up;
    const int c = upLeft;
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
    return upLeft;
}

std::optional<std::vector<std::uint8_t>> inflateZlib(const std::vector<std::uint8_t>& compressed,
                                                     std::size_t expectedSize) {
#ifdef LIBRESHOCKWAVE_HAVE_ZLIB
    if (expectedSize == 0 || expectedSize > MAX_INFLATED_BYTES ||
        expectedSize > std::numeric_limits<uLongf>::max()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> output(expectedSize);
    uLongf outputSize = static_cast<uLongf>(output.size());
    const int status = uncompress(output.data(),
                                  &outputSize,
                                  compressed.data(),
                                  static_cast<uLong>(compressed.size()));
    if (status != Z_OK || outputSize != static_cast<uLongf>(expectedSize)) {
        return std::nullopt;
    }
    return output;
#else
    (void)compressed;
    (void)expectedSize;
    return std::nullopt;
#endif
}

bool unfilterRows(const std::vector<std::uint8_t>& filtered,
                  int width,
                  int height,
                  int channels,
                  std::vector<std::uint8_t>& rows) {
    const std::size_t rowBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
    rows.assign(static_cast<std::size_t>(height) * rowBytes, 0);
    const std::size_t filteredRowBytes = rowBytes + 1U;
    if (filtered.size() != filteredRowBytes * static_cast<std::size_t>(height)) {
        return false;
    }

    for (int y = 0; y < height; ++y) {
        const auto rowIndex = static_cast<std::size_t>(y);
        const auto filter = filtered[rowIndex * filteredRowBytes];
        const auto* src = filtered.data() + rowIndex * filteredRowBytes + 1U;
        auto* dst = rows.data() + rowIndex * rowBytes;
        const auto* prev = y > 0 ? rows.data() + (rowIndex - 1U) * rowBytes : nullptr;

        for (std::size_t x = 0; x < rowBytes; ++x) {
            const std::uint8_t left = x >= static_cast<std::size_t>(channels) ? dst[x - channels] : 0;
            const std::uint8_t up = prev != nullptr ? prev[x] : 0;
            const std::uint8_t upLeft = prev != nullptr && x >= static_cast<std::size_t>(channels)
                ? prev[x - channels]
                : 0;
            int value = src[x];
            switch (filter) {
                case 0: break;
                case 1: value += left; break;
                case 2: value += up; break;
                case 3: value += (static_cast<int>(left) + static_cast<int>(up)) / 2; break;
                case 4: value += paethPredictor(left, up, upLeft); break;
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
                                   const std::vector<std::uint8_t>& paletteAlpha,
                                   const std::optional<std::uint16_t>& transparentGray,
                                   const std::optional<std::array<std::uint16_t, 3>>& transparentRgb) {
    Bitmap bitmap(info.width, info.height, info.colorType == 3 ? 8 : 32);
    std::vector<std::uint8_t> indices;
    std::shared_ptr<const Palette> imagePalette;
    if (info.colorType == 3) {
        if (palette.empty()) {
            return std::nullopt;
        }
        indices.assign(static_cast<std::size_t>(info.width) * static_cast<std::size_t>(info.height), 0);
        imagePalette = std::make_shared<Palette>(palette, "PNG Palette");
    }

    bool hasAlpha = false;
    const int channels = channelCount(info.colorType);
    const std::size_t rowBytes = static_cast<std::size_t>(info.width) * static_cast<std::size_t>(channels);
    for (int y = 0; y < info.height; ++y) {
        const auto* row = rows.data() + static_cast<std::size_t>(y) * rowBytes;
        for (int x = 0; x < info.width; ++x) {
            const auto pos = static_cast<std::size_t>(y * info.width + x);
            const auto offset = static_cast<std::size_t>(x) * static_cast<std::size_t>(channels);
            std::uint32_t rgb = 0;
            std::uint8_t alpha = 0xFF;

            switch (info.colorType) {
                case 0: {
                    const auto gray = row[offset];
                    rgb = (static_cast<std::uint32_t>(gray) << 16U) |
                          (static_cast<std::uint32_t>(gray) << 8U) |
                          static_cast<std::uint32_t>(gray);
                    if (transparentGray.has_value() && *transparentGray == gray) {
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
                    if (transparentRgb.has_value() &&
                        (*transparentRgb)[0] == r &&
                        (*transparentRgb)[1] == g &&
                        (*transparentRgb)[2] == b) {
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
                    if (index < paletteAlpha.size()) {
                        alpha = paletteAlpha[index];
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
                hasAlpha = true;
            }
            bitmap.setPixel(x, y, (static_cast<std::uint32_t>(alpha) << 24U) | rgb);
        }
    }

    if (info.colorType == 3) {
        bitmap.setPaletteIndices(std::move(indices));
        bitmap.setImagePalette(std::move(imagePalette));
    }
    if (hasAlpha) {
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
    bool haveIhdr = false;
    bool haveIend = false;
    std::vector<std::uint8_t> idat;
    std::vector<std::uint32_t> palette;
    std::vector<std::uint8_t> paletteAlpha;
    std::optional<std::uint16_t> transparentGray;
    std::optional<std::array<std::uint16_t, 3>> transparentRgb;

    std::size_t offset = PNG_SIGNATURE.size();
    while (offset + 12U <= data.size()) {
        const auto rawLength = readU32BE(data, offset);
        if (!rawLength.has_value()) {
            return std::nullopt;
        }
        offset += 4U;
        if (*rawLength > data.size() - offset - 8U) {
            return std::nullopt;
        }
        const auto chunkLength = static_cast<std::size_t>(*rawLength);
        const std::string_view type(reinterpret_cast<const char*>(data.data() + offset), 4);
        offset += 4U;
        const std::size_t chunkOffset = offset;
        offset += chunkLength;
        offset += 4U;

        if (type == "IHDR") {
            if (haveIhdr || chunkLength != 13U) {
                return std::nullopt;
            }
            const auto rawWidth = readU32BE(data, chunkOffset);
            const auto rawHeight = readU32BE(data, chunkOffset + 4U);
            if (!rawWidth.has_value() || !rawHeight.has_value() ||
                *rawWidth == 0 || *rawHeight == 0 ||
                *rawWidth > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
                *rawHeight > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
                return std::nullopt;
            }
            info.width = static_cast<int>(*rawWidth);
            info.height = static_cast<int>(*rawHeight);
            info.bitDepth = data[chunkOffset + 8U];
            info.colorType = data[chunkOffset + 9U];
            info.compression = data[chunkOffset + 10U];
            info.filter = data[chunkOffset + 11U];
            info.interlace = data[chunkOffset + 12U];
            haveIhdr = true;
        } else if (type == "PLTE") {
            if (chunkLength % 3U != 0) {
                return std::nullopt;
            }
            palette.clear();
            palette.reserve(chunkLength / 3U);
            for (std::size_t i = 0; i < chunkLength; i += 3U) {
                const auto r = data[chunkOffset + i];
                const auto g = data[chunkOffset + i + 1U];
                const auto b = data[chunkOffset + i + 2U];
                palette.push_back((static_cast<std::uint32_t>(r) << 16U) |
                                  (static_cast<std::uint32_t>(g) << 8U) |
                                  static_cast<std::uint32_t>(b));
            }
        } else if (type == "tRNS") {
            if (!haveIhdr) {
                return std::nullopt;
            }
            if (info.colorType == 3) {
                paletteAlpha.assign(data.begin() + static_cast<std::ptrdiff_t>(chunkOffset),
                                    data.begin() + static_cast<std::ptrdiff_t>(chunkOffset + chunkLength));
            } else if (info.colorType == 0 && chunkLength >= 2U) {
                transparentGray = readU16BE(data, chunkOffset);
            } else if (info.colorType == 2 && chunkLength >= 6U) {
                transparentRgb = std::array<std::uint16_t, 3>{
                    readU16BE(data, chunkOffset),
                    readU16BE(data, chunkOffset + 2U),
                    readU16BE(data, chunkOffset + 4U)
                };
            }
        } else if (type == "IDAT") {
            idat.insert(idat.end(),
                        data.begin() + static_cast<std::ptrdiff_t>(chunkOffset),
                        data.begin() + static_cast<std::ptrdiff_t>(chunkOffset + chunkLength));
        } else if (type == "IEND") {
            haveIend = true;
            break;
        }
    }

    const int channels = channelCount(info.colorType);
    if (!haveIhdr || !haveIend || idat.empty() ||
        info.bitDepth != 8 ||
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
    const auto rowBytes = width * static_cast<std::size_t>(channels);
    if (height > std::numeric_limits<std::size_t>::max() / (rowBytes + 1U)) {
        return std::nullopt;
    }
    const auto expectedInflatedSize = (rowBytes + 1U) * height;
    auto inflated = inflateZlib(idat, expectedInflatedSize);
    if (!inflated.has_value()) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> rows;
    if (!unfilterRows(*inflated, info.width, info.height, channels, rows)) {
        return std::nullopt;
    }
    return rowsToBitmap(info, rows, palette, paletteAlpha, transparentGray, transparentRgb);
}

} // namespace libreshockwave::bitmap
