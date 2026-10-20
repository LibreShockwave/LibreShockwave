#include "libreshockwave/player/player_event.hpp"

#include <stdexcept>

namespace libreshockwave::player {
namespace {

struct PlayerEventEntry {
    PlayerEvent event;
    std::string_view name;
    std::string_view handler_name;
};

constexpr std::array<PlayerEventEntry, 20> k_entries{{
    {PlayerEvent::PrepareMovie, "PREPARE_MOVIE", "prepareMovie"},
    {PlayerEvent::StartMovie, "START_MOVIE", "startMovie"},
    {PlayerEvent::StopMovie, "STOP_MOVIE", "stopMovie"},
    {PlayerEvent::PrepareFrame, "PREPARE_FRAME", "prepareFrame"},
    {PlayerEvent::EnterFrame, "ENTER_FRAME", "enterFrame"},
    {PlayerEvent::ExitFrame, "EXIT_FRAME", "exitFrame"},
    {PlayerEvent::StepFrame, "STEP_FRAME", "stepFrame"},
    {PlayerEvent::BeginSprite, "BEGIN_SPRITE", "beginSprite"},
    {PlayerEvent::EndSprite, "END_SPRITE", "endSprite"},
    {PlayerEvent::Idle, "IDLE", "idle"},
    {PlayerEvent::MouseDown, "MOUSE_DOWN", "mouseDown"},
    {PlayerEvent::MouseUp, "MOUSE_UP", "mouseUp"},
    {PlayerEvent::MouseEnter, "MOUSE_ENTER", "mouseEnter"},
    {PlayerEvent::MouseLeave, "MOUSE_LEAVE", "mouseLeave"},
    {PlayerEvent::MouseWithin, "MOUSE_WITHIN", "mouseWithin"},
    {PlayerEvent::MouseUpOutside, "MOUSE_UP_OUTSIDE", "mouseUpOutside"},
    {PlayerEvent::RightMouseDown, "RIGHT_MOUSE_DOWN", "rightMouseDown"},
    {PlayerEvent::RightMouseUp, "RIGHT_MOUSE_UP", "rightMouseUp"},
    {PlayerEvent::KeyDown, "KEY_DOWN", "keyDown"},
    {PlayerEvent::KeyUp, "KEY_UP", "keyUp"},
}};

constexpr std::array<PlayerEvent, 20> k_all_events{{
    PlayerEvent::PrepareMovie,
    PlayerEvent::StartMovie,
    PlayerEvent::StopMovie,
    PlayerEvent::PrepareFrame,
    PlayerEvent::EnterFrame,
    PlayerEvent::ExitFrame,
    PlayerEvent::StepFrame,
    PlayerEvent::BeginSprite,
    PlayerEvent::EndSprite,
    PlayerEvent::Idle,
    PlayerEvent::MouseDown,
    PlayerEvent::MouseUp,
    PlayerEvent::MouseEnter,
    PlayerEvent::MouseLeave,
    PlayerEvent::MouseWithin,
    PlayerEvent::MouseUpOutside,
    PlayerEvent::RightMouseDown,
    PlayerEvent::RightMouseUp,
    PlayerEvent::KeyDown,
    PlayerEvent::KeyUp,
}};

const PlayerEventEntry& entryFor(PlayerEvent event) {
    for (const auto& entry : k_entries) {
        if (entry.event == event) {
            return entry;
        }
    }
    throw std::logic_error("Unknown PlayerEvent enum value");
}

} // namespace

std::string_view name(PlayerEvent event) {
    return entryFor(event).name;
}

std::string_view handlerName(PlayerEvent event) {
    return entryFor(event).handler_name;
}

std::optional<PlayerEvent> playerEventFromHandlerName(std::string_view value) {
    for (const auto& entry : k_entries) {
        if (entry.handler_name == value) {
            return entry.event;
        }
    }
    return std::nullopt;
}

const std::array<PlayerEvent, 20>& allPlayerEvents() {
    return k_all_events;
}

} // namespace libreshockwave::player
