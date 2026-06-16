#include "libreshockwave/player/PlayerState.hpp"

#include <stdexcept>

namespace libreshockwave::player {

std::string_view name(PlayerState state) {
    switch (state) {
        case PlayerState::Stopped: return "STOPPED";
        case PlayerState::Paused: return "PAUSED";
        case PlayerState::Playing: return "PLAYING";
    }
    throw std::logic_error("Unknown PlayerState enum value");
}

} // namespace libreshockwave::player
