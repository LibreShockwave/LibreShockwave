#pragma once

#include <cstdint>
#include <vector>

namespace libreshockwave::font {

class PfrBitReader {
public:
    explicit PfrBitReader(std::vector<std::uint8_t> data);
    PfrBitReader(std::vector<std::uint8_t> data, int offset);

    [[nodiscard]] int position() const;
    void setPosition(int position);
    [[nodiscard]] int remaining() const;

    [[nodiscard]] int readU8();
    [[nodiscard]] int readI8();
    [[nodiscard]] int readU16();
    [[nodiscard]] int readI16();
    [[nodiscard]] int readU24();
    [[nodiscard]] int readI24();
    void skip(int count);

    [[nodiscard]] int readBits(int count);
    [[nodiscard]] bool readBit();

private:
    void alignToByte();

    std::vector<std::uint8_t> data_;
    int position_ = 0;
    int bitBuffer_ = 0;
    int bitsLeft_ = 0;
};

} // namespace libreshockwave::font
