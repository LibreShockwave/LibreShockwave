#include "libreshockwave/player/input/input_state.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <utility>

namespace libreshockwave::player::input {
namespace {

long long currentTimeMillis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

int elapsedTicksSince(long long timestamp_ms) {
    const long long elapsed_ms = std::max(0LL, currentTimeMillis() - timestamp_ms);
    return static_cast<int>((elapsed_ms * 60) / 1000);
}

} // namespace

InputState::InputState()
    : last_click_time_ms_(currentTimeMillis()),
      last_event_time_ms_(last_click_time_ms_),
      last_key_time_ms_(last_click_time_ms_),
      last_roll_time_ms_(last_click_time_ms_) {}

int InputState::mouseH() const { return mouse_h_; }
int InputState::mouseV() const { return mouse_v_; }

void InputState::setMousePosition(int h, int v) {
    if (mouse_h_ != h || mouse_v_ != v) {
        const long long now = currentTimeMillis();
        last_roll_time_ms_ = now;
        last_event_time_ms_ = now;
    }
    mouse_h_ = h;
    mouse_v_ = v;
}

bool InputState::isMouseDown() const { return mouse_down_; }
bool InputState::isRightMouseDown() const { return right_mouse_down_; }

void InputState::setMouseDown(bool down) { mouse_down_ = down; }
void InputState::setRightMouseDown(bool down) { right_mouse_down_ = down; }

int InputState::clickOnSprite() const { return click_on_sprite_; }
void InputState::setClickOnSprite(int channel) { click_on_sprite_ = channel; }

int InputState::clickLocH() const { return click_loc_h_; }
int InputState::clickLocV() const { return click_loc_v_; }

void InputState::setClickLoc(int h, int v) {
    click_loc_h_ = h;
    click_loc_v_ = v;
}

bool InputState::isDoubleClick() const { return double_click_; }

void InputState::updateDoubleClick(int h, int v) {
    const long long now = currentTimeMillis();
    const bool time_match = (now - last_click_time_ms_) <= 500;
    const bool dist_match = std::abs(h - last_click_loc_h_) <= 5 && std::abs(v - last_click_loc_v_) <= 5;

    double_click_ = time_match && dist_match;
    last_click_time_ms_ = now;
    last_event_time_ms_ = now;
    last_click_loc_h_ = h;
    last_click_loc_v_ = v;
}

int InputState::lastClickTicks() const { return elapsedTicksSince(last_click_time_ms_); }
int InputState::lastEventTicks() const { return elapsedTicksSince(last_event_time_ms_); }
int InputState::lastKeyTicks() const { return elapsedTicksSince(last_key_time_ms_); }
int InputState::lastRollTicks() const { return elapsedTicksSince(last_roll_time_ms_); }

int InputState::rolloverSprite() const { return rollover_sprite_; }
void InputState::setRolloverSprite(int channel) { rollover_sprite_ = channel; }

const std::string& InputState::lastKey() const { return last_key_; }

void InputState::setLastKey(std::string key) {
    last_key_ = std::move(key);
    if (!last_key_.empty()) {
        const long long now = currentTimeMillis();
        last_key_time_ms_ = now;
        last_event_time_ms_ = now;
    }
}

void InputState::setLastKey(const char* key) {
    setLastKey(key == nullptr ? std::string{} : std::string(key));
}

int InputState::lastKeyCode() const { return last_key_code_; }
void InputState::setLastKeyCode(int code) { last_key_code_ = code; }

bool InputState::isShiftDown() const { return shift_down_; }
void InputState::setShiftDown(bool down) { shift_down_ = down; }

bool InputState::isControlDown() const { return control_down_; }
void InputState::setControlDown(bool down) { control_down_ = down; }

bool InputState::isAltDown() const { return alt_down_; }
void InputState::setAltDown(bool down) { alt_down_ = down; }

int InputState::keyboardFocusSprite() const { return keyboard_focus_sprite_; }

void InputState::setKeyboardFocusSprite(int channel) {
    if (keyboard_focus_sprite_ != channel) {
        keyboard_focus_sprite_ = channel;
        resetCaretBlink();
    }
}

int InputState::selStart() const { return sel_start_; }
void InputState::setSelStart(int pos) { sel_start_ = pos; }

int InputState::selEnd() const { return sel_end_; }
void InputState::setSelEnd(int pos) { sel_end_ = pos; }

// Advances once per movie frame (called from Player::executeFrameCycle), so the
// blink cadence follows the movie tempo instead of host event-loop wake-ups.
void InputState::incrementCaretBlink() {
    if (keyboard_focus_sprite_ <= 0) {
        return;
    }
    ++caret_blink_counter_;
}

void InputState::resetCaretBlink() {
    caret_blink_counter_ = 0;
}

void InputState::setCaretBlinkRate(int tempo) {
    caret_blink_rate_ = std::max(1, (tempo * 530 + 500) / 1000);
}

bool InputState::isCaretVisible() const {
    return keyboard_focus_sprite_ > 0 && (caret_blink_counter_ / caret_blink_rate_) % 2 == 0;
}

void InputState::queueEvent(InputEvent event) {
    event_queue_.push(std::move(event));
}

std::optional<InputEvent> InputState::pollEvent() {
    if (event_queue_.empty()) {
        return std::nullopt;
    }
    InputEvent event = std::move(event_queue_.front());
    event_queue_.pop();
    return event;
}

bool InputState::hasEvents() const {
    return !event_queue_.empty();
}

} // namespace libreshockwave::player::input
