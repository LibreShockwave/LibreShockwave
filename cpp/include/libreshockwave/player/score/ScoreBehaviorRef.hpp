#pragma once

#include <string>
#include <vector>

#include "libreshockwave/id/Ids.hpp"
#include "libreshockwave/lingo/Datum.hpp"

namespace libreshockwave::player::score {

class ScoreBehaviorRef {
public:
    ScoreBehaviorRef(id::CastLibId castLibId, id::MemberId memberId, std::vector<lingo::Datum> parameters = {});
    ScoreBehaviorRef(int castLib, int castMember);
    ScoreBehaviorRef(int castLib, int castMember, std::vector<lingo::Datum> parameters);

    [[nodiscard]] id::CastLibId castLibId() const;
    [[nodiscard]] id::MemberId memberId() const;
    [[nodiscard]] int castLib() const;
    [[nodiscard]] int castMember() const;
    [[nodiscard]] const std::vector<lingo::Datum>& parameters() const;
    [[nodiscard]] bool hasParameters() const;
    [[nodiscard]] std::string toString() const;

    friend bool operator==(const ScoreBehaviorRef&, const ScoreBehaviorRef&) = default;

private:
    id::CastLibId castLibId_;
    id::MemberId memberId_;
    std::vector<lingo::Datum> parameters_;
};

} // namespace libreshockwave::player::score
