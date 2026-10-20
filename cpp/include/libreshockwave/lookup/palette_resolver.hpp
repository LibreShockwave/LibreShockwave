#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "libreshockwave/id/ids.hpp"

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
                    std::vector<std::shared_ptr<chunks::CastMemberChunk>> cast_members,
                    std::vector<std::shared_ptr<chunks::PaletteChunk>> palettes,
                    std::shared_ptr<chunks::CastListChunk> cast_list,
                    std::shared_ptr<chunks::ConfigChunk> config,
                    std::shared_ptr<chunks::KeyTableChunk> key_table,
                    ChunkLookup chunk_lookup);

    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolve(int palette_id) const;
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolveExact(int palette_id) const;

private:
    [[nodiscard]] int getMinMember(int cast_index) const;
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolveFromChunkId(id::ChunkId chunk_id) const;
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> paletteFromChunk(
        const std::shared_ptr<chunks::PaletteChunk>& chunk,
        const std::string& name) const;
    [[nodiscard]] static std::shared_ptr<const bitmap::Palette> builtInPalette(int palette_id);

    std::vector<std::shared_ptr<chunks::CastChunk>> casts_;
    std::vector<std::shared_ptr<chunks::CastMemberChunk>> cast_members_;
    std::vector<std::shared_ptr<chunks::PaletteChunk>> palettes_;
    std::shared_ptr<chunks::CastListChunk> cast_list_;
    std::shared_ptr<chunks::ConfigChunk> config_;
    std::shared_ptr<chunks::KeyTableChunk> key_table_;
    ChunkLookup chunk_lookup_;
    mutable std::unordered_map<int, std::shared_ptr<const bitmap::Palette>> palette_cache_;
};

} // namespace libreshockwave::lookup
