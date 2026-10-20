#pragma once

#include <cstdint>

#include "libreshockwave/chunks/chunk.hpp"
#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::chunks {

class ConfigChunk final : public Chunk {
public:
    ConfigChunk(const DirectorFile* file,
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
                std::int16_t platform);

    [[nodiscard]] const DirectorFile* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;

    [[nodiscard]] int directorVersion() const;
    [[nodiscard]] int stageTop() const;
    [[nodiscard]] int stageLeft() const;
    [[nodiscard]] int stageBottom() const;
    [[nodiscard]] int stageRight() const;
    [[nodiscard]] int minMember() const;
    [[nodiscard]] int maxMember() const;
    [[nodiscard]] int tempo() const;
    [[nodiscard]] int bgColor() const;
    [[nodiscard]] int stageColor() const;
    [[nodiscard]] int stageColorRGB() const;
    [[nodiscard]] int commentFont() const;
    [[nodiscard]] int commentSize() const;
    [[nodiscard]] int commentStyle() const;
    [[nodiscard]] int defaultPaletteCastLib() const;
    [[nodiscard]] int defaultPaletteMember() const;
    [[nodiscard]] int movieVersion() const;
    [[nodiscard]] std::int16_t platform() const;
    [[nodiscard]] int stageWidth() const;
    [[nodiscard]] int stageHeight() const;

    [[nodiscard]] static ConfigChunk read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version);

private:
    const DirectorFile* file_;
    id::ChunkId id_;
    int director_version_;
    int stage_top_;
    int stage_left_;
    int stage_bottom_;
    int stage_right_;
    int min_member_;
    int max_member_;
    int tempo_;
    int bg_color_;
    int stage_color_;
    int stage_color_rgb_;
    int comment_font_;
    int comment_size_;
    int comment_style_;
    int default_palette_cast_lib_;
    int default_palette_member_;
    int movie_version_;
    std::int16_t platform_;
};

} // namespace libreshockwave::chunks
