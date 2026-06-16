#include "libreshockwave/player/score/ScoreBehaviorRef.hpp"

#include <sstream>
#include <utility>

namespace libreshockwave::player::score {

ScoreBehaviorRef::ScoreBehaviorRef(id::CastLibId castLibId,
                                   id::MemberId memberId,
                                   std::vector<lingo::Datum> parameters)
    : castLibId_(castLibId), memberId_(memberId), parameters_(std::move(parameters)) {}

ScoreBehaviorRef::ScoreBehaviorRef(int castLib, int castMember)
    : ScoreBehaviorRef(id::CastLibId(castLib), id::MemberId(castMember), {}) {}

ScoreBehaviorRef::ScoreBehaviorRef(int castLib, int castMember, std::vector<lingo::Datum> parameters)
    : ScoreBehaviorRef(id::CastLibId(castLib), id::MemberId(castMember), std::move(parameters)) {}

id::CastLibId ScoreBehaviorRef::castLibId() const { return castLibId_; }
id::MemberId ScoreBehaviorRef::memberId() const { return memberId_; }
int ScoreBehaviorRef::castLib() const { return castLibId_.value(); }
int ScoreBehaviorRef::castMember() const { return memberId_.value(); }
const std::vector<lingo::Datum>& ScoreBehaviorRef::parameters() const { return parameters_; }
bool ScoreBehaviorRef::hasParameters() const { return !parameters_.empty(); }

std::string ScoreBehaviorRef::toString() const {
    std::ostringstream out;
    out << "behavior(member " << memberId_.value() << ", castLib " << castLibId_.value() << ")";
    return out.str();
}

} // namespace libreshockwave::player::score
