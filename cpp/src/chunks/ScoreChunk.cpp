#include "libreshockwave/chunks/ScoreChunk.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "libreshockwave/io/BinaryReader.hpp"
#include <iostream>

namespace libreshockwave::chunks {
namespace {

constexpr int kMaxScoreEntryCount = 100000;

class ScopedByteOrder {
public:
    ScopedByteOrder(io::BinaryReader& reader, io::ByteOrder order)
        : reader_(reader), originalOrder_(reader.order()) {
        reader_.setOrder(order);
    }

    ~ScopedByteOrder() {
        reader_.setOrder(originalOrder_);
    }

private:
    io::BinaryReader& reader_;
    io::ByteOrder originalOrder_;
};

ScoreChunk::Header readHeader(io::BinaryReader& reader) {
    return ScoreChunk::Header{
        reader.readI32(),
        reader.readI32(),
        reader.readI32(),
        reader.readI32(),
        reader.readI32(),
        reader.readI32()
    };
}

ScoreChunk::FrameIntervalPrimary readPrimaryInterval(io::BinaryReader& reader) {
    return ScoreChunk::FrameIntervalPrimary{
        reader.readI32(),
        reader.readI32(),
        reader.readI32(),
        reader.readI32(),
        reader.readI32(),
        reader.readU16(),
        reader.readI32(),
        reader.readU16(),
        reader.readI32(),
        reader.readI32(),
        reader.readI32(),
        reader.readI32()
    };
}

ScoreChunk::FrameIntervalSecondary readSecondaryInterval(io::BinaryReader& reader) {
    return ScoreChunk::FrameIntervalSecondary{
        reader.readU16(),
        reader.readU16(),
        reader.readI32()
    };
}

std::uint8_t readU8At(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return data[offset];
}

std::uint16_t readU16At(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[offset]) << 8) |
                                      static_cast<std::uint16_t>(data[offset + 1]));
}

std::int16_t readI16At(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::int16_t>(readU16At(data, offset));
}

ScoreChunk::ChannelData readChannelDataAt(const std::vector<std::uint8_t>& data,
                                          std::size_t offset,
                                          int spriteRecordSize) {
    const int spriteType = readU8At(data, offset);
    const int inkByte = readU8At(data, offset + 1);
    const int ink = inkByte & 0x3F;
    const int trails = (inkByte >> 6) & 0x1;
    const int stretch = (inkByte >> 7) & 0x1;
    const int foreColor = readU8At(data, offset + 2);
    const int backColor = readU8At(data, offset + 3);
    const int castLib = readU16At(data, offset + 4);
    const int castMember = readU16At(data, offset + 6);
    const int unk1 = readU16At(data, offset + 8);
    const int unk2 = readU16At(data, offset + 10);
    const int posY = readI16At(data, offset + 12);
    const int posX = readI16At(data, offset + 14);
    const int height = readU16At(data, offset + 16);
    const int width = readU16At(data, offset + 18);

    int colorFlag = 0;
    int blendByte = 0;
    int thicknessFlags = 0;
    int foreColorG = 0;
    int backColorG = 0;
    int foreColorB = 0;
    int backColorB = 0;

    if (spriteRecordSize >= 24) {
        const int unk3 = readU8At(data, offset + 20);
        colorFlag = (unk3 & 0xF0) >> 4;
        blendByte = readU8At(data, offset + 21);
        thicknessFlags = readU8At(data, offset + 22);
    }

    if (spriteRecordSize >= 28) {
        foreColorG = readU8At(data, offset + 24);
        backColorG = readU8At(data, offset + 25);
        foreColorB = readU8At(data, offset + 26);
        backColorB = readU8At(data, offset + 27);
    }

    return ScoreChunk::ChannelData{spriteType, ink, trails, stretch, foreColor, backColor, castLib, castMember,
                                   unk1, unk2, posY, posX, height, width, colorFlag, blendByte,
                                   thicknessFlags, foreColorG, backColorG, foreColorB, backColorB};
}

ScoreChunk::ScoreFrameData parseFrameData(const std::vector<std::uint8_t>& data) {
    io::BinaryReader reader(data, io::ByteOrder::BigEndian);
    if (reader.bytesLeft() < 20) {
        return ScoreChunk::ScoreFrameData::empty();
    }

    (void)reader.readI32();
    (void)reader.readI32();
    const int frameCount = reader.readI32();
    const int framesVersion = reader.readU16();
    const int spriteRecordSize = reader.readU16();
    const int numChannels = reader.readU16();
    reader.skip(2);

    const ScoreChunk::FrameDataHeader header{frameCount, spriteRecordSize, numChannels, framesVersion};
    const int mainChannelsSize = framesVersion <= 7 ? 48 : (framesVersion <= 13 ? 144 : 0);
    const int displayedChannels = framesVersion <= 7 ? 48 : (framesVersion <= 13 ? 120 : numChannels);
    const int spriteChannelBase = framesVersion <= 7 ? 6 : (framesVersion <= 13 ? 3 : 0);
    const int frameSize = mainChannelsSize + displayedChannels * spriteRecordSize;
    const long long totalSizeLong = static_cast<long long>(frameCount) * static_cast<long long>(frameSize);
    if (frameCount <= 0 || frameSize <= 0 || totalSizeLong <= 0 || totalSizeLong > 50'000'000LL) {
        return ScoreChunk::ScoreFrameData::empty();
    }

    const auto totalSize = static_cast<std::size_t>(totalSizeLong);
    std::vector<std::uint8_t> channelData(totalSize, 0);

    int frameIndex = 0;
    while (!reader.eof() && frameIndex < frameCount) {
        if (reader.bytesLeft() < 2) {
            break;
        }

        const int length = reader.readU16();
        if (length == 0) {
            break;
        }

        if (frameIndex > 0) {
            const auto prevOffset = static_cast<std::size_t>(frameIndex - 1) * static_cast<std::size_t>(frameSize);
            const auto currOffset = static_cast<std::size_t>(frameIndex) * static_cast<std::size_t>(frameSize);
            std::copy_n(channelData.begin() + static_cast<std::ptrdiff_t>(prevOffset),
                        static_cast<std::size_t>(frameSize),
                        channelData.begin() + static_cast<std::ptrdiff_t>(currOffset));
        }

        const int frameLength = length - 2;
        if (frameLength > 0 && reader.bytesLeft() >= static_cast<std::size_t>(frameLength)) {
            io::BinaryReader frameReader(reader.readBytes(static_cast<std::size_t>(frameLength)), io::ByteOrder::BigEndian);
            while (!frameReader.eof() && frameReader.bytesLeft() >= 4) {
                const int channelSize = frameReader.readU16();
                const int channelOffset = frameReader.readU16();
                if (channelSize <= 0 || frameReader.bytesLeft() < static_cast<std::size_t>(channelSize)) {
                    break;
                }

                auto channelDelta = frameReader.readBytes(static_cast<std::size_t>(channelSize));
                const auto frameOffset = static_cast<std::size_t>(frameIndex) * static_cast<std::size_t>(frameSize);
                const auto destOffset = frameOffset + static_cast<std::size_t>(channelOffset);
                const auto endOffset = destOffset + static_cast<std::size_t>(channelSize);
                if (endOffset <= channelData.size()) {
                    std::copy(channelDelta.begin(),
                              channelDelta.end(),
                              channelData.begin() + static_cast<std::ptrdiff_t>(destOffset));
                }
            }
        }
        ++frameIndex;
    }

    const bool hasLegacyMainChannels = mainChannelsSize > 0;
    const bool isD5 = framesVersion <= 7;
    const bool isD6 = framesVersion > 7 && framesVersion <= 13;
    std::vector<ScoreChunk::FrameChannelEntry> frameChannelEntries;
    std::vector<ScoreChunk::TempoChannelData> tempoChannelEntries;
    std::vector<ScoreChunk::PaletteChannelData> paletteChannelEntries;

    for (int frame = 0; frame < frameCount; ++frame) {
        const auto frameStart = static_cast<std::size_t>(frame) * static_cast<std::size_t>(frameSize);

        if (hasLegacyMainChannels) {
            if (frameStart + 22 <= channelData.size()) {
                const int tempoOffset = isD6 ? 24 + 6 : 21;
                const int tempo = channelData[frameStart + static_cast<std::size_t>(tempoOffset)] & 0xFF;
                if (tempo > 0) {
                    tempoChannelEntries.push_back(ScoreChunk::TempoChannelData{frame, tempo});
                }
            }

            if (isD6 && frameStart + 124 <= channelData.size()) {
                const auto paletteOffset = frameStart + 120U;
                const auto castLib = static_cast<std::int16_t>(
                    (channelData[paletteOffset] << 8) | channelData[paletteOffset + 1]);
                const auto member = static_cast<std::int16_t>(
                    (channelData[paletteOffset + 2] << 8) | channelData[paletteOffset + 3]);
                if (member != 0) {
                    paletteChannelEntries.push_back(ScoreChunk::PaletteChannelData{frame, castLib, member});
                }
            }

            const int numSprites = displayedChannels;
            for (int sprite = 0; sprite < numSprites; ++sprite) {
                const auto pos = frameStart + static_cast<std::size_t>(mainChannelsSize + sprite * spriteRecordSize);
                if (pos + 24 > channelData.size()) {
                    break;
                }
                auto channel = readChannelDataAt(channelData, pos, spriteRecordSize);
                if (!channel.isEmpty()) {
                    frameChannelEntries.push_back(ScoreChunk::FrameChannelEntry{
                        id::FrameIndex(frame),
                        id::ChannelId(sprite + spriteChannelBase),
                        channel});
                }
            }
        } else {
            for (int channelIndex = 0; channelIndex < numChannels; ++channelIndex) {
                const auto pos = frameStart + static_cast<std::size_t>(channelIndex * spriteRecordSize);
                if (pos + static_cast<std::size_t>(spriteRecordSize) > channelData.size()) {
                    break;
                }

                if (channelIndex == 4) {
                    if (pos + 7 <= channelData.size()) {
                        const int tempo = channelData[pos + 6] & 0xFF;
                        if (tempo > 0 && tempo <= 120) {
                            tempoChannelEntries.push_back(ScoreChunk::TempoChannelData{frame, tempo});
                        }
                    }
                } else if (channelIndex == 5) {
                    if (pos + 4 <= channelData.size()) {
                        const auto castLib = static_cast<std::int16_t>((channelData[pos] << 8) | channelData[pos + 1]);
                        const auto member = static_cast<std::int16_t>((channelData[pos + 2] << 8) | channelData[pos + 3]);
                        if (member != 0) {
                            paletteChannelEntries.push_back(ScoreChunk::PaletteChannelData{frame, castLib, member});
                        }
                    }
                } else if (pos + 24 <= channelData.size()) {
                    auto channel = readChannelDataAt(channelData, pos, spriteRecordSize);
                    if (!channel.isEmpty()) {
                        frameChannelEntries.push_back(ScoreChunk::FrameChannelEntry{id::FrameIndex(frame), id::ChannelId(channelIndex), channel});
                    }
                }
            }
        }
    }

    return ScoreChunk::ScoreFrameData{
        header,
        std::move(channelData),
        std::move(frameChannelEntries),
        std::move(tempoChannelEntries),
        std::move(paletteChannelEntries)
    };
}

bool isPlausiblePrimaryInterval(const ScoreChunk::FrameIntervalPrimary& primary,
                                const ScoreChunk::FrameDataHeader& frameHeader) {
    if (primary.startFrame < 1 || primary.endFrame < primary.startFrame || primary.channelIndex < 0) {
        return false;
    }
    if (frameHeader.frameCount > 0 && primary.endFrame > frameHeader.frameCount) {
        return false;
    }
    if (frameHeader.numChannels > 0 && primary.channelIndex >= frameHeader.numChannels) {
        return false;
    }
    return true;
}

std::vector<ScoreChunk::FrameInterval> parseFrameIntervals(const std::vector<std::vector<std::uint8_t>>& entries,
                                                           const ScoreChunk::FrameDataHeader& frameHeader) {
    std::vector<ScoreChunk::FrameInterval> results;
    std::size_t index = 2;

    while (index < entries.size()) {
        const auto& entryBytes = entries[index];
        if (entryBytes.empty()) {
            ++index;
            continue;
        }

        if (entryBytes.size() >= 44 && entryBytes.size() <= 48) {
            io::BinaryReader reader(entryBytes, io::ByteOrder::BigEndian);
            auto primary = readPrimaryInterval(reader);
            if (!isPlausiblePrimaryInterval(primary, frameHeader)) {
                ++index;
                continue;
            }

            std::vector<ScoreChunk::FrameIntervalSecondary> secondaries;
            std::size_t next = index + 1;
            while (next < entries.size()) {
                const auto nextSize = entries[next].size();
                if (nextSize < 8 || nextSize % 8 != 0) {
                    break;
                }

                const auto behaviorCount = nextSize / 8;
                io::BinaryReader secondaryReader(entries[next], io::ByteOrder::BigEndian);
                bool foundValidBehavior = false;
                for (std::size_t behavior = 0; behavior < behaviorCount; ++behavior) {
                    auto secondary = readSecondaryInterval(secondaryReader);
                    if (secondary.castLib > 0 && secondary.castMember > 0) {
                        secondaries.push_back(secondary);
                        foundValidBehavior = true;
                    }
                }

                if (!foundValidBehavior) {
                    break;
                }
                ++next;
            }

            if (secondaries.empty()) {
                results.push_back(ScoreChunk::FrameInterval{primary, std::nullopt});
            } else {
                for (const auto& secondary : secondaries) {
                    results.push_back(ScoreChunk::FrameInterval{primary, secondary});
                }
            }

            index = next;
            continue;
        }

        ++index;
    }

    return results;
}

ScoreChunk createEmptyScore(const DirectorFile* file, id::ChunkId id) {
    return ScoreChunk(file, id, ScoreChunk::Header{0, 0, 0, 0, 0, 0}, {}, ScoreChunk::ScoreFrameData::empty(), {});
}

} // namespace

ScoreChunk::ChannelData ScoreChunk::ChannelData::empty() {
    return ChannelData{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
}

ScoreChunk::ChannelData ScoreChunk::ChannelData::read(io::BinaryReader& reader, int spriteRecordSize) {
    int spriteType = reader.readU8();
    const int inkByte = reader.readU8();
    const int ink = inkByte & 0x3F;
    const int trails = (inkByte >> 6) & 0x1;
    const int stretch = (inkByte >> 7) & 0x1;
    const int foreColor = reader.readU8();
    const int backColor = reader.readU8();
    const int castLib = reader.readU16();
    const int castMember = reader.readU16();
    const int unk1 = reader.readU16();
    const int unk2 = reader.readU16();
    const int posY = reader.readI16();
    const int posX = reader.readI16();
    const int height = reader.readU16();
    const int width = reader.readU16();

    int colorFlag = 0;
    int blendByte = 0;
    int thicknessFlags = 0;
    int foreColorG = 0;
    int backColorG = 0;
    int foreColorB = 0;
    int backColorB = 0;

    if (spriteRecordSize >= 24) {
        const int unk3 = reader.readU8();
        colorFlag = (unk3 & 0xF0) >> 4;
        blendByte = reader.readU8();
        thicknessFlags = reader.readU8();
        (void)reader.readU8();
    }

    if (spriteRecordSize >= 28) {
        foreColorG = reader.readU8();
        backColorG = reader.readU8();
        foreColorB = reader.readU8();
        backColorB = reader.readU8();
    }

    return ChannelData{spriteType, ink, trails, stretch, foreColor, backColor, castLib, castMember, unk1, unk2,
                       posY, posX, height, width, colorFlag, blendByte, thicknessFlags,
                       foreColorG, backColorG, foreColorB, backColorB};
}

bool ScoreChunk::ChannelData::isEmpty() const {
    if (width <= 0 || height <= 0) {
        return true;
    }
    return spriteType == 0 && ink == 0 && foreColor == 0 && backColor == 0 &&
           castLib == 0 && castMember == 0 && posY == 0 && posX == 0 &&
           height == 0 && width == 0;
}

int ScoreChunk::ChannelData::resolvedForeColor() const {
    if ((colorFlag & 0x1) != 0) {
        return (foreColor << 16) | (foreColorG << 8) | foreColorB;
    }
    return foreColor;
}

int ScoreChunk::ChannelData::resolvedBackColor() const {
    if ((colorFlag & 0x2) != 0) {
        return (backColor << 16) | (backColorG << 8) | backColorB;
    }
    return backColor;
}

bool ScoreChunk::ChannelData::isForeColorRGB() const { return (colorFlag & 0x1) != 0; }
bool ScoreChunk::ChannelData::isBackColorRGB() const { return (colorFlag & 0x2) != 0; }
bool ScoreChunk::ChannelData::isFlipH() const { return (thicknessFlags & 0x20) != 0; }
bool ScoreChunk::ChannelData::isFlipV() const { return (thicknessFlags & 0x40) != 0; }

std::optional<id::CastLibId> ScoreChunk::ChannelData::castLibId() const {
    if (castLib > 0) {
        return id::CastLibId(castLib);
    }
    return std::nullopt;
}

std::optional<id::MemberId> ScoreChunk::ChannelData::memberId() const {
    if (castMember > 0) {
        return id::MemberId(castMember);
    }
    return std::nullopt;
}

ScoreChunk::ScoreFrameData ScoreChunk::ScoreFrameData::empty() {
    return ScoreFrameData{FrameDataHeader{0, 24, 0, 0}, {}, {}, {}, {}};
}

id::FrameId ScoreChunk::FrameIntervalPrimary::startFrameId() const {
    return id::FrameId(std::max(1, startFrame));
}

id::FrameId ScoreChunk::FrameIntervalPrimary::endFrameId() const {
    return id::FrameId(std::max(1, endFrame));
}

id::ChannelId ScoreChunk::FrameIntervalPrimary::channelId() const {
    return id::ChannelId(channelIndex);
}

std::optional<id::CastLibId> ScoreChunk::FrameIntervalSecondary::castLibId() const {
    if (castLib > 0) {
        return id::CastLibId(castLib);
    }
    return std::nullopt;
}

std::optional<id::MemberId> ScoreChunk::FrameIntervalSecondary::memberId() const {
    if (castMember > 0) {
        return id::MemberId(castMember);
    }
    return std::nullopt;
}

ScoreChunk::ScoreChunk(const DirectorFile* file,
                       id::ChunkId id,
                       Header header,
                       std::vector<std::vector<std::uint8_t>> entries,
                       ScoreFrameData frameData,
                       std::vector<FrameInterval> frameIntervals)
    : file_(file),
      id_(id),
      header_(header),
      entries_(std::move(entries)),
      frameData_(std::move(frameData)),
      frameIntervals_(std::move(frameIntervals)) {}

const DirectorFile* ScoreChunk::file() const { return file_; }
format::ChunkType ScoreChunk::type() const { return format::ChunkType::VWSC; }
id::ChunkId ScoreChunk::id() const { return id_; }
const ScoreChunk::Header& ScoreChunk::header() const { return header_; }
const std::vector<std::vector<std::uint8_t>>& ScoreChunk::entries() const { return entries_; }
const ScoreChunk::ScoreFrameData& ScoreChunk::frameData() const { return frameData_; }
const std::vector<ScoreChunk::FrameInterval>& ScoreChunk::frameIntervals() const { return frameIntervals_; }
int ScoreChunk::getFrameCount() const { return frameData_.header.frameCount; }
int ScoreChunk::getChannelCount() const { return frameData_.header.numChannels; }
const std::vector<std::uint8_t>& ScoreChunk::getRawChannelData() const { return frameData_.decompressedData; }
int ScoreChunk::getSpriteRecordSize() const { return frameData_.header.spriteRecordSize; }

int ScoreChunk::getFrameTempo(int frame) const {
    int result = -1;
    for (const auto& tempo : frameData_.tempoChannelData) {
        if (tempo.frameIndex <= frame) {
            result = tempo.tempo;
        } else {
            break;
        }
    }
    return result;
}

std::optional<ScoreChunk::PaletteChannelData> ScoreChunk::getFramePalette(int frame) const {
    std::optional<PaletteChannelData> result;
    for (const auto& palette : frameData_.paletteChannelData) {
        if (palette.frameIndex <= frame) {
            result = palette;
        } else {
            break;
        }
    }
    return result;
}

ScoreChunk ScoreChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version) {
    (void)version;
    ScopedByteOrder order(reader, io::ByteOrder::BigEndian);

    if (reader.bytesLeft() < 24) {
        return createEmptyScore(file, id);
    }

    const auto header = readHeader(reader);
    if (header.entryCount < 0 || header.entryCount > kMaxScoreEntryCount) {
        return createEmptyScore(file, id);
    }

    const auto offsetCount = static_cast<std::size_t>(header.entryCount) + 1U;
    if (offsetCount > reader.bytesLeft() / 4U) {
        return createEmptyScore(file, id);
    }

    std::vector<int> offsets;
    offsets.reserve(offsetCount);
    for (int index = 0; index <= header.entryCount; ++index) {
        if (reader.bytesLeft() < 4) {
            break;
        }
        offsets.push_back(reader.readI32());
    }

    if (offsets.size() != static_cast<std::size_t>(header.entryCount + 1)) {
        return createEmptyScore(file, id);
    }

    for (std::size_t index = 0; index + 1 < offsets.size(); ++index) {
        if (offsets[index] < 0 || offsets[index] > offsets[index + 1]) {
            return createEmptyScore(file, id);
        }
    }
    if (!offsets.empty() && (offsets.back() < 0 || static_cast<std::size_t>(offsets.back()) > reader.bytesLeft())) {
        return createEmptyScore(file, id);
    }

    std::vector<std::vector<std::uint8_t>> entries;
    entries.reserve(static_cast<std::size_t>(header.entryCount));
    for (int index = 0; index < header.entryCount; ++index) {
        const int length = offsets[static_cast<std::size_t>(index + 1)] - offsets[static_cast<std::size_t>(index)];
        if (length > 0 && reader.bytesLeft() >= static_cast<std::size_t>(length)) {
            entries.push_back(reader.readBytes(static_cast<std::size_t>(length)));
        } else {
            entries.emplace_back();
        }
    }

    auto frameData = ScoreFrameData::empty();
    if (!entries.empty() && !entries[0].empty()) {
        frameData = parseFrameData(entries[0]);
    }

    auto frameIntervals = parseFrameIntervals(entries, frameData.header);
    return ScoreChunk(file, id, header, std::move(entries), std::move(frameData), std::move(frameIntervals));
}

} // namespace libreshockwave::chunks
