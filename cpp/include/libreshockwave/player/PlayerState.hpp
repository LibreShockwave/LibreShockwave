#pragma once

#include <string_view>

namespace libreshockwave::player {

enum class PlayerState {
    Stopped,
    Paused,
    Playing
};

[[nodiscard]] std::string_view name(PlayerState state);

} // namespace libreshockwave::player
