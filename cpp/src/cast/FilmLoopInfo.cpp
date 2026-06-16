#include "libreshockwave/cast/FilmLoopInfo.hpp"

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::cast {

int FilmLoopInfo::width() const { return rectRight - rectLeft; }
int FilmLoopInfo::height() const { return rectBottom - rectTop; }
int FilmLoopInfo::regX() const { return -rectLeft; }
int FilmLoopInfo::regY() const { return -rectTop; }

FilmLoopInfo FilmLoopInfo::parse(const std::vector<std::uint8_t>& data) {
    if (data.size() < 11) {
        return FilmLoopInfo{0, 0, 0, 0, false, true, false, true};
    }

    io::BinaryReader reader(data, io::ByteOrder::BigEndian);
    const int rectTop = reader.readI16();
    const int rectLeft = reader.readI16();
    const int rectBottom = reader.readI16();
    const int rectRight = reader.readI16();
    reader.skip(3);
    const int flags = reader.readU8();

    return FilmLoopInfo{
        rectTop,
        rectLeft,
        rectBottom,
        rectRight,
        (flags & 0b1) != 0,
        (flags & 0b10) == 0,
        (flags & 0b1000) != 0,
        (flags & 0b100000) == 0
    };
}

} // namespace libreshockwave::cast
