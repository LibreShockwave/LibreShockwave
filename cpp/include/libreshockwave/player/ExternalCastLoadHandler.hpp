#pragma once

#include <string_view>

namespace libreshockwave::player {

class Player;

class ExternalCastLoadHandler {
public:
    virtual ~ExternalCastLoadHandler() = default;

    virtual void onExternalCastLoaded(Player& player, int castLibNumber, std::string_view fileName) = 0;
};

} // namespace libreshockwave::player
