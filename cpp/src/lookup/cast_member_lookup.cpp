#include "libreshockwave/lookup/cast_member_lookup.hpp"

#include <algorithm>
#include <utility>

#include "libreshockwave/chunks/cast_chunk.hpp"
#include "libreshockwave/chunks/cast_list_chunk.hpp"
#include "libreshockwave/chunks/cast_member_chunk.hpp"
#include "libreshockwave/chunks/config_chunk.hpp"

namespace libreshockwave::lookup {

CastMemberLookup::CastMemberLookup(std::vector<std::shared_ptr<chunks::CastChunk>> casts,
                                   std::vector<std::shared_ptr<chunks::CastMemberChunk>> cast_members,
                                   std::shared_ptr<chunks::CastListChunk> cast_list,
                                   std::shared_ptr<chunks::ConfigChunk> config)
    : casts_(std::move(casts)),
      cast_members_(std::move(cast_members)),
      cast_list_(std::move(cast_list)),
      config_(std::move(config)),
      cast_lib_to_ca_sp_(buildCastLibMapping()) {}

std::shared_ptr<chunks::CastChunk> CastMemberLookup::getMappedCast(int cast_lib) const {
    if (const auto found = cast_lib_to_ca_sp_.find(cast_lib); found != cast_lib_to_ca_sp_.end()) {
        return found->second;
    }
    return nullptr;
}

std::shared_ptr<chunks::CastMemberChunk> CastMemberLookup::getByIndex(int cast_lib, int cast_member_index) const {
    const int min_member = getMinMember(cast_lib);
    const int adjusted_member_id = cast_member_index + min_member;

    for (const auto& member : cast_members_) {
        if (member && member->id().value() == adjusted_member_id) {
            return member;
        }
    }
    for (const auto& member : cast_members_) {
        if (member && member->id().value() == cast_member_index) {
            return member;
        }
    }
    for (const auto& member : cast_members_) {
        if (member && member->id().value() == cast_member_index + 1) {
            return member;
        }
    }
    return nullptr;
}

std::shared_ptr<chunks::CastMemberChunk> CastMemberLookup::getByNumber(int cast_lib, int member_number) const {
    auto cast = getMappedCast(cast_lib);
    if (!cast) {
        const int lib_index = std::max(0, cast_lib - 1);
        if (lib_index >= static_cast<int>(casts_.size())) {
            return nullptr;
        }
        cast = casts_[static_cast<std::size_t>(lib_index)];
    }
    if (!cast) {
        return nullptr;
    }

    const int min_member = getMinMember(cast_lib);
    const int array_index = member_number - min_member;
    if (array_index < 0 || array_index >= static_cast<int>(cast->memberIds().size())) {
        return nullptr;
    }

    const int raw_chunk_id = cast->memberIds()[static_cast<std::size_t>(array_index)];
    if (raw_chunk_id <= 0) {
        return nullptr;
    }

    for (const auto& member : cast_members_) {
        if (member && member->id().value() == raw_chunk_id) {
            return member;
        }
    }
    return nullptr;
}

std::unordered_map<int, std::shared_ptr<chunks::CastChunk>> CastMemberLookup::buildCastLibMapping() const {
    std::unordered_map<int, std::shared_ptr<chunks::CastChunk>> mapping;
    if (!cast_list_ || cast_list_->entries().empty()) {
        for (int index = 0; index < static_cast<int>(casts_.size()); ++index) {
            mapping[index + 1] = casts_[static_cast<std::size_t>(index)];
        }
        return mapping;
    }

    std::vector<bool> assigned(casts_.size(), false);
    for (int lib_index = 0; lib_index < static_cast<int>(cast_list_->entries().size()); ++lib_index) {
        const auto& entry = cast_list_->entries()[static_cast<std::size_t>(lib_index)];
        for (int cast_index = 0; cast_index < static_cast<int>(casts_.size()); ++cast_index) {
            if (assigned[static_cast<std::size_t>(cast_index)] || !casts_[static_cast<std::size_t>(cast_index)]) {
                continue;
            }
            if (static_cast<int>(casts_[static_cast<std::size_t>(cast_index)]->memberIds().size()) == entry.member_count) {
                mapping[lib_index + 1] = casts_[static_cast<std::size_t>(cast_index)];
                assigned[static_cast<std::size_t>(cast_index)] = true;
                break;
            }
        }
    }
    return mapping;
}

int CastMemberLookup::getMinMember(int cast_lib) const {
    int min_member = 1;
    if (cast_list_ && !cast_list_->entries().empty()) {
        const int lib_index = std::max(0, cast_lib - 1);
        if (lib_index < static_cast<int>(cast_list_->entries().size())) {
            min_member = cast_list_->entries()[static_cast<std::size_t>(lib_index)].min_member;
        }
    } else if (config_) {
        min_member = config_->minMember();
    }
    return min_member <= 0 ? 1 : min_member;
}

} // namespace libreshockwave::lookup
