#pragma once

#include <optional>
#include <queue>
#include <string>

#include "libreshockwave/player/input/input_event.hpp"

namespace libreshockwave::player::input {

class InputState {
public:
    static constexpr int INITIAL_MOUSE_POSITION = -10000;

    InputState();

    [[nodiscard]] int mouseH() const;
    [[nodiscard]] int mouseV() const;
    void setMousePosition(int h, int v);

    [[nodiscard]] bool isMouseDown() const;
    [[nodiscard]] bool isRightMouseDown() const;
    void setMouseDown(bool down);
    void setRightMouseDown(bool down);

    [[nodiscard]] int clickOnSprite() const;
    void setClickOnSprite(int channel);
    [[nodiscard]] int clickLocH() const;
    [[nodiscard]] int clickLocV() const;
    void setClickLoc(int h, int v);
    [[nodiscard]] bool isDoubleClick() const;
    void updateDoubleClick(int h, int v);
    [[nodiscard]] int lastClickTicks() const;
    [[nodiscard]] int lastEventTicks() const;
    [[nodiscard]] int lastKeyTicks() const;
    [[nodiscard]] int lastRollTicks() const;

    [[nodiscard]] int rolloverSprite() const;
    void setRolloverSprite(int channel);

    [[nodiscard]] const std::string& lastKey() const;
    void setLastKey(std::string key);
    void setLastKey(const char* key);
    [[nodiscard]] int lastKeyCode() const;
    void setLastKeyCode(int code);
    [[nodiscard]] bool isShiftDown() const;
    void setShiftDown(bool down);
    [[nodiscard]] bool isControlDown() const;
    void setControlDown(bool down);
    [[nodiscard]] bool isAltDown() const;
    void setAltDown(bool down);

    [[nodiscard]] int keyboardFocusSprite() const;
    void setKeyboardFocusSprite(int channel);

    [[nodiscard]] int selStart() const;
    void setSelStart(int pos);
    [[nodiscard]] int selEnd() const;
    void setSelEnd(int pos);

    void incrementCaretBlink();
    void resetCaretBlink();
    void setCaretBlinkRate(int tempo);
    [[nodiscard]] bool isCaretVisible() const;

    void queueEvent(InputEvent event);
    [[nodiscard]] std::optional<InputEvent> pollEvent();
    [[nodiscard]] bool hasEvents() const;

private:
    int mouse_h_{INITIAL_MOUSE_POSITION};
    int mouse_v_{INITIAL_MOUSE_POSITION};
    bool mouse_down_{false};
    bool right_mouse_down_{false};

    int click_on_sprite_{0};
    int click_loc_h_{0};
    int click_loc_v_{0};
    long long last_click_time_ms_{0};
    long long last_event_time_ms_{0};
    long long last_key_time_ms_{0};
    long long last_roll_time_ms_{0};
    int last_click_loc_h_{0};
    int last_click_loc_v_{0};
    bool double_click_{false};

    int rollover_sprite_{0};

    std::string last_key_;
    int last_key_code_{0};
    bool shift_down_{false};
    bool control_down_{false};
    bool alt_down_{false};

    int keyboard_focus_sprite_{0};
    int sel_start_{0};
    int sel_end_{0};
    int caret_blink_counter_{0};
    int caret_blink_rate_{8};

    std::queue<InputEvent> event_queue_;
};

} // namespace libreshockwave::player::input
