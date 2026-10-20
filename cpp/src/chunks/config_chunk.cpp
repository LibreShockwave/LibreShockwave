#include "libreshockwave/chunks/config_chunk.hpp"

#include <algorithm>

#include "libreshockwave/bitmap/palette.hpp"

namespace libreshockwave::chunks {

ConfigChunk::ConfigChunk(const DirectorFile* file,
                         id::ChunkId id,
                         int director_version,
                         int stage_top,
                         int stage_left,
                         int stage_bottom,
                         int stage_right,
                         int min_member,
                         int max_member,
                         int tempo,
                         int bg_color,
                         int stage_color,
                         int stage_color_rgb,
                         int comment_font,
                         int comment_size,
                         int comment_style,
                         int default_palette_cast_lib,
                         int default_palette_member,
                         int movie_version,
                         std::int16_t platform)
    : file_(file),
      id_(id),
      director_version_(director_version),
      stage_top_(stage_top),
      stage_left_(stage_left),
      stage_bottom_(stage_bottom),
      stage_right_(stage_right),
      min_member_(min_member),
      max_member_(max_member),
      tempo_(tempo),
      bg_color_(bg_color),
      stage_color_(stage_color),
      stage_color_rgb_(stage_color_rgb),
      comment_font_(comment_font),
      comment_size_(comment_size),
      comment_style_(comment_style),
      default_palette_cast_lib_(default_palette_cast_lib),
      default_palette_member_(default_palette_member),
      movie_version_(movie_version),
      platform_(platform) {}

const DirectorFile* ConfigChunk::file() const { return file_; }
format::ChunkType ConfigChunk::type() const { return format::ChunkType::DRCF; }
id::ChunkId ConfigChunk::id() const { return id_; }
int ConfigChunk::directorVersion() const { return director_version_; }
int ConfigChunk::stageTop() const { return stage_top_; }
int ConfigChunk::stageLeft() const { return stage_left_; }
int ConfigChunk::stageBottom() const { return stage_bottom_; }
int ConfigChunk::stageRight() const { return stage_right_; }
int ConfigChunk::minMember() const { return min_member_; }
int ConfigChunk::maxMember() const { return max_member_; }
int ConfigChunk::tempo() const { return tempo_; }
int ConfigChunk::bgColor() const { return bg_color_; }
int ConfigChunk::stageColor() const { return stage_color_; }
int ConfigChunk::stageColorRGB() const { return stage_color_rgb_; }
int ConfigChunk::commentFont() const { return comment_font_; }
int ConfigChunk::commentSize() const { return comment_size_; }
int ConfigChunk::commentStyle() const { return comment_style_; }
int ConfigChunk::defaultPaletteCastLib() const { return default_palette_cast_lib_; }
int ConfigChunk::defaultPaletteMember() const { return default_palette_member_; }
int ConfigChunk::movieVersion() const { return movie_version_; }
std::int16_t ConfigChunk::platform() const { return platform_; }
int ConfigChunk::stageWidth() const { return stage_right_ - stage_left_; }
int ConfigChunk::stageHeight() const { return stage_bottom_ - stage_top_; }

ConfigChunk ConfigChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version) {
    (void)version;
    reader.setOrder(io::ByteOrder::BigEndian);

    reader.seek(36);
    const int director_version = reader.readU16();
    const bool is_d7_plus = director_version >= 0x208;

    reader.seek(0);
    (void)reader.readU16();
    const int file_version = reader.readU16();
    const int stage_top = reader.readI16();
    const int stage_left = reader.readI16();
    const int stage_bottom = reader.readI16();
    const int stage_right = reader.readI16();
    const int min_member = reader.readI16();
    const int max_member = reader.readI16();
    reader.skip(2);

    int d7_stage_color_g = 0;
    int d7_stage_color_b = 0;
    if (is_d7_plus) {
        d7_stage_color_g = reader.readU8();
        d7_stage_color_b = reader.readU8();
    } else {
        reader.skip(2);
    }

    const int comment_font = reader.readI16();
    const int comment_size = reader.readI16();
    const int comment_style = reader.readU16();

    int stage_color = 0;
    int stage_color_rgb = 0;
    if (is_d7_plus) {
        const int is_rgb = reader.readU8();
        const int stage_color_r = reader.readU8();
        stage_color = (is_rgb << 8) | stage_color_r;
        if (is_rgb != 0) {
            stage_color_rgb = (stage_color_r << 16) | (d7_stage_color_g << 8) | d7_stage_color_b;
        } else {
            stage_color_rgb = static_cast<int>(bitmap::Palette::systemMacPalette().getColor(stage_color_r & 0xFF));
        }
    } else {
        stage_color = reader.readI16();
        stage_color_rgb = static_cast<int>(bitmap::Palette::systemMacPalette().getColor(stage_color & 0xFF));
    }

    const int bg_color = reader.readI16();
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

    int default_palette_cast_lib = 0;
    int default_palette_member = 0;
    if (reader.bytesLeft() >= 22) {
        reader.skip(18);
        default_palette_cast_lib = reader.readI16();
        default_palette_member = reader.readI16();
    }

    return ConfigChunk(file, id, director_version, stage_top, stage_left, stage_bottom, stage_right,
                       min_member, max_member, tempo, bg_color, stage_color, stage_color_rgb,
                       comment_font, comment_size, comment_style, default_palette_cast_lib,
                       default_palette_member, file_version, platform);
}

} // namespace libreshockwave::chunks
