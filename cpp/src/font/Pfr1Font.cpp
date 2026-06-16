#include "libreshockwave/font/Pfr1Font.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <limits>
#include <string>
#include <utility>

#include "libreshockwave/font/PfrBitReader.hpp"

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
    const int signBit = 1 << (n * 8 - 1);
    if ((value & signBit) != 0) {
        const int mask = (-1) << (n * 8);
        return value | mask;
    }
    return value;
}

std::vector<std::uint8_t> decodeRleBitmap(const std::vector<std::uint8_t>& data,
                                          int offset,
                                          int dataLen,
                                          int width,
                                          int height) {
    const int totalBits = width * height;
    if (totalBits <= 0 || totalBits > 1'000'000) {
        return {};
    }
    const int totalBytes = (totalBits + 7) / 8;
    std::vector<std::uint8_t> result(static_cast<std::size_t>(totalBytes), 0);
    int outPos = 0;
    int pos = offset;
    const int end = offset + dataLen;

    while (pos < end && pos < static_cast<int>(data.size()) && outPos < totalBits) {
        const int byte = data[static_cast<std::size_t>(pos++)] & 0xFF;
        const int count = (byte >> 4) & 0x0F;
        const int value = byte & 0x0F;
        for (int i = 0; i < count && outPos < totalBits; ++i) {
            if (value != 0) {
                result[static_cast<std::size_t>(outPos / 8)] |=
                    static_cast<std::uint8_t>(1U << (7 - (outPos % 8)));
            }
            ++outPos;
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
    bool nibbleHigh = false;
};

struct CoordRead {
    int value = 0;
    int pos = 0;
    bool nibbleHigh = false;
};

struct CoordPairRead {
    int x = 0;
    int y = 0;
    int pos = 0;
    bool nibbleHigh = false;
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

ByteRead readByteAligned(const std::vector<std::uint8_t>& data, int pos, bool nibbleHigh) {
    if (pos >= static_cast<int>(data.size())) {
        return {0, pos};
    }
    if (nibbleHigh) {
        const int lo = data[static_cast<std::size_t>(pos)] & 0x0F;
        ++pos;
        const int hi = pos < static_cast<int>(data.size())
                           ? (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F
                           : 0;
        return {(lo << 4) | hi, pos};
    }
    return {data[static_cast<std::size_t>(pos)] & 0xFF, pos + 1};
}

NibbleRead readNibble(const std::vector<std::uint8_t>& data, int pos, bool nibbleHigh) {
    if (pos >= static_cast<int>(data.size())) {
        return {-1, pos, nibbleHigh};
    }

    nibbleHigh = !nibbleHigh;
    if (nibbleHigh) {
        return {(data[static_cast<std::size_t>(pos)] >> 4) & 0x0F, pos, nibbleHigh};
    }

    const int value = data[static_cast<std::size_t>(pos)] & 0x0F;
    return {value, pos + 1, nibbleHigh};
}

int orusLookup(const std::vector<int>& controlValues, int current, int direction) {
    if (controlValues.empty() || direction == 0) {
        return current;
    }

    if (direction > 0) {
        std::size_t pos = 0;
        while (pos < controlValues.size() && controlValues[pos] <= current) {
            ++pos;
        }
        if (pos >= controlValues.size()) {
            return current;
        }
        const auto target = std::min<std::size_t>(pos + static_cast<std::size_t>(direction - 1),
                                                  controlValues.size() - 1);
        return controlValues[target];
    }

    for (int i = static_cast<int>(controlValues.size()) - 1; i >= 0; --i) {
        if (controlValues[static_cast<std::size_t>(i)] < current) {
            return controlValues[static_cast<std::size_t>(std::max(i + direction + 1, 0))];
        }
    }
    return current;
}

CoordRead readCe9dCoordValue(const std::vector<std::uint8_t>& data,
                             int pos,
                             int v8,
                             bool threeByteMode,
                             bool nibbleAligned) {
    if (pos >= static_cast<int>(data.size())) {
        return {0, pos, nibbleAligned};
    }

    if ((v8 & 1) == 0) {
        int result = 0;
        if (nibbleAligned) {
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
        return {result, pos, nibbleAligned};
    }

    if (threeByteMode) {
        int result = 0;
        if (nibbleAligned) {
            const int b0Low = pos > 0 ? data[static_cast<std::size_t>(pos - 1)] & 0x0F : 0;
            const int b1 = pos < static_cast<int>(data.size())
                               ? data[static_cast<std::size_t>(pos)] & 0xFF
                               : 0;
            ++pos;
            const int b2High = pos < static_cast<int>(data.size())
                                   ? (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F
                                   : 0;
            result = sign16((b0Low << 12) | (b1 << 4) | b2High);
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
        return {result, pos, nibbleAligned};
    }

    int result = 0;
    if (nibbleAligned) {
        const int lo = data[static_cast<std::size_t>(pos)] & 0x0F;
        ++pos;
        const int nextByte = pos < static_cast<int>(data.size())
                                 ? data[static_cast<std::size_t>(pos)] & 0xFF
                                 : 0;
        ++pos;
        result = sign16(nextByte + 16 * sign8(lo << 4));
        nibbleAligned = false;
    } else {
        const int signedByte = sign8(data[static_cast<std::size_t>(pos)]);
        ++pos;
        const int nextHigh = pos < static_cast<int>(data.size())
                                 ? (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F
                                 : 0;
        result = sign16(nextHigh + 16 * signedByte);
        nibbleAligned = true;
    }
    return {result, pos, nibbleAligned};
}

int parseControlValues(const std::vector<std::uint8_t>& data,
                       int pos,
                       int flags,
                       std::vector<int>& controlX,
                       std::vector<int>& controlY) {
    if (controlX.empty() && controlY.empty()) {
        return pos;
    }

    const bool threeByteMode = (flags & 3) == 3;
    const bool flagPerCoordinate = (flags & 0x40) != 0;
    bool nibbleAligned = false;
    int flagCache = 0;
    int flagCacheCount = 0;

    int accumulatedX = 0;
    for (std::size_t i = 0; i < controlX.size(); ++i) {
        if (pos >= static_cast<int>(data.size())) {
            break;
        }

        int v8 = 0;
        if (i == 0) {
            v8 = (flags >> 4) & 1;
        } else if (flagPerCoordinate) {
            if (flagCacheCount > 0) {
                v8 = (flagCache >> 1) & 1;
                flagCache >>= 1;
                --flagCacheCount;
            } else if (pos < static_cast<int>(data.size())) {
                if (nibbleAligned) {
                    const int nibbleValue = data[static_cast<std::size_t>(pos)] & 0x0F;
                    ++pos;
                    nibbleAligned = false;
                    v8 = nibbleValue & 1;
                    flagCache = nibbleValue;
                    flagCacheCount = 3;
                } else {
                    const int nibbleValue = (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F;
                    nibbleAligned = true;
                    v8 = nibbleValue & 1;
                    flagCache = nibbleValue;
                    flagCacheCount = 3;
                }
            }
        }

        const auto read = readCe9dCoordValue(data, pos, v8, threeByteMode, nibbleAligned);
        pos = read.pos;
        nibbleAligned = read.nibbleHigh;
        accumulatedX += read.value;
        controlX[i] = accumulatedX;
    }

    int accumulatedY = 0;
    for (std::size_t i = 0; i < controlY.size(); ++i) {
        if (pos >= static_cast<int>(data.size())) {
            break;
        }

        int v8 = 0;
        if (i == 0) {
            v8 = (flags >> 5) & 1;
        } else if (flagPerCoordinate) {
            if (flagCacheCount > 0) {
                v8 = (flagCache >> 1) & 1;
                flagCache >>= 1;
                --flagCacheCount;
            } else if (pos < static_cast<int>(data.size())) {
                if (nibbleAligned) {
                    const int nibbleValue = data[static_cast<std::size_t>(pos)] & 0x0F;
                    ++pos;
                    nibbleAligned = false;
                    v8 = nibbleValue & 1;
                    flagCache = nibbleValue;
                    flagCacheCount = 3;
                } else {
                    const int nibbleValue = (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F;
                    nibbleAligned = true;
                    v8 = nibbleValue & 1;
                    flagCache = nibbleValue;
                    flagCacheCount = 3;
                }
            }
        }

        const auto read = readCe9dCoordValue(data, pos, v8, threeByteMode, nibbleAligned);
        pos = read.pos;
        nibbleAligned = read.nibbleHigh;
        accumulatedY += read.value;
        controlY[i] = accumulatedY;
    }

    if (nibbleAligned) {
        ++pos;
    }
    return pos;
}

CoordRead readEncodedCoordValue(const std::vector<std::uint8_t>& data,
                                int pos,
                                int encoding,
                                bool nibbleHigh,
                                int current,
                                const std::vector<int>& controlValues) {
    switch (encoding) {
        case 1: {
            nibbleHigh = !nibbleHigh;
            if (pos >= static_cast<int>(data.size())) {
                return {current, pos, nibbleHigh};
            }
            int nibble = 0;
            if (nibbleHigh) {
                nibble = (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F;
            } else {
                nibble = data[static_cast<std::size_t>(pos)] & 0x0F;
                ++pos;
            }
            return {sign16(current + nibble - 8), pos, nibbleHigh};
        }
        case 2: {
            const auto read = readByteAligned(data, pos, nibbleHigh);
            const int signedByte = sign8(read.value);
            if (signedByte >= -8 && signedByte < 8) {
                const int direction = (signedByte & 0x80) == 0 ? signedByte + 1 : signedByte;
                return {orusLookup(controlValues, current, direction), read.pos, nibbleHigh};
            }
            return {sign16(current + signedByte), read.pos, nibbleHigh};
        }
        case 3: {
            const auto highRead = readByteAligned(data, pos, nibbleHigh);
            pos = highRead.pos;
            nibbleHigh = !nibbleHigh;
            int low = 0;
            if (pos >= static_cast<int>(data.size())) {
                low = 0;
            } else if (nibbleHigh) {
                low = (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F;
            } else {
                low = data[static_cast<std::size_t>(pos)] & 0x0F;
                ++pos;
            }

            const int delta12 = (sign8(highRead.value) << 4) | low;
            if (delta12 >= -128 && delta12 < 128) {
                const auto extraRead = readByteAligned(data, pos, nibbleHigh);
                const int delta16 = (delta12 << 8) | (extraRead.value & 0xFF);
                return {sign16(current + delta16), extraRead.pos, nibbleHigh};
            }
            return {sign16(current + delta12), pos, nibbleHigh};
        }
        default:
            return {current, pos, nibbleHigh};
    }
}

CoordPairRead readEncodedCoordsInto(const std::vector<std::uint8_t>& data,
                                    int pos,
                                    int encoding,
                                    int& currentX,
                                    int& currentY,
                                    int& previousX,
                                    int& previousY,
                                    int outX,
                                    int outY,
                                    bool nibbleHigh,
                                    const std::vector<int>& controlX,
                                    const std::vector<int>& controlY) {
    const int xEncoding = encoding & 3;
    const int yEncoding = (encoding >> 2) & 3;

    if (xEncoding != 0) {
        const auto read = readEncodedCoordValue(data, pos, xEncoding, nibbleHigh, currentX, controlX);
        outX = read.value;
        pos = read.pos;
        nibbleHigh = read.nibbleHigh;
    }
    previousX = currentX;
    currentX = outX;

    if (yEncoding != 0) {
        const auto read = readEncodedCoordValue(data, pos, yEncoding, nibbleHigh, currentY, controlY);
        outY = read.value;
        pos = read.pos;
        nibbleHigh = read.nibbleHigh;
    }
    previousY = currentY;
    currentY = outY;

    return {outX, outY, pos, nibbleHigh};
}

std::uint32_t calculateCurveEncoding14(std::uint32_t byte) {
    const std::uint32_t value =
        CURVE_TABLE_14B[(byte >> 3U) & 0x03U] + 16U * CURVE_TABLE_14A[(byte >> 5U) & 0x07U];
    return CURVE_TABLE_14A[byte & 0x07U] + value * 16U;
}

void parseNibbleCommands(const std::vector<std::uint8_t>& data,
                         int startPos,
                         int endLimit,
                         Pfr1Font::OutlineGlyph& glyph,
                         const std::vector<int>& controlX,
                         const std::vector<int>& controlY) {
    int pos = startPos;
    const int endPos = endLimit > 0 ? endLimit - 1 : 0;
    bool nibbleHigh = false;
    int currentX = 0;
    int currentY = 0;
    int previousX = 0;
    int previousY = 0;
    Pfr1Font::Contour currentContour;
    bool firstIteration = true;

    for (int iterations = 0; iterations < 500; ++iterations) {
        if (pos >= endPos && (pos != endPos || nibbleHigh)) {
            break;
        }
        if (pos >= static_cast<int>(data.size())) {
            break;
        }

        int command = 0;
        if (firstIteration) {
            command = 6;
            firstIteration = false;
        } else {
            nibbleHigh = !nibbleHigh;
            if (nibbleHigh) {
                command = (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F;
            } else {
                command = data[static_cast<std::size_t>(pos)] & 0x0F;
                ++pos;
            }
        }

        const int beforeX = currentX;
        const int beforeY = currentY;

        switch (command) {
            case 0: {
                const auto nibbleRead = readNibble(data, pos, nibbleHigh);
                if (nibbleRead.value < 0) {
                    break;
                }
                const int nibble = nibbleRead.value;
                pos = nibbleRead.pos;
                nibbleHigh = nibbleRead.nibbleHigh;
                const int direction = (nibble & 4) != 0 ? (nibble & 7) - 8 : (nibble & 7) + 1;
                previousX = beforeX;
                previousY = beforeY;
                if ((nibble & 8) != 0) {
                    currentY = orusLookup(controlY, currentY, direction);
                } else {
                    currentX = orusLookup(controlX, currentX, direction);
                }
                currentContour.lineTo(static_cast<float>(currentX), static_cast<float>(currentY));
                break;
            }
            case 1: {
                const auto read = readByteAligned(data, pos, nibbleHigh);
                pos = read.pos;
                previousX = beforeX;
                previousY = beforeY;
                currentX = sign16(currentX + sign8(read.value));
                currentContour.lineTo(static_cast<float>(currentX), static_cast<float>(currentY));
                break;
            }
            case 2: {
                const auto read = readByteAligned(data, pos, nibbleHigh);
                pos = read.pos;
                previousX = beforeX;
                previousY = beforeY;
                currentY = sign16(currentY + sign8(read.value));
                currentContour.lineTo(static_cast<float>(currentX), static_cast<float>(currentY));
                break;
            }
            case 3:
            case 4: {
                const auto highRead = readByteAligned(data, pos, nibbleHigh);
                pos = highRead.pos;
                nibbleHigh = !nibbleHigh;
                int low = 0;
                if (pos >= static_cast<int>(data.size())) {
                    low = 0;
                } else if (nibbleHigh) {
                    low = (data[static_cast<std::size_t>(pos)] >> 4) & 0x0F;
                } else {
                    low = data[static_cast<std::size_t>(pos)] & 0x0F;
                    ++pos;
                }

                const int delta12 = (sign8(highRead.value) << 4) | low;
                int delta = delta12;
                if (delta12 >= -128 && delta12 < 128) {
                    const auto extraRead = readByteAligned(data, pos, nibbleHigh);
                    pos = extraRead.pos;
                    delta = (delta12 << 8) | (extraRead.value & 0xFF);
                }
                if (command == 3) {
                    currentX += sign16(delta);
                } else {
                    currentY += sign16(delta);
                }
                previousX = beforeX;
                previousY = beforeY;
                currentContour.lineTo(static_cast<float>(currentX), static_cast<float>(currentY));
                break;
            }
            case 5:
            case 6: {
                const auto encodingRead = readNibble(data, pos, nibbleHigh);
                if (encodingRead.value < 0) {
                    break;
                }
                pos = encodingRead.pos;
                nibbleHigh = encodingRead.nibbleHigh;
                const auto read = readEncodedCoordsInto(data,
                                                        pos,
                                                        encodingRead.value,
                                                        currentX,
                                                        currentY,
                                                        previousX,
                                                        previousY,
                                                        currentX,
                                                        currentY,
                                                        nibbleHigh,
                                                        controlX,
                                                        controlY);
                pos = read.pos;
                nibbleHigh = read.nibbleHigh;

                if (command == 6) {
                    if (!currentContour.commands.empty()) {
                        glyph.contours.push_back(std::move(currentContour));
                        currentContour = Pfr1Font::Contour();
                    }
                    currentContour.moveTo(static_cast<float>(currentX), static_cast<float>(currentY));
                } else {
                    currentContour.lineTo(static_cast<float>(currentX), static_cast<float>(currentY));
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
                            const auto nibbleRead = readNibble(data, pos, nibbleHigh);
                            if (nibbleRead.value < 0) {
                                break;
                            }
                            pos = nibbleRead.pos;
                            nibbleHigh = nibbleRead.nibbleHigh;
                            encoding = CURVE_TABLE_9[static_cast<std::size_t>(nibbleRead.value & 0x0F)];
                            path = 49;
                            break;
                        }
                        case 10: {
                            const auto nibbleRead = readNibble(data, pos, nibbleHigh);
                            if (nibbleRead.value < 0) {
                                break;
                            }
                            pos = nibbleRead.pos;
                            nibbleHigh = nibbleRead.nibbleHigh;
                            encoding = CURVE_TABLE_10[static_cast<std::size_t>(nibbleRead.value & 0x0F)];
                            path = 54;
                            break;
                        }
                        case 11: {
                            const auto byteRead = readByteAligned(data, pos, nibbleHigh);
                            pos = byteRead.pos;
                            const auto byte = static_cast<std::uint32_t>(byteRead.value);
                            encoding = (byte & 3U) + 4U * ((byte & 0x3CU) + 4U * (byte & 0xC0U));
                            path = 49;
                            break;
                        }
                        case 12: {
                            const auto byteRead = readByteAligned(data, pos, nibbleHigh);
                            pos = byteRead.pos;
                            encoding = static_cast<std::uint32_t>(byteRead.value) * 4U;
                            path = 54;
                            break;
                        }
                        case 13: {
                            const auto nibbleRead = readNibble(data, pos, nibbleHigh);
                            if (nibbleRead.value < 0) {
                                break;
                            }
                            pos = nibbleRead.pos;
                            nibbleHigh = nibbleRead.nibbleHigh;
                            encoding = CURVE_TABLE_13[static_cast<std::size_t>(nibbleRead.value & 0x0F)];
                            path = 70;
                            break;
                        }
                        case 14: {
                            const auto byteRead = readByteAligned(data, pos, nibbleHigh);
                            pos = byteRead.pos;
                            encoding = calculateCurveEncoding14(static_cast<std::uint32_t>(byteRead.value));
                            path = 70;
                            break;
                        }
                        case 15: {
                            const auto nibbleRead = readNibble(data, pos, nibbleHigh);
                            if (nibbleRead.value < 0) {
                                break;
                            }
                            pos = nibbleRead.pos;
                            nibbleHigh = nibbleRead.nibbleHigh;
                            const auto byteRead = readByteAligned(data, pos, nibbleHigh);
                            pos = byteRead.pos;
                            encoding = static_cast<std::uint32_t>(byteRead.value) |
                                       (static_cast<std::uint32_t>(nibbleRead.value) << 8U);
                            path = 70;
                            break;
                        }
                        default:
                            break;
                    }

                    if (path == 0) {
                        break;
                    }

                    const int startX = beforeX;
                    const int startY = beforeY;
                    int control1X = currentX;
                    int control1Y = currentY;
                    int control2X = 0;
                    int control2Y = 0;
                    int endX = 0;
                    int endY = 0;

                    if (path == 49) {
                        auto read = readEncodedCoordsInto(data,
                                                          pos,
                                                          static_cast<int>(encoding & 0x0FU),
                                                          currentX,
                                                          currentY,
                                                          previousX,
                                                          previousY,
                                                          control1X,
                                                          control1Y,
                                                          nibbleHigh,
                                                          controlX,
                                                          controlY);
                        control1X = read.x;
                        control1Y = read.y;
                        pos = read.pos;
                        nibbleHigh = read.nibbleHigh;

                        control2X = orusLookup(controlX, currentX, 0);
                        control2Y = control1Y;
                        read = readEncodedCoordsInto(data,
                                                     pos,
                                                     static_cast<int>((encoding >> 4U) & 0x0FU),
                                                     currentX,
                                                     currentY,
                                                     previousX,
                                                     previousY,
                                                     control2X,
                                                     control2Y,
                                                     nibbleHigh,
                                                     controlX,
                                                     controlY);
                        control2X = read.x;
                        control2Y = read.y;
                        pos = read.pos;
                        nibbleHigh = read.nibbleHigh;

                        endX = control2X;
                        endY = orusLookup(controlY, currentY, 0);
                        read = readEncodedCoordsInto(data,
                                                     pos,
                                                     static_cast<int>((encoding >> 8U) & 0x0FU),
                                                     currentX,
                                                     currentY,
                                                     previousX,
                                                     previousY,
                                                     endX,
                                                     endY,
                                                     nibbleHigh,
                                                     controlX,
                                                     controlY);
                        endX = read.x;
                        endY = read.y;
                        pos = read.pos;
                        nibbleHigh = read.nibbleHigh;
                    } else if (path == 54) {
                        auto read = readEncodedCoordsInto(data,
                                                          pos,
                                                          static_cast<int>(encoding & 0x0FU),
                                                          currentX,
                                                          currentY,
                                                          previousX,
                                                          previousY,
                                                          control1X,
                                                          control1Y,
                                                          nibbleHigh,
                                                          controlX,
                                                          controlY);
                        control1X = read.x;
                        control1Y = read.y;
                        pos = read.pos;
                        nibbleHigh = read.nibbleHigh;

                        control2X = control1X;
                        control2Y = orusLookup(controlY, currentY, 0);
                        read = readEncodedCoordsInto(data,
                                                     pos,
                                                     static_cast<int>((encoding >> 4U) & 0x0FU),
                                                     currentX,
                                                     currentY,
                                                     previousX,
                                                     previousY,
                                                     control2X,
                                                     control2Y,
                                                     nibbleHigh,
                                                     controlX,
                                                     controlY);
                        control2X = read.x;
                        control2Y = read.y;
                        pos = read.pos;
                        nibbleHigh = read.nibbleHigh;

                        endX = orusLookup(controlX, currentX, 0);
                        endY = control2Y;
                        read = readEncodedCoordsInto(data,
                                                     pos,
                                                     static_cast<int>((encoding >> 8U) & 0x0FU),
                                                     currentX,
                                                     currentY,
                                                     previousX,
                                                     previousY,
                                                     endX,
                                                     endY,
                                                     nibbleHigh,
                                                     controlX,
                                                     controlY);
                        endX = read.x;
                        endY = read.y;
                        pos = read.pos;
                        nibbleHigh = read.nibbleHigh;
                    } else {
                        control1X = sign16(currentX + (currentX - previousX));
                        control1Y = sign16(currentY + (currentY - previousY));
                        auto read = readEncodedCoordsInto(data,
                                                          pos,
                                                          static_cast<int>(encoding & 0x0FU),
                                                          currentX,
                                                          currentY,
                                                          previousX,
                                                          previousY,
                                                          control1X,
                                                          control1Y,
                                                          nibbleHigh,
                                                          controlX,
                                                          controlY);
                        control1X = read.x;
                        control1Y = read.y;
                        pos = read.pos;
                        nibbleHigh = read.nibbleHigh;

                        control2X = control1X;
                        control2Y = control1Y;
                        read = readEncodedCoordsInto(data,
                                                     pos,
                                                     static_cast<int>((encoding >> 4U) & 0x0FU),
                                                     currentX,
                                                     currentY,
                                                     previousX,
                                                     previousY,
                                                     control2X,
                                                     control2Y,
                                                     nibbleHigh,
                                                     controlX,
                                                     controlY);
                        control2X = read.x;
                        control2Y = read.y;
                        pos = read.pos;
                        nibbleHigh = read.nibbleHigh;

                        endX = control2X;
                        endY = control2Y;
                        read = readEncodedCoordsInto(data,
                                                     pos,
                                                     static_cast<int>((encoding >> 8U) & 0x0FU),
                                                     currentX,
                                                     currentY,
                                                     previousX,
                                                     previousY,
                                                     endX,
                                                     endY,
                                                     nibbleHigh,
                                                     controlX,
                                                     controlY);
                        endX = read.x;
                        endY = read.y;
                        pos = read.pos;
                        nibbleHigh = read.nibbleHigh;
                    }

                    if (currentContour.commands.empty()) {
                        currentContour.moveTo(static_cast<float>(startX), static_cast<float>(startY));
                    }
                    currentContour.curveTo(static_cast<float>(control1X),
                                           static_cast<float>(control1Y),
                                           static_cast<float>(control2X),
                                           static_cast<float>(control2Y),
                                           static_cast<float>(endX),
                                           static_cast<float>(endY));
                    previousX = control2X;
                    previousY = control2Y;
                    currentX = endX;
                    currentY = endY;
                }
                break;
            }
        }

        if (std::abs(currentX - beforeX) > 8192 || std::abs(currentY - beforeY) > 8192) {
            currentX = beforeX;
            currentY = beforeY;
        }
        if (currentContour.commands.size() > 300) {
            break;
        }
    }

    if (!currentContour.commands.empty()) {
        glyph.contours.push_back(std::move(currentContour));
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
                                         const std::vector<int>& knownOffsets,
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

int findNextOffset(const std::vector<int>& sortedOffsets, int currentOffset) {
    const auto it = std::upper_bound(sortedOffsets.begin(), sortedOffsets.end(), currentOffset);
    if (it == sortedOffsets.end()) {
        return std::numeric_limits<int>::max();
    }
    return *it;
}

void appendTransformedContours(Pfr1Font::OutlineGlyph& glyph,
                               const Pfr1Font::OutlineGlyph& subGlyph,
                               int xScale,
                               int yScale,
                               int xOffset,
                               int yOffset) {
    const float xScaleF = static_cast<float>(xScale) / 4096.0F;
    const float yScaleF = static_cast<float>(yScale) / 4096.0F;
    const float xOffsetF = static_cast<float>(xOffset);
    const float yOffsetF = static_cast<float>(yOffset);

    for (const auto& sourceContour : subGlyph.contours) {
        Pfr1Font::Contour transformed;
        for (const auto& command : sourceContour.commands) {
            const float x = command.x * xScaleF + xOffsetF;
            const float y = command.y * yScaleF + yOffsetF;
            if (command.type == 0) {
                transformed.moveTo(x, y);
            } else if (command.type == 1) {
                transformed.lineTo(x, y);
            } else if (command.type == 2) {
                transformed.curveTo(command.x1 * xScaleF + xOffsetF,
                                    command.y1 * yScaleF + yOffsetF,
                                    command.x2 * xScaleF + xOffsetF,
                                    command.y2 * yScaleF + yOffsetF,
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
                        const std::vector<int>& knownOffsets,
                        int depth) {
    if (depth >= 8) {
        return;
    }

    const int componentCount = data[static_cast<std::size_t>(start)] & 0x3F;
    int pos = start + 1;

    if ((data[static_cast<std::size_t>(start)] & 0x40) != 0 && pos + 2 <= start + size) {
        const int extraCount = (data[static_cast<std::size_t>(pos)] & 0xFF) |
                               ((data[static_cast<std::size_t>(pos + 1)] & 0xFF) << 8);
        pos += 2;
        for (int i = 0; i < extraCount; ++i) {
            if (pos >= start + size) {
                break;
            }
            const int length = data[static_cast<std::size_t>(pos)] & 0xFF;
            pos += length + 2;
        }
    }

    const int glyphGpsOffset = start - font.gpsOffset;
    int offsetAccumulator = glyphGpsOffset;

    for (int component = 0; component < componentCount; ++component) {
        if (pos >= start + size) {
            break;
        }

        const int formatByte = data[static_cast<std::size_t>(pos++)] & 0xFF;
        const int xFormat = formatByte % 6;
        const int yFormat = (formatByte / 6) % 6;
        const int offsetFormat = formatByte / 36;

        const auto xTransform = parseTransformModulo6(data, pos, xFormat);
        pos = xTransform.pos;
        const auto yTransform = parseTransformModulo6(data, pos, yFormat);
        pos = yTransform.pos;

        const auto glyphOffset = parseGlyphOffsetModulo6(data, pos, offsetFormat, offsetAccumulator);
        pos = glyphOffset.pos;
        offsetAccumulator = glyphOffset.accumulator;

        const int absolutePosition = font.gpsOffset + glyphOffset.offset;
        if (absolutePosition < 0 || absolutePosition >= static_cast<int>(data.size())) {
            continue;
        }

        int maxSize = static_cast<int>(data.size()) - absolutePosition;
        if (font.gpsSize > 0 && glyphOffset.offset < font.gpsSize) {
            maxSize = std::min(maxSize, font.gpsSize - glyphOffset.offset);
        }
        const int nextOffset = findNextOffset(knownOffsets, glyphOffset.offset);
        if (nextOffset > glyphOffset.offset) {
            maxSize = std::min(maxSize, nextOffset - glyphOffset.offset);
        }

        const int effectiveSize = glyphOffset.size > 0 ? std::min(glyphOffset.size, maxSize) : std::min(64, maxSize);
        if (effectiveSize <= 0) {
            continue;
        }

        Pfr1Font::CharacterRecord subRecord;
        subRecord.charCode = glyph.charCode;
        subRecord.setWidth = static_cast<int>(glyph.setWidth);
        const auto subGlyph = parseOutlineGlyph(font,
                                                data,
                                                absolutePosition,
                                                effectiveSize,
                                                subRecord,
                                                knownOffsets,
                                                depth + 1);
        if (!subGlyph.contours.empty()) {
            appendTransformedContours(glyph,
                                      subGlyph,
                                      xTransform.scale,
                                      yTransform.scale,
                                      xTransform.offset,
                                      yTransform.offset);
        }
    }
}

Pfr1Font::OutlineGlyph parseOutlineGlyph(const Pfr1Font& font,
                                         const std::vector<std::uint8_t>& data,
                                         int start,
                                         int size,
                                         const Pfr1Font::CharacterRecord& record,
                                         const std::vector<int>& knownOffsets,
                                         int depth) {
    Pfr1Font::OutlineGlyph glyph{record.charCode, static_cast<float>(record.setWidth), {}};
    if (size <= 0 || start < 0 || start + size > static_cast<int>(data.size())) {
        return glyph;
    }

    const int flags = data[static_cast<std::size_t>(start)] & 0xFF;
    const int outlineFormat = (flags >> 6) & 0x03;
    const bool compoundGlyph = outlineFormat >= 2 && (flags & 0x3F) > 0;
    if (compoundGlyph) {
        parseCompoundGlyph(font, data, start, size, glyph, knownOffsets, depth);
        return glyph;
    }

    return parseSimpleOutlineGlyph(data, start, size, record);
}

Pfr1Font::OutlineGlyph parseSimpleOutlineGlyph(const std::vector<std::uint8_t>& data,
                                               int start,
                                               int size,
                                               const Pfr1Font::CharacterRecord& record) {
    Pfr1Font::OutlineGlyph glyph{record.charCode, static_cast<float>(record.setWidth), {}};
    if (size <= 0 || start < 0 || start + size > static_cast<int>(data.size())) {
        return glyph;
    }

    const int flags = data[static_cast<std::size_t>(start)] & 0xFF;
    int pos = start + 1;
    const int countEncoding = flags & 3;

    int xOrusCount = 0;
    int yOrusCount = 0;
    switch (countEncoding) {
        case 1:
            if (pos < start + size) {
                const int countByte = data[static_cast<std::size_t>(pos++)] & 0xFF;
                xOrusCount = countByte & 0x0F;
                yOrusCount = (countByte >> 4) & 0x0F;
            }
            break;
        case 2:
        case 3:
            if (pos + 1 < start + size) {
                xOrusCount = data[static_cast<std::size_t>(pos++)] & 0xFF;
                yOrusCount = data[static_cast<std::size_t>(pos++)] & 0xFF;
            }
            break;
        default:
            break;
    }

    std::vector<int> controlX(static_cast<std::size_t>(xOrusCount), 0);
    std::vector<int> controlY(static_cast<std::size_t>(yOrusCount), 0);
    pos = parseControlValues(data, pos, flags, controlX, controlY);

    if ((flags & 0x08) != 0 && pos < start + size) {
        const int extraCount = data[static_cast<std::size_t>(pos++)] & 0xFF;
        for (int i = 0; i < extraCount; ++i) {
            if (pos + 1 >= start + size) {
                break;
            }
            const int itemLength = data[static_cast<std::size_t>(pos)] & 0xFF;
            pos += itemLength + 2;
        }
    }

    parseNibbleCommands(data, pos, start + size, glyph, controlX, controlY);
    return glyph;
}

} // namespace

void Pfr1Font::Contour::moveTo(float xValue, float yValue) {
    commands.push_back(Command{0, xValue, yValue});
}

void Pfr1Font::Contour::lineTo(float xValue, float yValue) {
    commands.push_back(Command{1, xValue, yValue});
}

void Pfr1Font::Contour::curveTo(float x1Value,
                                float y1Value,
                                float x2Value,
                                float y2Value,
                                float xValue,
                                float yValue) {
    commands.push_back(Command{2, xValue, yValue, x1Value, y1Value, x2Value, y2Value});
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
    const int logFontDirSize = reader.readU16();
    const int logFontDirOffset = reader.readU16();
    skipU16(reader);
    const int logFontSectionSize = reader.readU24();
    const int logFontSectionOffset = reader.readU24();
    skipU16(reader);
    skipU24(reader);
    skipU24(reader);
    skipU16(reader);
    gpsSize = reader.readU24();
    gpsOffset = reader.readU24();
    skipU8(reader);
    skipU8(reader);
    skipU8(reader);
    skipU8(reader);

    const int flagsByte = reader.readU8();
    pfrBlackPixel = (flagsByte & 0x01) != 0;

    skipU24(reader);
    skipU24(reader);
    skipU24(reader);
    skipU16(reader);
    skipU8(reader);
    skipU8(reader);
    storedMaxChars_ = reader.readU16();

    parseLogicalFontDirectory(data, logFontDirSize, logFontDirOffset, logFontSectionSize, logFontSectionOffset);
}

void Pfr1Font::parseLogicalFontDirectory(const std::vector<std::uint8_t>& data,
                                         int dirSize,
                                         int dirOffset,
                                         int sectionSize,
                                         int sectionOffset) {
    if (dirOffset == 0 || dirSize == 0) {
        return;
    }

    if (dirSize < 14) {
        if (sectionSize >= 18 && sectionOffset > 0 && sectionOffset < static_cast<int>(data.size())) {
            PfrBitReader reader(data, sectionOffset);
            for (int i = 0; i < 4; ++i) {
                fontMatrix[static_cast<std::size_t>(i)] = reader.readI24();
            }
        }
        return;
    }

    if (dirOffset >= static_cast<int>(data.size())) {
        return;
    }
    PfrBitReader reader(data, dirOffset);
    const int nLogFonts = reader.readU16();
    if (nLogFonts > 0) {
        for (int i = 0; i < 4; ++i) {
            fontMatrix[static_cast<std::size_t>(i)] = reader.readI24();
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
    const int physFontSectionSize = header.readU24();
    const int physOffset = header.readU24();

    if (physOffset >= static_cast<int>(data.size())) {
        return;
    }
    int physEnd = std::min(static_cast<int>(data.size()), physOffset + physFontSectionSize);
    if (gpsOffset > physOffset && gpsOffset <= static_cast<int>(data.size())) {
        physEnd = std::min(physEnd, gpsOffset);
    }

    PfrBitReader reader(data, physOffset);
    skipU16(reader);
    metrics.outlineResolution = reader.readU16();
    if (metrics.outlineResolution == 0) {
        metrics.outlineResolution = 2048;
    }
    metrics.metricsResolution = reader.readU16();
    if (metrics.metricsResolution == 0) {
        metrics.metricsResolution = metrics.outlineResolution;
    }

    metrics.xMin = sign16(reader.readU16());
    metrics.yMin = sign16(reader.readU16());
    metrics.xMax = sign16(reader.readU16());
    metrics.yMax = sign16(reader.readU16());
    metrics.ascender = metrics.yMax;
    metrics.descender = metrics.yMin;

    const bool extraItemsPresent = reader.readBit();
    skipBit(reader);
    skipBit(reader);
    skipBit(reader);
    skipBit(reader);
    const bool proportionalEscapement = reader.readBit();
    skipBit(reader);
    skipBit(reader);

    int standardSetWidth = 0;
    if (!proportionalEscapement) {
        standardSetWidth = sign16(reader.readU16());
    }

    if (extraItemsPresent) {
        const int nExtraItems = reader.readU8();
        for (int i = 0; i < nExtraItems; ++i) {
            if (reader.remaining() < 2) {
                break;
            }
            const int itemSize = reader.readU8();
            const int itemType = reader.readU8();
            const int itemStart = reader.position();

            if (itemType == 1) {
                metrics.hasBitmapSection = true;
                reader.skip(itemSize);
            } else if (itemType == 2) {
                std::string id;
                for (int j = 0; j < itemSize; ++j) {
                    const int ch = reader.readU8();
                    if (ch == 0) {
                        break;
                    }
                    id.push_back(static_cast<char>(ch));
                }
                metrics.fontId = id;
                fontName = id;
                const int consumed = reader.position() - itemStart;
                if (consumed < itemSize) {
                    reader.skip(itemSize - consumed);
                }
            } else {
                reader.skip(itemSize);
            }
        }
    }

    const int nAuxBytes = reader.readU24();
    if (nAuxBytes > 0 && nAuxBytes < 10000) {
        reader.skip(nAuxBytes);
    } else if (nAuxBytes >= 10000) {
        while (reader.position() < physEnd) {
            const int probePos = reader.position();
            const int nBlueValues = reader.readU8();
            const int byteCounter = nBlueValues * 2 + 6;
            const int nCharsPos = reader.position() + byteCounter;
            if (nCharsPos + 2 > physEnd) {
                reader.setPosition(probePos + 1);
                continue;
            }
            reader.setPosition(nCharsPos);
            const int nCharacters = reader.readU16();
            if (nCharacters == storedMaxChars_) {
                reader.setPosition(probePos);
                break;
            }
            reader.setPosition(probePos + 1);
        }
    }

    const int nBlueValues = reader.readU8();
    for (int i = 0; i < nBlueValues; ++i) {
        skipU16(reader);
    }
    skipU8(reader);
    skipU8(reader);

    metrics.stdVW = sign16(reader.readU16());
    metrics.stdHW = sign16(reader.readU16());
    const int nCharacters = reader.readU16();
    parseDeltaEncodedCharRecords(reader, nCharacters, standardSetWidth);
}

void Pfr1Font::parseDeltaEncodedCharRecords(PfrBitReader& reader,
                                            int nCharacters,
                                            int standardSetWidth) {
    int charCode = -1;
    int setWidth = standardSetWidth;
    int glyphSize = 0;
    int glyphOffset = 0;

    for (int i = 0; i < nCharacters; ++i) {
        if (reader.remaining() < 1) {
            break;
        }

        const int flags = reader.readU8();
        const int nextGpsOffset = glyphOffset + glyphSize;

        const int charCodeMode = flags & 0x03;
        ++charCode;
        switch (charCodeMode) {
            case 1:
                charCode += reader.readU8();
                break;
            case 2:
                charCode += reader.readU16();
                break;
            default:
                break;
        }

        const int setWidthMode = (flags >> 2) & 0x03;
        switch (setWidthMode) {
            case 1:
                setWidth += reader.readU8();
                break;
            case 2:
                setWidth -= reader.readU8();
                break;
            case 3:
                setWidth = sign16(reader.readU16());
                break;
            default:
                break;
        }

        const int gpsSizeMode = (flags >> 4) & 0x03;
        switch (gpsSizeMode) {
            case 0:
                glyphSize = reader.readU8();
                break;
            case 1:
                glyphSize = reader.readU8() + 256;
                break;
            case 2:
                glyphSize = reader.readU8() + 512;
                break;
            case 3:
                glyphSize = reader.readU16();
                break;
            default:
                break;
        }

        const int gpsOffsetMode = (flags >> 6) & 0x03;
        switch (gpsOffsetMode) {
            case 0:
                glyphOffset = nextGpsOffset;
                break;
            case 1:
                glyphOffset = nextGpsOffset + reader.readU8();
                break;
            case 2:
                glyphOffset = reader.readU16();
                break;
            case 3:
                glyphOffset = reader.readU24();
                break;
            default:
                break;
        }

        charRecords.push_back(CharacterRecord{charCode, setWidth, glyphSize, glyphOffset});
    }
}

void Pfr1Font::parseGlyphStubsAndBitmaps(const std::vector<std::uint8_t>& data) {
    if (gpsOffset + gpsSize > static_cast<int>(data.size())) {
        return;
    }

    std::vector<int> knownOffsets;
    knownOffsets.reserve(charRecords.size());
    for (const auto& record : charRecords) {
        knownOffsets.push_back(record.gpsOffset);
    }
    std::sort(knownOffsets.begin(), knownOffsets.end());

    for (const auto& record : charRecords) {
        if (record.gpsSize <= 1) {
            glyphs[record.charCode] = OutlineGlyph{record.charCode, static_cast<float>(record.setWidth), {}};
            continue;
        }

        const int start = gpsOffset + record.gpsOffset;
        const int size = record.gpsSize;
        if (start < 0 || size <= 0 || start + size > static_cast<int>(data.size())) {
            continue;
        }

        if (metrics.hasBitmapSection) {
            const int zerosField = (data[static_cast<std::size_t>(start)] >> 4) & 0x07;
            if (zerosField != 0) {
                try {
                    auto glyph = parseBitmapGlyph(data, start, size, record.charCode);
                    if (record.setWidth > 0) {
                        glyph.setWidth = record.setWidth;
                    }
                    bitmapGlyphs[record.charCode] = std::move(glyph);
                } catch (const std::exception&) {
                }
            }
        }

        try {
            glyphs[record.charCode] = parseOutlineGlyph(*this, data, start, size, record, knownOffsets, 0);
        } catch (const std::exception&) {
        }
    }

    for (int lower = 'a'; lower <= 'z'; ++lower) {
        const int upper = lower - 32;
        const auto lowerIt = glyphs.find(lower);
        if (lowerIt != glyphs.end() && !lowerIt->second.contours.empty()) {
            continue;
        }
        const auto upperIt = glyphs.find(upper);
        if (upperIt != glyphs.end() && !upperIt->second.contours.empty()) {
            auto copy = upperIt->second;
            copy.charCode = lower;
            glyphs[lower] = std::move(copy);
        }
    }
}

Pfr1Font::BitmapGlyph Pfr1Font::parseBitmapGlyph(const std::vector<std::uint8_t>& data,
                                                 int start,
                                                 int size,
                                                 int charCode) const {
    if (size < 2) {
        throw std::runtime_error("bitmap glyph too small");
    }

    const int end = start + size;
    int pos = start;
    const int formatByte = data[static_cast<std::size_t>(pos++)] & 0xFF;

    const int imageFormat = (formatByte >> 6) & 0x03;
    const int escapementFormat = (formatByte >> 4) & 0x03;
    const int sizeFormat = (formatByte >> 2) & 0x03;
    const int positionFormat = formatByte & 0x03;

    const int posBytes = positionFormat == 0 ? 0 : positionFormat == 1 ? 2 : positionFormat == 2 ? 4 : 8;
    const int sizeBytes = sizeFormat == 0 ? 2 : sizeFormat == 1 ? 4 : sizeFormat == 2 ? 6 : 8;
    const int escBytes = escapementFormat == 0 ? 0 : escapementFormat == 1 ? 1 : escapementFormat == 2 ? 2 : 4;
    if (pos + posBytes + sizeBytes + escBytes > end ||
        pos + posBytes + sizeBytes + escBytes > static_cast<int>(data.size())) {
        throw std::runtime_error("bitmap glyph bounds exceeded");
    }

    int xPos = 0;
    int yPos = 0;
    switch (positionFormat) {
        case 1:
            xPos = readSignedN(data, pos, 1);
            ++pos;
            yPos = readSignedN(data, pos, 1);
            ++pos;
            break;
        case 2:
            xPos = readSignedN(data, pos, 2);
            pos += 2;
            yPos = readSignedN(data, pos, 2);
            pos += 2;
            break;
        case 3:
            xPos = readSignedN(data, pos, 4);
            pos += 4;
            yPos = readSignedN(data, pos, 4);
            pos += 4;
            break;
        default:
            break;
    }

    int xSize = 0;
    int ySize = 0;
    switch (sizeFormat) {
        case 0:
            xSize = readUnsignedN(data, pos, 1);
            ++pos;
            ySize = readUnsignedN(data, pos, 1);
            ++pos;
            break;
        case 1:
            xSize = readUnsignedN(data, pos, 2);
            pos += 2;
            ySize = readUnsignedN(data, pos, 2);
            pos += 2;
            break;
        case 2:
            xSize = readUnsignedN(data, pos, 3);
            pos += 3;
            ySize = readUnsignedN(data, pos, 3);
            pos += 3;
            break;
        default:
            xSize = readUnsignedN(data, pos, 4);
            pos += 4;
            ySize = readUnsignedN(data, pos, 4);
            pos += 4;
            break;
    }

    int setWidth = xSize;
    switch (escapementFormat) {
        case 1:
            setWidth = readUnsignedN(data, pos, 1);
            ++pos;
            break;
        case 2:
            setWidth = readUnsignedN(data, pos, 2);
            pos += 2;
            break;
        case 3:
            setWidth = readUnsignedN(data, pos, 4);
            pos += 4;
            break;
        default:
            break;
    }

    if (xSize <= 0 || ySize <= 0 || xSize > 4096 || ySize > 4096) {
        throw std::runtime_error("bitmap glyph size invalid");
    }

    const int totalBits = xSize * ySize;
    if (totalBits <= 0 || totalBits > 1'000'000) {
        throw std::runtime_error("bitmap glyph bit count invalid");
    }

    const int remaining = end - pos;
    if (remaining <= 0 || pos > static_cast<int>(data.size())) {
        throw std::runtime_error("bitmap glyph data missing");
    }

    std::vector<std::uint8_t> imageData;
    if (imageFormat == 0) {
        const int expected = (totalBits + 7) / 8;
        if (expected > remaining || expected <= 0) {
            throw std::runtime_error("bitmap glyph packed data invalid");
        }
        imageData.assign(data.begin() + pos, data.begin() + pos + expected);
    } else if (imageFormat == 1) {
        imageData = decodeRleBitmap(data, pos, remaining, xSize, ySize);
    } else {
        imageData.assign(data.begin() + pos, data.begin() + end);
    }

    return BitmapGlyph{
        charCode,
        imageFormat,
        sign16(xPos & 0xFFFF),
        sign16(yPos & 0xFFFF),
        xSize,
        ySize,
        setWidth,
        std::move(imageData)
    };
}

} // namespace libreshockwave::font
