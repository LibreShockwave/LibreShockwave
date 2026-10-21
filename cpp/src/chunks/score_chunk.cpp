#include "libreshockwave/chunks/score_chunk.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "libreshockwave/io/binary_reader.hpp"
#include <iostream>

namespace libreshockwave::chunks {
namespace {

constexpr int k_max_score_entry_count = 100000;

class ScopedByteOrder {
public:
    ScopedByteOrder(io::BinaryReader& reader, io::ByteOrder order)
        : reader_(reader), original_order_(reader.order()) {
        reader_.setOrder(order);
    }

    ~ScopedByteOrder() {
        reader_.setOrder(original_order_);
    }

private:
    io::BinaryReader& reader_;
    io::ByteOrder original_order_;
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
                                          int sprite_record_size) {
    const int sprite_type = readU8At(data, offset);
    const int ink_byte = readU8At(data, offset + 1);
    const int ink = ink_byte & 0x3F;
    const int trails = (ink_byte >> 6) & 0x1;
    const int stretch = (ink_byte >> 7) & 0x1;
    const int fore_color = readU8At(data, offset + 2);
    const int back_color = readU8At(data, offset + 3);
    const int cast_lib = readU16At(data, offset + 4);
    const int cast_member = readU16At(data, offset + 6);
    const int unk1 = readU16At(data, offset + 8);
    const int unk2 = readU16At(data, offset + 10);
    const int pos_y = readI16At(data, offset + 12);
    const int pos_x = readI16At(data, offset + 14);
    const int height = readU16At(data, offset + 16);
    const int width = readU16At(data, offset + 18);

    int color_flag = 0;
    int blend_byte = 0;
    int thickness_flags = 0;
    int fore_color_g = 0;
    int back_color_g = 0;
    int fore_color_b = 0;
    int back_color_b = 0;

    if (sprite_record_size >= 24) {
        const int unk3 = readU8At(data, offset + 20);
        color_flag = (unk3 & 0xF0) >> 4;
        blend_byte = readU8At(data, offset + 21);
        thickness_flags = readU8At(data, offset + 22);
    }

    if (sprite_record_size >= 28) {
        fore_color_g = readU8At(data, offset + 24);
        back_color_g = readU8At(data, offset + 25);
        fore_color_b = readU8At(data, offset + 26);
        back_color_b = readU8At(data, offset + 27);
    }

    return ScoreChunk::ChannelData{sprite_type, ink, trails, stretch, fore_color, back_color, cast_lib, cast_member,
                                   unk1, unk2, pos_y, pos_x, height, width, color_flag, blend_byte,
                                   thickness_flags, fore_color_g, back_color_g, fore_color_b, back_color_b};
}

ScoreChunk::ScoreFrameData parseFrameData(const std::vector<std::uint8_t>& data) {
    io::BinaryReader reader(data, io::ByteOrder::BigEndian);
    if (reader.bytesLeft() < 20) {
        return ScoreChunk::ScoreFrameData::empty();
    }

    (void)reader.readI32();
    (void)reader.readI32();
    const int frame_count = reader.readI32();
    const int frames_version = reader.readU16();
    const int sprite_record_size = reader.readU16();
    const int num_channels = reader.readU16();
    reader.skip(2);

    const ScoreChunk::FrameDataHeader header{frame_count, sprite_record_size, num_channels, frames_version};
    const int main_channels_size = frames_version <= 7 ? 48 : (frames_version <= 13 ? 144 : 0);
    const int displayed_channels = frames_version <= 7 ? 48 : (frames_version <= 13 ? 120 : num_channels);
    const int sprite_channel_base = frames_version <= 7 ? 6 : (frames_version <= 13 ? 3 : 0);
    const int frame_size = main_channels_size + displayed_channels * sprite_record_size;
    const long long total_size_long = static_cast<long long>(frame_count) * static_cast<long long>(frame_size);
    if (frame_count <= 0 || frame_size <= 0 || total_size_long <= 0 || total_size_long > 50'000'000LL) {
        return ScoreChunk::ScoreFrameData::empty();
    }

    const auto total_size = static_cast<std::size_t>(total_size_long);
    std::vector<std::uint8_t> channel_data(total_size, 0);

    int frame_index = 0;
    while (!reader.eof() && frame_index < frame_count) {
        if (reader.bytesLeft() < 2) {
            break;
        }

        const int length = reader.readU16();
        if (length == 0) {
            break;
        }

        if (frame_index > 0) {
            const auto prev_offset = static_cast<std::size_t>(frame_index - 1) * static_cast<std::size_t>(frame_size);
            const auto curr_offset = static_cast<std::size_t>(frame_index) * static_cast<std::size_t>(frame_size);
            std::copy_n(channel_data.begin() + static_cast<std::ptrdiff_t>(prev_offset),
                        static_cast<std::size_t>(frame_size),
                        channel_data.begin() + static_cast<std::ptrdiff_t>(curr_offset));
        }

        const int frame_length = length - 2;
        if (frame_length > 0 && reader.bytesLeft() >= static_cast<std::size_t>(frame_length)) {
            io::BinaryReader frame_reader(reader.readBytes(static_cast<std::size_t>(frame_length)), io::ByteOrder::BigEndian);
            while (!frame_reader.eof() && frame_reader.bytesLeft() >= 4) {
                const int channel_size = frame_reader.readU16();
                const int channel_offset = frame_reader.readU16();
                if (channel_size <= 0 || frame_reader.bytesLeft() < static_cast<std::size_t>(channel_size)) {
                    break;
                }

                auto channel_delta = frame_reader.readBytes(static_cast<std::size_t>(channel_size));
                const auto frame_offset = static_cast<std::size_t>(frame_index) * static_cast<std::size_t>(frame_size);
                const auto dest_offset = frame_offset + static_cast<std::size_t>(channel_offset);
                const auto end_offset = dest_offset + static_cast<std::size_t>(channel_size);
                if (end_offset <= channel_data.size()) {
                    std::copy(channel_delta.begin(),
                              channel_delta.end(),
                              channel_data.begin() + static_cast<std::ptrdiff_t>(dest_offset));
                }
            }
        }
        ++frame_index;
    }

    const bool has_legacy_main_channels = main_channels_size > 0;
    const bool is_d5 = frames_version <= 7;
    const bool is_d6 = frames_version > 7 && frames_version <= 13;
    std::vector<ScoreChunk::FrameChannelEntry> frame_channel_entries;
    std::vector<ScoreChunk::TempoChannelData> tempo_channel_entries;
    std::vector<ScoreChunk::PaletteChannelData> palette_channel_entries;

    for (int frame = 0; frame < frame_count; ++frame) {
        const auto frame_start = static_cast<std::size_t>(frame) * static_cast<std::size_t>(frame_size);

        if (has_legacy_main_channels) {
            if (frame_start + 22 <= channel_data.size()) {
                const int tempo_offset = is_d6 ? 24 + 6 : 21;
                const int tempo = channel_data[frame_start + static_cast<std::size_t>(tempo_offset)] & 0xFF;
                if (tempo > 0) {
                    tempo_channel_entries.push_back(ScoreChunk::TempoChannelData{frame, tempo});
                }
            }

            if (is_d6 && frame_start + 124 <= channel_data.size()) {
                const auto palette_offset = frame_start + 120U;
                const auto cast_lib = static_cast<std::int16_t>(
                    (channel_data[palette_offset] << 8) | channel_data[palette_offset + 1]);
                const auto member = static_cast<std::int16_t>(
                    (channel_data[palette_offset + 2] << 8) | channel_data[palette_offset + 3]);
                if (member != 0) {
                    palette_channel_entries.push_back(ScoreChunk::PaletteChannelData{frame, cast_lib, member});
                }
            }

            const int num_sprites = displayed_channels;
            for (int sprite = 0; sprite < num_sprites; ++sprite) {
                const auto pos = frame_start + static_cast<std::size_t>(main_channels_size + sprite * sprite_record_size);
                if (pos + 24 > channel_data.size()) {
                    break;
                }
                auto channel = readChannelDataAt(channel_data, pos, sprite_record_size);
                if (!channel.isEmpty()) {
                    frame_channel_entries.push_back(ScoreChunk::FrameChannelEntry{
                        id::FrameIndex(frame),
                        id::ChannelId(sprite + sprite_channel_base),
                        channel});
                }
            }
        } else {
            for (int channel_index = 0; channel_index < num_channels; ++channel_index) {
                const auto pos = frame_start + static_cast<std::size_t>(channel_index * sprite_record_size);
                if (pos + static_cast<std::size_t>(sprite_record_size) > channel_data.size()) {
                    break;
                }

                if (channel_index == 4) {
                    if (pos + 7 <= channel_data.size()) {
                        const int tempo = channel_data[pos + 6] & 0xFF;
                        if (tempo > 0 && tempo <= 120) {
                            tempo_channel_entries.push_back(ScoreChunk::TempoChannelData{frame, tempo});
                        }
                    }
                } else if (channel_index == 5) {
                    if (pos + 4 <= channel_data.size()) {
                        const auto cast_lib = static_cast<std::int16_t>((channel_data[pos] << 8) | channel_data[pos + 1]);
                        const auto member = static_cast<std::int16_t>((channel_data[pos + 2] << 8) | channel_data[pos + 3]);
                        if (member != 0) {
                            palette_channel_entries.push_back(ScoreChunk::PaletteChannelData{frame, cast_lib, member});
                        }
                    }
                } else if (pos + 24 <= channel_data.size()) {
                    auto channel = readChannelDataAt(channel_data, pos, sprite_record_size);
                    if (!channel.isEmpty()) {
                        frame_channel_entries.push_back(ScoreChunk::FrameChannelEntry{id::FrameIndex(frame), id::ChannelId(channel_index), channel});
                    }
                }
            }
        }
    }

    return ScoreChunk::ScoreFrameData{
        header,
        std::move(channel_data),
        std::move(frame_channel_entries),
        std::move(tempo_channel_entries),
        std::move(palette_channel_entries)
    };
}

bool isPlausiblePrimaryInterval(const ScoreChunk::FrameIntervalPrimary& primary,
                                const ScoreChunk::FrameDataHeader& frame_header) {
    if (primary.start_frame < 1 || primary.end_frame < primary.start_frame || primary.channel_index < 0) {
        return false;
    }
    if (frame_header.frame_count > 0 && primary.end_frame > frame_header.frame_count) {
        return false;
    }
    if (frame_header.num_channels > 0 && primary.channel_index >= frame_header.num_channels) {
        return false;
    }
    return true;
}

std::vector<ScoreChunk::FrameInterval> parseFrameIntervals(const std::vector<std::vector<std::uint8_t>>& entries,
                                                           const ScoreChunk::FrameDataHeader& frame_header) {
    std::vector<ScoreChunk::FrameInterval> results;
    std::size_t index = 2;

    while (index < entries.size()) {
        const auto& entry_bytes = entries[index];
        if (entry_bytes.empty()) {
            ++index;
            continue;
        }

        if (entry_bytes.size() >= 44 && entry_bytes.size() <= 48) {
            io::BinaryReader reader(entry_bytes, io::ByteOrder::BigEndian);
            auto primary = readPrimaryInterval(reader);
            if (!isPlausiblePrimaryInterval(primary, frame_header)) {
                ++index;
                continue;
            }

            std::vector<ScoreChunk::FrameIntervalSecondary> secondaries;
            std::size_t next = index + 1;
            while (next < entries.size()) {
                const auto next_size = entries[next].size();
                if (next_size < 8 || next_size % 8 != 0) {
                    break;
                }

                const auto behavior_count = next_size / 8;
                io::BinaryReader secondary_reader(entries[next], io::ByteOrder::BigEndian);
                bool found_valid_behavior = false;
                for (std::size_t behavior = 0; behavior < behavior_count; ++behavior) {
                    auto secondary = readSecondaryInterval(secondary_reader);
                    if (secondary.cast_lib > 0 && secondary.cast_member > 0) {
                        secondaries.push_back(secondary);
                        found_valid_behavior = true;
                    }
                }

                if (!found_valid_behavior) {
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

ScoreChunk createEmptyScore(const moviesource::MovieSource* file, id::ChunkId id) {
    return ScoreChunk(file, id, ScoreChunk::Header{0, 0, 0, 0, 0, 0}, {}, ScoreChunk::ScoreFrameData::empty(), {});
}

} // namespace

ScoreChunk::ChannelData ScoreChunk::ChannelData::empty() {
    return ChannelData{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
}

ScoreChunk::ChannelData ScoreChunk::ChannelData::read(io::BinaryReader& reader, int sprite_record_size) {
    int sprite_type = reader.readU8();
    const int ink_byte = reader.readU8();
    const int ink = ink_byte & 0x3F;
    const int trails = (ink_byte >> 6) & 0x1;
    const int stretch = (ink_byte >> 7) & 0x1;
    const int fore_color = reader.readU8();
    const int back_color = reader.readU8();
    const int cast_lib = reader.readU16();
    const int cast_member = reader.readU16();
    const int unk1 = reader.readU16();
    const int unk2 = reader.readU16();
    const int pos_y = reader.readI16();
    const int pos_x = reader.readI16();
    const int height = reader.readU16();
    const int width = reader.readU16();

    int color_flag = 0;
    int blend_byte = 0;
    int thickness_flags = 0;
    int fore_color_g = 0;
    int back_color_g = 0;
    int fore_color_b = 0;
    int back_color_b = 0;

    if (sprite_record_size >= 24) {
        const int unk3 = reader.readU8();
        color_flag = (unk3 & 0xF0) >> 4;
        blend_byte = reader.readU8();
        thickness_flags = reader.readU8();
        (void)reader.readU8();
    }

    if (sprite_record_size >= 28) {
        fore_color_g = reader.readU8();
        back_color_g = reader.readU8();
        fore_color_b = reader.readU8();
        back_color_b = reader.readU8();
    }

    return ChannelData{sprite_type, ink, trails, stretch, fore_color, back_color, cast_lib, cast_member, unk1, unk2,
                       pos_y, pos_x, height, width, color_flag, blend_byte, thickness_flags,
                       fore_color_g, back_color_g, fore_color_b, back_color_b};
}

bool ScoreChunk::ChannelData::isEmpty() const {
    if (width <= 0 || height <= 0) {
        return true;
    }
    return sprite_type == 0 && ink == 0 && fore_color == 0 && back_color == 0 &&
           cast_lib == 0 && cast_member == 0 && pos_y == 0 && pos_x == 0 &&
           height == 0 && width == 0;
}

int ScoreChunk::ChannelData::resolvedForeColor() const {
    if ((color_flag & 0x1) != 0) {
        return (fore_color << 16) | (fore_color_g << 8) | fore_color_b;
    }
    return fore_color;
}

int ScoreChunk::ChannelData::resolvedBackColor() const {
    if ((color_flag & 0x2) != 0) {
        return (back_color << 16) | (back_color_g << 8) | back_color_b;
    }
    return back_color;
}

bool ScoreChunk::ChannelData::isForeColorRGB() const { return (color_flag & 0x1) != 0; }
bool ScoreChunk::ChannelData::isBackColorRGB() const { return (color_flag & 0x2) != 0; }
bool ScoreChunk::ChannelData::isFlipH() const { return (thickness_flags & 0x20) != 0; }
bool ScoreChunk::ChannelData::isFlipV() const { return (thickness_flags & 0x40) != 0; }

std::optional<id::CastLibId> ScoreChunk::ChannelData::castLibId() const {
    if (cast_lib > 0) {
        return id::CastLibId(cast_lib);
    }
    return std::nullopt;
}

std::optional<id::MemberId> ScoreChunk::ChannelData::memberId() const {
    if (cast_member > 0) {
        return id::MemberId(cast_member);
    }
    return std::nullopt;
}

ScoreChunk::ScoreFrameData ScoreChunk::ScoreFrameData::empty() {
    return ScoreFrameData{FrameDataHeader{0, 24, 0, 0}, {}, {}, {}, {}};
}

id::FrameId ScoreChunk::FrameIntervalPrimary::startFrameId() const {
    return id::FrameId(std::max(1, start_frame));
}

id::FrameId ScoreChunk::FrameIntervalPrimary::endFrameId() const {
    return id::FrameId(std::max(1, end_frame));
}

id::ChannelId ScoreChunk::FrameIntervalPrimary::channelId() const {
    return id::ChannelId(channel_index);
}

std::optional<id::CastLibId> ScoreChunk::FrameIntervalSecondary::castLibId() const {
    if (cast_lib > 0) {
        return id::CastLibId(cast_lib);
    }
    return std::nullopt;
}

std::optional<id::MemberId> ScoreChunk::FrameIntervalSecondary::memberId() const {
    if (cast_member > 0) {
        return id::MemberId(cast_member);
    }
    return std::nullopt;
}

ScoreChunk::ScoreChunk(const moviesource::MovieSource* file,
                       id::ChunkId id,
                       Header header,
                       std::vector<std::vector<std::uint8_t>> entries,
                       ScoreFrameData frame_data,
                       std::vector<FrameInterval> frame_intervals)
    : file_(file),
      id_(id),
      header_(header),
      entries_(std::move(entries)),
      frame_data_(std::move(frame_data)),
      frame_intervals_(std::move(frame_intervals)) {}

const moviesource::MovieSource* ScoreChunk::file() const { return file_; }
format::ChunkType ScoreChunk::type() const { return format::ChunkType::VWSC; }
id::ChunkId ScoreChunk::id() const { return id_; }
const ScoreChunk::Header& ScoreChunk::header() const { return header_; }
const std::vector<std::vector<std::uint8_t>>& ScoreChunk::entries() const { return entries_; }
const ScoreChunk::ScoreFrameData& ScoreChunk::frameData() const { return frame_data_; }
const std::vector<ScoreChunk::FrameInterval>& ScoreChunk::frameIntervals() const { return frame_intervals_; }
int ScoreChunk::getFrameCount() const { return frame_data_.header.frame_count; }
int ScoreChunk::getChannelCount() const { return frame_data_.header.num_channels; }
const std::vector<std::uint8_t>& ScoreChunk::getRawChannelData() const { return frame_data_.decompressed_data; }
int ScoreChunk::getSpriteRecordSize() const { return frame_data_.header.sprite_record_size; }

int ScoreChunk::getFrameTempo(int frame) const {
    int result = -1;
    for (const auto& tempo : frame_data_.tempo_channel_data) {
        if (tempo.frame_index <= frame) {
            result = tempo.tempo;
        } else {
            break;
        }
    }
    return result;
}

std::optional<ScoreChunk::PaletteChannelData> ScoreChunk::getFramePalette(int frame) const {
    std::optional<PaletteChannelData> result;
    for (const auto& palette : frame_data_.palette_channel_data) {
        if (palette.frame_index <= frame) {
            result = palette;
        } else {
            break;
        }
    }
    return result;
}

ScoreChunk ScoreChunk::read(const moviesource::MovieSource* file, io::BinaryReader& reader, id::ChunkId id, int version) {
    (void)version;
    ScopedByteOrder order(reader, io::ByteOrder::BigEndian);

    if (reader.bytesLeft() < 24) {
        return createEmptyScore(file, id);
    }

    const auto header = readHeader(reader);
    if (header.entry_count < 0 || header.entry_count > k_max_score_entry_count) {
        return createEmptyScore(file, id);
    }

    const auto offset_count = static_cast<std::size_t>(header.entry_count) + 1U;
    if (offset_count > reader.bytesLeft() / 4U) {
        return createEmptyScore(file, id);
    }

    std::vector<int> offsets;
    offsets.reserve(offset_count);
    for (int index = 0; index <= header.entry_count; ++index) {
        if (reader.bytesLeft() < 4) {
            break;
        }
        offsets.push_back(reader.readI32());
    }

    if (offsets.size() != static_cast<std::size_t>(header.entry_count + 1)) {
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
    entries.reserve(static_cast<std::size_t>(header.entry_count));
    for (int index = 0; index < header.entry_count; ++index) {
        const int length = offsets[static_cast<std::size_t>(index + 1)] - offsets[static_cast<std::size_t>(index)];
        if (length > 0 && reader.bytesLeft() >= static_cast<std::size_t>(length)) {
            entries.push_back(reader.readBytes(static_cast<std::size_t>(length)));
        } else {
            entries.emplace_back();
        }
    }

    auto frame_data = ScoreFrameData::empty();
    if (!entries.empty() && !entries[0].empty()) {
        frame_data = parseFrameData(entries[0]);
    }

    auto frame_intervals = parseFrameIntervals(entries, frame_data.header);
    return ScoreChunk(file, id, header, std::move(entries), std::move(frame_data), std::move(frame_intervals));
}

} // namespace libreshockwave::chunks
