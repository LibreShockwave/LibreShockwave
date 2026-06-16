#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace libreshockwave::io {

enum class ByteOrder {
    BigEndian,
    LittleEndian
};

class BinaryReader {
public:
    explicit BinaryReader(std::vector<std::uint8_t> data, ByteOrder order = ByteOrder::BigEndian);

    [[nodiscard]] ByteOrder order() const;
    void setOrder(ByteOrder order);
    [[nodiscard]] bool isBigEndian() const;
    [[nodiscard]] bool isLittleEndian() const;

    [[nodiscard]] std::size_t position() const;
    void setPosition(std::size_t position);
    void skip(std::size_t bytes);
    void seek(std::size_t offset);

    [[nodiscard]] const std::vector<std::uint8_t>& data() const;
    [[nodiscard]] std::size_t length() const;
    [[nodiscard]] std::size_t bytesLeft() const;
    [[nodiscard]] bool eof() const;

    [[nodiscard]] std::int8_t readI8();
    [[nodiscard]] std::uint8_t readU8();
    [[nodiscard]] std::int16_t readI16();
    [[nodiscard]] std::uint16_t readU16();
    [[nodiscard]] std::int32_t readI32();
    [[nodiscard]] std::uint32_t readU32();
    [[nodiscard]] std::int64_t readI64();
    [[nodiscard]] float readF32();
    [[nodiscard]] double readF64();

    [[nodiscard]] std::vector<std::uint8_t> readBytes(std::size_t length);
    [[nodiscard]] std::vector<std::uint8_t> peekBytes(std::size_t length) const;

    [[nodiscard]] std::uint32_t readFourCC();
    [[nodiscard]] std::string readFourCCString();

    [[nodiscard]] static std::uint32_t fourCC(std::string_view value);
    [[nodiscard]] static std::string fourCCToString(std::uint32_t fourcc);

    [[nodiscard]] std::string readString(std::size_t length);
    [[nodiscard]] std::string readStringMacRoman(std::size_t length);
    [[nodiscard]] std::string readPascalString();
    [[nodiscard]] std::string readPString16();
    [[nodiscard]] std::string readNullTerminatedString();

    [[nodiscard]] int readVarInt();
    [[nodiscard]] double readAppleFloat80();

    [[nodiscard]] std::vector<std::uint8_t> readZlibBytes(std::size_t compressedLength);
    [[nodiscard]] static std::vector<std::uint8_t> decompressZlib(const std::vector<std::uint8_t>& compressed);

    [[nodiscard]] std::int32_t readInt();
    [[nodiscard]] std::int16_t readShort();
    [[nodiscard]] std::uint8_t readUnsignedByte();
    [[nodiscard]] std::uint16_t readUnsignedShort();

    [[nodiscard]] BinaryReader sliceReader(std::size_t length);
    [[nodiscard]] BinaryReader sliceReaderAt(std::size_t offset, std::size_t length) const;

private:
    void ensureAvailable(std::size_t length, const char* operation) const;

    std::vector<std::uint8_t> data_;
    std::size_t position_ = 0;
    ByteOrder order_ = ByteOrder::BigEndian;
};

} // namespace libreshockwave::io
