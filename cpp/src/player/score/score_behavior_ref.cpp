#include "libreshockwave/player/score/score_behavior_ref.hpp"

#include <sstream>
#include <utility>

namespace libreshockwave::player::score {

ScoreBehaviorRef::ScoreBehaviorRef(id::CastLibId cast_lib_id,
                                   id::MemberId member_id,
                                   std::vector<lingo::Datum> parameters)
    : cast_lib_id_(cast_lib_id), member_id_(member_id), parameters_(std::move(parameters)) {}

ScoreBehaviorRef::ScoreBehaviorRef(int cast_lib, int cast_member)
    : ScoreBehaviorRef(id::CastLibId(cast_lib), id::MemberId(cast_member), {}) {}

ScoreBehaviorRef::ScoreBehaviorRef(int cast_lib, int cast_member, std::vector<lingo::Datum> parameters)
    : ScoreBehaviorRef(id::CastLibId(cast_lib), id::MemberId(cast_member), std::move(parameters)) {}

id::CastLibId ScoreBehaviorRef::castLibId() const { return cast_lib_id_; }
id::MemberId ScoreBehaviorRef::memberId() const { return member_id_; }
int ScoreBehaviorRef::castLib() const { return cast_lib_id_.value(); }
int ScoreBehaviorRef::castMember() const { return member_id_.value(); }
const std::vector<lingo::Datum>& ScoreBehaviorRef::parameters() const { return parameters_; }
bool ScoreBehaviorRef::hasParameters() const { return !parameters_.empty(); }

std::string ScoreBehaviorRef::toString() const {
    std::ostringstream out;
    out << "behavior(member " << member_id_.value() << ", castLib " << cast_lib_id_.value() << ")";
    return out.str();
}

} // namespace libreshockwave::player::score
