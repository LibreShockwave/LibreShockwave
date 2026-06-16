#pragma once

#include <cstdint>
#include <string>

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::format {

struct MoaID {
    std::uint32_t data1;
    std::uint16_t data2;
    std::uint16_t data3;
    std::uint32_t data4;
    std::uint32_t data5;

    static const MoaID ZLIB_COMPRESSION;
    static const MoaID ZLIB_COMPRESSION_ALT;
    static const MoaID SND_COMPRESSION;
    static const MoaID NULL_COMPRESSION;
    static const MoaID FONTMAP_COMPRESSION;

    [[nodiscard]] static MoaID read(io::BinaryReader& reader);

    [[nodiscard]] bool isZlib() const;
    [[nodiscard]] bool isSound() const;
    [[nodiscard]] bool isNull() const;
    [[nodiscard]] bool isFontMap() const;
    [[nodiscard]] std::string toString() const;

    friend bool operator==(const MoaID& lhs, const MoaID& rhs) = default;
};

} // namespace libreshockwave::format
