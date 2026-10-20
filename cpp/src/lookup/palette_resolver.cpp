#include "libreshockwave/lookup/palette_resolver.hpp"

#include <string>
#include <utility>

#include "libreshockwave/bitmap/palette.hpp"
#include "libreshockwave/cast/member_type.hpp"
#include "libreshockwave/chunks/cast_chunk.hpp"
#include "libreshockwave/chunks/cast_list_chunk.hpp"
#include "libreshockwave/chunks/cast_member_chunk.hpp"
#include "libreshockwave/chunks/chunk.hpp"
#include "libreshockwave/chunks/config_chunk.hpp"
#include "libreshockwave/chunks/key_table_chunk.hpp"
#include "libreshockwave/chunks/palette_chunk.hpp"
#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::lookup {
namespace {

std::shared_ptr<const bitmap::Palette> borrowedPalette(const bitmap::Palette* palette) {
    if (palette == nullptr) {
        return nullptr;
    }
    return std::shared_ptr<const bitmap::Palette>(palette, [](const bitmap::Palette*) {});
}

} // namespace

PaletteResolver::PaletteResolver(std::vector<std::shared_ptr<chunks::CastChunk>> casts,
                                 std::vector<std::shared_ptr<chunks::CastMemberChunk>> cast_members,
                                 std::vector<std::shared_ptr<chunks::PaletteChunk>> palettes,
                                 std::shared_ptr<chunks::CastListChunk> cast_list,
                                 std::shared_ptr<chunks::ConfigChunk> config,
                                 std::shared_ptr<chunks::KeyTableChunk> key_table,
                                 ChunkLookup chunk_lookup)
    : casts_(std::move(casts)),
      cast_members_(std::move(cast_members)),
      palettes_(std::move(palettes)),
      cast_list_(std::move(cast_list)),
      config_(std::move(config)),
      key_table_(std::move(key_table)),
      chunk_lookup_(std::move(chunk_lookup)) {}

std::shared_ptr<const bitmap::Palette> PaletteResolver::resolve(int palette_id) const {
    if (auto exact = resolveExact(palette_id)) {
        return exact;
    }

    for (const auto& palette : palettes_) {
        if (palette) {
            return paletteFromChunk(palette, "Custom Palette");
        }
    }

    return builtInPalette(bitmap::Palette::SYSTEM_MAC);
}

std::shared_ptr<const bitmap::Palette> PaletteResolver::resolveExact(int palette_id) const {
    if (palette_id < 0) {
        return builtInPalette(palette_id);
    }

    const int member_number = palette_id + 1;
    for (int cast_index = 0; cast_index < static_cast<int>(casts_.size()); ++cast_index) {
        const auto& cast = casts_[static_cast<std::size_t>(cast_index)];
        if (!cast) {
            continue;
        }

        const int index = member_number - getMinMember(cast_index);
        if (index < 0 || index >= static_cast<int>(cast->memberIds().size())) {
            continue;
        }

        const int raw_chunk_id = cast->memberIds()[static_cast<std::size_t>(index)];
        if (raw_chunk_id <= 0) {
            continue;
        }

        if (auto resolved = resolveFromChunkId(id::ChunkId(raw_chunk_id))) {
            return resolved;
        }
    }

    if (auto resolved = resolveFromChunkId(id::ChunkId(palette_id))) {
        return resolved;
    }

    for (const auto& palette : palettes_) {
        if (!palette) {
            continue;
        }
        const int chunk_id = palette->id().value();
        if (chunk_id == palette_id || chunk_id == palette_id + 1) {
            return paletteFromChunk(palette, "Custom Palette #" + std::to_string(chunk_id));
        }
    }

    // A file with cast libraries uses the bitmap's palette ID as a cast-member
    // reference.  Treating it as an ordinal in the global palette-member list
    // can silently select an unrelated palette when the cast member was
    // removed or the reference is stale.  The ordinal form is only useful for
    // synthetic/minimal files that have no cast-library table.
    if (casts_.empty()) {
        int palette_index = 0;
        for (const auto& member : cast_members_) {
            if (!member || member->memberType() != cast::MemberType::Palette) {
                continue;
            }
            if (palette_index == palette_id) {
                if (auto resolved = resolveFromChunkId(member->id())) {
                    return resolved;
                }
            }
            ++palette_index;
        }
    }

    return nullptr;
}

int PaletteResolver::getMinMember(int cast_index) const {
    int min_member = 1;
    if (cast_list_ && cast_index >= 0 && cast_index < static_cast<int>(cast_list_->entries().size())) {
        min_member = cast_list_->entries()[static_cast<std::size_t>(cast_index)].min_member;
    } else if (config_) {
        min_member = config_->minMember();
    }
    return min_member <= 0 ? 1 : min_member;
}

std::shared_ptr<const bitmap::Palette> PaletteResolver::resolveFromChunkId(id::ChunkId chunk_id) const {
    if (!key_table_) {
        return nullptr;
    }

    for (const auto& entry : key_table_->getEntriesForOwner(chunk_id)) {
        if (entry.fourcc != io::BinaryReader::fourCC("CLUT") &&
            entry.fourcc != io::BinaryReader::fourCC("TULC")) {
            continue;
        }

        std::shared_ptr<chunks::Chunk> chunk;
        if (chunk_lookup_) {
            chunk = chunk_lookup_(entry.section_id);
        }

        auto palette_chunk = std::dynamic_pointer_cast<chunks::PaletteChunk>(chunk);
        if (!palette_chunk) {
            for (const auto& candidate : palettes_) {
                if (candidate && candidate->id().value() == entry.section_id.value()) {
                    palette_chunk = candidate;
                    break;
                }
            }
        }
        if (palette_chunk) {
            return paletteFromChunk(palette_chunk, "Custom Palette");
        }
    }

    return nullptr;
}

std::shared_ptr<const bitmap::Palette> PaletteResolver::paletteFromChunk(
    const std::shared_ptr<chunks::PaletteChunk>& chunk,
    const std::string& name) const {
    if (!chunk) {
        return nullptr;
    }

    if (const auto found = palette_cache_.find(chunk->id().value()); found != palette_cache_.end()) {
        return found->second;
    }

    auto colors = chunk->colors();
    if (colors.size() == 256) {
        for (int index = 246; index < 256; ++index) {
            const auto mac_color = bitmap::Palette::systemMacPalette().getColor(index);
            auto& color = colors[static_cast<std::size_t>(index)];
            if ((color & 0xFFFFFFU) == 0 && (mac_color & 0xFFFFFFU) != 0) {
                color = mac_color;
            }
        }
    }

    auto palette = std::make_shared<bitmap::Palette>(std::move(colors), name);
    palette_cache_[chunk->id().value()] = palette;
    return palette;
}

std::shared_ptr<const bitmap::Palette> PaletteResolver::builtInPalette(int palette_id) {
    return borrowedPalette(&bitmap::Palette::builtIn(palette_id));
}

} // namespace libreshockwave::lookup
