#include "libreshockwave/font/pfr1_font.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "libreshockwave/font/pfr_bit_reader.hpp"

namespace libreshockwave::font {
namespace {

int sign16(int value) {
    return (value & 0x8000) != 0 ? value | ~0xFFFF : value;
}

int readUnsignedN(const std::vector<std::uint8_t>& data, int pos, int n) {
    int value = 0;
    for (int i = 0; i < n; ++i) {
        if (pos + i >= static_cast<int>(data.size())) {
            break;
        }
        value = (value << 8) | (data[static_cast<std::size_t>(pos + i)] & 0xFF);
    }
    return value;
}

int readSignedN(const std::vector<std::uint8_t>& data, int pos, int n) {
    const int value = readUnsignedN(data, pos, n);
    if (n == 0) {
        return 0;
    }
    const int sign_bit = 1 << (n * 8 - 1);
    if ((value & sign_bit) != 0) {
        const int mask = (-1) << (n * 8);
        return value | mask;
    }
    return value;
}

std::vector<std::uint8_t> decodeRleBitmap(const std::vector<std::uint8_t>& data,
                                          int offset,
                                          int data_len,
                                          int width,
                                          int height) {
    const int total_bits = width * height;
    if (total_bits <= 0 || total_bits > 1'000'000) {
        return {};
    }
    const int total_bytes = (total_bits + 7) / 8;
    std::vector<std::uint8_t> result(static_cast<std::size_t>(total_bytes), 0);
    int out_pos = 0;
    int pos = offset;
    const int end = offset + data_len;

    while (pos < end && pos < static_cast<int>(data.size()) && out_pos < total_bits) {
        const int byte = data[static_cast<std::size_t>(pos++)] & 0xFF;
        const int count = (byte >> 4) & 0x0F;
        const int value = byte & 0x0F;
        for (int i = 0; i < count && out_pos < total_bits; ++i) {
            if (value != 0) {
                result[static_cast<std::size_t>(out_pos / 8)] |=
                    static_cast<std::uint8_t>(1U << (7 - (out_pos % 8)));
            }
            ++out_pos;
        }
    }

    return result;
}

void skipU8(PfrBitReader& reader) {
    (void)reader.readU8();
}

void skipU16(PfrBitReader& reader) {
    (void)reader.readU16();
}

void skipU24(PfrBitReader& reader) {
    (void)reader.readU24();
}

void skipBit(PfrBitReader& reader) {
    (void)reader.readBit();
}

int sign8(int value) {
    value &= 0xFF;
    return (value & 0x80) != 0 ? value | ~0xFF : value;
}

constexpr std::uint32_t CURVE_TABLE_9[16] = {
    0x0451, 0x0452, 0x0461, 0x0462, 0x0491, 0x0492, 0x04A1, 0x04A2,
    0x0851, 0x0852, 0x0861, 0x0862, 0x0891, 0x0892, 0x08A1, 0x08A2,
};

constexpr std::uint32_t CURVE_TABLE_10[16] = {
    0x0154, 0x0158, 0x0164, 0x0168, 0x0194, 0x0198, 0x01A4, 0x01A8,
    0x0254, 0x0258, 0x0264, 0x0268, 0x0294, 0x0298, 0x02A4, 0x02A8,
};

constexpr std::uint32_t CURVE_TABLE_13[16] = {
    0x0FFF, 0x03AA, 0x0CAA, 0x0AA3, 0x0AAC, 0x0AAA, 0x02AA, 0x08AA,
    0x0AA2, 0x0AA8, 0x00AA, 0x0555, 0x0155, 0x0455, 0x0551, 0x0554,
};

constexpr std::uint32_t CURVE_TABLE_14A[8] = {1, 2, 4, 5, 6, 8, 9, 10};
constexpr std::uint32_t CURVE_TABLE_14B[4] = {5, 6, 9, 10};

struct ByteRead {
    int value = 0;
    int pos = 0;
};

struct NibbleRead {
    int value = 0;
    int pos = 0;
    bool nibble_high = false;
};

struct CoordRead {
    int value = 0;
    int pos = 0;
    bool nibble_high = false;
};

struct CoordPairRead {
    int x = 0;
    int y = 0;
    int pos = 0;
    bool nibble_high = false;
};

struct TransformRead {
    int scale = 4096;
    int offset = 0;
    int pos = 0;
};

struct GlyphOffsetRead {
    int offset = 0;
    int size = 0;
    int pos = 0;
    int accumulator = 0;
};

ByteRead readByteAligned(const std::vector<std::uint8_t>& data, int pos, bool nibble_high) {
    if (pos >= static_cast<int>(data.size())) {
        return {0, pos};
    }
    if (nibble_high) {
        const int lo = data[static_cast<std::size_t>(pos)] & 0x0F;
        ++pos;
        const int hi = pos < static_cast<int>(data.size())
                           ? (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F
                           : 0;
        return {(lo << 4) | hi, pos};
    }
    return {data[static_cast<std::size_t>(pos)] & 0xFF, pos + 1};
}

NibbleRead readNibble(const std::vector<std::uint8_t>& data, int pos, bool nibble_high) {
    if (pos >= static_cast<int>(data.size())) {
        return {-1, pos, nibble_high};
    }

    nibble_high = !nibble_high;
    if (nibble_high) {
        return {(data[static_cast<std::size_t>(pos)] >> 4) & 0x0F, pos, nibble_high};
    }

    const int value = data[static_cast<std::size_t>(pos)] & 0x0F;
    return {value, pos + 1, nibble_high};
}

int orusLookup(const std::vector<int>& control_values, int current, int direction) {
    if (control_values.empty() || direction == 0) {
        return current;
    }

    if (direction > 0) {
        std::size_t pos = 0;
        while (pos < control_values.size() && control_values[pos] <= current) {
            ++pos;
        }
        if (pos >= control_values.size()) {
            return current;
        }
        const auto target = std::min<std::size_t>(pos + static_cast<std::size_t>(direction - 1),
                                                  control_values.size() - 1);
        return control_values[target];
    }

    for (int i = static_cast<int>(control_values.size()) - 1; i >= 0; --i) {
        if (control_values[static_cast<std::size_t>(i)] < current) {
            return control_values[static_cast<std::size_t>(std::max(i + direction + 1, 0))];
        }
    }
    return current;
}

CoordRead readCe9dCoordValue(const std::vector<std::uint8_t>& data,
                             int pos,
                             int v8,
                             bool three_byte_mode,
                             bool nibble_aligned) {
    if (pos >= static_cast<int>(data.size())) {
        return {0, pos, nibble_aligned};
    }

    if ((v8 & 1) == 0) {
        int result = 0;
        if (nibble_aligned) {
            const int lo = data[static_cast<std::size_t>(pos)] & 0x0F;
            ++pos;
            const int hi = pos < static_cast<int>(data.size())
                               ? (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F
                               : 0;
            result = sign16((lo << 4) | hi);
        } else {
            result = sign16(data[static_cast<std::size_t>(pos)] & 0xFF);
            ++pos;
        }
        return {result, pos, nibble_aligned};
    }

    if (three_byte_mode) {
        int result = 0;
        if (nibble_aligned) {
            const int b0_low = pos > 0 ? data[static_cast<std::size_t>(pos - 1)] & 0x0F : 0;
            const int b1 = pos < static_cast<int>(data.size())
                               ? data[static_cast<std::size_t>(pos)] & 0xFF
                               : 0;
            ++pos;
            const int b2_high = pos < static_cast<int>(data.size())
                                   ? (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F
                                   : 0;
            result = sign16((b0_low << 12) | (b1 << 4) | b2_high);
        } else {
            const int b0 = pos < static_cast<int>(data.size())
                               ? data[static_cast<std::size_t>(pos)] & 0xFF
                               : 0;
            ++pos;
            const int b1 = pos < static_cast<int>(data.size())
                               ? data[static_cast<std::size_t>(pos)] & 0xFF
                               : 0;
            ++pos;
            result = sign16((b0 << 8) | b1);
        }
        return {result, pos, nibble_aligned};
    }

    int result = 0;
    if (nibble_aligned) {
        const int lo = data[static_cast<std::size_t>(pos)] & 0x0F;
        ++pos;
        const int next_byte = pos < static_cast<int>(data.size())
                                 ? data[static_cast<std::size_t>(pos)] & 0xFF
                                 : 0;
        ++pos;
        result = sign16(next_byte + 16 * sign8(lo << 4));
        nibble_aligned = false;
    } else {
        const int signed_byte = sign8(data[static_cast<std::size_t>(pos)]);
        ++pos;
        const int next_high = pos < static_cast<int>(data.size())
                                 ? (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F
                                 : 0;
        result = sign16(next_high + 16 * signed_byte);
        nibble_aligned = true;
    }
    return {result, pos, nibble_aligned};
}

int parseControlValues(const std::vector<std::uint8_t>& data,
                       int pos,
                       int flags,
                       std::vector<int>& control_x,
                       std::vector<int>& control_y) {
    if (control_x.empty() && control_y.empty()) {
        return pos;
    }

    const bool three_byte_mode = (flags & 3) == 3;
    const bool flag_per_coordinate = (flags & 0x40) != 0;
    bool nibble_aligned = false;
    int flag_cache = 0;
    int flag_cache_count = 0;

    int accumulated_x = 0;
    for (std::size_t i = 0; i < control_x.size(); ++i) {
        if (pos >= static_cast<int>(data.size())) {
            break;
        }

        int v8 = 0;
        if (i == 0) {
            v8 = (flags >> 4) & 1;
        } else if (flag_per_coordinate) {
            if (flag_cache_count > 0) {
                v8 = (flag_cache >> 1) & 1;
                flag_cache >>= 1;
                --flag_cache_count;
            } else if (pos < static_cast<int>(data.size())) {
                if (nibble_aligned) {
                    const int nibble_value = data[static_cast<std::size_t>(pos)] & 0x0F;
                    ++pos;
                    nibble_aligned = false;
                    v8 = nibble_value & 1;
                    flag_cache = nibble_value;
                    flag_cache_count = 3;
                } else {
                    const int nibble_value = (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F;
                    nibble_aligned = true;
                    v8 = nibble_value & 1;
                    flag_cache = nibble_value;
                    flag_cache_count = 3;
                }
            }
        }

        const auto read = readCe9dCoordValue(data, pos, v8, three_byte_mode, nibble_aligned);
        pos = read.pos;
        nibble_aligned = read.nibble_high;
        accumulated_x += read.value;
        control_x[i] = accumulated_x;
    }

    int accumulated_y = 0;
    for (std::size_t i = 0; i < control_y.size(); ++i) {
        if (pos >= static_cast<int>(data.size())) {
            break;
        }

        int v8 = 0;
        if (i == 0) {
            v8 = (flags >> 5) & 1;
        } else if (flag_per_coordinate) {
            if (flag_cache_count > 0) {
                v8 = (flag_cache >> 1) & 1;
                flag_cache >>= 1;
                --flag_cache_count;
            } else if (pos < static_cast<int>(data.size())) {
                if (nibble_aligned) {
                    const int nibble_value = data[static_cast<std::size_t>(pos)] & 0x0F;
                    ++pos;
                    nibble_aligned = false;
                    v8 = nibble_value & 1;
                    flag_cache = nibble_value;
                    flag_cache_count = 3;
                } else {
                    const int nibble_value = (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F;
                    nibble_aligned = true;
                    v8 = nibble_value & 1;
                    flag_cache = nibble_value;
                    flag_cache_count = 3;
                }
            }
        }

        const auto read = readCe9dCoordValue(data, pos, v8, three_byte_mode, nibble_aligned);
        pos = read.pos;
        nibble_aligned = read.nibble_high;
        accumulated_y += read.value;
        control_y[i] = accumulated_y;
    }

    if (nibble_aligned) {
        ++pos;
    }
    return pos;
}

CoordRead readEncodedCoordValue(const std::vector<std::uint8_t>& data,
                                int pos,
                                int encoding,
                                bool nibble_high,
                                int current,
                                const std::vector<int>& control_values) {
    switch (encoding) {
        case 1: {
            nibble_high = !nibble_high;
            if (pos >= static_cast<int>(data.size())) {
                return {current, pos, nibble_high};
            }
            int nibble = 0;
            if (nibble_high) {
                nibble = (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F;
            } else {
                nibble = data[static_cast<std::size_t>(pos)] & 0x0F;
                ++pos;
            }
            return {sign16(current + nibble - 8), pos, nibble_high};
        }
        case 2: {
            const auto read = readByteAligned(data, pos, nibble_high);
            const int signed_byte = sign8(read.value);
            if (signed_byte >= -8 && signed_byte < 8) {
                const int direction = (signed_byte & 0x80) == 0 ? signed_byte + 1 : signed_byte;
                return {orusLookup(control_values, current, direction), read.pos, nibble_high};
            }
            return {sign16(current + signed_byte), read.pos, nibble_high};
        }
        case 3: {
            const auto high_read = readByteAligned(data, pos, nibble_high);
            pos = high_read.pos;
            nibble_high = !nibble_high;
            int low = 0;
            if (pos >= static_cast<int>(data.size())) {
                low = 0;
            } else if (nibble_high) {
                low = (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F;
            } else {
                low = data[static_cast<std::size_t>(pos)] & 0x0F;
                ++pos;
            }

            const int delta12 = (sign8(high_read.value) << 4) | low;
            if (delta12 >= -128 && delta12 < 128) {
                const auto extra_read = readByteAligned(data, pos, nibble_high);
                const int delta16 = (delta12 << 8) | (extra_read.value & 0xFF);
                return {sign16(current + delta16), extra_read.pos, nibble_high};
            }
            return {sign16(current + delta12), pos, nibble_high};
        }
        default:
            return {current, pos, nibble_high};
    }
}

CoordPairRead readEncodedCoordsInto(const std::vector<std::uint8_t>& data,
                                    int pos,
                                    int encoding,
                                    int& current_x,
                                    int& current_y,
                                    int& previous_x,
                                    int& previous_y,
                                    int out_x,
                                    int out_y,
                                    bool nibble_high,
                                    const std::vector<int>& control_x,
                                    const std::vector<int>& control_y) {
    const int x_encoding = encoding & 3;
    const int y_encoding = (encoding >> 2) & 3;

    if (x_encoding != 0) {
        const auto read = readEncodedCoordValue(data, pos, x_encoding, nibble_high, current_x, control_x);
        out_x = read.value;
        pos = read.pos;
        nibble_high = read.nibble_high;
    }
    previous_x = current_x;
    current_x = out_x;

    if (y_encoding != 0) {
        const auto read = readEncodedCoordValue(data, pos, y_encoding, nibble_high, current_y, control_y);
        out_y = read.value;
        pos = read.pos;
        nibble_high = read.nibble_high;
    }
    previous_y = current_y;
    current_y = out_y;

    return {out_x, out_y, pos, nibble_high};
}

std::uint32_t calculateCurveEncoding14(std::uint32_t byte) {
    const std::uint32_t value =
        CURVE_TABLE_14B[(byte >> 3U) & 0x03U] + 16U * CURVE_TABLE_14A[(byte >> 5U) & 0x07U];
    return CURVE_TABLE_14A[byte & 0x07U] + value * 16U;
}

void parseNibbleCommands(const std::vector<std::uint8_t>& data,
                         int start_pos,
                         int end_limit,
                         Pfr1Font::OutlineGlyph& glyph,
                         const std::vector<int>& control_x,
                         const std::vector<int>& control_y) {
    int pos = start_pos;
    const int end_pos = end_limit > 0 ? end_limit - 1 : 0;
    bool nibble_high = false;
    int current_x = 0;
    int current_y = 0;
    int previous_x = 0;
    int previous_y = 0;
    Pfr1Font::Contour current_contour;
    bool first_iteration = true;

    for (int iterations = 0; iterations < 500; ++iterations) {
        if (pos >= end_pos && (pos != end_pos || nibble_high)) {
            break;
        }
        if (pos >= static_cast<int>(data.size())) {
            break;
        }

        int command = 0;
        if (first_iteration) {
            command = 6;
            first_iteration = false;
        } else {
            nibble_high = !nibble_high;
            if (nibble_high) {
                command = (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F;
            } else {
                command = data[static_cast<std::size_t>(pos)] & 0x0F;
                ++pos;
            }
        }

        const int before_x = current_x;
        const int before_y = current_y;

        switch (command) {
            case 0: {
                const auto nibble_read = readNibble(data, pos, nibble_high);
                if (nibble_read.value < 0) {
                    break;
                }
                const int nibble = nibble_read.value;
                pos = nibble_read.pos;
                nibble_high = nibble_read.nibble_high;
                const int direction = (nibble & 4) != 0 ? (nibble & 7) - 8 : (nibble & 7) + 1;
                previous_x = before_x;
                previous_y = before_y;
                if ((nibble & 8) != 0) {
                    current_y = orusLookup(control_y, current_y, direction);
                } else {
                    current_x = orusLookup(control_x, current_x, direction);
                }
                current_contour.lineTo(static_cast<float>(current_x), static_cast<float>(current_y));
                break;
            }
            case 1: {
                const auto read = readByteAligned(data, pos, nibble_high);
                pos = read.pos;
                previous_x = before_x;
                previous_y = before_y;
                current_x = sign16(current_x + sign8(read.value));
                current_contour.lineTo(static_cast<float>(current_x), static_cast<float>(current_y));
                break;
            }
            case 2: {
                const auto read = readByteAligned(data, pos, nibble_high);
                pos = read.pos;
                previous_x = before_x;
                previous_y = before_y;
                current_y = sign16(current_y + sign8(read.value));
                current_contour.lineTo(static_cast<float>(current_x), static_cast<float>(current_y));
                break;
            }
            case 3:
            case 4: {
                const auto high_read = readByteAligned(data, pos, nibble_high);
                pos = high_read.pos;
                nibble_high = !nibble_high;
                int low = 0;
                if (pos >= static_cast<int>(data.size())) {
                    low = 0;
                } else if (nibble_high) {
                    low = (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F;
                } else {
                    low = data[static_cast<std::size_t>(pos)] & 0x0F;
                    ++pos;
                }

                const int delta12 = (sign8(high_read.value) << 4) | low;
                int delta = delta12;
                if (delta12 >= -128 && delta12 < 128) {
                    const auto extra_read = readByteAligned(data, pos, nibble_high);
                    pos = extra_read.pos;
                    delta = (delta12 << 8) | (extra_read.value & 0xFF);
                }
                if (command == 3) {
                    current_x += sign16(delta);
                } else {
                    current_y += sign16(delta);
                }
                previous_x = before_x;
                previous_y = before_y;
                current_contour.lineTo(static_cast<float>(current_x), static_cast<float>(current_y));
                break;
            }
            case 5:
            case 6: {
                const auto encoding_read = readNibble(data, pos, nibble_high);
                if (encoding_read.value < 0) {
                    break;
                }
                pos = encoding_read.pos;
                nibble_high = encoding_read.nibble_high;
                const auto read = readEncodedCoordsInto(data,
                                                        pos,
                                                        encoding_read.value,
                                                        current_x,
                                                        current_y,
                                                        previous_x,
                                                        previous_y,
                                                        current_x,
                                                        current_y,
                                                        nibble_high,
                                                        control_x,
                                                        control_y);
                pos = read.pos;
                nibble_high = read.nibble_high;

                if (command == 6) {
                    if (!current_contour.commands.empty()) {
                        glyph.contours.push_back(std::move(current_contour));
                        current_contour = Pfr1Font::Contour();
                    }
                    current_contour.moveTo(static_cast<float>(current_x), static_cast<float>(current_y));
                } else {
                    current_contour.lineTo(static_cast<float>(current_x), static_cast<float>(current_y));
                }
                break;
            }
            default: {
                if (command >= 7) {
                    std::uint32_t encoding = 0;
                    int path = 0;
                    switch (command) {
                        case 7:
                            encoding = 2210U;
                            path = 49;
                            break;
                        case 8:
                            encoding = 680U;
                            path = 54;
                            break;
                        case 9: {
                            const auto nibble_read = readNibble(data, pos, nibble_high);
                            if (nibble_read.value < 0) {
                                break;
                            }
                            pos = nibble_read.pos;
                            nibble_high = nibble_read.nibble_high;
                            encoding = CURVE_TABLE_9[static_cast<std::size_t>(nibble_read.value & 0x0F)];
                            path = 49;
                            break;
                        }
                        case 10: {
                            const auto nibble_read = readNibble(data, pos, nibble_high);
                            if (nibble_read.value < 0) {
                                break;
                            }
                            pos = nibble_read.pos;
                            nibble_high = nibble_read.nibble_high;
                            encoding = CURVE_TABLE_10[static_cast<std::size_t>(nibble_read.value & 0x0F)];
                            path = 54;
                            break;
                        }
                        case 11: {
                            const auto byte_read = readByteAligned(data, pos, nibble_high);
                            pos = byte_read.pos;
                            const auto byte = static_cast<std::uint32_t>(byte_read.value);
                            encoding = (byte & 3U) + 4U * ((byte & 0x3CU) + 4U * (byte & 0xC0U));
                            path = 49;
                            break;
                        }
                        case 12: {
                            const auto byte_read = readByteAligned(data, pos, nibble_high);
                            pos = byte_read.pos;
                            encoding = static_cast<std::uint32_t>(byte_read.value) * 4U;
                            path = 54;
                            break;
                        }
                        case 13: {
                            const auto nibble_read = readNibble(data, pos, nibble_high);
                            if (nibble_read.value < 0) {
                                break;
                            }
                            pos = nibble_read.pos;
                            nibble_high = nibble_read.nibble_high;
                            encoding = CURVE_TABLE_13[static_cast<std::size_t>(nibble_read.value & 0x0F)];
                            path = 70;
                            break;
                        }
                        case 14: {
                            const auto byte_read = readByteAligned(data, pos, nibble_high);
                            pos = byte_read.pos;
                            encoding = calculateCurveEncoding14(static_cast<std::uint32_t>(byte_read.value));
                            path = 70;
                            break;
                        }
                        case 15: {
                            const auto nibble_read = readNibble(data, pos, nibble_high);
                            if (nibble_read.value < 0) {
                                break;
                            }
                            pos = nibble_read.pos;
                            nibble_high = nibble_read.nibble_high;
                            const auto byte_read = readByteAligned(data, pos, nibble_high);
                            pos = byte_read.pos;
                            encoding = static_cast<std::uint32_t>(byte_read.value) |
                                       (static_cast<std::uint32_t>(nibble_read.value) << 8U);
                            path = 70;
                            break;
                        }
                        default:
                            break;
                    }

                    if (path == 0) {
                        break;
                    }

                    const int start_x = before_x;
                    const int start_y = before_y;
                    int control1x = current_x;
                    int control1y = current_y;
                    int control2x = 0;
                    int control2y = 0;
                    int end_x = 0;
                    int end_y = 0;

                    if (path == 49) {
                        auto read = readEncodedCoordsInto(data,
                                                          pos,
                                                          static_cast<int>(encoding & 0x0FU),
                                                          current_x,
                                                          current_y,
                                                          previous_x,
                                                          previous_y,
                                                          control1x,
                                                          control1y,
                                                          nibble_high,
                                                          control_x,
                                                          control_y);
                        control1x = read.x;
                        control1y = read.y;
                        pos = read.pos;
                        nibble_high = read.nibble_high;

                        control2x = orusLookup(control_x, current_x, 0);
                        control2y = control1y;
                        read = readEncodedCoordsInto(data,
                                                     pos,
                                                     static_cast<int>((encoding >> 4U) & 0x0FU),
                                                     current_x,
                                                     current_y,
                                                     previous_x,
                                                     previous_y,
                                                     control2x,
                                                     control2y,
                                                     nibble_high,
                                                     control_x,
                                                     control_y);
                        control2x = read.x;
                        control2y = read.y;
                        pos = read.pos;
                        nibble_high = read.nibble_high;

                        end_x = control2x;
                        end_y = orusLookup(control_y, current_y, 0);
                        read = readEncodedCoordsInto(data,
                                                     pos,
                                                     static_cast<int>((encoding >> 8U) & 0x0FU),
                                                     current_x,
                                                     current_y,
                                                     previous_x,
                                                     previous_y,
                                                     end_x,
                                                     end_y,
                                                     nibble_high,
                                                     control_x,
                                                     control_y);
                        end_x = read.x;
                        end_y = read.y;
                        pos = read.pos;
                        nibble_high = read.nibble_high;
                    } else if (path == 54) {
                        auto read = readEncodedCoordsInto(data,
                                                          pos,
                                                          static_cast<int>(encoding & 0x0FU),
                                                          current_x,
                                                          current_y,
                                                          previous_x,
                                                          previous_y,
                                                          control1x,
                                                          control1y,
                                                          nibble_high,
                                                          control_x,
                                                          control_y);
                        control1x = read.x;
                        control1y = read.y;
                        pos = read.pos;
                        nibble_high = read.nibble_high;

                        control2x = control1x;
                        control2y = orusLookup(control_y, current_y, 0);
                        read = readEncodedCoordsInto(data,
                                                     pos,
                                                     static_cast<int>((encoding >> 4U) & 0x0FU),
                                                     current_x,
                                                     current_y,
                                                     previous_x,
                                                     previous_y,
                                                     control2x,
                                                     control2y,
                                                     nibble_high,
                                                     control_x,
                                                     control_y);
                        control2x = read.x;
                        control2y = read.y;
                        pos = read.pos;
                        nibble_high = read.nibble_high;

                        end_x = orusLookup(control_x, current_x, 0);
                        end_y = control2y;
                        read = readEncodedCoordsInto(data,
                                                     pos,
                                                     static_cast<int>((encoding >> 8U) & 0x0FU),
                                                     current_x,
                                                     current_y,
                                                     previous_x,
                                                     previous_y,
                                                     end_x,
                                                     end_y,
                                                     nibble_high,
                                                     control_x,
                                                     control_y);
                        end_x = read.x;
                        end_y = read.y;
                        pos = read.pos;
                        nibble_high = read.nibble_high;
                    } else {
                        control1x = sign16(current_x + (current_x - previous_x));
                        control1y = sign16(current_y + (current_y - previous_y));
                        auto read = readEncodedCoordsInto(data,
                                                          pos,
                                                          static_cast<int>(encoding & 0x0FU),
                                                          current_x,
                                                          current_y,
                                                          previous_x,
                                                          previous_y,
                                                          control1x,
                                                          control1y,
                                                          nibble_high,
                                                          control_x,
                                                          control_y);
                        control1x = read.x;
                        control1y = read.y;
                        pos = read.pos;
                        nibble_high = read.nibble_high;

                        control2x = control1x;
                        control2y = control1y;
                        read = readEncodedCoordsInto(data,
                                                     pos,
                                                     static_cast<int>((encoding >> 4U) & 0x0FU),
                                                     current_x,
                                                     current_y,
                                                     previous_x,
                                                     previous_y,
                                                     control2x,
                                                     control2y,
                                                     nibble_high,
                                                     control_x,
                                                     control_y);
                        control2x = read.x;
                        control2y = read.y;
                        pos = read.pos;
                        nibble_high = read.nibble_high;

                        end_x = control2x;
                        end_y = control2y;
                        read = readEncodedCoordsInto(data,
                                                     pos,
                                                     static_cast<int>((encoding >> 8U) & 0x0FU),
                                                     current_x,
                                                     current_y,
                                                     previous_x,
                                                     previous_y,
                                                     end_x,
                                                     end_y,
                                                     nibble_high,
                                                     control_x,
                                                     control_y);
                        end_x = read.x;
                        end_y = read.y;
                        pos = read.pos;
                        nibble_high = read.nibble_high;
                    }

                    if (current_contour.commands.empty()) {
                        current_contour.moveTo(static_cast<float>(start_x), static_cast<float>(start_y));
                    }
                    current_contour.curveTo(static_cast<float>(control1x),
                                           static_cast<float>(control1y),
                                           static_cast<float>(control2x),
                                           static_cast<float>(control2y),
                                           static_cast<float>(end_x),
                                           static_cast<float>(end_y));
                    previous_x = control2x;
                    previous_y = control2y;
                    current_x = end_x;
                    current_y = end_y;
                }
                break;
            }
        }

        if (std::abs(current_x - before_x) > 8192 || std::abs(current_y - before_y) > 8192) {
            current_x = before_x;
            current_y = before_y;
        }
        if (current_contour.commands.size() > 300) {
            break;
        }
    }

    if (!current_contour.commands.empty()) {
        glyph.contours.push_back(std::move(current_contour));
    }
}

Pfr1Font::OutlineGlyph parseSimpleOutlineGlyph(const std::vector<std::uint8_t>& data,
                                               int start,
                                               int size,
                                               const Pfr1Font::CharacterRecord& record);

Pfr1Font::OutlineGlyph parseOutlineGlyph(const Pfr1Font& font,
                                         const std::vector<std::uint8_t>& data,
                                         int start,
                                         int size,
                                         const Pfr1Font::CharacterRecord& record,
                                         const std::vector<int>& known_offsets,
                                         int depth);

TransformRead parseTransformModulo6(const std::vector<std::uint8_t>& data, int pos, int format) {
    TransformRead result{4096, 0, pos};

    if (format <= 2) {
        result.scale = 4096;
    } else if (format == 5) {
        result.scale = 0;
    } else if (pos + 2 <= static_cast<int>(data.size())) {
        result.scale = ((data[static_cast<std::size_t>(pos)] & 0xFF) << 8) |
                       (data[static_cast<std::size_t>(pos + 1)] & 0xFF);
        pos += 2;
    }

    if (format == 0 || format == 5) {
        result.offset = 0;
    } else if (format == 1 || format == 3) {
        if (pos < static_cast<int>(data.size())) {
            result.offset = sign8(data[static_cast<std::size_t>(pos)]);
            ++pos;
        }
    } else if (pos + 2 <= static_cast<int>(data.size())) {
        result.offset = sign16(((data[static_cast<std::size_t>(pos)] & 0xFF) << 8) |
                               (data[static_cast<std::size_t>(pos + 1)] & 0xFF));
        pos += 2;
    }

    result.pos = pos;
    return result;
}

GlyphOffsetRead parseGlyphOffsetModulo6(const std::vector<std::uint8_t>& data,
                                        int pos,
                                        int format,
                                        int accumulator) {
    GlyphOffsetRead result{0, 0, pos, accumulator};

    switch (format) {
        case 0:
            if (pos < static_cast<int>(data.size())) {
                const int delta = data[static_cast<std::size_t>(pos++)] & 0xFF;
                result.size = delta;
                accumulator -= delta;
                result.offset = accumulator;
            }
            break;
        case 1:
            if (pos < static_cast<int>(data.size())) {
                const int delta = (data[static_cast<std::size_t>(pos++)] & 0xFF) + 256;
                result.size = delta;
                accumulator -= delta;
                result.offset = accumulator;
            }
            break;
        case 2:
            if (pos + 2 <= static_cast<int>(data.size())) {
                const int delta = ((data[static_cast<std::size_t>(pos)] & 0xFF) << 8) |
                                  (data[static_cast<std::size_t>(pos + 1)] & 0xFF);
                pos += 2;
                result.size = delta;
                accumulator -= delta;
                result.offset = accumulator;
            }
            break;
        case 3:
            if (pos + 3 <= static_cast<int>(data.size())) {
                const int combined = ((data[static_cast<std::size_t>(pos)] & 0xFF) << 16) |
                                     ((data[static_cast<std::size_t>(pos + 1)] & 0xFF) << 8) |
                                     (data[static_cast<std::size_t>(pos + 2)] & 0xFF);
                pos += 3;
                result.size = combined >> 15;
                const int delta = combined & 0x7FFF;
                result.offset = accumulator - delta;
            }
            break;
        case 4:
            if (pos + 3 <= static_cast<int>(data.size())) {
                const int combined = ((data[static_cast<std::size_t>(pos)] & 0xFF) << 16) |
                                     ((data[static_cast<std::size_t>(pos + 1)] & 0xFF) << 8) |
                                     (data[static_cast<std::size_t>(pos + 2)] & 0xFF);
                pos += 3;
                result.size = combined >> 15;
                result.offset = combined & 0x7FFF;
            }
            break;
        case 5:
            if (pos + 4 <= static_cast<int>(data.size())) {
                const auto combined =
                    (static_cast<std::uint32_t>(data[static_cast<std::size_t>(pos)] & 0xFF) << 24U) |
                    (static_cast<std::uint32_t>(data[static_cast<std::size_t>(pos + 1)] & 0xFF) << 16U) |
                    (static_cast<std::uint32_t>(data[static_cast<std::size_t>(pos + 2)] & 0xFF) << 8U) |
                    static_cast<std::uint32_t>(data[static_cast<std::size_t>(pos + 3)] & 0xFF);
                pos += 4;
                result.size = static_cast<int>((combined >> 23U) & 0x1FFU);
                result.offset = static_cast<int>(combined & 0x7FFFFFU);
            }
            break;
        default:
            if (pos + 5 <= static_cast<int>(data.size())) {
                result.size = ((data[static_cast<std::size_t>(pos)] & 0xFF) << 8) |
                              (data[static_cast<std::size_t>(pos + 1)] & 0xFF);
                pos += 2;
                result.offset = ((data[static_cast<std::size_t>(pos)] & 0xFF) << 16) |
                                ((data[static_cast<std::size_t>(pos + 1)] & 0xFF) << 8) |
                                (data[static_cast<std::size_t>(pos + 2)] & 0xFF);
                pos += 3;
            }
            break;
    }

    result.pos = pos;
    result.accumulator = accumulator;
    return result;
}

int findNextOffset(const std::vector<int>& sorted_offsets, int current_offset) {
    const auto it = std::upper_bound(sorted_offsets.begin(), sorted_offsets.end(), current_offset);
    if (it == sorted_offsets.end()) {
        return std::numeric_limits<int>::max();
    }
    return *it;
}

void appendTransformedContours(Pfr1Font::OutlineGlyph& glyph,
                               const Pfr1Font::OutlineGlyph& sub_glyph,
                               int x_scale,
                               int y_scale,
                               int x_offset,
                               int y_offset) {
    const float x_scale_f = static_cast<float>(x_scale) / 4096.0F;
    const float y_scale_f = static_cast<float>(y_scale) / 4096.0F;
    const float x_offset_f = static_cast<float>(x_offset);
    const float y_offset_f = static_cast<float>(y_offset);

    for (const auto& source_contour : sub_glyph.contours) {
        Pfr1Font::Contour transformed;
        for (const auto& command : source_contour.commands) {
            const float x = command.x * x_scale_f + x_offset_f;
            const float y = command.y * y_scale_f + y_offset_f;
            if (command.type == 0) {
                transformed.moveTo(x, y);
            } else if (command.type == 1) {
                transformed.lineTo(x, y);
            } else if (command.type == 2) {
                transformed.curveTo(command.x1 * x_scale_f + x_offset_f,
                                    command.y1 * y_scale_f + y_offset_f,
                                    command.x2 * x_scale_f + x_offset_f,
                                    command.y2 * y_scale_f + y_offset_f,
                                    x,
                                    y);
            }
        }
        if (!transformed.commands.empty()) {
            glyph.contours.push_back(std::move(transformed));
        }
    }
}

void parseCompoundGlyph(const Pfr1Font& font,
                        const std::vector<std::uint8_t>& data,
                        int start,
                        int size,
                        Pfr1Font::OutlineGlyph& glyph,
                        const std::vector<int>& known_offsets,
                        int depth) {
    if (depth >= 8) {
        return;
    }

    const int component_count = data[static_cast<std::size_t>(start)] & 0x3F;
    int pos = start + 1;

    if ((data[static_cast<std::size_t>(start)] & 0x40) != 0 && pos + 2 <= start + size) {
        const int extra_count = (data[static_cast<std::size_t>(pos)] & 0xFF) |
                               ((data[static_cast<std::size_t>(pos + 1)] & 0xFF) << 8);
        pos += 2;
        for (int i = 0; i < extra_count; ++i) {
            if (pos >= start + size) {
                break;
            }
            const int length = data[static_cast<std::size_t>(pos)] & 0xFF;
            pos += length + 2;
        }
    }

    const int glyph_gps_offset = start - font.gps_offset;
    int offset_accumulator = glyph_gps_offset;

    for (int component = 0; component < component_count; ++component) {
        if (pos >= start + size) {
            break;
        }

        const int format_byte = data[static_cast<std::size_t>(pos++)] & 0xFF;
        const int x_format = format_byte % 6;
        const int y_format = (format_byte / 6) % 6;
        const int offset_format = format_byte / 36;

        const auto x_transform = parseTransformModulo6(data, pos, x_format);
        pos = x_transform.pos;
        const auto y_transform = parseTransformModulo6(data, pos, y_format);
        pos = y_transform.pos;

        const auto glyph_offset = parseGlyphOffsetModulo6(data, pos, offset_format, offset_accumulator);
        pos = glyph_offset.pos;
        offset_accumulator = glyph_offset.accumulator;

        const int absolute_position = font.gps_offset + glyph_offset.offset;
        if (absolute_position < 0 || absolute_position >= static_cast<int>(data.size())) {
            continue;
        }

        int max_size = static_cast<int>(data.size()) - absolute_position;
        if (font.gps_size > 0 && glyph_offset.offset < font.gps_size) {
            max_size = std::min(max_size, font.gps_size - glyph_offset.offset);
        }
        const int next_offset = findNextOffset(known_offsets, glyph_offset.offset);
        if (next_offset > glyph_offset.offset) {
            max_size = std::min(max_size, next_offset - glyph_offset.offset);
        }

        const int effective_size = glyph_offset.size > 0 ? std::min(glyph_offset.size, max_size) : std::min(64, max_size);
        if (effective_size <= 0) {
            continue;
        }

        Pfr1Font::CharacterRecord sub_record;
        sub_record.char_code = glyph.char_code;
        sub_record.set_width = static_cast<int>(glyph.set_width);
        const auto sub_glyph = parseOutlineGlyph(font,
                                                data,
                                                absolute_position,
                                                effective_size,
                                                sub_record,
                                                known_offsets,
                                                depth + 1);
        if (!sub_glyph.contours.empty()) {
            appendTransformedContours(glyph,
                                      sub_glyph,
                                      x_transform.scale,
                                      y_transform.scale,
                                      x_transform.offset,
                                      y_transform.offset);
        }
    }
}

Pfr1Font::OutlineGlyph parseOutlineGlyph(const Pfr1Font& font,
                                         const std::vector<std::uint8_t>& data,
                                         int start,
                                         int size,
                                         const Pfr1Font::CharacterRecord& record,
                                         const std::vector<int>& known_offsets,
                                         int depth) {
    Pfr1Font::OutlineGlyph glyph{record.char_code, static_cast<float>(record.set_width), {}};
    if (size <= 0 || start < 0 || start + size > static_cast<int>(data.size())) {
        return glyph;
    }

    const int flags = data[static_cast<std::size_t>(start)] & 0xFF;
    const int outline_format = (flags >> 6) & 0x03;
    const bool compound_glyph = outline_format >= 2 && (flags & 0x3F) > 0;
    if (compound_glyph) {
        parseCompoundGlyph(font, data, start, size, glyph, known_offsets, depth);
        return glyph;
    }

    return parseSimpleOutlineGlyph(data, start, size, record);
}

Pfr1Font::OutlineGlyph parseSimpleOutlineGlyph(const std::vector<std::uint8_t>& data,
                                               int start,
                                               int size,
                                               const Pfr1Font::CharacterRecord& record) {
    Pfr1Font::OutlineGlyph glyph{record.char_code, static_cast<float>(record.set_width), {}};
    if (size <= 0 || start < 0 || start + size > static_cast<int>(data.size())) {
        return glyph;
    }

    const int flags = data[static_cast<std::size_t>(start)] & 0xFF;
    int pos = start + 1;
    const int count_encoding = flags & 3;

    int x_orus_count = 0;
    int y_orus_count = 0;
    switch (count_encoding) {
        case 1:
            if (pos < start + size) {
                const int count_byte = data[static_cast<std::size_t>(pos++)] & 0xFF;
                x_orus_count = count_byte & 0x0F;
                y_orus_count = (count_byte >> 4) & 0x0F;
            }
            break;
        case 2:
        case 3:
            if (pos + 1 < start + size) {
                x_orus_count = data[static_cast<std::size_t>(pos++)] & 0xFF;
                y_orus_count = data[static_cast<std::size_t>(pos++)] & 0xFF;
            }
            break;
        default:
            break;
    }

    std::vector<int> control_x(static_cast<std::size_t>(x_orus_count), 0);
    std::vector<int> control_y(static_cast<std::size_t>(y_orus_count), 0);
    pos = parseControlValues(data, pos, flags, control_x, control_y);

    if ((flags & 0x08) != 0 && pos < start + size) {
        const int extra_count = data[static_cast<std::size_t>(pos++)] & 0xFF;
        for (int i = 0; i < extra_count; ++i) {
            if (pos + 1 >= start + size) {
                break;
            }
            const int item_length = data[static_cast<std::size_t>(pos)] & 0xFF;
            pos += item_length + 2;
        }
    }

    parseNibbleCommands(data, pos, start + size, glyph, control_x, control_y);
    return glyph;
}

} // namespace

void Pfr1Font::Contour::moveTo(float x_value, float y_value) {
    commands.push_back(Command{0, x_value, y_value});
}

void Pfr1Font::Contour::lineTo(float x_value, float y_value) {
    commands.push_back(Command{1, x_value, y_value});
}

void Pfr1Font::Contour::curveTo(float x1_value,
                                float y1_value,
                                float x2_value,
                                float y2_value,
                                float x_value,
                                float y_value) {
    commands.push_back(Command{2, x_value, y_value, x1_value, y1_value, x2_value, y2_value});
}

std::shared_ptr<Pfr1Font> Pfr1Font::parse(const std::vector<std::uint8_t>& data) {
    if (data.size() < 58 || data[0] != 'P' || data[1] != 'F' || data[2] != 'R' || data[3] != '1') {
        return nullptr;
    }

    auto font = std::shared_ptr<Pfr1Font>(new Pfr1Font());
    try {
        font->parseHeader(data);
        font->parsePhysicalFont(data);
        font->parseGlyphStubsAndBitmaps(data);
    } catch (const std::exception&) {
        // Match the Java parser's lenient behavior: keep whatever was parsed.
    }
    return font;
}

void Pfr1Font::parseHeader(const std::vector<std::uint8_t>& data) {
    PfrBitReader reader(data, 4);

    skipU16(reader);
    skipU16(reader);
    skipU16(reader);
    const int log_font_dir_size = reader.readU16();
    const int log_font_dir_offset = reader.readU16();
    skipU16(reader);
    const int log_font_section_size = reader.readU24();
    const int log_font_section_offset = reader.readU24();
    skipU16(reader);
    skipU24(reader);
    skipU24(reader);
    skipU16(reader);
    gps_size = reader.readU24();
    gps_offset = reader.readU24();
    skipU8(reader);
    skipU8(reader);
    skipU8(reader);
    skipU8(reader);

    const int flags_byte = reader.readU8();
    pfr_black_pixel = (flags_byte & 0x01) != 0;

    skipU24(reader);
    skipU24(reader);
    skipU24(reader);
    skipU16(reader);
    skipU8(reader);
    skipU8(reader);
    stored_max_chars_ = reader.readU16();

    parseLogicalFontDirectory(data, log_font_dir_size, log_font_dir_offset, log_font_section_size, log_font_section_offset);
}

void Pfr1Font::parseLogicalFontDirectory(const std::vector<std::uint8_t>& data,
                                         int dir_size,
                                         int dir_offset,
                                         int section_size,
                                         int section_offset) {
    if (dir_offset == 0 || dir_size == 0) {
        return;
    }

    if (dir_size < 14) {
        if (section_size >= 18 && section_offset > 0 && section_offset < static_cast<int>(data.size())) {
            PfrBitReader reader(data, section_offset);
            for (int i = 0; i < 4; ++i) {
                font_matrix[static_cast<std::size_t>(i)] = reader.readI24();
            }
        }
        return;
    }

    if (dir_offset >= static_cast<int>(data.size())) {
        return;
    }
    PfrBitReader reader(data, dir_offset);
    const int n_log_fonts = reader.readU16();
    if (n_log_fonts > 0) {
        for (int i = 0; i < 4; ++i) {
            font_matrix[static_cast<std::size_t>(i)] = reader.readI24();
        }
    }
}

void Pfr1Font::parsePhysicalFont(const std::vector<std::uint8_t>& data) {
    PfrBitReader header(data, 4);
    skipU16(header);
    skipU16(header);
    skipU16(header);
    skipU16(header);
    skipU16(header);
    skipU16(header);
    skipU24(header);
    skipU24(header);
    skipU16(header);
    const int phys_font_section_size = header.readU24();
    const int phys_offset = header.readU24();

    if (phys_offset >= static_cast<int>(data.size())) {
        return;
    }
    int phys_end = std::min(static_cast<int>(data.size()), phys_offset + phys_font_section_size);
    if (gps_offset > phys_offset && gps_offset <= static_cast<int>(data.size())) {
        phys_end = std::min(phys_end, gps_offset);
    }

    PfrBitReader reader(data, phys_offset);
    skipU16(reader);
    metrics.outline_resolution = reader.readU16();
    if (metrics.outline_resolution == 0) {
        metrics.outline_resolution = 2048;
    }
    metrics.metrics_resolution = reader.readU16();
    if (metrics.metrics_resolution == 0) {
        metrics.metrics_resolution = metrics.outline_resolution;
    }

    metrics.x_min = sign16(reader.readU16());
    metrics.y_min = sign16(reader.readU16());
    metrics.x_max = sign16(reader.readU16());
    metrics.y_max = sign16(reader.readU16());
    metrics.ascender = metrics.y_max;
    metrics.descender = metrics.y_min;

    const bool extra_items_present = reader.readBit();
    skipBit(reader);
    skipBit(reader);
    skipBit(reader);
    skipBit(reader);
    const bool proportional_escapement = reader.readBit();
    skipBit(reader);
    skipBit(reader);

    int standard_set_width = 0;
    if (!proportional_escapement) {
        standard_set_width = sign16(reader.readU16());
    }

    if (extra_items_present) {
        const int n_extra_items = reader.readU8();
        for (int i = 0; i < n_extra_items; ++i) {
            if (reader.remaining() < 2) {
                break;
            }
            const int item_size = reader.readU8();
            const int item_type = reader.readU8();
            const int item_start = reader.position();

            if (item_type == 1) {
                metrics.has_bitmap_section = true;
                reader.skip(item_size);
            } else if (item_type == 2) {
                std::string id;
                for (int j = 0; j < item_size; ++j) {
                    const int ch = reader.readU8();
                    if (ch == 0) {
                        break;
                    }
                    id.push_back(static_cast<char>(ch));
                }
                metrics.font_id = id;
                font_name = id;
                const int consumed = reader.position() - item_start;
                if (consumed < item_size) {
                    reader.skip(item_size - consumed);
                }
            } else {
                reader.skip(item_size);
            }
        }
    }

    const int n_aux_bytes = reader.readU24();
    if (n_aux_bytes > 0 && n_aux_bytes < 10000) {
        reader.skip(n_aux_bytes);
    } else if (n_aux_bytes >= 10000) {
        while (reader.position() < phys_end) {
            const int probe_pos = reader.position();
            const int n_blue_values = reader.readU8();
            const int byte_counter = n_blue_values * 2 + 6;
            const int n_chars_pos = reader.position() + byte_counter;
            if (n_chars_pos + 2 > phys_end) {
                reader.setPosition(probe_pos + 1);
                continue;
            }
            reader.setPosition(n_chars_pos);
            const int n_characters = reader.readU16();
            if (n_characters == stored_max_chars_) {
                reader.setPosition(probe_pos);
                break;
            }
            reader.setPosition(probe_pos + 1);
        }
    }

    const int n_blue_values = reader.readU8();
    for (int i = 0; i < n_blue_values; ++i) {
        skipU16(reader);
    }
    skipU8(reader);
    skipU8(reader);

    metrics.std_vw = sign16(reader.readU16());
    metrics.std_hw = sign16(reader.readU16());
    const int n_characters = reader.readU16();
    parseDeltaEncodedCharRecords(reader, n_characters, standard_set_width);
}

void Pfr1Font::parseDeltaEncodedCharRecords(PfrBitReader& reader,
                                            int n_characters,
                                            int standard_set_width) {
    int char_code = -1;
    int set_width = standard_set_width;
    int glyph_size = 0;
    int glyph_offset = 0;

    for (int i = 0; i < n_characters; ++i) {
        if (reader.remaining() < 1) {
            break;
        }

        const int flags = reader.readU8();
        const int next_gps_offset = glyph_offset + glyph_size;

        const int char_code_mode = flags & 0x03;
        ++char_code;
        switch (char_code_mode) {
            case 1:
                char_code += reader.readU8();
                break;
            case 2:
                char_code += reader.readU16();
                break;
            default:
                break;
        }

        const int set_width_mode = (flags >> 2) & 0x03;
        switch (set_width_mode) {
            case 1:
                set_width += reader.readU8();
                break;
            case 2:
                set_width -= reader.readU8();
                break;
            case 3:
                set_width = sign16(reader.readU16());
                break;
            default:
                break;
        }

        const int gps_size_mode = (flags >> 4) & 0x03;
        switch (gps_size_mode) {
            case 0:
                glyph_size = reader.readU8();
                break;
            case 1:
                glyph_size = reader.readU8() + 256;
                break;
            case 2:
                glyph_size = reader.readU8() + 512;
                break;
            case 3:
                glyph_size = reader.readU16();
                break;
            default:
                break;
        }

        const int gps_offset_mode = (flags >> 6) & 0x03;
        switch (gps_offset_mode) {
            case 0:
                glyph_offset = next_gps_offset;
                break;
            case 1:
                glyph_offset = next_gps_offset + reader.readU8();
                break;
            case 2:
                glyph_offset = reader.readU16();
                break;
            case 3:
                glyph_offset = reader.readU24();
                break;
            default:
                break;
        }

        char_records.push_back(CharacterRecord{char_code, set_width, glyph_size, glyph_offset});
    }
}

void Pfr1Font::parseGlyphStubsAndBitmaps(const std::vector<std::uint8_t>& data) {
    if (gps_offset + gps_size > static_cast<int>(data.size())) {
        return;
    }

    std::vector<int> known_offsets;
    known_offsets.reserve(char_records.size());
    for (const auto& record : char_records) {
        known_offsets.push_back(record.gps_offset);
    }
    std::sort(known_offsets.begin(), known_offsets.end());

    for (const auto& record : char_records) {
        if (record.gps_size <= 1) {
            glyphs[record.char_code] = OutlineGlyph{record.char_code, static_cast<float>(record.set_width), {}};
            continue;
        }

        const int start = gps_offset + record.gps_offset;
        const int size = record.gps_size;
        if (start < 0 || size <= 0 || start + size > static_cast<int>(data.size())) {
            continue;
        }

        if (metrics.has_bitmap_section) {
            const int zeros_field = (data[static_cast<std::size_t>(start)] >> 4) & 0x07;
            if (zeros_field != 0) {
                try {
                    auto glyph = parseBitmapGlyph(data, start, size, record.char_code);
                    if (record.set_width > 0) {
                        glyph.set_width = record.set_width;
                    }
                    bitmap_glyphs[record.char_code] = std::move(glyph);
                } catch (const std::exception&) {
                }
            }
        }

        try {
            glyphs[record.char_code] = parseOutlineGlyph(*this, data, start, size, record, known_offsets, 0);
        } catch (const std::exception&) {
        }
    }

    for (int lower = 'a'; lower <= 'z'; ++lower) {
        const int upper = lower - 32;
        const auto lower_it = glyphs.find(lower);
        if (lower_it != glyphs.end() && !lower_it->second.contours.empty()) {
            continue;
        }
        const auto upper_it = glyphs.find(upper);
        if (upper_it != glyphs.end() && !upper_it->second.contours.empty()) {
            auto copy = upper_it->second;
            copy.char_code = lower;
            glyphs[lower] = std::move(copy);
        }
    }
}

Pfr1Font::BitmapGlyph Pfr1Font::parseBitmapGlyph(const std::vector<std::uint8_t>& data,
                                                 int start,
                                                 int size,
                                                 int char_code) const {
    if (size < 2) {
        throw std::runtime_error("bitmap glyph too small");
    }

    const int end = start + size;
    int pos = start;
    const int format_byte = data[static_cast<std::size_t>(pos++)] & 0xFF;

    const int image_format = (format_byte >> 6) & 0x03;
    const int escapement_format = (format_byte >> 4) & 0x03;
    const int size_format = (format_byte >> 2) & 0x03;
    const int position_format = format_byte & 0x03;

    const int pos_bytes = position_format == 0 ? 0 : position_format == 1 ? 2 : position_format == 2 ? 4 : 8;
    const int size_bytes = size_format == 0 ? 2 : size_format == 1 ? 4 : size_format == 2 ? 6 : 8;
    const int esc_bytes = escapement_format == 0 ? 0 : escapement_format == 1 ? 1 : escapement_format == 2 ? 2 : 4;
    if (pos + pos_bytes + size_bytes + esc_bytes > end ||
        pos + pos_bytes + size_bytes + esc_bytes > static_cast<int>(data.size())) {
        throw std::runtime_error("bitmap glyph bounds exceeded");
    }

    int x_pos = 0;
    int y_pos = 0;
    switch (position_format) {
        case 1:
            x_pos = readSignedN(data, pos, 1);
            ++pos;
            y_pos = readSignedN(data, pos, 1);
            ++pos;
            break;
        case 2:
            x_pos = readSignedN(data, pos, 2);
            pos += 2;
            y_pos = readSignedN(data, pos, 2);
            pos += 2;
            break;
        case 3:
            x_pos = readSignedN(data, pos, 4);
            pos += 4;
            y_pos = readSignedN(data, pos, 4);
            pos += 4;
            break;
        default:
            break;
    }

    int x_size = 0;
    int y_size = 0;
    switch (size_format) {
        case 0:
            x_size = readUnsignedN(data, pos, 1);
            ++pos;
            y_size = readUnsignedN(data, pos, 1);
            ++pos;
            break;
        case 1:
            x_size = readUnsignedN(data, pos, 2);
            pos += 2;
            y_size = readUnsignedN(data, pos, 2);
            pos += 2;
            break;
        case 2:
            x_size = readUnsignedN(data, pos, 3);
            pos += 3;
            y_size = readUnsignedN(data, pos, 3);
            pos += 3;
            break;
        default:
            x_size = readUnsignedN(data, pos, 4);
            pos += 4;
            y_size = readUnsignedN(data, pos, 4);
            pos += 4;
            break;
    }

    int set_width = x_size;
    switch (escapement_format) {
        case 1:
            set_width = readUnsignedN(data, pos, 1);
            ++pos;
            break;
        case 2:
            set_width = readUnsignedN(data, pos, 2);
            pos += 2;
            break;
        case 3:
            set_width = readUnsignedN(data, pos, 4);
            pos += 4;
            break;
        default:
            break;
    }

    if (x_size <= 0 || y_size <= 0 || x_size > 4096 || y_size > 4096) {
        throw std::runtime_error("bitmap glyph size invalid");
    }

    const int total_bits = x_size * y_size;
    if (total_bits <= 0 || total_bits > 1'000'000) {
        throw std::runtime_error("bitmap glyph bit count invalid");
    }

    const int remaining = end - pos;
    if (remaining <= 0 || pos > static_cast<int>(data.size())) {
        throw std::runtime_error("bitmap glyph data missing");
    }

    std::vector<std::uint8_t> image_data;
    if (image_format == 0) {
        const int expected = (total_bits + 7) / 8;
        if (expected > remaining || expected <= 0) {
            throw std::runtime_error("bitmap glyph packed data invalid");
        }
        image_data.assign(data.begin() + pos, data.begin() + pos + expected);
    } else if (image_format == 1) {
        image_data = decodeRleBitmap(data, pos, remaining, x_size, y_size);
    } else {
        image_data.assign(data.begin() + pos, data.begin() + end);
    }

    return BitmapGlyph{
        char_code,
        image_format,
        sign16(x_pos & 0xFFFF),
        sign16(y_pos & 0xFFFF),
        x_size,
        y_size,
        set_width,
        std::move(image_data)
    };
}

} // namespace libreshockwave::font
