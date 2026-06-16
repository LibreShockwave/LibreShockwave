#pragma once

#include <optional>
#include <queue>
#include <string>

#include "libreshockwave/player/input/InputEvent.hpp"

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
    int mouseH_{INITIAL_MOUSE_POSITION};
    int mouseV_{INITIAL_MOUSE_POSITION};
    bool mouseDown_{false};
    bool rightMouseDown_{false};

    int clickOnSprite_{0};
    int clickLocH_{0};
    int clickLocV_{0};
    long long lastClickTimeMs_{0};
    long long lastEventTimeMs_{0};
    long long lastKeyTimeMs_{0};
    long long lastRollTimeMs_{0};
    int lastClickLocH_{0};
    int lastClickLocV_{0};
    bool doubleClick_{false};

    int rolloverSprite_{0};

    std::string lastKey_;
    int lastKeyCode_{0};
    bool shiftDown_{false};
    bool controlDown_{false};
    bool altDown_{false};

    int keyboardFocusSprite_{0};
    int selStart_{0};
    int selEnd_{0};
    int caretBlinkCounter_{0};
    int caretBlinkRate_{8};

    std::queue<InputEvent> eventQueue_;
};

} // namespace libreshockwave::player::input
