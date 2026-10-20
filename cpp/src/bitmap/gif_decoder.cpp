#include "libreshockwave/bitmap/gif_decoder.hpp"

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
                                                         int color_count) {
    if (color_count <= 0 || offset + static_cast<std::size_t>(color_count) * 3U > data.size()) {
        return std::nullopt;
    }

    std::vector<std::uint32_t> colors;
    colors.reserve(static_cast<std::size_t>(color_count));
    for (int index = 0; index < color_count; ++index) {
        const std::uint32_t r = data[offset++];
        const std::uint32_t g = data[offset++];
        const std::uint32_t b = data[offset++];
        colors.push_back(0xFF000000U | (r << 16U) | (g << 8U) | b);
    }
    return colors;
}

bool skipSubBlocks(const std::vector<std::uint8_t>& data, std::size_t& offset) {
    while (offset < data.size()) {
        const std::size_t block_size = data[offset++];
        if (block_size == 0) {
            return true;
        }
        if (offset + block_size > data.size()) {
            return false;
        }
        offset += block_size;
    }
    return false;
}

std::optional<std::vector<std::uint8_t>> readSubBlocks(const std::vector<std::uint8_t>& data,
                                                       std::size_t& offset) {
    std::vector<std::uint8_t> bytes;
    while (offset < data.size()) {
        const std::size_t block_size = data[offset++];
        if (block_size == 0) {
            return bytes;
        }
        if (offset + block_size > data.size()) {
            return std::nullopt;
        }
        bytes.insert(bytes.end(), data.begin() + static_cast<std::ptrdiff_t>(offset),
                     data.begin() + static_cast<std::ptrdiff_t>(offset + block_size));
        offset += block_size;
    }
    return std::nullopt;
}

class BitReader {
public:
    explicit BitReader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    std::optional<int> read(int bit_count) {
        int value = 0;
        for (int bit = 0; bit < bit_count; ++bit) {
            if (bit_offset_ / 8U >= bytes_.size()) {
                return std::nullopt;
            }
            const auto byte = bytes_[bit_offset_ / 8U];
            value |= static_cast<int>((byte >> (bit_offset_ % 8U)) & 1U) << bit;
            ++bit_offset_;
        }
        return value;
    }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t bit_offset_{0};
};

std::optional<std::vector<std::uint8_t>> decodeLzw(const std::vector<std::uint8_t>& bytes,
                                                   int min_code_size,
                                                   std::size_t expected_pixels) {
    if (min_code_size < 2 || min_code_size > 8) {
        return std::nullopt;
    }

    const int clear_code = 1 << min_code_size;
    const int end_code = clear_code + 1;
    int next_code = end_code + 1;
    int code_size = min_code_size + 1;

    std::array<std::vector<std::uint8_t>, 4096> table;
    const auto reset_table = [&]() {
        for (auto& entry : table) {
            entry.clear();
        }
        for (int code = 0; code < clear_code; ++code) {
            table[code] = {static_cast<std::uint8_t>(code)};
        }
        next_code = end_code + 1;
        code_size = min_code_size + 1;
    };
    reset_table();

    BitReader reader(bytes);
    std::vector<std::uint8_t> output;
    output.reserve(expected_pixels);
    std::vector<std::uint8_t> previous;

    while (auto code_value = reader.read(code_size)) {
        const int code = *code_value;
        if (code == clear_code) {
            reset_table();
            previous.clear();
            continue;
        }
        if (code == end_code) {
            break;
        }

        std::vector<std::uint8_t> entry;
        if (code >= 0 && code < next_code && !table[code].empty()) {
            entry = table[code];
        } else if (code == next_code && !previous.empty()) {
            entry = previous;
            entry.push_back(previous.front());
        } else {
            return std::nullopt;
        }

        output.insert(output.end(), entry.begin(), entry.end());
        if (!previous.empty() && next_code < static_cast<int>(table.size())) {
            auto added = previous;
            added.push_back(entry.front());
            table[next_code++] = std::move(added);
            if (next_code == (1 << code_size) && code_size < 12) {
                ++code_size;
            }
        }
        previous = std::move(entry);
        if (output.size() >= expected_pixels) {
            output.resize(expected_pixels);
            return output;
        }
    }

    if (output.size() < expected_pixels) {
        return std::nullopt;
    }
    output.resize(expected_pixels);
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

    const int screen_width = readU16LE(data, 6);
    const int screen_height = readU16LE(data, 8);
    if (screen_width <= 0 || screen_height <= 0) {
        return std::nullopt;
    }

    std::size_t offset = 10;
    const auto packed = data[offset++];
    const bool has_global_table = (packed & 0x80U) != 0;
    const int global_table_size = 1 << ((packed & 0x07U) + 1U);
    ++offset; // background color index
    ++offset; // pixel aspect ratio

    std::vector<std::uint32_t> global_colors;
    if (has_global_table) {
        auto colors = readColorTable(data, offset, global_table_size);
        if (!colors.has_value()) {
            return std::nullopt;
        }
        global_colors = std::move(*colors);
    }

    bool has_transparent_index = false;
    int transparent_index = 0;

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
                const auto gce_packed = data[offset++];
                offset += 2; // delay
                transparent_index = data[offset++];
                has_transparent_index = (gce_packed & 0x01U) != 0;
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
        const auto image_packed = data[offset++];
        if (width <= 0 || height <= 0 || left < 0 || top < 0 ||
            left + width > screen_width || top + height > screen_height) {
            return std::nullopt;
        }

        const bool has_local_table = (image_packed & 0x80U) != 0;
        const bool interlaced = (image_packed & 0x40U) != 0;
        const int local_table_size = 1 << ((image_packed & 0x07U) + 1U);
        std::vector<std::uint32_t> local_colors;
        if (has_local_table) {
            auto colors = readColorTable(data, offset, local_table_size);
            if (!colors.has_value()) {
                return std::nullopt;
            }
            local_colors = std::move(*colors);
        }
        const auto& colors = has_local_table ? local_colors : global_colors;
        if (colors.empty() || offset >= data.size()) {
            return std::nullopt;
        }

        const int min_code_size = data[offset++];
        auto image_data = readSubBlocks(data, offset);
        if (!image_data.has_value()) {
            return std::nullopt;
        }
        auto indices = decodeLzw(*image_data, min_code_size, static_cast<std::size_t>(width) * height);
        if (!indices.has_value()) {
            return std::nullopt;
        }

        Bitmap bitmap(screen_width, screen_height, 32);
        bitmap.fill(0x00000000U);
        bool has_alpha = has_transparent_index;
        const auto rows = interlaced ? interlacedRows(height) : std::vector<int>{};
        for (int source_y = 0; source_y < height; ++source_y) {
            const int y = interlaced ? rows[static_cast<std::size_t>(source_y)] : source_y;
            for (int x = 0; x < width; ++x) {
                const auto color_index = static_cast<int>((*indices)[static_cast<std::size_t>(source_y * width + x)]);
                if (color_index < 0 || color_index >= static_cast<int>(colors.size())) {
                    return std::nullopt;
                }
                const auto pixel = has_transparent_index && color_index == transparent_index
                    ? (colors[static_cast<std::size_t>(color_index)] & 0x00FFFFFFU)
                    : colors[static_cast<std::size_t>(color_index)];
                bitmap.setPixel(left + x, top + y, pixel);
            }
        }
        if (has_alpha) {
            bitmap.setNativeAlpha(true);
        }
        return bitmap;
    }

    return std::nullopt;
}

} // namespace libreshockwave::bitmap
