#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "libreshockwave/cast/member_type.hpp"
#include "libreshockwave/chunks/chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

enum class CastMemberScriptType {
    Score = 1,
    Behavior = 2,
    MovieScript = 3,
    Parent = 7,
    Unknown = -1
};

[[nodiscard]] CastMemberScriptType castMemberScriptTypeFromCode(int code);

class CastMemberChunk final : public Chunk {
public:
    CastMemberChunk(const moviesource::MovieSource* file,
                    id::ChunkId id,
                    cast::MemberType member_type,
                    int info_len,
                    int data_len,
                    std::vector<std::uint8_t> info,
                    std::vector<std::uint8_t> specific_data,
                    std::string name,
                    int script_id,
                    int reg_point_x,
                    int reg_point_y);

    [[nodiscard]] const moviesource::MovieSource* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] cast::MemberType memberType() const;
    [[nodiscard]] int infoLen() const;
    [[nodiscard]] int dataLen() const;
    [[nodiscard]] const std::vector<std::uint8_t>& info() const;
    [[nodiscard]] const std::vector<std::uint8_t>& specificData() const;
    [[nodiscard]] const std::string& name() const;
    [[nodiscard]] int scriptId() const;
    [[nodiscard]] int regPointX() const;
    [[nodiscard]] int regPointY() const;

    [[nodiscard]] bool isBitmap() const;
    [[nodiscard]] bool isScript() const;
    [[nodiscard]] bool isText() const;
    [[nodiscard]] bool isSound() const;
    [[nodiscard]] bool isShockwave3D() const;
    [[nodiscard]] bool isTextXtra() const;
    [[nodiscard]] std::optional<CastMemberScriptType> getScriptType() const;

    [[nodiscard]] static CastMemberChunk read(const moviesource::MovieSource* file, io::BinaryReader& reader, id::ChunkId id, int version);

private:
    const moviesource::MovieSource* file_;
    id::ChunkId id_;
    cast::MemberType member_type_;
    int info_len_;
    int data_len_;
    std::vector<std::uint8_t> info_;
    std::vector<std::uint8_t> specific_data_;
    std::string name_;
    int script_id_;
    int reg_point_x_;
    int reg_point_y_;
};

} // namespace libreshockwave::chunks
