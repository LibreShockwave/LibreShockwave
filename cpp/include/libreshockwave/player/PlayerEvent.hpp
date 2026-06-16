#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace libreshockwave::player {

enum class PlayerEvent {
    PrepareMovie,
    StartMovie,
    StopMovie,
    PrepareFrame,
    EnterFrame,
    ExitFrame,
    StepFrame,
    BeginSprite,
    EndSprite,
    Idle,
    MouseDown,
    MouseUp,
    MouseEnter,
    MouseLeave,
    MouseWithin,
    MouseUpOutside,
    RightMouseDown,
    RightMouseUp,
    KeyDown,
    KeyUp
};

[[nodiscard]] std::string_view name(PlayerEvent event);
[[nodiscard]] std::string_view handlerName(PlayerEvent event);
[[nodiscard]] std::optional<PlayerEvent> playerEventFromHandlerName(std::string_view value);
[[nodiscard]] const std::array<PlayerEvent, 20>& allPlayerEvents();

} // namespace libreshockwave::player
