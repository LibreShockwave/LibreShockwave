#pragma once

#include <string>

namespace libreshockwave::player {

struct ExternalCastLoadEvent {
    int castLibNumber{0};
    std::string fileName;

    friend bool operator==(const ExternalCastLoadEvent&, const ExternalCastLoadEvent&) = default;
};

} // namespace libreshockwave::player
