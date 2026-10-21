#include "libreshockwave/chunks/cast_member_chunk.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "libreshockwave/cast/bitmap_info.hpp"
#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::chunks {
namespace {

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

std::string readNameFromInfo(const std::vector<std::uint8_t>& info, int& script_id) {
    if (info.size() < 20) {
        return "";
    }

    io::BinaryReader reader(info, io::ByteOrder::BigEndian);
    const int data_offset = reader.readI32();
    (void)reader.readI32();
    (void)reader.readI32();
    (void)reader.readI32();
    script_id = reader.readI32();

    if (data_offset <= 0 || static_cast<std::size_t>(data_offset) >= info.size()) {
        return "";
    }

    reader.seek(static_cast<std::size_t>(data_offset));
    if (reader.bytesLeft() < 2) {
        return "";
    }

    const int offset_table_len = reader.readU16();
    if (offset_table_len <= 0 || offset_table_len > 10000 ||
        reader.bytesLeft() < static_cast<std::size_t>(offset_table_len) * 4U + 4U) {
        return "";
    }

    std::vector<int> offsets;
    offsets.reserve(static_cast<std::size_t>(offset_table_len));
    for (int index = 0; index < offset_table_len; ++index) {
        offsets.push_back(reader.readI32());
    }

    const int items_len = reader.readI32();
    const auto items_start = reader.position();
    if (offsets.size() <= 1) {
        return "";
    }

    const int name_offset = offsets[1];
    const int name_end = offsets.size() > 2 ? offsets[2] : items_len;
    const int name_len = name_end - name_offset;
    if (name_offset < 0 || name_len <= 0) {
        return "";
    }

    const auto name_start = items_start + static_cast<std::size_t>(name_offset);
    if (name_start >= info.size()) {
        return "";
    }

    reader.seek(name_start);
    if (reader.bytesLeft() < 1) {
        return "";
    }

    const int pascal_len = reader.readU8();
    if (pascal_len <= 0 || static_cast<std::size_t>(pascal_len) > reader.bytesLeft()) {
        return "";
    }
    return reader.readStringMacRoman(static_cast<std::size_t>(pascal_len));
}

} // namespace

CastMemberScriptType castMemberScriptTypeFromCode(int code) {
    switch (code) {
        case 1: return CastMemberScriptType::Score;
        case 2: return CastMemberScriptType::Behavior;
        case 3: return CastMemberScriptType::MovieScript;
        case 7: return CastMemberScriptType::Parent;
        default: return CastMemberScriptType::Unknown;
    }
}

CastMemberChunk::CastMemberChunk(const moviesource::MovieSource* file,
                                 id::ChunkId id,
                                 cast::MemberType member_type,
                                 int info_len,
                                 int data_len,
                                 std::vector<std::uint8_t> info,
                                 std::vector<std::uint8_t> specific_data,
                                 std::string name,
                                 int script_id,
                                 int reg_point_x,
                                 int reg_point_y)
    : file_(file),
      id_(id),
      member_type_(member_type),
      info_len_(info_len),
      data_len_(data_len),
      info_(std::move(info)),
      specific_data_(std::move(specific_data)),
      name_(std::move(name)),
      script_id_(script_id),
      reg_point_x_(reg_point_x),
      reg_point_y_(reg_point_y) {}

const moviesource::MovieSource* CastMemberChunk::file() const { return file_; }
format::ChunkType CastMemberChunk::type() const { return format::ChunkType::CASt; }
id::ChunkId CastMemberChunk::id() const { return id_; }
cast::MemberType CastMemberChunk::memberType() const { return member_type_; }
int CastMemberChunk::infoLen() const { return info_len_; }
int CastMemberChunk::dataLen() const { return data_len_; }
const std::vector<std::uint8_t>& CastMemberChunk::info() const { return info_; }
const std::vector<std::uint8_t>& CastMemberChunk::specificData() const { return specific_data_; }
const std::string& CastMemberChunk::name() const { return name_; }
int CastMemberChunk::scriptId() const { return script_id_; }
int CastMemberChunk::regPointX() const { return reg_point_x_; }
int CastMemberChunk::regPointY() const { return reg_point_y_; }

bool CastMemberChunk::isBitmap() const { return member_type_ == cast::MemberType::Bitmap; }
bool CastMemberChunk::isScript() const { return member_type_ == cast::MemberType::Script; }
bool CastMemberChunk::isText() const { return member_type_ == cast::MemberType::Text || member_type_ == cast::MemberType::RichText; }
bool CastMemberChunk::isSound() const { return member_type_ == cast::MemberType::Sound; }
bool CastMemberChunk::isShockwave3D() const { return member_type_ == cast::MemberType::Shockwave3D; }

bool CastMemberChunk::isTextXtra() const {
    return member_type_ == cast::MemberType::Xtra &&
           specific_data_.size() >= 8 &&
           specific_data_[4] == static_cast<std::uint8_t>('t') &&
           specific_data_[5] == static_cast<std::uint8_t>('e') &&
           specific_data_[6] == static_cast<std::uint8_t>('x') &&
           specific_data_[7] == static_cast<std::uint8_t>('t');
}

std::optional<CastMemberScriptType> CastMemberChunk::getScriptType() const {
    if (!isScript() || specific_data_.size() < 2) {
        return std::nullopt;
    }
    const int code = (static_cast<int>(specific_data_[0]) << 8) | static_cast<int>(specific_data_[1]);
    return castMemberScriptTypeFromCode(code);
}

CastMemberChunk CastMemberChunk::read(const moviesource::MovieSource* file, io::BinaryReader& reader, id::ChunkId id, int version) {
    ScopedByteOrder order(reader, io::ByteOrder::BigEndian);

    cast::MemberType member_type = cast::MemberType::Null;
    int info_len = 0;
    int data_len = 0;
    std::vector<std::uint8_t> info;
    std::vector<std::uint8_t> specific_data;
    std::string name;
    int script_id = 0;
    int reg_point_x = 0;
    int reg_point_y = 0;

    if (version >= 0x4B1) {
        if (reader.bytesLeft() >= 12) {
            member_type = cast::memberTypeFromCode(reader.readI32());
            info_len = reader.readI32();
            data_len = reader.readI32();
            if (info_len > 0 && reader.bytesLeft() >= static_cast<std::size_t>(info_len)) {
                info = reader.readBytes(static_cast<std::size_t>(info_len));
            }
            if (data_len > 0 && reader.bytesLeft() >= static_cast<std::size_t>(data_len)) {
                specific_data = reader.readBytes(static_cast<std::size_t>(data_len));
            }
        }
    } else if (reader.bytesLeft() >= 7) {
        const int specific_data_len = reader.readU16();
        info_len = reader.readI32();
        member_type = cast::memberTypeFromCode(reader.readU8());

        int specific_data_left = specific_data_len - 1;
        if (specific_data_left > 0 && reader.bytesLeft() > 0) {
            reader.skip(1);
            --specific_data_left;
        }
        data_len = specific_data_left;

        if (specific_data_left > 0 && reader.bytesLeft() >= static_cast<std::size_t>(specific_data_left)) {
            specific_data = reader.readBytes(static_cast<std::size_t>(specific_data_left));
        }
        if (info_len > 0 && reader.bytesLeft() >= static_cast<std::size_t>(info_len)) {
            info = reader.readBytes(static_cast<std::size_t>(info_len));
        }
    }

    if (member_type == cast::MemberType::Bitmap && specific_data.size() >= 22) {
        const auto bitmap_info = cast::BitmapInfo::parse(specific_data, version);
        reg_point_x = bitmap_info.reg_x;
        reg_point_y = bitmap_info.reg_y;
    }

    name = readNameFromInfo(info, script_id);

    return CastMemberChunk(file, id, member_type, info_len, data_len, std::move(info), std::move(specific_data),
                           std::move(name), script_id, reg_point_x, reg_point_y);
}

} // namespace libreshockwave::chunks
