#pragma once

#include <string_view>

namespace libreshockwave::player {

class Player;

class ExternalCastLoadHandler {
public:
    virtual ~ExternalCastLoadHandler() = default;

    virtual void onExternalCastLoaded(Player& player, int cast_lib_number, std::string_view file_name) = 0;
};

} // namespace libreshockwave::player
