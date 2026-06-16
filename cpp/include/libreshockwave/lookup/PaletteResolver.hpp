#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "libreshockwave/id/Ids.hpp"

namespace libreshockwave::bitmap {
class Palette;
}

namespace libreshockwave::chunks {
class CastChunk;
class CastListChunk;
class CastMemberChunk;
class Chunk;
class ConfigChunk;
class KeyTableChunk;
class PaletteChunk;
}

namespace libreshockwave::lookup {

class PaletteResolver {
public:
    using ChunkLookup = std::function<std::shared_ptr<chunks::Chunk>(id::ChunkId)>;

    PaletteResolver(std::vector<std::shared_ptr<chunks::CastChunk>> casts,
                    std::vector<std::shared_ptr<chunks::CastMemberChunk>> castMembers,
                    std::vector<std::shared_ptr<chunks::PaletteChunk>> palettes,
                    std::shared_ptr<chunks::CastListChunk> castList,
                    std::shared_ptr<chunks::ConfigChunk> config,
                    std::shared_ptr<chunks::KeyTableChunk> keyTable,
                    ChunkLookup chunkLookup);

    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolve(int paletteId) const;
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolveExact(int paletteId) const;

private:
    [[nodiscard]] int getMinMember(int castIndex) const;
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolveFromChunkId(id::ChunkId chunkId) const;
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> paletteFromChunk(
        const std::shared_ptr<chunks::PaletteChunk>& chunk,
        const std::string& name) const;
    [[nodiscard]] static std::shared_ptr<const bitmap::Palette> builtInPalette(int paletteId);

    std::vector<std::shared_ptr<chunks::CastChunk>> casts_;
    std::vector<std::shared_ptr<chunks::CastMemberChunk>> castMembers_;
    std::vector<std::shared_ptr<chunks::PaletteChunk>> palettes_;
    std::shared_ptr<chunks::CastListChunk> castList_;
    std::shared_ptr<chunks::ConfigChunk> config_;
    std::shared_ptr<chunks::KeyTableChunk> keyTable_;
    ChunkLookup chunkLookup_;
    mutable std::unordered_map<int, std::shared_ptr<const bitmap::Palette>> paletteCache_;
};

} // namespace libreshockwave::lookup
