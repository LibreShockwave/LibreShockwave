#pragma once

#include <string>

namespace libreshockwave::player {

struct ExternalCastLoadEvent {
    int cast_lib_number{0};
    std::string file_name;

    friend bool operator==(const ExternalCastLoadEvent&, const ExternalCastLoadEvent&) = default;
};

} // namespace libreshockwave::player
