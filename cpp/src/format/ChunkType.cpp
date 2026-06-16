#include "libreshockwave/format/ChunkType.hpp"

#include <array>
#include <stdexcept>

namespace libreshockwave::format {
namespace {

struct ChunkTypeEntry {
    ChunkType type;
    std::string_view fourCC;
    std::string_view description;
};

constexpr std::array<ChunkTypeEntry, 41> kEntries{{
    {ChunkType::RIFX, "RIFX", "RIFX Container"},
    {ChunkType::XFIR, "XFIR", "RIFX Container (little-endian)"},
    {ChunkType::RIFF, "RIFF", "RIFF Container (D3 Windows)"},
    {ChunkType::FFIR, "FFIR", "RIFF Container (D3 Windows, little-endian)"},
    {ChunkType::MV93, "MV93", "Director Movie"},
    {ChunkType::MC95, "MC95", "Director Cast"},
    {ChunkType::FGDM, "FGDM", "Shockwave Movie (Afterburner)"},
    {ChunkType::FGDC, "FGDC", "Shockwave Cast (Afterburner)"},
    {ChunkType::RMMP, "RMMP", "RIFF Resource Map Marker (D3)"},
    {ChunkType::IMAP, "imap", "Initial Map"},
    {ChunkType::MMAP, "mmap", "Memory Map"},
    {ChunkType::JUNK, "junk", "Junk/Padding"},
    {ChunkType::FREE, "free", "Free Space"},
    {ChunkType::DRCF, "DRCF", "Director Config (D6+)"},
    {ChunkType::VWCF, "VWCF", "Director Config (D5)"},
    {ChunkType::MCsL, "MCsL", "Cast List"},
    {ChunkType::CASp, "CAS*", "Cast Member Array"},
    {ChunkType::CASt, "CASt", "Cast Member Definition"},
    {ChunkType::KEYp, "KEY*", "Key Table"},
    {ChunkType::Cinf, "Cinf", "Cast Info"},
    {ChunkType::VWCR, "VWCR", "Cast Members (D3)"},
    {ChunkType::VWCI, "VWCI", "Cast Info (D3)"},
    {ChunkType::LctX, "LctX", "Script Context (capital X)"},
    {ChunkType::Lctx, "Lctx", "Script Context"},
    {ChunkType::Lnam, "Lnam", "Script Names"},
    {ChunkType::Lscr, "Lscr", "Script Bytecode"},
    {ChunkType::VWSC, "VWSC", "Score Data"},
    {ChunkType::SCVW, "SCVW", "Score Data (alternate)"},
    {ChunkType::VWLB, "VWLB", "Frame Labels"},
    {ChunkType::Sord, "Sord", "Score Ordering"},
    {ChunkType::BITD, "BITD", "Bitmap Data"},
    {ChunkType::ALFA, "ALFA", "Alpha Channel Data"},
    {ChunkType::CLUT, "CLUT", "Color Lookup Table (Palette)"},
    {ChunkType::STXT, "STXT", "Styled Text"},
    {ChunkType::Fmap, "Fmap", "Font Map"},
    {ChunkType::snd_, "snd ", "Sound Data"},
    {ChunkType::ediM, "ediM", "Media Resource"},
    {ChunkType::XMED, "XMED", "Extended Media"},
    {ChunkType::FXmp, "FXmp", "Effect Map"},
    {ChunkType::Thum, "Thum", "Thumbnail"},
    {ChunkType::UNKNOWN, "????", "Unknown Chunk"},
}};

constexpr std::uint32_t fourCCValue(std::string_view value) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(value[0])) << 24) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(value[1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(value[2])) << 8) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(value[3]));
}

const ChunkTypeEntry& entryFor(ChunkType type) {
    for (const auto& entry : kEntries) {
        if (entry.type == type) {
            return entry;
        }
    }
    throw std::logic_error("Unknown ChunkType enum value");
}

} // namespace

std::uint32_t fourCC(ChunkType type) {
    return fourCCValue(fourCCString(type));
}

std::string_view fourCCString(ChunkType type) {
    return entryFor(type).fourCC;
}

std::string_view description(ChunkType type) {
    return entryFor(type).description;
}

bool isContainer(ChunkType type) {
    return type == ChunkType::RIFX || type == ChunkType::XFIR ||
           type == ChunkType::RIFF || type == ChunkType::FFIR;
}

bool isRIFF(ChunkType type) {
    return type == ChunkType::RIFF || type == ChunkType::FFIR;
}

bool isMovieType(ChunkType type) {
    return type == ChunkType::MV93 || type == ChunkType::MC95 ||
           type == ChunkType::FGDM || type == ChunkType::FGDC;
}

bool isAfterburner(ChunkType type) {
    return type == ChunkType::FGDM || type == ChunkType::FGDC;
}

bool isConfig(ChunkType type) {
    return type == ChunkType::DRCF || type == ChunkType::VWCF;
}

bool isScript(ChunkType type) {
    return type == ChunkType::LctX || type == ChunkType::Lctx ||
           type == ChunkType::Lnam || type == ChunkType::Lscr;
}

bool isScore(ChunkType type) {
    return type == ChunkType::VWSC || type == ChunkType::SCVW ||
           type == ChunkType::VWLB || type == ChunkType::Sord;
}

bool isMedia(ChunkType type) {
    return type == ChunkType::BITD || type == ChunkType::CLUT ||
           type == ChunkType::STXT || type == ChunkType::Fmap ||
           type == ChunkType::snd_ || type == ChunkType::ediM ||
           type == ChunkType::XMED;
}

ChunkType chunkTypeFromFourCC(std::uint32_t value) {
    for (const auto& entry : kEntries) {
        if (entry.type != ChunkType::UNKNOWN && fourCCValue(entry.fourCC) == value) {
            return entry.type;
        }
    }
    return ChunkType::UNKNOWN;
}

ChunkType chunkTypeFromString(std::string_view value) {
    for (const auto& entry : kEntries) {
        if (entry.type != ChunkType::UNKNOWN && entry.fourCC == value) {
            return entry.type;
        }
    }
    return ChunkType::UNKNOWN;
}

std::string toString(ChunkType type) {
    return std::string(fourCCString(type)) + " (" + std::string(description(type)) + ")";
}

} // namespace libreshockwave::format
