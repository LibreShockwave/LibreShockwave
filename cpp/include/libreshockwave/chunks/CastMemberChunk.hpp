#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "libreshockwave/cast/MemberType.hpp"
#include "libreshockwave/chunks/Chunk.hpp"

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
    CastMemberChunk(const DirectorFile* file,
                    id::ChunkId id,
                    cast::MemberType memberType,
                    int infoLen,
                    int dataLen,
                    std::vector<std::uint8_t> info,
                    std::vector<std::uint8_t> specificData,
                    std::string name,
                    int scriptId,
                    int regPointX,
                    int regPointY);

    [[nodiscard]] const DirectorFile* file() const override;
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

    [[nodiscard]] static CastMemberChunk read(const DirectorFile* file, io::BinaryReader& reader, id::ChunkId id, int version);

private:
    const DirectorFile* file_;
    id::ChunkId id_;
    cast::MemberType memberType_;
    int infoLen_;
    int dataLen_;
    std::vector<std::uint8_t> info_;
    std::vector<std::uint8_t> specificData_;
    std::string name_;
    int scriptId_;
    int regPointX_;
    int regPointY_;
};

} // namespace libreshockwave::chunks
