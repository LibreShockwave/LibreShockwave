#pragma once

#include "libreshockwave/player/PlayerEvent.hpp"

namespace libreshockwave::player::frame {

struct FrameEvent {
    PlayerEvent event{PlayerEvent::Idle};
    int frame{0};

    friend bool operator==(const FrameEvent&, const FrameEvent&) = default;
};

} // namespace libreshockwave::player::frame
