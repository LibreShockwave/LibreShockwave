#include "libreshockwave/cast/bitmap_info.hpp"

#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::cast {

int BitmapInfo::regXLocal() const { return reg_x - initial_left; }
int BitmapInfo::regYLocal() const { return reg_y - initial_top; }

int BitmapInfo::bytesPerPixel() const {
    switch (bit_depth) {
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
    return bit_depth <= 8;
}

BitmapInfo BitmapInfo::parse(const std::vector<std::uint8_t>& data, int director_version) {
    if (data.size() < 10) {
        return BitmapInfo{0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, false};
    }

    io::BinaryReader reader(data, io::ByteOrder::BigEndian);
    const int raw_pitch = reader.readU16();
    const int top = reader.bytesLeft() >= 2 ? reader.readI16() : 0;
    const int left = reader.bytesLeft() >= 2 ? reader.readI16() : 0;
    const int bottom = reader.bytesLeft() >= 2 ? reader.readI16() : 0;
    const int right = reader.bytesLeft() >= 2 ? reader.readI16() : 0;
    const int height = bottom - top;
    const int width = right - left;

    int reg_x = 0;
    int reg_y = 0;
    int bit_depth = 1;
    int palette_id = 0;
    int palette_cast_lib = 0;
    int pitch = 0;
    int alpha_threshold = 0;
    bool use_alpha = false;

    if (director_version < 1200) {
        if (reader.bytesLeft() >= 8) reader.skip(8);
        if (reader.bytesLeft() >= 2) reg_y = reader.readI16();
        if (reader.bytesLeft() >= 2) reg_x = reader.readI16();
        if (reader.bytesLeft() >= 1) reader.skip(1);
        if (reader.bytesLeft() >= 1) {
            bit_depth = reader.readU8();
            if (director_version >= 1100 && reader.bytesLeft() >= 2) {
                palette_cast_lib = reader.readI16();
            }
            if (reader.bytesLeft() >= 2) {
                palette_id = reader.readI16() - 1;
            }
        }
        pitch = raw_pitch & 0x0FFF;
        if (bit_depth == 0) bit_depth = 1;
    } else {
        if (reader.bytesLeft() >= 1) {
            alpha_threshold = reader.readU8();
        }
        if (reader.bytesLeft() >= 7) reader.skip(7);
        if (reader.bytesLeft() >= 2) reg_y = reader.readI16();
        if (reader.bytesLeft() >= 2) reg_x = reader.readI16();
        if (reader.bytesLeft() >= 1) {
            const int update_flags = reader.readU8();
            use_alpha = (update_flags & 0x10) != 0;
        }
        if ((raw_pitch & 0x8000) != 0) {
            pitch = raw_pitch & 0x3FFF;
            if (reader.bytesLeft() >= 1) bit_depth = reader.readU8();
            if (reader.bytesLeft() >= 2) palette_cast_lib = reader.readI16();
            if (reader.bytesLeft() >= 2) palette_id = reader.readI16() - 1;
        } else {
            bit_depth = 1;
            pitch = raw_pitch & 0x3FFF;
        }
    }

    return BitmapInfo{width, height, reg_x, reg_y, left, top, bit_depth, palette_id, palette_cast_lib, pitch, alpha_threshold, use_alpha};
}

} // namespace libreshockwave::cast
