#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace libreshockwave::format {

enum class ChunkType {
    RIFX,
    XFIR,
    RIFF,
    FFIR,
    MV93,
    MC95,
    FGDM,
    FGDC,
    RMMP,
    IMAP,
    MMAP,
    JUNK,
    FREE,
    DRCF,
    VWCF,
    MCsL,
    CASp,
    CASt,
    KEYp,
    Cinf,
    VWCR,
    VWCI,
    LctX,
    Lctx,
    Lnam,
    Lscr,
    VWSC,
    SCVW,
    VWLB,
    Sord,
    BITD,
    ALFA,
    CLUT,
    STXT,
    Fmap,
    snd_,
    ediM,
    XMED,
    FXmp,
    Thum,
    UNKNOWN
};

[[nodiscard]] std::uint32_t fourCC(ChunkType type);
[[nodiscard]] std::string_view fourCCString(ChunkType type);
[[nodiscard]] std::string_view description(ChunkType type);

[[nodiscard]] bool isContainer(ChunkType type);
[[nodiscard]] bool isRIFF(ChunkType type);
[[nodiscard]] bool isMovieType(ChunkType type);
[[nodiscard]] bool isAfterburner(ChunkType type);
[[nodiscard]] bool isConfig(ChunkType type);
[[nodiscard]] bool isScript(ChunkType type);
[[nodiscard]] bool isScore(ChunkType type);
[[nodiscard]] bool isMedia(ChunkType type);

[[nodiscard]] ChunkType chunkTypeFromFourCC(std::uint32_t value);
[[nodiscard]] ChunkType chunkTypeFromString(std::string_view value);
[[nodiscard]] std::string toString(ChunkType type);

} // namespace libreshockwave::format
