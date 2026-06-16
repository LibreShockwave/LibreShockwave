#include "libreshockwave/lookup/PaletteResolver.hpp"

#include <string>
#include <utility>

#include "libreshockwave/bitmap/Palette.hpp"
#include "libreshockwave/cast/MemberType.hpp"
#include "libreshockwave/chunks/CastChunk.hpp"
#include "libreshockwave/chunks/CastListChunk.hpp"
#include "libreshockwave/chunks/CastMemberChunk.hpp"
#include "libreshockwave/chunks/Chunk.hpp"
#include "libreshockwave/chunks/ConfigChunk.hpp"
#include "libreshockwave/chunks/KeyTableChunk.hpp"
#include "libreshockwave/chunks/PaletteChunk.hpp"
#include "libreshockwave/io/BinaryReader.hpp"

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
                                 std::vector<std::shared_ptr<chunks::CastMemberChunk>> castMembers,
                                 std::vector<std::shared_ptr<chunks::PaletteChunk>> palettes,
                                 std::shared_ptr<chunks::CastListChunk> castList,
                                 std::shared_ptr<chunks::ConfigChunk> config,
                                 std::shared_ptr<chunks::KeyTableChunk> keyTable,
                                 ChunkLookup chunkLookup)
    : casts_(std::move(casts)),
      castMembers_(std::move(castMembers)),
      palettes_(std::move(palettes)),
      castList_(std::move(castList)),
      config_(std::move(config)),
      keyTable_(std::move(keyTable)),
      chunkLookup_(std::move(chunkLookup)) {}

std::shared_ptr<const bitmap::Palette> PaletteResolver::resolve(int paletteId) const {
    if (auto exact = resolveExact(paletteId)) {
        return exact;
    }

    for (const auto& palette : palettes_) {
        if (palette) {
            return paletteFromChunk(palette, "Custom Palette");
        }
    }

    return builtInPalette(bitmap::Palette::SYSTEM_MAC);
}

std::shared_ptr<const bitmap::Palette> PaletteResolver::resolveExact(int paletteId) const {
    if (paletteId < 0) {
        return builtInPalette(paletteId);
    }

    const int memberNumber = paletteId + 1;
    for (int castIndex = 0; castIndex < static_cast<int>(casts_.size()); ++castIndex) {
        const auto& cast = casts_[static_cast<std::size_t>(castIndex)];
        if (!cast) {
            continue;
        }

        const int index = memberNumber - getMinMember(castIndex);
        if (index < 0 || index >= static_cast<int>(cast->memberIds().size())) {
            continue;
        }

        const int rawChunkId = cast->memberIds()[static_cast<std::size_t>(index)];
        if (rawChunkId <= 0) {
            continue;
        }

        if (auto resolved = resolveFromChunkId(id::ChunkId(rawChunkId))) {
            return resolved;
        }
    }

    if (auto resolved = resolveFromChunkId(id::ChunkId(paletteId))) {
        return resolved;
    }

    for (const auto& palette : palettes_) {
        if (!palette) {
            continue;
        }
        const int chunkId = palette->id().value();
        if (chunkId == paletteId || chunkId == paletteId + 1) {
            return paletteFromChunk(palette, "Custom Palette #" + std::to_string(chunkId));
        }
    }

    int paletteIndex = 0;
    for (const auto& member : castMembers_) {
        if (!member || member->memberType() != cast::MemberType::Palette) {
            continue;
        }
        if (paletteIndex == paletteId) {
            if (auto resolved = resolveFromChunkId(member->id())) {
                return resolved;
            }
        }
        ++paletteIndex;
    }

    return nullptr;
}

int PaletteResolver::getMinMember(int castIndex) const {
    int minMember = 1;
    if (castList_ && castIndex >= 0 && castIndex < static_cast<int>(castList_->entries().size())) {
        minMember = castList_->entries()[static_cast<std::size_t>(castIndex)].minMember;
    } else if (config_) {
        minMember = config_->minMember();
    }
    return minMember <= 0 ? 1 : minMember;
}

std::shared_ptr<const bitmap::Palette> PaletteResolver::resolveFromChunkId(id::ChunkId chunkId) const {
    if (!keyTable_) {
        return nullptr;
    }

    for (const auto& entry : keyTable_->getEntriesForOwner(chunkId)) {
        if (entry.fourcc != io::BinaryReader::fourCC("CLUT") &&
            entry.fourcc != io::BinaryReader::fourCC("TULC")) {
            continue;
        }

        std::shared_ptr<chunks::Chunk> chunk;
        if (chunkLookup_) {
            chunk = chunkLookup_(entry.sectionId);
        }

        auto paletteChunk = std::dynamic_pointer_cast<chunks::PaletteChunk>(chunk);
        if (!paletteChunk) {
            for (const auto& candidate : palettes_) {
                if (candidate && candidate->id().value() == entry.sectionId.value()) {
                    paletteChunk = candidate;
                    break;
                }
            }
        }
        if (paletteChunk) {
            return paletteFromChunk(paletteChunk, "Custom Palette");
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

    if (const auto found = paletteCache_.find(chunk->id().value()); found != paletteCache_.end()) {
        return found->second;
    }

    auto colors = chunk->colors();
    if (colors.size() == 256) {
        for (int index = 246; index < 256; ++index) {
            const auto macColor = bitmap::Palette::systemMacPalette().getColor(index);
            auto& color = colors[static_cast<std::size_t>(index)];
            if ((color & 0xFFFFFFU) == 0 && (macColor & 0xFFFFFFU) != 0) {
                color = macColor;
            }
        }
    }

    auto palette = std::make_shared<bitmap::Palette>(std::move(colors), name);
    paletteCache_[chunk->id().value()] = palette;
    return palette;
}

std::shared_ptr<const bitmap::Palette> PaletteResolver::builtInPalette(int paletteId) {
    return borrowedPalette(&bitmap::Palette::builtIn(paletteId));
}

} // namespace libreshockwave::lookup
