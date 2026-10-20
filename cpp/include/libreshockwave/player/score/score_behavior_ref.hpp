#pragma once

#include <string>
#include <vector>

#include "libreshockwave/id/ids.hpp"
#include "libreshockwave/lingo/datum.hpp"

namespace libreshockwave::player::score {

class ScoreBehaviorRef {
public:
    ScoreBehaviorRef(id::CastLibId cast_lib_id, id::MemberId member_id, std::vector<lingo::Datum> parameters = {});
    ScoreBehaviorRef(int cast_lib, int cast_member);
    ScoreBehaviorRef(int cast_lib, int cast_member, std::vector<lingo::Datum> parameters);

    [[nodiscard]] id::CastLibId castLibId() const;
    [[nodiscard]] id::MemberId memberId() const;
    [[nodiscard]] int castLib() const;
    [[nodiscard]] int castMember() const;
    [[nodiscard]] const std::vector<lingo::Datum>& parameters() const;
    [[nodiscard]] bool hasParameters() const;
    [[nodiscard]] std::string toString() const;

    friend bool operator==(const ScoreBehaviorRef&, const ScoreBehaviorRef&) = default;

private:
    id::CastLibId cast_lib_id_;
    id::MemberId member_id_;
    std::vector<lingo::Datum> parameters_;
};

} // namespace libreshockwave::player::score
