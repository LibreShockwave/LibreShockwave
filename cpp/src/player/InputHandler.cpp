#include "libreshockwave/player/InputHandler.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <optional>
#include <utility>

#include "libreshockwave/cast/CastMember.hpp"
#include "libreshockwave/cast/MemberType.hpp"
#include "libreshockwave/player/PlayerEvent.hpp"
#include "libreshockwave/player/cast/CastLibManager.hpp"
#include "libreshockwave/player/event/EventDispatcher.hpp"
#include "libreshockwave/player/input/HitTester.hpp"
#include "libreshockwave/player/input/InputState.hpp"
#include "libreshockwave/player/render/SpriteRegistry.hpp"
#include "libreshockwave/player/render/pipeline/StageRenderer.hpp"

namespace libreshockwave::player {

InputHandler::InputHandler(input::InputState* inputState,
                           render::pipeline::StageRenderer* stageRenderer,
                           event::EventDispatcher* eventDispatcher,
                           cast::CastLibManager* castLibManager)
    : inputState_(inputState),
      stageRenderer_(stageRenderer),
      eventDispatcher_(eventDispatcher),
      castLibManager_(castLibManager) {}

void InputHandler::setInputState(input::InputState* inputState) {
    inputState_ = inputState;
}

void InputHandler::setStageRenderer(render::pipeline::StageRenderer* stageRenderer) {
    stageRenderer_ = stageRenderer;
}

void InputHandler::setEventDispatcher(event::EventDispatcher* eventDispatcher) {
    eventDispatcher_ = eventDispatcher;
}

void InputHandler::setCastLibManager(cast::CastLibManager* castLibManager) {
    castLibManager_ = castLibManager;
}

void InputHandler::setEventDispatcherSupplier(EventDispatcherSupplier supplier) {
    eventDispatcherSupplier_ = std::move(supplier);
}

void InputHandler::setCurrentFrameSupplier(CurrentFrameSupplier supplier) {
    currentFrameSupplier_ = std::move(supplier);
}

void InputHandler::setHitSpritesSupplier(HitSpritesSupplier supplier) {
    hitSpritesSupplier_ = std::move(supplier);
}

void InputHandler::setLegacyEventScriptDispatcher(LegacyEventScriptDispatcher dispatcher) {
    legacyEventScriptDispatcher_ = std::move(dispatcher);
}

void InputHandler::setSpriteLocationSetter(SpriteLocationSetter setter) {
    spriteLocationSetter_ = std::move(setter);
}

int InputHandler::previousRolloverSprite() const {
    return previousRolloverSprite_;
}

int InputHandler::hitTestExact(int stageX, int stageY) const {
    if (eventDispatcher() == nullptr) {
        return 0;
    }

    auto exactHits = getInteractiveHits(stageX, stageY, false);
    if (!exactHits.empty()) {
        return exactHits.front();
    }

    auto boundingHits = getInteractiveHits(stageX, stageY, true);
    return boundingHits.empty() ? 0 : boundingHits.front();
}

void InputHandler::onMouseMove(int stageX, int stageY) {
    if (inputState_ == nullptr) {
        return;
    }
    inputState_->setMousePosition(stageX, stageY);
    updateMoveableSpriteDrag(stageX, stageY);
    inputState_->setRolloverSprite(hitTestExact(stageX, stageY));

    const int focusChannel = inputState_->keyboardFocusSprite();
    if (!moveableDrag_.has_value() && inputState_->isMouseDown() && focusChannel > 0) {
        auto member = resolveSpriteMember(focusChannel);
        auto sprite = findHitSprite(focusChannel);
        if (member != nullptr && sprite.has_value() && isEditableFieldSprite(focusChannel, *member)) {
            const int localX = stageX - sprite->x();
            const int localY = stageY - sprite->y();
            const int charPos = castLibManager_->locToCharPos(
                member->castLib(),
                member->memberNum(),
                localX,
                localY,
                sprite->width());
            inputState_->setSelEnd(charPos);
            inputState_->resetCaretBlink();
        }
    }
}

void InputHandler::onMouseDown(int stageX, int stageY, bool rightButton) {
    if (inputState_ == nullptr) {
        return;
    }
    inputState_->setMousePosition(stageX, stageY);
    if (rightButton) {
        inputState_->setRightMouseDown(true);
        inputState_->queueEvent(input::InputEvent::rightMouseDown(stageX, stageY));
        return;
    }

    inputState_->setMouseDown(true);
    inputState_->setClickOnSprite(hitTestExact(stageX, stageY));
    inputState_->setClickLoc(stageX, stageY);
    inputState_->updateDoubleClick(stageX, stageY);
    beginMoveableSpriteDrag(stageX, stageY);
    inputState_->queueEvent(input::InputEvent::mouseDown(stageX, stageY));
}

void InputHandler::onMouseUp(int stageX, int stageY, bool rightButton) {
    if (inputState_ == nullptr) {
        return;
    }
    inputState_->setMousePosition(stageX, stageY);
    if (rightButton) {
        inputState_->setRightMouseDown(false);
        inputState_->queueEvent(input::InputEvent::rightMouseUp(stageX, stageY));
        return;
    }

    updateMoveableSpriteDrag(stageX, stageY);
    endMoveableSpriteDrag();
    inputState_->setMouseDown(false);
    inputState_->queueEvent(input::InputEvent::mouseUp(stageX, stageY));
}

void InputHandler::onBlur() {
    if (inputState_ == nullptr) {
        return;
    }
    const int stageX = inputState_->mouseH();
    const int stageY = inputState_->mouseV();
    if (inputState_->isMouseDown()) {
        endMoveableSpriteDrag();
        inputState_->setMouseDown(false);
        inputState_->queueEvent(input::InputEvent::mouseUp(stageX, stageY));
    }
    if (inputState_->isRightMouseDown()) {
        inputState_->setRightMouseDown(false);
        inputState_->queueEvent(input::InputEvent::rightMouseUp(stageX, stageY));
    }
    inputState_->setRolloverSprite(0);
}

void InputHandler::onKeyDown(int directorKeyCode, std::string keyChar, bool shift, bool ctrl, bool alt) {
    if (inputState_ == nullptr) {
        return;
    }
    inputState_->setLastKey(keyChar);
    inputState_->setLastKeyCode(directorKeyCode);
    inputState_->setShiftDown(shift);
    inputState_->setControlDown(ctrl);
    inputState_->setAltDown(alt);
    inputState_->queueEvent(input::InputEvent::keyDown(directorKeyCode, std::move(keyChar)));
}

void InputHandler::onKeyUp(int directorKeyCode, std::string keyChar, bool shift, bool ctrl, bool alt) {
    if (inputState_ == nullptr) {
        return;
    }
    inputState_->setShiftDown(shift);
    inputState_->setControlDown(ctrl);
    inputState_->setAltDown(alt);
    inputState_->queueEvent(input::InputEvent::keyUp(directorKeyCode, std::move(keyChar)));
}

std::optional<InputHandler::CaretInfo> InputHandler::getCaretInfo() const {
    if (inputState_ == nullptr || !inputState_->isCaretVisible() || castLibManager_ == nullptr) {
        return std::nullopt;
    }
    auto focused = focusedEditableField();
    if (!focused.has_value()) {
        return std::nullopt;
    }

    const std::string text = memberText(*focused->member);
    const auto [selMin, selMax] = clampedSelection(
        inputState_->selStart(),
        inputState_->selEnd(),
        static_cast<int>(text.size()));
    if (selMin != selMax) {
        return std::nullopt;
    }

    const auto loc = castLibManager_->charPosToLoc(
        focused->member->castLib(),
        focused->member->memberNum(),
        selMin,
        focused->sprite.width());
    if (loc.size() < 2) {
        return std::nullopt;
    }

    const int lineHeight = castLibManager_->textLineHeight(focused->member->castLib(), focused->member->memberNum());
    if (lineHeight <= 0) {
        return std::nullopt;
    }

    return CaretInfo{focused->sprite.x() + loc[0], focused->sprite.y() + loc[1], lineHeight};
}

std::vector<InputHandler::SelectionRect> InputHandler::getSelectionInfo() const {
    if (inputState_ == nullptr || castLibManager_ == nullptr) {
        return {};
    }
    auto focused = focusedEditableField();
    if (!focused.has_value()) {
        return {};
    }

    const std::string text = memberText(*focused->member);
    const auto [selMin, selMax] = clampedSelection(
        inputState_->selStart(),
        inputState_->selEnd(),
        static_cast<int>(text.size()));
    if (selMin == selMax) {
        return {};
    }

    const int lineHeight = castLibManager_->textLineHeight(focused->member->castLib(), focused->member->memberNum());
    if (lineHeight <= 0) {
        return {};
    }

    const auto startPos = castLibManager_->charPosToLoc(
        focused->member->castLib(),
        focused->member->memberNum(),
        selMin,
        focused->sprite.width());
    const auto endPos = castLibManager_->charPosToLoc(
        focused->member->castLib(),
        focused->member->memberNum(),
        selMax,
        focused->sprite.width());
    if (startPos.size() < 2 || endPos.size() < 2) {
        return {};
    }

    const int spriteX = focused->sprite.x();
    const int spriteY = focused->sprite.y();
    const int startY = startPos[1];
    const int endY = endPos[1];
    if (startY == endY) {
        return {SelectionRect{spriteX + startPos[0], spriteY + startY, endPos[0] - startPos[0], lineHeight}};
    }

    std::vector<SelectionRect> rects;
    rects.push_back(SelectionRect{
        spriteX + startPos[0],
        spriteY + startY,
        focused->sprite.width() - startPos[0],
        lineHeight
    });
    for (int midY = startY + lineHeight; midY < endY; midY += lineHeight) {
        rects.push_back(SelectionRect{spriteX, spriteY + midY, focused->sprite.width(), lineHeight});
    }
    rects.push_back(SelectionRect{spriteX, spriteY + endY, endPos[0], lineHeight});
    return rects;
}

InputHandler::EditableFieldOverlay InputHandler::editableFieldOverlay() const {
    return EditableFieldOverlay{getCaretInfo(), getSelectionInfo()};
}

void InputHandler::applyEditableFieldOverlay(bitmap::Bitmap& bitmap, const EditableFieldOverlay& overlay) {
    if (overlay.empty() || bitmap.width() <= 0 || bitmap.height() <= 0 || bitmap.pixels().empty()) {
        return;
    }

    auto& pixels = bitmap.pixels();
    const int width = bitmap.width();
    const int height = bitmap.height();

    for (const auto& rect : overlay.selectionRects) {
        if (rect.width <= 0 || rect.height <= 0) {
            continue;
        }
        const int x0 = std::max(0, rect.x);
        const int y0 = std::max(0, rect.y);
        const int x1 = std::min(width, rect.x + rect.width);
        const int y1 = std::min(height, rect.y + rect.height);
        if (x0 >= x1 || y0 >= y1) {
            continue;
        }
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                auto& pixel = pixels[static_cast<std::size_t>(y * width + x)];
                pixel = (pixel & 0xFF000000U) | ((~pixel) & 0x00FFFFFFU);
            }
        }
    }

    if (!overlay.caret.has_value() || overlay.caret->height <= 0 ||
        overlay.caret->x < 0 || overlay.caret->x >= width) {
        return;
    }
    const int x = overlay.caret->x;
    const int y0 = std::max(0, overlay.caret->y);
    const int y1 = std::min(height, overlay.caret->y + overlay.caret->height);
    for (int y = y0; y < y1; ++y) {
        pixels[static_cast<std::size_t>(y * width + x)] = 0xFF000000U;
    }
}

bitmap::Bitmap InputHandler::withEditableFieldOverlay(const bitmap::Bitmap& bitmap,
                                                      const EditableFieldOverlay& overlay) {
    auto result = bitmap.copy();
    applyEditableFieldOverlay(result, overlay);
    return result;
}

void InputHandler::onPasteText(std::string pasteText) {
    if (inputState_ == nullptr) {
        return;
    }
    auto focused = focusedEditableField();
    if (!focused.has_value()) {
        return;
    }

    std::string text = memberText(*focused->member);
    const auto [selMin, selMax] = clampedSelection(
        inputState_->selStart(),
        inputState_->selEnd(),
        static_cast<int>(text.size()));
    text.replace(static_cast<std::size_t>(selMin), static_cast<std::size_t>(selMax - selMin), pasteText);
    const int newPos = selMin + static_cast<int>(pasteText.size());
    inputState_->setSelStart(newPos);
    inputState_->setSelEnd(newPos);
    inputState_->resetCaretBlink();
    focused->member->setDynamicText(std::move(text));
    bumpSpriteRevision();
}

std::optional<std::string> InputHandler::getSelectedText() const {
    if (inputState_ == nullptr) {
        return std::nullopt;
    }
    auto focused = focusedEditableField();
    if (!focused.has_value()) {
        return std::nullopt;
    }

    const std::string text = memberText(*focused->member);
    if (text.empty()) {
        return std::string();
    }
    const auto [selMin, selMax] = clampedSelection(
        inputState_->selStart(),
        inputState_->selEnd(),
        static_cast<int>(text.size()));
    if (selMin != selMax) {
        return text.substr(static_cast<std::size_t>(selMin), static_cast<std::size_t>(selMax - selMin));
    }
    return text;
}

std::optional<std::string> InputHandler::cutSelectedText() {
    if (inputState_ == nullptr) {
        return std::nullopt;
    }
    auto focused = focusedEditableField();
    if (!focused.has_value()) {
        return std::nullopt;
    }

    std::string text = memberText(*focused->member);
    if (text.empty()) {
        return std::string();
    }

    const auto [selMin, selMax] = clampedSelection(
        inputState_->selStart(),
        inputState_->selEnd(),
        static_cast<int>(text.size()));
    std::string cutText;
    if (selMin != selMax) {
        cutText = text.substr(static_cast<std::size_t>(selMin), static_cast<std::size_t>(selMax - selMin));
        text.erase(static_cast<std::size_t>(selMin), static_cast<std::size_t>(selMax - selMin));
        inputState_->setSelStart(selMin);
        inputState_->setSelEnd(selMin);
    } else {
        cutText = text;
        text.clear();
        inputState_->setSelStart(0);
        inputState_->setSelEnd(0);
    }
    inputState_->resetCaretBlink();
    focused->member->setDynamicText(std::move(text));
    bumpSpriteRevision();
    return cutText;
}

void InputHandler::selectAll() {
    if (inputState_ == nullptr) {
        return;
    }
    auto focused = focusedEditableField();
    if (!focused.has_value()) {
        return;
    }

    const std::string text = memberText(*focused->member);
    inputState_->setSelStart(0);
    inputState_->setSelEnd(static_cast<int>(text.size()));
    inputState_->resetCaretBlink();
}

bool InputHandler::processInputEvents() {
    if (inputState_ == nullptr) {
        return false;
    }

    bool hadEvents = false;
    while (auto queued = inputState_->pollEvent()) {
        dispatchInputEvent(*queued);
        hadEvents = true;
    }

    dispatchRolloverEvents();

    if (hadEvents && stageRenderer_ != nullptr) {
        stageRenderer_->spriteRegistry().bumpRevision();
    }

    if (inputState_->keyboardFocusSprite() > 0) {
        inputState_->incrementCaretBlink();
    }

    return hadEvents;
}

event::EventDispatcher* InputHandler::eventDispatcher() const {
    if (eventDispatcherSupplier_) {
        return eventDispatcherSupplier_();
    }
    return eventDispatcher_;
}

std::vector<render::pipeline::RenderSprite> InputHandler::hitSprites() const {
    if (hitSpritesSupplier_) {
        return hitSpritesSupplier_();
    }
    if (stageRenderer_ == nullptr) {
        return {};
    }

    const auto& baked = stageRenderer_->lastBakedSprites();
    if (!baked.empty()) {
        return baked;
    }

    if (currentFrameSupplier_) {
        const int frame = currentFrameSupplier_();
        if (frame > 0) {
            return stageRenderer_->getSpritesForFrame(frame);
        }
    }
    return {};
}

std::vector<int> InputHandler::getInteractiveHits(int stageX, int stageY, bool forceBoundingBox) const {
    auto* dispatcher = eventDispatcher();
    if (dispatcher == nullptr) {
        return {};
    }

    auto sprites = hitSprites();
    auto hitChannels = input::HitTester::hitTestAll(
        sprites,
        stageX,
        stageY,
        [dispatcher, forceBoundingBox](int channel) {
            return forceBoundingBox && dispatcher->isSpriteMouseInteractive(channel);
        });

    std::vector<int> interactive;
    interactive.reserve(hitChannels.size());
    std::copy_if(hitChannels.begin(), hitChannels.end(), std::back_inserter(interactive), [dispatcher](int channel) {
        return dispatcher->isSpriteMouseInteractive(channel);
    });
    return interactive;
}

void InputHandler::dispatchRolloverEvents() {
    if (inputState_ == nullptr) {
        return;
    }

    auto* dispatcher = eventDispatcher();
    if (dispatcher == nullptr) {
        return;
    }

    const int currentRollover = inputState_->rolloverSprite();
    if (currentRollover != previousRolloverSprite_) {
        if (previousRolloverSprite_ > 0) {
            dispatcher->dispatchSpriteEvent(previousRolloverSprite_, PlayerEvent::MouseLeave);
        }
        if (currentRollover > 0) {
            dispatcher->dispatchSpriteEvent(currentRollover, PlayerEvent::MouseEnter);
        }
        previousRolloverSprite_ = currentRollover;
    }

    if (currentRollover > 0) {
        dispatcher->dispatchSpriteEvent(currentRollover, PlayerEvent::MouseWithin);
    }
}

void InputHandler::dispatchInputEvent(const input::InputEvent& inputEvent) {
    if (inputState_ == nullptr) {
        return;
    }

    auto* dispatcher = eventDispatcher();
    if (dispatcher == nullptr) {
        return;
    }

    switch (inputEvent.type) {
        case input::InputEventType::MouseDown: {
            const int hitSprite = hitTestExact(inputEvent.stageX, inputEvent.stageY);
            const int stageHitSprite = hitTestStage(inputEvent.stageX, inputEvent.stageY);
            const int editableHitSprite = hitTestEditableField(inputEvent.stageX, inputEvent.stageY);
            const int lastClicked = inputState_->clickOnSprite();
            if (lastClicked > 0 && lastClicked != hitSprite) {
                dispatcher->dispatchSpriteEvent(lastClicked, "mouseUpOutSide");
            }

            inputState_->setClickOnSprite(hitSprite);
            autoFocusEditableField(editableHitSprite > 0 ? editableHitSprite : stageHitSprite,
                                   inputEvent.stageX,
                                   inputEvent.stageY);
            dispatcher->resetEventStopped();
            if (!dispatchLegacyEventScript(PlayerEvent::MouseDown) && !dispatcher->isEventStopped()) {
                if (hitSprite > 0) {
                    dispatcher->dispatchSpriteEvent(hitSprite, PlayerEvent::MouseDown);
                }
                dispatcher->dispatchFrameAndMovieEvent(PlayerEvent::MouseDown);
            }
            break;
        }
        case input::InputEventType::MouseUp: {
            const int pressedSprite = inputState_->clickOnSprite();
            const int releaseSprite = hitTestExact(inputEvent.stageX, inputEvent.stageY);
            dispatcher->resetEventStopped();
            const bool legacyConsumed = dispatchLegacyEventScript(PlayerEvent::MouseUp);
            if (!legacyConsumed && !dispatcher->isEventStopped()) {
                if (pressedSprite > 0 && releaseSprite == pressedSprite) {
                    dispatcher->dispatchSpriteEvent(pressedSprite, PlayerEvent::MouseUp);
                } else if (pressedSprite > 0 && dispatcher->spriteHasHandler(pressedSprite, "mouseUpOutSide")) {
                    dispatcher->dispatchSpriteEvent(pressedSprite, "mouseUpOutSide");
                } else if (pressedSprite > 0 && dispatcher->spriteHasHandler(pressedSprite, handlerName(PlayerEvent::MouseUp))) {
                    dispatcher->dispatchSpriteEvent(pressedSprite, PlayerEvent::MouseUp);
                } else if (releaseSprite > 0) {
                    dispatcher->dispatchSpriteEvent(releaseSprite, PlayerEvent::MouseUp);
                }
            }
            inputState_->setClickOnSprite(0);
            if (!legacyConsumed && !dispatcher->isEventStopped()) {
                dispatcher->dispatchFrameAndMovieEvent(PlayerEvent::MouseUp);
            }
            break;
        }
        case input::InputEventType::RightMouseDown:
            dispatcher->dispatchFrameAndMovieEvent(PlayerEvent::RightMouseDown);
            break;
        case input::InputEventType::RightMouseUp:
            dispatcher->dispatchFrameAndMovieEvent(PlayerEvent::RightMouseUp);
            break;
        case input::InputEventType::KeyDown: {
            dispatcher->resetEventStopped();
            inputState_->setLastKey(inputEvent.keyChar);
            inputState_->setLastKeyCode(inputEvent.keyCode);
            const int focusSprite = inputState_->keyboardFocusSprite();
            if (focusSprite > 0) {
                handleEditableFieldInput(focusSprite, inputEvent.keyChar);
            }
            if (!dispatchLegacyEventScript(PlayerEvent::KeyDown) && !dispatcher->isEventStopped()) {
                if (focusSprite > 0) {
                    dispatcher->dispatchSpriteEvent(focusSprite, PlayerEvent::KeyDown);
                }
                dispatcher->dispatchFrameAndMovieEvent(PlayerEvent::KeyDown);
            }
            break;
        }
        case input::InputEventType::KeyUp: {
            dispatcher->resetEventStopped();
            inputState_->setLastKey(inputEvent.keyChar);
            inputState_->setLastKeyCode(inputEvent.keyCode);
            if (!dispatchLegacyEventScript(PlayerEvent::KeyUp) && !dispatcher->isEventStopped()) {
                const int focusSprite = inputState_->keyboardFocusSprite();
                if (focusSprite > 0) {
                    dispatcher->dispatchSpriteEvent(focusSprite, PlayerEvent::KeyUp);
                }
                dispatcher->dispatchFrameAndMovieEvent(PlayerEvent::KeyUp);
            }
            break;
        }
    }
}

bool InputHandler::dispatchLegacyEventScript(PlayerEvent event) {
    if (!legacyEventScriptDispatcher_) {
        return false;
    }

    const LegacyEventScriptResult result = legacyEventScriptDispatcher_(event);
    return result.handled && !result.passed;
}

int InputHandler::hitTestStage(int stageX, int stageY) const {
    return input::HitTester::hitTest(hitSprites(), stageX, stageY);
}

int InputHandler::hitTestEditableField(int stageX, int stageY) const {
    const auto sprites = hitSprites();
    for (auto it = sprites.rbegin(); it != sprites.rend(); ++it) {
        const auto& sprite = *it;
        if (!sprite.isVisible() || sprite.channel() <= 0 || sprite.width() <= 0 || sprite.height() <= 0) {
            continue;
        }
        if (stageX < sprite.x() || stageY < sprite.y() ||
            stageX >= sprite.x() + sprite.width() ||
            stageY >= sprite.y() + sprite.height()) {
            continue;
        }
        auto member = resolveSpriteMember(sprite.channel());
        if (member != nullptr && isEditableFieldSprite(sprite.channel(), *member)) {
            return sprite.channel();
        }
    }
    return 0;
}

int InputHandler::hitTestMoveableSprite(int stageX, int stageY) const {
    const auto hitChannels = input::HitTester::hitTestAll(hitSprites(), stageX, stageY, [](int) {
        return false;
    });
    const auto found = std::ranges::find_if(hitChannels, [this](int channel) {
        return isMoveableSprite(channel);
    });
    return found == hitChannels.end() ? 0 : *found;
}

std::optional<render::pipeline::RenderSprite> InputHandler::findHitSprite(int channel) const {
    auto sprites = hitSprites();
    auto found = std::find_if(sprites.begin(), sprites.end(), [channel](const auto& sprite) {
        return sprite.channel() == channel;
    });
    if (found == sprites.end()) {
        return std::nullopt;
    }
    return *found;
}

std::shared_ptr<::libreshockwave::cast::CastMember> InputHandler::resolveSpriteMember(int channel) const {
    if (stageRenderer_ == nullptr || castLibManager_ == nullptr || channel <= 0) {
        return nullptr;
    }
    const auto sprite = stageRenderer_->spriteRegistry().get(channel);
    if (sprite == nullptr || sprite->effectiveCastMember() <= 0) {
        return nullptr;
    }
    return castLibManager_->resolveMember(sprite->effectiveCastLib(), sprite->effectiveCastMember());
}

std::optional<InputHandler::FocusedField> InputHandler::focusedEditableField() const {
    if (inputState_ == nullptr) {
        return std::nullopt;
    }
    const int focusChannel = inputState_->keyboardFocusSprite();
    if (focusChannel <= 0) {
        return std::nullopt;
    }
    auto member = resolveSpriteMember(focusChannel);
    if (member == nullptr || !isEditableFieldSprite(focusChannel, *member)) {
        return std::nullopt;
    }
    auto sprite = findHitSprite(focusChannel);
    if (!sprite.has_value()) {
        return std::nullopt;
    }
    return FocusedField{*sprite, std::move(member)};
}

std::string InputHandler::memberText(const ::libreshockwave::cast::CastMember& member) const {
    if (castLibManager_ == nullptr) {
        return member.textContent();
    }
    return castLibManager_->getMemberProp(member.castLib(), member.memberNum(), "text").stringValue();
}

std::pair<int, int> InputHandler::clampedSelection(int selStart, int selEnd, int textLength) {
    const int start = std::clamp(selStart, 0, textLength);
    const int end = std::clamp(selEnd, 0, textLength);
    return {std::min(start, end), std::max(start, end)};
}

bool InputHandler::isMoveableSprite(int channel) const {
    if (stageRenderer_ == nullptr || channel <= 0) {
        return false;
    }
    const auto sprite = stageRenderer_->spriteRegistry().get(channel);
    if (sprite == nullptr) {
        return false;
    }
    const auto moveable = sprite->legacyProperty("moveablesprite");
    return moveable.has_value() && moveable->boolValue();
}

bool InputHandler::isEditableFieldSprite(int channel, const ::libreshockwave::cast::CastMember& member) const {
    if (!member.isTextLike()) {
        return false;
    }
    if (member.editable()) {
        return true;
    }
    if (stageRenderer_ == nullptr || channel <= 0) {
        return false;
    }
    const auto sprite = stageRenderer_->spriteRegistry().get(channel);
    if (sprite == nullptr) {
        return false;
    }
    const auto editable = sprite->legacyProperty("editabletext");
    return editable.has_value() && editable->boolValue();
}

bool InputHandler::setSpriteLoc(int channel, int locH, int locV) const {
    if (channel <= 0) {
        return false;
    }
    if (spriteLocationSetter_ && spriteLocationSetter_(channel, locH, locV)) {
        return true;
    }
    if (stageRenderer_ == nullptr) {
        return false;
    }
    const auto sprite = stageRenderer_->spriteRegistry().get(channel);
    if (sprite == nullptr) {
        return false;
    }
    sprite->setLocH(locH);
    sprite->setLocV(locV);
    return true;
}

void InputHandler::bumpSpriteRevision() {
    if (stageRenderer_ != nullptr) {
        stageRenderer_->spriteRegistry().bumpRevision();
    }
}

void InputHandler::beginMoveableSpriteDrag(int stageX, int stageY) {
    moveableDrag_.reset();
    if (stageRenderer_ == nullptr) {
        return;
    }

    const int hitChannel = hitTestMoveableSprite(stageX, stageY);
    if (hitChannel <= 0) {
        return;
    }

    const auto sprite = stageRenderer_->spriteRegistry().get(hitChannel);
    if (sprite == nullptr) {
        return;
    }

    moveableDrag_ = MoveableDrag{hitChannel, stageX - sprite->locH(), stageY - sprite->locV()};
}

void InputHandler::updateMoveableSpriteDrag(int stageX, int stageY) {
    if (!moveableDrag_.has_value() || stageRenderer_ == nullptr) {
        return;
    }
    if (!inputState_->isMouseDown() || !isMoveableSprite(moveableDrag_->channel)) {
        endMoveableSpriteDrag();
        return;
    }

    if (setSpriteLoc(moveableDrag_->channel, stageX - moveableDrag_->offsetH, stageY - moveableDrag_->offsetV)) {
        bumpSpriteRevision();
    }
}

void InputHandler::endMoveableSpriteDrag() {
    moveableDrag_.reset();
}

void InputHandler::autoFocusEditableField(int hitChannel, int stageX, int stageY) {
    if (inputState_ == nullptr) {
        return;
    }
    auto member = resolveSpriteMember(hitChannel);
    auto sprite = findHitSprite(hitChannel);
    if (member != nullptr && sprite.has_value() && isEditableFieldSprite(hitChannel, *member) &&
        member->isTextLike()) {
        inputState_->setKeyboardFocusSprite(hitChannel);
        const int localX = stageX - sprite->x();
        const int localY = stageY - sprite->y();
        const int charPos = castLibManager_->locToCharPos(
            member->castLib(),
            member->memberNum(),
            localX,
            localY,
            sprite->width());
        inputState_->setSelStart(charPos);
        inputState_->setSelEnd(charPos);
        inputState_->resetCaretBlink();
        return;
    }

    inputState_->setKeyboardFocusSprite(0);
}

void InputHandler::handleEditableFieldInput(int channel, const std::string& keyChar) {
    if (inputState_ == nullptr || castLibManager_ == nullptr) {
        return;
    }

    auto member = resolveSpriteMember(channel);
    if (member == nullptr || !isEditableFieldSprite(channel, *member)) {
        return;
    }

    std::string text = castLibManager_->getMemberProp(member->castLib(), member->memberNum(), "text").stringValue();
    int selStart = std::clamp(inputState_->selStart(), 0, static_cast<int>(text.size()));
    int selEnd = std::clamp(inputState_->selEnd(), 0, static_cast<int>(text.size()));
    const int selMin = std::min(selStart, selEnd);
    const int selMax = std::max(selStart, selEnd);
    const int keyCode = inputState_->lastKeyCode();

    if (keyCode == 51) {
        if (selMin != selMax) {
            text.erase(static_cast<std::size_t>(selMin), static_cast<std::size_t>(selMax - selMin));
            selStart = selEnd = selMin;
        } else if (selStart > 0) {
            text.erase(static_cast<std::size_t>(selStart - 1), 1);
            selStart = selEnd = selStart - 1;
        }
        member->setDynamicText(text);
    } else if (keyCode == 123) {
        selStart = selEnd = std::max(0, selMin - (selMin == selMax ? 1 : 0));
    } else if (keyCode == 124) {
        selStart = selEnd = std::min(static_cast<int>(text.size()), selMax + (selMin == selMax ? 1 : 0));
    } else if (keyCode == 48) {
        tabToNextField(channel, inputState_->isShiftDown());
        return;
    } else if (keyCode == 36) {
        return;
    } else if (keyChar.size() == 1 && std::isprint(static_cast<unsigned char>(keyChar.front())) != 0) {
        text.replace(static_cast<std::size_t>(selMin), static_cast<std::size_t>(selMax - selMin), keyChar);
        selStart = selEnd = selMin + 1;
        member->setDynamicText(text);
    } else {
        return;
    }

    inputState_->setSelStart(selStart);
    inputState_->setSelEnd(selEnd);
    inputState_->resetCaretBlink();
}

void InputHandler::tabToNextField(int currentChannel, bool reverse) {
    if (inputState_ == nullptr || stageRenderer_ == nullptr || castLibManager_ == nullptr) {
        return;
    }

    std::vector<int> editableChannels;
    for (const auto& [channel, state] : stageRenderer_->spriteRegistry().getAll()) {
        if (state == nullptr || state->effectiveCastMember() <= 0) {
            continue;
        }
        auto member = castLibManager_->resolveMember(state->effectiveCastLib(), state->effectiveCastMember());
        if (member != nullptr && isEditableFieldSprite(channel, *member)) {
            editableChannels.push_back(channel);
        }
    }
    if (editableChannels.empty()) {
        return;
    }
    std::ranges::sort(editableChannels);

    const auto found = std::ranges::find(editableChannels, currentChannel);
    const int index = found == editableChannels.end()
        ? -1
        : static_cast<int>(std::distance(editableChannels.begin(), found));
    int nextIndex = 0;
    if (reverse) {
        nextIndex = index <= 0 ? static_cast<int>(editableChannels.size()) - 1 : index - 1;
    } else {
        nextIndex = index < 0 || index >= static_cast<int>(editableChannels.size()) - 1 ? 0 : index + 1;
    }

    const int nextChannel = editableChannels[static_cast<std::size_t>(nextIndex)];
    inputState_->setKeyboardFocusSprite(nextChannel);
    auto member = resolveSpriteMember(nextChannel);
    const std::string text = member != nullptr
        ? castLibManager_->getMemberProp(member->castLib(), member->memberNum(), "text").stringValue()
        : "";
    inputState_->setSelStart(0);
    inputState_->setSelEnd(static_cast<int>(text.size()));
    inputState_->resetCaretBlink();
}

} // namespace libreshockwave::player
