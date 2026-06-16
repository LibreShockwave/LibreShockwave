#include "libreshockwave/font/PfrBitReader.hpp"

#include <algorithm>
#include <utility>

namespace libreshockwave::font {

PfrBitReader::PfrBitReader(std::vector<std::uint8_t> data)
    : PfrBitReader(std::move(data), 0) {}

PfrBitReader::PfrBitReader(std::vector<std::uint8_t> data, int offset)
    : data_(std::move(data)), position_(offset) {}

int PfrBitReader::position() const {
    return position_;
}

void PfrBitReader::setPosition(int position) {
    position_ = position;
    bitBuffer_ = 0;
    bitsLeft_ = 0;
}

int PfrBitReader::remaining() const {
    return position_ >= static_cast<int>(data_.size()) ? 0 : static_cast<int>(data_.size()) - position_;
}

void PfrBitReader::alignToByte() {
    bitBuffer_ = 0;
    bitsLeft_ = 0;
}

int PfrBitReader::readU8() {
    alignToByte();
    if (position_ < 0 || position_ >= static_cast<int>(data_.size())) {
        return 0;
    }
    return data_[static_cast<std::size_t>(position_++)] & 0xFF;
}

int PfrBitReader::readI8() {
    const int value = readU8();
    return (value & 0x80) != 0 ? value | ~0xFF : value;
}

int PfrBitReader::readU16() {
    const int hi = readU8();
    const int lo = readU8();
    return (hi << 8) | lo;
}

int PfrBitReader::readI16() {
    const int value = readU16();
    return (value & 0x8000) != 0 ? value | ~0xFFFF : value;
}

int PfrBitReader::readU24() {
    const int b0 = readU8();
    const int b1 = readU8();
    const int b2 = readU8();
    return (b0 << 16) | (b1 << 8) | b2;
}

int PfrBitReader::readI24() {
    const int value = readU24();
    return (value & 0x800000) != 0 ? value | ~0xFFFFFF : value;
}

void PfrBitReader::skip(int count) {
    alignToByte();
    position_ += count;
    if (position_ > static_cast<int>(data_.size())) {
        position_ = static_cast<int>(data_.size());
    }
}

int PfrBitReader::readBits(int count) {
    if (count == 0) {
        return 0;
    }

    int result = 0;
    int remaining = count;
    while (remaining > 0) {
        if (bitsLeft_ == 0) {
            if (position_ < 0 || position_ >= static_cast<int>(data_.size())) {
                return result;
            }
            bitBuffer_ = data_[static_cast<std::size_t>(position_++)] & 0xFF;
            bitsLeft_ = 8;
        }

        const int take = std::min(remaining, bitsLeft_);
        const int shift = bitsLeft_ - take;
        const int mask = ((1 << take) - 1) << shift;
        const int bits = (bitBuffer_ & mask) >> shift;
        result = (result << take) | bits;
        bitsLeft_ -= take;
        remaining -= take;
    }

    return result;
}

bool PfrBitReader::readBit() {
    return readBits(1) != 0;
}

} // namespace libreshockwave::font
