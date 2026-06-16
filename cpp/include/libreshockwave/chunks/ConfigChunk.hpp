#pragma once

#include <cstdint>

#include "libreshockwave/chunks/Chunk.hpp"
#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::chunks {

class ConfigChunk final : public Chunk {
public:
    ConfigChunk(const DirectorFile* file,
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
    int directorVersion_;
    int stageTop_;
    int stageLeft_;
    int stageBottom_;
    int stageRight_;
    int minMember_;
    int maxMember_;
    int tempo_;
    int bgColor_;
    int stageColor_;
    int stageColorRGB_;
    int commentFont_;
    int commentSize_;
    int commentStyle_;
    int defaultPaletteCastLib_;
    int defaultPaletteMember_;
    int movieVersion_;
    std::int16_t platform_;
};

} // namespace libreshockwave::chunks
