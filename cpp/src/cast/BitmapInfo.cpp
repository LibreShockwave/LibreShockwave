#include "libreshockwave/cast/BitmapInfo.hpp"

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::cast {

int BitmapInfo::regXLocal() const { return regX - initialLeft; }
int BitmapInfo::regYLocal() const { return regY - initialTop; }

int BitmapInfo::bytesPerPixel() const {
    switch (bitDepth) {
        case 1:
        case 2:
        case 4:
            return 0;
        case 8:
            return 1;
        case 16:
            return 2;
        case 24:
            return 3;
        case 32:
            return 4;
        default:
            return 1;
    }
}

bool BitmapInfo::isPaletted() const {
    return bitDepth <= 8;
}

BitmapInfo BitmapInfo::parse(const std::vector<std::uint8_t>& data, int directorVersion) {
    if (data.size() < 10) {
        return BitmapInfo{0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, false};
    }

    io::BinaryReader reader(data, io::ByteOrder::BigEndian);
    const int rawPitch = reader.readU16();
    const int top = reader.bytesLeft() >= 2 ? reader.readI16() : 0;
    const int left = reader.bytesLeft() >= 2 ? reader.readI16() : 0;
    const int bottom = reader.bytesLeft() >= 2 ? reader.readI16() : 0;
    const int right = reader.bytesLeft() >= 2 ? reader.readI16() : 0;
    const int height = bottom - top;
    const int width = right - left;

    int regX = 0;
    int regY = 0;
    int bitDepth = 1;
    int paletteId = 0;
    int paletteCastLib = 0;
    int pitch = 0;
    int alphaThreshold = 0;
    bool useAlpha = false;

    if (directorVersion < 1200) {
        if (reader.bytesLeft() >= 8) reader.skip(8);
        if (reader.bytesLeft() >= 2) regY = reader.readI16();
        if (reader.bytesLeft() >= 2) regX = reader.readI16();
        if (reader.bytesLeft() >= 1) reader.skip(1);
        if (reader.bytesLeft() >= 1) {
            bitDepth = reader.readU8();
            if (directorVersion >= 1100 && reader.bytesLeft() >= 2) {
                paletteCastLib = reader.readI16();
            }
            if (reader.bytesLeft() >= 2) {
                paletteId = reader.readI16() - 1;
            }
        }
        pitch = rawPitch & 0x0FFF;
        if (bitDepth == 0) bitDepth = 1;
    } else {
        if (reader.bytesLeft() >= 1) {
            alphaThreshold = reader.readU8();
        }
        if (reader.bytesLeft() >= 7) reader.skip(7);
        if (reader.bytesLeft() >= 2) regY = reader.readI16();
        if (reader.bytesLeft() >= 2) regX = reader.readI16();
        if (reader.bytesLeft() >= 1) {
            const int updateFlags = reader.readU8();
            useAlpha = (updateFlags & 0x10) != 0;
        }
        if ((rawPitch & 0x8000) != 0) {
            pitch = rawPitch & 0x3FFF;
            if (reader.bytesLeft() >= 1) bitDepth = reader.readU8();
            if (reader.bytesLeft() >= 2) paletteCastLib = reader.readI16();
            if (reader.bytesLeft() >= 2) paletteId = reader.readI16() - 1;
        } else {
            bitDepth = 1;
            pitch = rawPitch & 0x3FFF;
        }
    }

    return BitmapInfo{width, height, regX, regY, left, top, bitDepth, paletteId, paletteCastLib, pitch, alphaThreshold, useAlpha};
}

} // namespace libreshockwave::cast
