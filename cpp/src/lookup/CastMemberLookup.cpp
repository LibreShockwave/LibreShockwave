#include "libreshockwave/lookup/CastMemberLookup.hpp"

#include <algorithm>
#include <utility>

#include "libreshockwave/chunks/CastChunk.hpp"
#include "libreshockwave/chunks/CastListChunk.hpp"
#include "libreshockwave/chunks/CastMemberChunk.hpp"
#include "libreshockwave/chunks/ConfigChunk.hpp"

namespace libreshockwave::lookup {

CastMemberLookup::CastMemberLookup(std::vector<std::shared_ptr<chunks::CastChunk>> casts,
                                   std::vector<std::shared_ptr<chunks::CastMemberChunk>> castMembers,
                                   std::shared_ptr<chunks::CastListChunk> castList,
                                   std::shared_ptr<chunks::ConfigChunk> config)
    : casts_(std::move(casts)),
      castMembers_(std::move(castMembers)),
      castList_(std::move(castList)),
      config_(std::move(config)),
      castLibToCASp_(buildCastLibMapping()) {}

std::shared_ptr<chunks::CastChunk> CastMemberLookup::getMappedCast(int castLib) const {
    if (const auto found = castLibToCASp_.find(castLib); found != castLibToCASp_.end()) {
        return found->second;
    }
    return nullptr;
}

std::shared_ptr<chunks::CastMemberChunk> CastMemberLookup::getByIndex(int castLib, int castMemberIndex) const {
    const int minMember = getMinMember(castLib);
    const int adjustedMemberId = castMemberIndex + minMember;

    for (const auto& member : castMembers_) {
        if (member && member->id().value() == adjustedMemberId) {
            return member;
        }
    }
    for (const auto& member : castMembers_) {
        if (member && member->id().value() == castMemberIndex) {
            return member;
        }
    }
    for (const auto& member : castMembers_) {
        if (member && member->id().value() == castMemberIndex + 1) {
            return member;
        }
    }
    return nullptr;
}

std::shared_ptr<chunks::CastMemberChunk> CastMemberLookup::getByNumber(int castLib, int memberNumber) const {
    auto cast = getMappedCast(castLib);
    if (!cast) {
        const int libIndex = std::max(0, castLib - 1);
        if (libIndex >= static_cast<int>(casts_.size())) {
            return nullptr;
        }
        cast = casts_[static_cast<std::size_t>(libIndex)];
    }
    if (!cast) {
        return nullptr;
    }

    const int minMember = getMinMember(castLib);
    const int arrayIndex = memberNumber - minMember;
    if (arrayIndex < 0 || arrayIndex >= static_cast<int>(cast->memberIds().size())) {
        return nullptr;
    }

    const int rawChunkId = cast->memberIds()[static_cast<std::size_t>(arrayIndex)];
    if (rawChunkId <= 0) {
        return nullptr;
    }

    for (const auto& member : castMembers_) {
        if (member && member->id().value() == rawChunkId) {
            return member;
        }
    }
    return nullptr;
}

std::unordered_map<int, std::shared_ptr<chunks::CastChunk>> CastMemberLookup::buildCastLibMapping() const {
    std::unordered_map<int, std::shared_ptr<chunks::CastChunk>> mapping;
    if (!castList_ || castList_->entries().empty()) {
        for (int index = 0; index < static_cast<int>(casts_.size()); ++index) {
            mapping[index + 1] = casts_[static_cast<std::size_t>(index)];
        }
        return mapping;
    }

    std::vector<bool> assigned(casts_.size(), false);
    for (int libIndex = 0; libIndex < static_cast<int>(castList_->entries().size()); ++libIndex) {
        const auto& entry = castList_->entries()[static_cast<std::size_t>(libIndex)];
        for (int castIndex = 0; castIndex < static_cast<int>(casts_.size()); ++castIndex) {
            if (assigned[static_cast<std::size_t>(castIndex)] || !casts_[static_cast<std::size_t>(castIndex)]) {
                continue;
            }
            if (static_cast<int>(casts_[static_cast<std::size_t>(castIndex)]->memberIds().size()) == entry.memberCount) {
                mapping[libIndex + 1] = casts_[static_cast<std::size_t>(castIndex)];
                assigned[static_cast<std::size_t>(castIndex)] = true;
                break;
            }
        }
    }
    return mapping;
}

int CastMemberLookup::getMinMember(int castLib) const {
    int minMember = 1;
    if (castList_ && !castList_->entries().empty()) {
        const int libIndex = std::max(0, castLib - 1);
        if (libIndex < static_cast<int>(castList_->entries().size())) {
            minMember = castList_->entries()[static_cast<std::size_t>(libIndex)].minMember;
        }
    } else if (config_) {
        minMember = config_->minMember();
    }
    return minMember <= 0 ? 1 : minMember;
}

} // namespace libreshockwave::lookup
