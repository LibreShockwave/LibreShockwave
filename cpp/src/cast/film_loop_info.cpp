#include "libreshockwave/cast/film_loop_info.hpp"

#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::cast {

int FilmLoopInfo::width() const { return rect_right - rect_left; }
int FilmLoopInfo::height() const { return rect_bottom - rect_top; }
int FilmLoopInfo::regX() const { return -rect_left; }
int FilmLoopInfo::regY() const { return -rect_top; }

FilmLoopInfo FilmLoopInfo::parse(const std::vector<std::uint8_t>& data) {
    if (data.size() < 11) {
        return FilmLoopInfo{0, 0, 0, 0, false, true, false, true};
    }

    io::BinaryReader reader(data, io::ByteOrder::BigEndian);
    const int rect_top = reader.readI16();
    const int rect_left = reader.readI16();
    const int rect_bottom = reader.readI16();
    const int rect_right = reader.readI16();
    reader.skip(3);
    const int flags = reader.readU8();

    return FilmLoopInfo{
        rect_top,
        rect_left,
        rect_bottom,
        rect_right,
        (flags & 0b1) != 0,
        (flags & 0b10) == 0,
        (flags & 0b1000) != 0,
        (flags & 0b100000) == 0
    };
}

} // namespace libreshockwave::cast
