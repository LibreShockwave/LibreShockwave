#include "libreshockwave/chunks/CastMemberChunk.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "libreshockwave/cast/BitmapInfo.hpp"
#include "libreshockwave/io/BinaryReader.hpp"

namespace libreshockwave::chunks {
namespace {

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

std::string readNameFromInfo(const std::vector<std::uint8_t>& info, int& scriptId) {
    if (info.size() < 20) {
        return "";
    }

    io::BinaryReader reader(info, io::ByteOrder::BigEndian);
    const int dataOffset = reader.readI32();
    (void)reader.readI32();
    (void)reader.readI32();
    (void)reader.readI32();
    scriptId = reader.readI32();

    if (dataOffset <= 0 || static_cast<std::size_t>(dataOffset) >= info.size()) {
        return "";
    }

    reader.seek(static_cast<std::size_t>(dataOffset));
    if (reader.bytesLeft() < 2) {
        return "";
    }

    const int offsetTableLen = reader.readU16();
    if (offsetTableLen <= 0 || offsetTableLen > 10000 ||
        reader.bytesLeft() < static_cast<std::size_t>(offsetTableLen) * 4U + 4U) {
        return "";
    }

    std::vector<int> offsets;
    offsets.reserve(static_cast<std::size_t>(offsetTableLen));
    for (int index = 0; index < offsetTableLen; ++index) {
        offsets.push_back(reader.readI32());
    }

    const int itemsLen = reader.readI32();
    const auto itemsStart = reader.position();
    if (offsets.size() <= 1) {
        return "";
    }

    const int nameOffset = offsets[1];
    const int nameEnd = offsets.size() > 2 ? offsets[2] : itemsLen;
    const int nameLen = nameEnd - nameOffset;
    if (nameOffset < 0 || nameLen <= 0) {
        return "";
    }

    const auto nameStart = itemsStart + static_cast<std::size_t>(nameOffset);
    if (nameStart >= info.size()) {
        return "";
    }

    reader.seek(nameStart);
    if (reader.bytesLeft() < 1) {
        return "";
    }

    const int pascalLen = reader.readU8();
    if (pascalLen <= 0 || static_cast<std::size_t>(pascalLen) > reader.bytesLeft()) {
        return "";
    }
    return reader.readStringMacRoman(static_cast<std::size_t>(pascalLen));
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

CastMemberChunk::CastMemberChunk(const DirectorFile* file,
                                 id::ChunkId id,
                                 cast::MemberType memberType,
                                 int infoLen,
                                 int dataLen,
                                 std::vector<std::uint8_t> info,
                                 std::vector<std::uint8_t> specificData,
                                 std::string name,
                                 int scriptId,
                                 int regPointX,
                                 int regPointY)
    : file_(file),
      id_(id),
      memberType_(memberType),
      infoLen_(infoLen),
      dataLen_(dataLen),
      info_(std::move(info)),
      specificData_(std::move(specificData)),
      name_(std::move(name)),
      scriptId_(scriptId),
      regPointX_(regPointX),
      regPointY_(regPointY) {}

const DirectorFile* CastMemberChunk::file() const { return file_; }
format::ChunkType CastMemberChunk::type() const { return format::ChunkType::CASt; }
id::ChunkId CastMemberChunk::id() const { return id_; }
cast::MemberType CastMemberChunk::memberType() const { return memberType_; }
int CastMemberChunk::infoLen() const { return infoLen_; }
int CastMemberChunk::dataLen() const { return dataLen_; }
const std::vector<std::uint8_t>& CastMemberChunk::info() const { return info_; }
const std::vector<std::uint8_t>& CastMemberChunk::specificData() const { return specificData_; }
const std::string& CastMemberChunk::name() const { return name_; }
int CastMemberChunk::scriptId() const { return scriptId_; }
int CastMemberChunk::regPointX() const { return regPointX_; }
int CastMemberChunk::regPointY() const { return regPointY_; }

bool CastMemberChunk::isBitmap() const { return memberType_ == cast::MemberType::Bitmap; }
bool CastMemberChunk::isScript() const { return memberType_ == cast::MemberType::Script; }
bool CastMemberChunk::isText() const { return memberType_ == cast::MemberType::Text || memberType_ == cast::MemberType::RichText; }
bool CastMemberChunk::isSound() const { return memberType_ == cast::MemberType::Sound; }
bool CastMemberChunk::isShockwave3D() const { return memberType_ == cast::MemberType::Shockwave3D; }

bool CastMemberChunk::isTextXtra() const {
    return memberType_ == cast::MemberType::Xtra &&
           specificData_.size() >= 8 &&
           specificData_[4] == static_cast<std::uint8_t>('t') &&
           specificData_[5] == static_cast<std::uint8_t>('e') &&
           specificData_[6] == static_cast<std::uint8_t>('x') &&
           specificData_[7] == static_cast<std::uint8_t>('t');
}

std::optional<CastMemberScriptType> CastMemberChunk::getScriptType() const {
    if (!isScript() || specificData_.size() < 2) {
        return std::nullopt;
    }
    const int code = (static_cast<int>(specificData_[0]) << 8) | static_cast<int>(specificData_[1]);
    return castMemberScriptTypeFromCode(code);
}

CastMemberChunk CastMemberChunk::read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version) {
    ScopedByteOrder order(reader, io::ByteOrder::BigEndian);

    cast::MemberType memberType = cast::MemberType::Null;
    int infoLen = 0;
    int dataLen = 0;
    std::vector<std::uint8_t> info;
    std::vector<std::uint8_t> specificData;
    std::string name;
    int scriptId = 0;
    int regPointX = 0;
    int regPointY = 0;

    if (version >= 0x4B1) {
        if (reader.bytesLeft() >= 12) {
            memberType = cast::memberTypeFromCode(reader.readI32());
            infoLen = reader.readI32();
            dataLen = reader.readI32();
            if (infoLen > 0 && reader.bytesLeft() >= static_cast<std::size_t>(infoLen)) {
                info = reader.readBytes(static_cast<std::size_t>(infoLen));
            }
            if (dataLen > 0 && reader.bytesLeft() >= static_cast<std::size_t>(dataLen)) {
                specificData = reader.readBytes(static_cast<std::size_t>(dataLen));
            }
        }
    } else if (reader.bytesLeft() >= 7) {
        const int specificDataLen = reader.readU16();
        infoLen = reader.readI32();
        memberType = cast::memberTypeFromCode(reader.readU8());

        int specificDataLeft = specificDataLen - 1;
        if (specificDataLeft > 0 && reader.bytesLeft() > 0) {
            reader.skip(1);
            --specificDataLeft;
        }
        dataLen = specificDataLeft;

        if (specificDataLeft > 0 && reader.bytesLeft() >= static_cast<std::size_t>(specificDataLeft)) {
            specificData = reader.readBytes(static_cast<std::size_t>(specificDataLeft));
        }
        if (infoLen > 0 && reader.bytesLeft() >= static_cast<std::size_t>(infoLen)) {
            info = reader.readBytes(static_cast<std::size_t>(infoLen));
        }
    }

    if (memberType == cast::MemberType::Bitmap && specificData.size() >= 22) {
        const auto bitmapInfo = cast::BitmapInfo::parse(specificData, version);
        regPointX = bitmapInfo.regX;
        regPointY = bitmapInfo.regY;
    }

    name = readNameFromInfo(info, scriptId);

    return CastMemberChunk(file, id, memberType, infoLen, dataLen, std::move(info), std::move(specificData),
                           std::move(name), scriptId, regPointX, regPointY);
}

} // namespace libreshockwave::chunks
