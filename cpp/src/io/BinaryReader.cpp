#include "libreshockwave/io/BinaryReader.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>

#ifdef LIBRESHOCKWAVE_HAVE_ZLIB
#include <zlib.h>
#endif

namespace libreshockwave::io {
namespace {

void appendUtf8(std::string& output, char32_t codepoint) {
    if (codepoint <= 0x7F) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

std::string decodeMacRoman(const std::vector<std::uint8_t>& bytes) {
    static constexpr std::array<char32_t, 128> kMacRomanHigh{{
        U'\u00C4', U'\u00C5', U'\u00C7', U'\u00C9', U'\u00D1', U'\u00D6', U'\u00DC', U'\u00E1',
        U'\u00E0', U'\u00E2', U'\u00E4', U'\u00E3', U'\u00E5', U'\u00E7', U'\u00E9', U'\u00E8',
        U'\u00EA', U'\u00EB', U'\u00ED', U'\u00EC', U'\u00EE', U'\u00EF', U'\u00F1', U'\u00F3',
        U'\u00F2', U'\u00F4', U'\u00F6', U'\u00F5', U'\u00FA', U'\u00F9', U'\u00FB', U'\u00FC',
        U'\u2020', U'\u00B0', U'\u00A2', U'\u00A3', U'\u00A7', U'\u2022', U'\u00B6', U'\u00DF',
        U'\u00AE', U'\u00A9', U'\u2122', U'\u00B4', U'\u00A8', U'\u2260', U'\u00C6', U'\u00D8',
        U'\u221E', U'\u00B1', U'\u2264', U'\u2265', U'\u00A5', U'\u00B5', U'\u2202', U'\u2211',
        U'\u220F', U'\u03C0', U'\u222B', U'\u00AA', U'\u00BA', U'\u03A9', U'\u00E6', U'\u00F8',
        U'\u00BF', U'\u00A1', U'\u00AC', U'\u221A', U'\u0192', U'\u2248', U'\u2206', U'\u00AB',
        U'\u00BB', U'\u2026', U'\u00A0', U'\u00C0', U'\u00C3', U'\u00D5', U'\u0152', U'\u0153',
        U'\u2013', U'\u2014', U'\u201C', U'\u201D', U'\u2018', U'\u2019', U'\u00F7', U'\u25CA',
        U'\u00FF', U'\u0178', U'\u2044', U'\u20AC', U'\u2039', U'\u203A', U'\uFB01', U'\uFB02',
        U'\u2021', U'\u00B7', U'\u201A', U'\u201E', U'\u2030', U'\u00C2', U'\u00CA', U'\u00C1',
        U'\u00CB', U'\u00C8', U'\u00CD', U'\u00CE', U'\u00CF', U'\u00CC', U'\u00D3', U'\u00D4',
        U'\uF8FF', U'\u00D2', U'\u00DA', U'\u00DB', U'\u00D9', U'\u0131', U'\u02C6', U'\u02DC',
        U'\u00AF', U'\u02D8', U'\u02D9', U'\u02DA', U'\u00B8', U'\u02DD', U'\u02DB', U'\u02C7',
    }};

    std::string output;
    output.reserve(bytes.size());
    for (const std::uint8_t byte : bytes) {
        if (byte < 0x80) {
            output.push_back(static_cast<char>(byte));
        } else {
            appendUtf8(output, kMacRomanHigh[byte - 0x80]);
        }
    }
    return output;
}

template <typename T>
T bitCastFromUnsigned(std::uint64_t bits) {
    T value{};
    static_assert(sizeof(T) <= sizeof(bits));
    std::memcpy(&value, &bits, sizeof(T));
    return value;
}

} // namespace

BinaryReader::BinaryReader(std::vector<std::uint8_t> data, ByteOrder order)
    : data_(std::move(data)), order_(order) {}

ByteOrder BinaryReader::order() const {
    return order_;
}

void BinaryReader::setOrder(ByteOrder order) {
    order_ = order;
}

bool BinaryReader::isBigEndian() const {
    return order_ == ByteOrder::BigEndian;
}

bool BinaryReader::isLittleEndian() const {
    return order_ == ByteOrder::LittleEndian;
}

std::size_t BinaryReader::position() const {
    return position_;
}

void BinaryReader::setPosition(std::size_t position) {
    if (position > data_.size()) {
        throw std::out_of_range("Cannot set reader position beyond data length");
    }
    position_ = position;
}

void BinaryReader::skip(std::size_t bytes) {
    setPosition(position_ + bytes);
}

void BinaryReader::seek(std::size_t offset) {
    setPosition(offset);
}

const std::vector<std::uint8_t>& BinaryReader::data() const {
    return data_;
}

std::size_t BinaryReader::length() const {
    return data_.size();
}

std::size_t BinaryReader::bytesLeft() const {
    return position_ >= data_.size() ? 0 : data_.size() - position_;
}

bool BinaryReader::eof() const {
    return position_ >= data_.size();
}

std::int8_t BinaryReader::readI8() {
    return static_cast<std::int8_t>(readU8());
}

std::uint8_t BinaryReader::readU8() {
    ensureAvailable(1, "read");
    return data_[position_++];
}

std::int16_t BinaryReader::readI16() {
    const auto bytes = readBytes(2);
    const auto raw = isBigEndian()
        ? static_cast<std::uint16_t>((bytes[0] << 8) | bytes[1])
        : static_cast<std::uint16_t>((bytes[1] << 8) | bytes[0]);
    return static_cast<std::int16_t>(raw);
}

std::uint16_t BinaryReader::readU16() {
    return static_cast<std::uint16_t>(readI16());
}

std::int32_t BinaryReader::readI32() {
    const auto bytes = readBytes(4);
    const auto raw = isBigEndian()
        ? (static_cast<std::uint32_t>(bytes[0]) << 24) |
          (static_cast<std::uint32_t>(bytes[1]) << 16) |
          (static_cast<std::uint32_t>(bytes[2]) << 8) |
          static_cast<std::uint32_t>(bytes[3])
        : (static_cast<std::uint32_t>(bytes[3]) << 24) |
          (static_cast<std::uint32_t>(bytes[2]) << 16) |
          (static_cast<std::uint32_t>(bytes[1]) << 8) |
          static_cast<std::uint32_t>(bytes[0]);
    return static_cast<std::int32_t>(raw);
}

std::uint32_t BinaryReader::readU32() {
    return static_cast<std::uint32_t>(readI32());
}

std::int64_t BinaryReader::readI64() {
    const auto bytes = readBytes(8);
    std::uint64_t raw = 0;
    if (isBigEndian()) {
        for (const auto byte : bytes) {
            raw = (raw << 8) | byte;
        }
    } else {
        for (auto it = bytes.rbegin(); it != bytes.rend(); ++it) {
            raw = (raw << 8) | *it;
        }
    }
    return static_cast<std::int64_t>(raw);
}

float BinaryReader::readF32() {
    const auto bits = readU32();
    return std::bit_cast<float>(bits);
}

double BinaryReader::readF64() {
    const auto bits = static_cast<std::uint64_t>(readI64());
    return std::bit_cast<double>(bits);
}

std::vector<std::uint8_t> BinaryReader::readBytes(std::size_t length) {
    ensureAvailable(length, "read");
    std::vector<std::uint8_t> result(data_.begin() + static_cast<std::ptrdiff_t>(position_),
                                     data_.begin() + static_cast<std::ptrdiff_t>(position_ + length));
    position_ += length;
    return result;
}

std::vector<std::uint8_t> BinaryReader::peekBytes(std::size_t length) const {
    ensureAvailable(length, "peek");
    return std::vector<std::uint8_t>(data_.begin() + static_cast<std::ptrdiff_t>(position_),
                                     data_.begin() + static_cast<std::ptrdiff_t>(position_ + length));
}

std::uint32_t BinaryReader::readFourCC() {
    return fourCC(readFourCCString());
}

std::string BinaryReader::readFourCCString() {
    return readString(4);
}

std::uint32_t BinaryReader::fourCC(std::string_view value) {
    if (value.size() != 4) {
        throw std::invalid_argument("FourCC must be exactly 4 characters");
    }
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(value[0])) << 24) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(value[1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(value[2])) << 8) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(value[3]));
}

std::string BinaryReader::fourCCToString(std::uint32_t fourcc) {
    std::string result(4, '\0');
    result[0] = static_cast<char>((fourcc >> 24) & 0xFF);
    result[1] = static_cast<char>((fourcc >> 16) & 0xFF);
    result[2] = static_cast<char>((fourcc >> 8) & 0xFF);
    result[3] = static_cast<char>(fourcc & 0xFF);
    return result;
}

std::string BinaryReader::readString(std::size_t length) {
    const auto bytes = readBytes(length);
    return std::string(bytes.begin(), bytes.end());
}

std::string BinaryReader::readStringMacRoman(std::size_t length) {
    return decodeMacRoman(readBytes(length));
}

std::string BinaryReader::readPascalString() {
    const auto stringLength = readU8();
    return readStringMacRoman(stringLength);
}

std::string BinaryReader::readPString16() {
    const auto stringLength = readU16();
    if (stringLength == 0) {
        return "";
    }
    return readString(stringLength);
}

std::string BinaryReader::readNullTerminatedString() {
    const auto start = position_;
    while (position_ < data_.size() && data_[position_] != 0) {
        ++position_;
    }
    std::string result(data_.begin() + static_cast<std::ptrdiff_t>(start),
                       data_.begin() + static_cast<std::ptrdiff_t>(position_));
    if (position_ < data_.size()) {
        ++position_;
    }
    return result;
}

int BinaryReader::readVarInt() {
    int value = 0;
    int byte = 0;
    do {
        if (position_ >= data_.size()) {
            return value;
        }
        byte = data_[position_++] & 0xFF;
        value = (value << 7) | (byte & 0x7F);
    } while ((byte & 0x80) != 0);
    return value;
}

double BinaryReader::readAppleFloat80() {
    const auto bytes = readBytes(10);
    std::uint16_t exponent = static_cast<std::uint16_t>((bytes[0] << 8) | bytes[1]);
    const auto sign = (exponent & 0x8000U) != 0 ? 1ULL : 0ULL;
    exponent &= 0x7FFFU;

    std::uint64_t fraction = 0;
    for (std::size_t index = 2; index < 10; ++index) {
        fraction = (fraction << 8) | bytes[index];
    }
    fraction &= 0x7FFFFFFFFFFFFFFFULL;

    std::uint64_t f64exp = 0;
    if (exponent == 0) {
        f64exp = 0;
    } else if (exponent == 0x7FFFU) {
        f64exp = 0x7FFU;
    } else {
        const auto normalizedExponent = static_cast<long>(exponent) - 0x3FFF;
        if (normalizedExponent < -0x3FE || normalizedExponent >= 0x3FF) {
            return 0.0;
        }
        f64exp = static_cast<std::uint64_t>(normalizedExponent + 0x3FF);
    }

    const auto bits = (sign << 63) | (f64exp << 52) | (fraction >> 11);
    return bitCastFromUnsigned<double>(bits);
}

std::vector<std::uint8_t> BinaryReader::readZlibBytes(std::size_t compressedLength) {
    return decompressZlib(readBytes(compressedLength));
}

std::vector<std::uint8_t> BinaryReader::decompressZlib(const std::vector<std::uint8_t>& compressed) {
    if (compressed.empty()) {
        return {};
    }

#ifdef LIBRESHOCKWAVE_HAVE_ZLIB
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());

    if (inflateInit(&stream) != Z_OK) {
        throw std::runtime_error("Failed to initialize zlib inflater");
    }

    std::vector<std::uint8_t> output;
    output.reserve(std::max<std::size_t>(compressed.size() * 4, 4096));
    std::array<std::uint8_t, 4096> buffer{};
    const auto maxOutput = std::max<std::size_t>(compressed.size() * 20, 16 * 1024 * 1024);

    int status = Z_OK;
    do {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());

        status = inflate(&stream, Z_NO_FLUSH);
        if (status == Z_STREAM_ERROR || status == Z_DATA_ERROR || status == Z_MEM_ERROR) {
            break;
        }

        const auto produced = buffer.size() - stream.avail_out;
        output.insert(output.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(produced));
        if (produced == 0 && status != Z_STREAM_END) {
            break;
        }
        if (output.size() > maxOutput) {
            break;
        }
    } while (status != Z_STREAM_END);

    inflateEnd(&stream);
    return output;
#else
    (void)compressed;
    throw std::runtime_error("LibreShockwave C++ was built without zlib support");
#endif
}

std::int32_t BinaryReader::readInt() {
    return readI32();
}

std::int16_t BinaryReader::readShort() {
    return readI16();
}

std::uint8_t BinaryReader::readUnsignedByte() {
    return readU8();
}

std::uint16_t BinaryReader::readUnsignedShort() {
    return readU16();
}

BinaryReader BinaryReader::sliceReader(std::size_t length) {
    return BinaryReader(readBytes(length), order_);
}

BinaryReader BinaryReader::sliceReaderAt(std::size_t offset, std::size_t length) const {
    if (offset > data_.size() || length > data_.size() - offset) {
        throw std::out_of_range("Cannot create reader slice outside data bounds");
    }
    return BinaryReader(
        std::vector<std::uint8_t>(data_.begin() + static_cast<std::ptrdiff_t>(offset),
                                  data_.begin() + static_cast<std::ptrdiff_t>(offset + length)),
        order_);
}

void BinaryReader::ensureAvailable(std::size_t length, const char* operation) const {
    if (length > bytesLeft()) {
        throw std::out_of_range(
            std::string("Cannot ") + operation + " " + std::to_string(length) +
            " bytes at position " + std::to_string(position_) +
            " (data length: " + std::to_string(data_.size()) +
            ", remaining: " + std::to_string(bytesLeft()) + ")");
    }
}

} // namespace libreshockwave::io
