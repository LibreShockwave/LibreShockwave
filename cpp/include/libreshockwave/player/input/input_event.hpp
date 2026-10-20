#pragma once

#include <string>
#include <string_view>

namespace libreshockwave::player::input {

enum class InputEventType {
    MouseDown,
    MouseUp,
    RightMouseDown,
    RightMouseUp,
    KeyDown,
    KeyUp
};

[[nodiscard]] std::string_view name(InputEventType type);

struct InputEvent {
    InputEventType type{InputEventType::MouseDown};
    int stage_x{0};
    int stage_y{0};
    int button{0};
    int key_code{0};
    std::string key_char;

    [[nodiscard]] static InputEvent mouseDown(int x, int y);
    [[nodiscard]] static InputEvent mouseUp(int x, int y);
    [[nodiscard]] static InputEvent rightMouseDown(int x, int y);
    [[nodiscard]] static InputEvent rightMouseUp(int x, int y);
    [[nodiscard]] static InputEvent keyDown(int director_key_code, std::string ch);
    [[nodiscard]] static InputEvent keyUp(int director_key_code, std::string ch);

    friend bool operator==(const InputEvent&, const InputEvent&) = default;
};

} // namespace libreshockwave::player::input
