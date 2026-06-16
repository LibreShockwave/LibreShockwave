#include "libreshockwave/chunks/ConfigChunk.hpp"

#include <algorithm>

#include "libreshockwave/bitmap/Palette.hpp"

namespace libreshockwave::chunks {

ConfigChunk::ConfigChunk(const DirectorFile* file,
                         id::ChunkId id,
                         int directorVersion,
                         int stageTop,
                         int stageLeft,
                         int stageBottom,
                         int stageRight,
                         int minMember,
                         int maxMember,
                         int tempo,
                         int bgColor,
                         int stageColor,
                         int stageColorRGB,
                         int commentFont,
                         int commentSize,
                         int commentStyle,
                         int defaultPaletteCastLib,
                         int defaultPaletteMember,
                         int movieVersion,
                         std::int16_t platform)
    : file_(file),
      id_(id),
      directorVersion_(directorVersion),
      stageTop_(stageTop),
      stageLeft_(stageLeft),
      stageBottom_(stageBottom),
      stageRight_(stageRight),
      minMember_(minMember),
      maxMember_(maxMember),
      tempo_(tempo),
      bgColor_(bgColor),
      stageColor_(stageColor),
      stageColorRGB_(stageColorRGB),
      commentFont_(commentFont),
      commentSize_(commentSize),
      commentStyle_(commentStyle),
      defaultPaletteCastLib_(defaultPaletteCastLib),
      defaultPaletteMember_(defaultPaletteMember),
      movieVersion_(movieVersion),
      platform_(platform) {}

const DirectorFile* ConfigChunk::file() const { return file_; }
format::ChunkType ConfigChunk::type() const { return format::ChunkType::DRCF; }
id::ChunkId ConfigChunk::id() const { return id_; }
int ConfigChunk::directorVersion() const { return directorVersion_; }
int ConfigChunk::stageTop() const { return stageTop_; }
int ConfigChunk::stageLeft() const { return stageLeft_; }
int ConfigChunk::stageBottom() const { return stageBottom_; }
int ConfigChunk::stageRight() const { return stageRight_; }
int ConfigChunk::minMember() const { return minMember_; }
int ConfigChunk::maxMember() const { return maxMember_; }
int ConfigChunk::tempo() const { return tempo_; }
int ConfigChunk::bgColor() const { return bgColor_; }
int ConfigChunk::stageColor() const { return stageColor_; }
int ConfigChunk::stageColorRGB() const { return stageColorRGB_; }
int ConfigChunk::commentFont() const { return commentFont_; }
int ConfigChunk::commentSize() const { return commentSize_; }
int ConfigChunk::commentStyle() const { return commentStyle_; }
int ConfigChunk::defaultPaletteCastLib() const { return defaultPaletteCastLib_; }
int ConfigChunk::defaultPaletteMember() const { return defaultPaletteMember_; }
int ConfigChunk::movieVersion() const { return movieVersion_; }
std::int16_t ConfigChunk::platform() const { return platform_; }
int ConfigChunk::stageWidth() const { return stageRight_ - stageLeft_; }
int ConfigChunk::stageHeight() const { return stageBottom_ - stageTop_; }

ConfigChunk ConfigChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version) {
    (void)version;
    reader.setOrder(io::ByteOrder::BigEndian);

    reader.seek(36);
    const int directorVersion = reader.readU16();
    const bool isD7Plus = directorVersion >= 0x208;

    reader.seek(0);
    (void)reader.readU16();
    const int fileVersion = reader.readU16();
    const int stageTop = reader.readI16();
    const int stageLeft = reader.readI16();
    const int stageBottom = reader.readI16();
    const int stageRight = reader.readI16();
    const int minMember = reader.readI16();
    const int maxMember = reader.readI16();
    reader.skip(2);

    int d7StageColorG = 0;
    int d7StageColorB = 0;
    if (isD7Plus) {
        d7StageColorG = reader.readU8();
        d7StageColorB = reader.readU8();
    } else {
        reader.skip(2);
    }

    const int commentFont = reader.readI16();
    const int commentSize = reader.readI16();
    const int commentStyle = reader.readU16();

    int stageColor = 0;
    int stageColorRGB = 0;
    if (isD7Plus) {
        const int isRgb = reader.readU8();
        const int stageColorR = reader.readU8();
        stageColor = (isRgb << 8) | stageColorR;
        if (isRgb != 0) {
            stageColorRGB = (stageColorR << 16) | (d7StageColorG << 8) | d7StageColorB;
        } else {
            stageColorRGB = static_cast<int>(bitmap::Palette::systemMacPalette().getColor(stageColorR & 0xFF));
        }
    } else {
        stageColor = reader.readI16();
        stageColorRGB = static_cast<int>(bitmap::Palette::systemMacPalette().getColor(stageColor & 0xFF));
    }

    const int bgColor = reader.readI16();
    reader.skip(2);
    reader.skip(4);
    reader.skip(2);
    reader.skip(2);
    reader.skip(4);
    reader.skip(4);
    reader.skip(4);
    reader.skip(2);
    const int tempo = reader.readI16();
    const auto platform = reader.readShort();

    int defaultPaletteCastLib = 0;
    int defaultPaletteMember = 0;
    if (reader.bytesLeft() >= 22) {
        reader.skip(18);
        defaultPaletteCastLib = reader.readI16();
        defaultPaletteMember = reader.readI16();
    }

    return ConfigChunk(file, id, directorVersion, stageTop, stageLeft, stageBottom, stageRight,
                       minMember, maxMember, tempo, bgColor, stageColor, stageColorRGB,
                       commentFont, commentSize, commentStyle, defaultPaletteCastLib,
                       defaultPaletteMember, fileVersion, platform);
}

} // namespace libreshockwave::chunks
