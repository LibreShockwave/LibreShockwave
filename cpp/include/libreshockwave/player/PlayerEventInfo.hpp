#pragma once

#include "libreshockwave/player/PlayerEvent.hpp"

namespace libreshockwave::player {

struct PlayerEventInfo {
    PlayerEvent event{PlayerEvent::Idle};
    int frame{0};
    int data{0};

    friend bool operator==(const PlayerEventInfo&, const PlayerEventInfo&) = default;
};

} // namespace libreshockwave::player
