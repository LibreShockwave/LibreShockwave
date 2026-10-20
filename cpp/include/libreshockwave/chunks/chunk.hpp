#pragma once

#include "libreshockwave/format/chunk_type.hpp"
#include "libreshockwave/id/ids.hpp"

namespace libreshockwave {
class DirectorFile;
}

namespace libreshockwave::chunks {

class Chunk {
public:
    virtual ~Chunk() = default;

    [[nodiscard]] virtual const DirectorFile* file() const = 0;
    [[nodiscard]] virtual format::ChunkType type() const = 0;
    [[nodiscard]] virtual id::ChunkId id() const = 0;
};

} // namespace libreshockwave::chunks
