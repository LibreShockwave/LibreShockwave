#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "libreshockwave/chunks/Chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class ScoreChunk final : public Chunk {
public:
    struct Header {
        int totalLength;
        int unk1;
        int unk2;
        int entryCount;
        int unk3;
        int entrySizeSum;
    };

    struct FrameDataHeader {
        int frameCount;
        int spriteRecordSize;
        int numChannels;
        int framesVersion;
    };

    struct TempoChannelData {
        int frameIndex;
        int tempo;
    };

    struct PaletteChannelData {
        int frameIndex;
        int castLib;
        int castMember;
    };

    struct ChannelData {
        int spriteType;
        int ink;
        int trails;
        int stretch;
        int foreColor;
        int backColor;
        int castLib;
        int castMember;
        int unk1;
        int unk2;
        int posY;
        int posX;
        int height;
        int width;
        int colorFlag;
        int blendByte;
        int thicknessFlags;
        int foreColorG;
        int backColorG;
        int foreColorB;
        int backColorB;

        [[nodiscard]] static ChannelData empty();
        [[nodiscard]] static ChannelData read(io::BinaryReader& reader, int spriteRecordSize = 28);
        [[nodiscard]] bool isEmpty() const;
        [[nodiscard]] int resolvedForeColor() const;
        [[nodiscard]] int resolvedBackColor() const;
        [[nodiscard]] bool isForeColorRGB() const;
        [[nodiscard]] bool isBackColorRGB() const;
        [[nodiscard]] bool isFlipH() const;
        [[nodiscard]] bool isFlipV() const;
        [[nodiscard]] std::optional<id::CastLibId> castLibId() const;
        [[nodiscard]] std::optional<id::MemberId> memberId() const;
    };

    struct FrameChannelEntry {
        id::FrameIndex frameIndex;
        id::ChannelId channelIndex;
        ChannelData data;
    };

    struct ScoreFrameData {
        FrameDataHeader header;
        std::vector<std::uint8_t> decompressedData;
        std::vector<FrameChannelEntry> frameChannelData;
        std::vector<TempoChannelData> tempoChannelData;
        std::vector<PaletteChannelData> paletteChannelData;

        [[nodiscard]] static ScoreFrameData empty();
    };

    struct FrameIntervalPrimary {
        int startFrame;
        int endFrame;
        int unk0;
        int unk1;
        int channelIndex;
        int unk2;
        int unk3;
        int unk4;
        int unk5;
        int unk6;
        int unk7;
        int unk8;

        [[nodiscard]] id::FrameId startFrameId() const;
        [[nodiscard]] id::FrameId endFrameId() const;
        [[nodiscard]] id::ChannelId channelId() const;
    };

    struct FrameIntervalSecondary {
        int castLib;
        int castMember;
        int unk0;

        [[nodiscard]] std::optional<id::CastLibId> castLibId() const;
        [[nodiscard]] std::optional<id::MemberId> memberId() const;
    };

    struct FrameInterval {
        FrameIntervalPrimary primary;
        std::optional<FrameIntervalSecondary> secondary;
    };

    ScoreChunk(const DirectorFile* file,
               id::ChunkId id,
               Header header,
               std::vector<std::vector<std::uint8_t>> entries,
               ScoreFrameData frameData,
               std::vector<FrameInterval> frameIntervals);

    [[nodiscard]] const DirectorFile* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] const Header& header() const;
    [[nodiscard]] const std::vector<std::vector<std::uint8_t>>& entries() const;
    [[nodiscard]] const ScoreFrameData& frameData() const;
    [[nodiscard]] const std::vector<FrameInterval>& frameIntervals() const;
    [[nodiscard]] int getFrameCount() const;
    [[nodiscard]] int getChannelCount() const;
    [[nodiscard]] const std::vector<std::uint8_t>& getRawChannelData() const;
    [[nodiscard]] int getSpriteRecordSize() const;
    [[nodiscard]] int getFrameTempo(int frame) const;
    [[nodiscard]] std::optional<PaletteChannelData> getFramePalette(int frame) const;

    [[nodiscard]] static ScoreChunk read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version);

private:
    const DirectorFile* file_;
    id::ChunkId id_;
    Header header_;
    std::vector<std::vector<std::uint8_t>> entries_;
    ScoreFrameData frameData_;
    std::vector<FrameInterval> frameIntervals_;
};

} // namespace libreshockwave::chunks
