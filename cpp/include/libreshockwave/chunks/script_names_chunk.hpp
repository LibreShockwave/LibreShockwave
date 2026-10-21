#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/chunks/chunk.hpp"

namespace libreshockwave::io {
class BinaryReader;
}

namespace libreshockwave::chunks {

class ScriptNamesChunk final : public Chunk {
public:
    ScriptNamesChunk(const moviesource::MovieSource* file, id::ChunkId id, std::vector<std::string> names);

    [[nodiscard]] const moviesource::MovieSource* file() const override;
    [[nodiscard]] format::ChunkType type() const override;
    [[nodiscard]] id::ChunkId id() const override;
    [[nodiscard]] const std::vector<std::string>& names() const;
    [[nodiscard]] std::string getName(int index) const;
    [[nodiscard]] int findName(std::string_view name) const;

    [[nodiscard]] static ScriptNamesChunk read(const moviesource::MovieSource* file, io::BinaryReader& reader, id::ChunkId id, int version);

private:
    const moviesource::MovieSource* file_;
    id::ChunkId id_;
    std::vector<std::string> names_;
};

} // namespace libreshockwave::chunks
