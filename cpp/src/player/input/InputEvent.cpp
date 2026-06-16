#include "libreshockwave/player/input/InputEvent.hpp"

#include <stdexcept>
#include <utility>

namespace libreshockwave::player::input {

std::string_view name(InputEventType type) {
    switch (type) {
        case InputEventType::MouseDown: return "MOUSE_DOWN";
        case InputEventType::MouseUp: return "MOUSE_UP";
        case InputEventType::RightMouseDown: return "RIGHT_MOUSE_DOWN";
        case InputEventType::RightMouseUp: return "RIGHT_MOUSE_UP";
        case InputEventType::KeyDown: return "KEY_DOWN";
        case InputEventType::KeyUp: return "KEY_UP";
    }
    throw std::logic_error("Unknown InputEventType enum value");
}

InputEvent InputEvent::mouseDown(int x, int y) {
    return InputEvent{InputEventType::MouseDown, x, y, 1, 0, ""};
}

InputEvent InputEvent::mouseUp(int x, int y) {
    return InputEvent{InputEventType::MouseUp, x, y, 1, 0, ""};
}

InputEvent InputEvent::rightMouseDown(int x, int y) {
    return InputEvent{InputEventType::RightMouseDown, x, y, 3, 0, ""};
}

InputEvent InputEvent::rightMouseUp(int x, int y) {
    return InputEvent{InputEventType::RightMouseUp, x, y, 3, 0, ""};
}

InputEvent InputEvent::keyDown(int directorKeyCode, std::string ch) {
    return InputEvent{InputEventType::KeyDown, 0, 0, 0, directorKeyCode, std::move(ch)};
}

InputEvent InputEvent::keyUp(int directorKeyCode, std::string ch) {
    return InputEvent{InputEventType::KeyUp, 0, 0, 0, directorKeyCode, std::move(ch)};
}

} // namespace libreshockwave::player::input
