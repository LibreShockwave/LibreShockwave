#include "libreshockwave/player/input/InputState.hpp"

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

int elapsedTicksSince(long long timestampMs) {
    const long long elapsedMs = std::max(0LL, currentTimeMillis() - timestampMs);
    return static_cast<int>((elapsedMs * 60) / 1000);
}

} // namespace

InputState::InputState()
    : lastClickTimeMs_(currentTimeMillis()),
      lastEventTimeMs_(lastClickTimeMs_),
      lastKeyTimeMs_(lastClickTimeMs_),
      lastRollTimeMs_(lastClickTimeMs_) {}

int InputState::mouseH() const { return mouseH_; }
int InputState::mouseV() const { return mouseV_; }

void InputState::setMousePosition(int h, int v) {
    if (mouseH_ != h || mouseV_ != v) {
        const long long now = currentTimeMillis();
        lastRollTimeMs_ = now;
        lastEventTimeMs_ = now;
    }
    mouseH_ = h;
    mouseV_ = v;
}

bool InputState::isMouseDown() const { return mouseDown_; }
bool InputState::isRightMouseDown() const { return rightMouseDown_; }

void InputState::setMouseDown(bool down) { mouseDown_ = down; }
void InputState::setRightMouseDown(bool down) { rightMouseDown_ = down; }

int InputState::clickOnSprite() const { return clickOnSprite_; }
void InputState::setClickOnSprite(int channel) { clickOnSprite_ = channel; }

int InputState::clickLocH() const { return clickLocH_; }
int InputState::clickLocV() const { return clickLocV_; }

void InputState::setClickLoc(int h, int v) {
    clickLocH_ = h;
    clickLocV_ = v;
}

bool InputState::isDoubleClick() const { return doubleClick_; }

void InputState::updateDoubleClick(int h, int v) {
    const long long now = currentTimeMillis();
    const bool timeMatch = (now - lastClickTimeMs_) <= 500;
    const bool distMatch = std::abs(h - lastClickLocH_) <= 5 && std::abs(v - lastClickLocV_) <= 5;

    doubleClick_ = timeMatch && distMatch;
    lastClickTimeMs_ = now;
    lastEventTimeMs_ = now;
    lastClickLocH_ = h;
    lastClickLocV_ = v;
}

int InputState::lastClickTicks() const { return elapsedTicksSince(lastClickTimeMs_); }
int InputState::lastEventTicks() const { return elapsedTicksSince(lastEventTimeMs_); }
int InputState::lastKeyTicks() const { return elapsedTicksSince(lastKeyTimeMs_); }
int InputState::lastRollTicks() const { return elapsedTicksSince(lastRollTimeMs_); }

int InputState::rolloverSprite() const { return rolloverSprite_; }
void InputState::setRolloverSprite(int channel) { rolloverSprite_ = channel; }

const std::string& InputState::lastKey() const { return lastKey_; }

void InputState::setLastKey(std::string key) {
    lastKey_ = std::move(key);
    if (!lastKey_.empty()) {
        const long long now = currentTimeMillis();
        lastKeyTimeMs_ = now;
        lastEventTimeMs_ = now;
    }
}

void InputState::setLastKey(const char* key) {
    setLastKey(key == nullptr ? std::string{} : std::string(key));
}

int InputState::lastKeyCode() const { return lastKeyCode_; }
void InputState::setLastKeyCode(int code) { lastKeyCode_ = code; }

bool InputState::isShiftDown() const { return shiftDown_; }
void InputState::setShiftDown(bool down) { shiftDown_ = down; }

bool InputState::isControlDown() const { return controlDown_; }
void InputState::setControlDown(bool down) { controlDown_ = down; }

bool InputState::isAltDown() const { return altDown_; }
void InputState::setAltDown(bool down) { altDown_ = down; }

int InputState::keyboardFocusSprite() const { return keyboardFocusSprite_; }

void InputState::setKeyboardFocusSprite(int channel) {
    if (keyboardFocusSprite_ != channel) {
        keyboardFocusSprite_ = channel;
        resetCaretBlink();
    }
}

int InputState::selStart() const { return selStart_; }
void InputState::setSelStart(int pos) { selStart_ = pos; }

int InputState::selEnd() const { return selEnd_; }
void InputState::setSelEnd(int pos) { selEnd_ = pos; }

void InputState::incrementCaretBlink() {
    ++caretBlinkCounter_;
}

void InputState::resetCaretBlink() {
    caretBlinkCounter_ = 0;
}

void InputState::setCaretBlinkRate(int tempo) {
    caretBlinkRate_ = std::max(1, (tempo * 530 + 500) / 1000);
}

bool InputState::isCaretVisible() const {
    return keyboardFocusSprite_ > 0 && (caretBlinkCounter_ / caretBlinkRate_) % 2 == 0;
}

void InputState::queueEvent(InputEvent event) {
    eventQueue_.push(std::move(event));
}

std::optional<InputEvent> InputState::pollEvent() {
    if (eventQueue_.empty()) {
        return std::nullopt;
    }
    InputEvent event = std::move(eventQueue_.front());
    eventQueue_.pop();
    return event;
}

bool InputState::hasEvents() const {
    return !eventQueue_.empty();
}

} // namespace libreshockwave::player::input
