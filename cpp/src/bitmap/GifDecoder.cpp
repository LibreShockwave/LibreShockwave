#include "libreshockwave/bitmap/GifDecoder.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace libreshockwave::bitmap {
namespace {

std::uint16_t readU16LE(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[offset + 1]) << 8U);
}

bool hasSignature(const std::vector<std::uint8_t>& data) {
    if (data.size() < 13) {
        return false;
    }
    const std::string_view signature(reinterpret_cast<const char*>(data.data()), 6);
    return signature == "GIF87a" || signature == "GIF89a";
}

std::optional<std::vector<std::uint32_t>> readColorTable(const std::vector<std::uint8_t>& data,
                                                         std::size_t& offset,
                                                         int colorCount) {
    if (colorCount <= 0 || offset + static_cast<std::size_t>(colorCount) * 3U > data.size()) {
        return std::nullopt;
    }

    std::vector<std::uint32_t> colors;
    colors.reserve(static_cast<std::size_t>(colorCount));
    for (int index = 0; index < colorCount; ++index) {
        const std::uint32_t r = data[offset++];
        const std::uint32_t g = data[offset++];
        const std::uint32_t b = data[offset++];
        colors.push_back(0xFF000000U | (r << 16U) | (g << 8U) | b);
    }
    return colors;
}

bool skipSubBlocks(const std::vector<std::uint8_t>& data, std::size_t& offset) {
    while (offset < data.size()) {
        const std::size_t blockSize = data[offset++];
        if (blockSize == 0) {
            return true;
        }
        if (offset + blockSize > data.size()) {
            return false;
        }
        offset += blockSize;
    }
    return false;
}

std::optional<std::vector<std::uint8_t>> readSubBlocks(const std::vector<std::uint8_t>& data,
                                                       std::size_t& offset) {
    std::vector<std::uint8_t> bytes;
    while (offset < data.size()) {
        const std::size_t blockSize = data[offset++];
        if (blockSize == 0) {
            return bytes;
        }
        if (offset + blockSize > data.size()) {
            return std::nullopt;
        }
        bytes.insert(bytes.end(), data.begin() + static_cast<std::ptrdiff_t>(offset),
                     data.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));
        offset += blockSize;
    }
    return std::nullopt;
}

class BitReader {
public:
    explicit BitReader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    std::optional<int> read(int bitCount) {
        int value = 0;
        for (int bit = 0; bit < bitCount; ++bit) {
            if (bitOffset_ / 8U >= bytes_.size()) {
                return std::nullopt;
            }
            const auto byte = bytes_[bitOffset_ / 8U];
            value |= static_cast<int>((byte >> (bitOffset_ % 8U)) & 1U) << bit;
            ++bitOffset_;
        }
        return value;
    }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t bitOffset_{0};
};

std::optional<std::vector<std::uint8_t>> decodeLzw(const std::vector<std::uint8_t>& bytes,
                                                   int minCodeSize,
                                                   std::size_t expectedPixels) {
    if (minCodeSize < 2 || minCodeSize > 8) {
        return std::nullopt;
    }

    const int clearCode = 1 << minCodeSize;
    const int endCode = clearCode + 1;
    int nextCode = endCode + 1;
    int codeSize = minCodeSize + 1;

    std::array<std::vector<std::uint8_t>, 4096> table;
    const auto resetTable = [&]() {
        for (auto& entry : table) {
            entry.clear();
        }
        for (int code = 0; code < clearCode; ++code) {
            table[code] = {static_cast<std::uint8_t>(code)};
        }
        nextCode = endCode + 1;
        codeSize = minCodeSize + 1;
    };
    resetTable();

    BitReader reader(bytes);
    std::vector<std::uint8_t> output;
    output.reserve(expectedPixels);
    std::vector<std::uint8_t> previous;

    while (auto codeValue = reader.read(codeSize)) {
        const int code = *codeValue;
        if (code == clearCode) {
            resetTable();
            previous.clear();
            continue;
        }
        if (code == endCode) {
            break;
        }

        std::vector<std::uint8_t> entry;
        if (code >= 0 && code < nextCode && !table[code].empty()) {
            entry = table[code];
        } else if (code == nextCode && !previous.empty()) {
            entry = previous;
            entry.push_back(previous.front());
        } else {
            return std::nullopt;
        }

        output.insert(output.end(), entry.begin(), entry.end());
        if (!previous.empty() && nextCode < static_cast<int>(table.size())) {
            auto added = previous;
            added.push_back(entry.front());
            table[nextCode++] = std::move(added);
            if (nextCode == (1 << codeSize) && codeSize < 12) {
                ++codeSize;
            }
        }
        previous = std::move(entry);
        if (output.size() >= expectedPixels) {
            output.resize(expectedPixels);
            return output;
        }
    }

    if (output.size() < expectedPixels) {
        return std::nullopt;
    }
    output.resize(expectedPixels);
    return output;
}

std::vector<int> interlacedRows(int height) {
    std::vector<int> rows;
    rows.reserve(static_cast<std::size_t>(std::max(0, height)));
    for (const auto [start, step] : {std::pair{0, 8}, std::pair{4, 8}, std::pair{2, 4}, std::pair{1, 2}}) {
        for (int y = start; y < height; y += step) {
            rows.push_back(y);
        }
    }
    return rows;
}

} // namespace

std::optional<Bitmap> GifDecoder::decode(const std::vector<std::uint8_t>& data) {
    if (!hasSignature(data)) {
        return std::nullopt;
    }

    const int screenWidth = readU16LE(data, 6);
    const int screenHeight = readU16LE(data, 8);
    if (screenWidth <= 0 || screenHeight <= 0) {
        return std::nullopt;
    }

    std::size_t offset = 10;
    const auto packed = data[offset++];
    const bool hasGlobalTable = (packed & 0x80U) != 0;
    const int globalTableSize = 1 << ((packed & 0x07U) + 1U);
    ++offset; // background color index
    ++offset; // pixel aspect ratio

    std::vector<std::uint32_t> globalColors;
    if (hasGlobalTable) {
        auto colors = readColorTable(data, offset, globalTableSize);
        if (!colors.has_value()) {
            return std::nullopt;
        }
        globalColors = std::move(*colors);
    }

    bool hasTransparentIndex = false;
    int transparentIndex = 0;

    while (offset < data.size()) {
        const auto marker = data[offset++];
        if (marker == 0x3BU) {
            break;
        }
        if (marker == 0x21U) {
            if (offset >= data.size()) {
                return std::nullopt;
            }
            const auto label = data[offset++];
            if (label == 0xF9U) {
                if (offset + 6 > data.size() || data[offset++] != 4) {
                    return std::nullopt;
                }
                const auto gcePacked = data[offset++];
                offset += 2; // delay
                transparentIndex = data[offset++];
                hasTransparentIndex = (gcePacked & 0x01U) != 0;
                if (data[offset++] != 0) {
                    return std::nullopt;
                }
            } else if (!skipSubBlocks(data, offset)) {
                return std::nullopt;
            }
            continue;
        }
        if (marker != 0x2CU) {
            return std::nullopt;
        }
        if (offset + 9 > data.size()) {
            return std::nullopt;
        }
        const int left = readU16LE(data, offset);
        offset += 2;
        const int top = readU16LE(data, offset);
        offset += 2;
        const int width = readU16LE(data, offset);
        offset += 2;
        const int height = readU16LE(data, offset);
        offset += 2;
        const auto imagePacked = data[offset++];
        if (width <= 0 || height <= 0 || left < 0 || top < 0 ||
            left + width > screenWidth || top + height > screenHeight) {
            return std::nullopt;
        }

        const bool hasLocalTable = (imagePacked & 0x80U) != 0;
        const bool interlaced = (imagePacked & 0x40U) != 0;
        const int localTableSize = 1 << ((imagePacked & 0x07U) + 1U);
        std::vector<std::uint32_t> localColors;
        if (hasLocalTable) {
            auto colors = readColorTable(data, offset, localTableSize);
            if (!colors.has_value()) {
                return std::nullopt;
            }
            localColors = std::move(*colors);
        }
        const auto& colors = hasLocalTable ? localColors : globalColors;
        if (colors.empty() || offset >= data.size()) {
            return std::nullopt;
        }

        const int minCodeSize = data[offset++];
        auto imageData = readSubBlocks(data, offset);
        if (!imageData.has_value()) {
            return std::nullopt;
        }
        auto indices = decodeLzw(*imageData, minCodeSize, static_cast<std::size_t>(width) * height);
        if (!indices.has_value()) {
            return std::nullopt;
        }

        Bitmap bitmap(screenWidth, screenHeight, 32);
        bitmap.fill(0x00000000U);
        bool hasAlpha = hasTransparentIndex;
        const auto rows = interlaced ? interlacedRows(height) : std::vector<int>{};
        for (int sourceY = 0; sourceY < height; ++sourceY) {
            const int y = interlaced ? rows[static_cast<std::size_t>(sourceY)] : sourceY;
            for (int x = 0; x < width; ++x) {
                const auto colorIndex = static_cast<int>((*indices)[static_cast<std::size_t>(sourceY * width + x)]);
                if (colorIndex < 0 || colorIndex >= static_cast<int>(colors.size())) {
                    return std::nullopt;
                }
                const auto pixel = hasTransparentIndex && colorIndex == transparentIndex
                    ? (colors[static_cast<std::size_t>(colorIndex)] & 0x00FFFFFFU)
                    : colors[static_cast<std::size_t>(colorIndex)];
                bitmap.setPixel(left + x, top + y, pixel);
            }
        }
        if (hasAlpha) {
            bitmap.setNativeAlpha(true);
        }
        return bitmap;
    }

    return std::nullopt;
}

} // namespace libreshockwave::bitmap
