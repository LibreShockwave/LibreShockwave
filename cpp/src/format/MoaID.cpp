#include "libreshockwave/format/MoaID.hpp"

#include <iomanip>
#include <sstream>

#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::format {

const MoaID MoaID::ZLIB_COMPRESSION{0xAC99E904U, 0x0070U, 0x0B36U, 0x00080000U, 0x347A3707U};
const MoaID MoaID::ZLIB_COMPRESSION_ALT{0xAC99E904U, 0x0070U, 0x0B36U, 0x00000800U, 0x07377A34U};
const MoaID MoaID::SND_COMPRESSION{0x7204A889U, 0xAFD0U, 0x11CFU, 0xA00022A2U, 0x4C445323U};
const MoaID MoaID::NULL_COMPRESSION{0xAC99982EU, 0x005DU, 0x0D50U, 0x00080000U, 0x347A3707U};
const MoaID MoaID::FONTMAP_COMPRESSION{0x8A4679A1U, 0x3720U, 0x11D0U, 0xA0002392U, 0xB16808C9U};

MoaID MoaID::read(io::BinaryReader& reader) {
    return MoaID{
        static_cast<std::uint32_t>(reader.readInt()),
        static_cast<std::uint16_t>(reader.readShort()),
        static_cast<std::uint16_t>(reader.readShort()),
        static_cast<std::uint32_t>(reader.readInt()),
        static_cast<std::uint32_t>(reader.readInt()),
    };
}

bool MoaID::isZlib() const {
    return *this == ZLIB_COMPRESSION || *this == ZLIB_COMPRESSION_ALT;
}

bool MoaID::isSound() const {
    return *this == SND_COMPRESSION;
}

bool MoaID::isNull() const {
    return *this == NULL_COMPRESSION;
}

bool MoaID::isFontMap() const {
    return *this == FONTMAP_COMPRESSION;
}

std::string MoaID::toString() const {
    std::ostringstream out;
    out << "MoaID["
        << std::uppercase << std::hex << std::setfill('0')
        << std::setw(8) << data1 << '-'
        << std::setw(4) << data2 << '-'
        << std::setw(4) << data3 << '-'
        << std::setw(8) << data4 << '-'
        << std::setw(8) << data5 << ']';
    return out.str();
}

} // namespace libreshockwave::format
