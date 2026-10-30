#include "libreshockwave/player/input_handler.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <optional>
#include <utility>

#include "libreshockwave/cast/cast_member.hpp"
#include "libreshockwave/cast/member_type.hpp"
#include "libreshockwave/player/player_event.hpp"
#include "libreshockwave/player/cast/cast_lib_manager.hpp"
#include "libreshockwave/player/event/event_dispatcher.hpp"
#include "libreshockwave/player/input/hit_tester.hpp"
#include "libreshockwave/player/input/input_state.hpp"
#include "libreshockwave/player/cursor_manager.hpp"
#include "libreshockwave/player/render/sprite_registry.hpp"
#include "libreshockwave/player/render/pipeline/stage_renderer.hpp"

namespace libreshockwave::player {

InputHandler::InputHandler(input::InputState* input_state,
                           render::pipeline::StageRenderer* stage_renderer,
                           event::EventDispatcher* event_dispatcher,
                           cast::CastLibManager* cast_lib_manager)
    : input_state_(input_state),
      stage_renderer_(stage_renderer),
      event_dispatcher_(event_dispatcher),
      cast_lib_manager_(cast_lib_manager) {}

void InputHandler::setInputState(input::InputState* input_state) {
    input_state_ = input_state;
}

void InputHandler::setStageRenderer(render::pipeline::StageRenderer* stage_renderer) {
    stage_renderer_ = stage_renderer;
}

void InputHandler::setEventDispatcher(event::EventDispatcher* event_dispatcher) {
    event_dispatcher_ = event_dispatcher;
}

void InputHandler::setCastLibManager(cast::CastLibManager* cast_lib_manager) {
    cast_lib_manager_ = cast_lib_manager;
}

void InputHandler::setEventDispatcherSupplier(EventDispatcherSupplier supplier) {
    event_dispatcher_supplier_ = std::move(supplier);
}

void InputHandler::setCurrentFrameSupplier(CurrentFrameSupplier supplier) {
    current_frame_supplier_ = std::move(supplier);
}

void InputHandler::setHitSpritesSupplier(HitSpritesSupplier supplier) {
    hit_sprites_supplier_ = std::move(supplier);
}

void InputHandler::setLegacyEventScriptDispatcher(LegacyEventScriptDispatcher dispatcher) {
    legacy_event_script_dispatcher_ = std::move(dispatcher);
}

void InputHandler::setSpriteLocationSetter(SpriteLocationSetter setter) {
    sprite_location_setter_ = std::move(setter);
}

int InputHandler::previousRolloverSprite() const {
    return previous_rollover_sprite_;
}

int InputHandler::hitTestExact(int stage_x, int stage_y) const {
    if (eventDispatcher() == nullptr) {
        return 0;
    }

    const auto hits = getInteractiveHitPath(stage_x, stage_y);
    return hits.empty() ? 0 : hits.front();
}

void InputHandler::onMouseMove(int stage_x, int stage_y) {
    if (input_state_ == nullptr) {
        return;
    }
    input_state_->setMousePosition(stage_x, stage_y);
    updateMoveableSpriteDrag(stage_x, stage_y);
    input_state_->setRolloverSprite(hitTestExact(stage_x, stage_y));

    const int focus_channel = input_state_->keyboardFocusSprite();
    if (!moveable_drag_.has_value() && input_state_->isMouseDown() && focus_channel > 0) {
        auto member = resolveSpriteMember(focus_channel);
        auto sprite = findHitSprite(focus_channel);
        if (member != nullptr && sprite.has_value() && canReceiveKeyboardTextInput(focus_channel, *member)) {
            const int local_x = stage_x - sprite->x();
            const int local_y = stage_y - sprite->y();
            const int char_pos = cast_lib_manager_->locToCharPos(
                member->castLib(),
                member->memberNum(),
                local_x,
                local_y,
                sprite->width());
            input_state_->setSelEnd(char_pos);
            input_state_->resetCaretBlink();
        }
    }
}

void InputHandler::onMouseDown(int stage_x, int stage_y, bool right_button) {
    if (input_state_ == nullptr) {
        return;
    }
    input_state_->setMousePosition(stage_x, stage_y);
    if (right_button) {
        input_state_->setRightMouseDown(true);
        input_state_->queueEvent(input::InputEvent::rightMouseDown(stage_x, stage_y));
        return;
    }

    input_state_->setMouseDown(true);
    input_state_->setClickOnSprite(hitTestExact(stage_x, stage_y));
    input_state_->setClickLoc(stage_x, stage_y);
    input_state_->updateDoubleClick(stage_x, stage_y);
    beginMoveableSpriteDrag(stage_x, stage_y);
    input_state_->queueEvent(input::InputEvent::mouseDown(stage_x, stage_y));
}

void InputHandler::onMouseUp(int stage_x, int stage_y, bool right_button) {
    if (input_state_ == nullptr) {
        return;
    }
    input_state_->setMousePosition(stage_x, stage_y);
    if (right_button) {
        input_state_->setRightMouseDown(false);
        input_state_->queueEvent(input::InputEvent::rightMouseUp(stage_x, stage_y));
        return;
    }

    updateMoveableSpriteDrag(stage_x, stage_y);
    endMoveableSpriteDrag();
    input_state_->setMouseDown(false);
    input_state_->queueEvent(input::InputEvent::mouseUp(stage_x, stage_y));
}

void InputHandler::onBlur() {
    if (input_state_ == nullptr) {
        return;
    }
    const int stage_x = input_state_->mouseH();
    const int stage_y = input_state_->mouseV();
    if (input_state_->isMouseDown()) {
        endMoveableSpriteDrag();
        input_state_->setMouseDown(false);
        input_state_->queueEvent(input::InputEvent::mouseUp(stage_x, stage_y));
    }
    if (input_state_->isRightMouseDown()) {
        input_state_->setRightMouseDown(false);
        input_state_->queueEvent(input::InputEvent::rightMouseUp(stage_x, stage_y));
    }
    input_state_->setRolloverSprite(0);
}

void InputHandler::onKeyDown(int director_key_code, std::string key_char, bool shift, bool ctrl, bool alt) {
    if (input_state_ == nullptr) {
        return;
    }
    input_state_->setLastKey(key_char);
    input_state_->setLastKeyCode(director_key_code);
    input_state_->setShiftDown(shift);
    input_state_->setControlDown(ctrl);
    input_state_->setAltDown(alt);
    input_state_->queueEvent(input::InputEvent::keyDown(director_key_code, std::move(key_char)));
}

void InputHandler::onKeyUp(int director_key_code, std::string key_char, bool shift, bool ctrl, bool alt) {
    if (input_state_ == nullptr) {
        return;
    }
    input_state_->setShiftDown(shift);
    input_state_->setControlDown(ctrl);
    input_state_->setAltDown(alt);
    input_state_->queueEvent(input::InputEvent::keyUp(director_key_code, std::move(key_char)));
}

std::optional<input::CaretInfo> InputHandler::getCaretInfo() const {
    if (input_state_ == nullptr || !input_state_->isCaretVisible() || cast_lib_manager_ == nullptr) {
        return std::nullopt;
    }
    auto focused = focusedEditableField();
    if (!focused.has_value()) {
        return std::nullopt;
    }

    const std::string text = memberText(*focused->member);
    const auto [sel_min, sel_max] = clampedSelection(
        input_state_->selStart(),
        input_state_->selEnd(),
        static_cast<int>(text.size()));
    if (sel_min != sel_max) {
        return std::nullopt;
    }

    const auto loc = cast_lib_manager_->charPosToLoc(
        focused->member->castLib(),
        focused->member->memberNum(),
        sel_min,
        focused->sprite.width());
    if (loc.size() < 2) {
        return std::nullopt;
    }

    const int line_height = cast_lib_manager_->textLineHeight(focused->member->castLib(), focused->member->memberNum());
    if (line_height <= 0) {
        return std::nullopt;
    }

    return input::CaretInfo{focused->sprite.x() + loc[0], focused->sprite.y() + loc[1], line_height};
}

std::vector<input::SelectionRect> InputHandler::getSelectionInfo() const {
    if (input_state_ == nullptr || cast_lib_manager_ == nullptr) {
        return {};
    }
    auto focused = focusedEditableField();
    if (!focused.has_value()) {
        return {};
    }

    const std::string text = memberText(*focused->member);
    const auto [sel_min, sel_max] = clampedSelection(
        input_state_->selStart(),
        input_state_->selEnd(),
        static_cast<int>(text.size()));
    if (sel_min == sel_max) {
        return {};
    }

    const int line_height = cast_lib_manager_->textLineHeight(focused->member->castLib(), focused->member->memberNum());
    if (line_height <= 0) {
        return {};
    }

    const auto start_pos = cast_lib_manager_->charPosToLoc(
        focused->member->castLib(),
        focused->member->memberNum(),
        sel_min,
        focused->sprite.width());
    const auto end_pos = cast_lib_manager_->charPosToLoc(
        focused->member->castLib(),
        focused->member->memberNum(),
        sel_max,
        focused->sprite.width());
    if (start_pos.size() < 2 || end_pos.size() < 2) {
        return {};
    }

    const int sprite_x = focused->sprite.x();
    const int sprite_y = focused->sprite.y();
    const int start_y = start_pos[1];
    const int end_y = end_pos[1];
    if (start_y == end_y) {
        return {input::SelectionRect{sprite_x + start_pos[0], sprite_y + start_y, end_pos[0] - start_pos[0], line_height}};
    }

    std::vector<input::SelectionRect> rects;
    rects.push_back(input::SelectionRect{
        sprite_x + start_pos[0],
        sprite_y + start_y,
        focused->sprite.width() - start_pos[0],
        line_height
    });
    for (int mid_y = start_y + line_height; mid_y < end_y; mid_y += line_height) {
        rects.push_back(input::SelectionRect{sprite_x, sprite_y + mid_y, focused->sprite.width(), line_height});
    }
    rects.push_back(input::SelectionRect{sprite_x, sprite_y + end_y, end_pos[0], line_height});
    return rects;
}

input::EditableFieldOverlay InputHandler::editableFieldOverlay() const {
    return input::EditableFieldOverlay{getCaretInfo(), getSelectionInfo()};
}

void InputHandler::onPasteText(std::string paste_text) {
    if (input_state_ == nullptr) {
        return;
    }
    auto focused = focusedEditableField();
    if (!focused.has_value()) {
        return;
    }

    std::string text = memberText(*focused->member);
    const auto [sel_min, sel_max] = clampedSelection(
        input_state_->selStart(),
        input_state_->selEnd(),
        static_cast<int>(text.size()));
    text.replace(static_cast<std::size_t>(sel_min), static_cast<std::size_t>(sel_max - sel_min), paste_text);
    const int new_pos = sel_min + static_cast<int>(paste_text.size());
    input_state_->setSelStart(new_pos);
    input_state_->setSelEnd(new_pos);
    input_state_->resetCaretBlink();
    focused->member->setDynamicText(std::move(text));
    bumpSpriteRevision();
}

std::optional<std::string> InputHandler::getSelectedText() const {
    if (input_state_ == nullptr) {
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
    const auto [sel_min, sel_max] = clampedSelection(
        input_state_->selStart(),
        input_state_->selEnd(),
        static_cast<int>(text.size()));
    if (sel_min != sel_max) {
        return text.substr(static_cast<std::size_t>(sel_min), static_cast<std::size_t>(sel_max - sel_min));
    }
    return text;
}

std::optional<std::string> InputHandler::cutSelectedText() {
    if (input_state_ == nullptr) {
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

    const auto [sel_min, sel_max] = clampedSelection(
        input_state_->selStart(),
        input_state_->selEnd(),
        static_cast<int>(text.size()));
    std::string cut_text;
    if (sel_min != sel_max) {
        cut_text = text.substr(static_cast<std::size_t>(sel_min), static_cast<std::size_t>(sel_max - sel_min));
        text.erase(static_cast<std::size_t>(sel_min), static_cast<std::size_t>(sel_max - sel_min));
        input_state_->setSelStart(sel_min);
        input_state_->setSelEnd(sel_min);
    } else {
        cut_text = text;
        text.clear();
        input_state_->setSelStart(0);
        input_state_->setSelEnd(0);
    }
    input_state_->resetCaretBlink();
    focused->member->setDynamicText(std::move(text));
    bumpSpriteRevision();
    return cut_text;
}

void InputHandler::selectAll() {
    if (input_state_ == nullptr) {
        return;
    }
    auto focused = focusedEditableField();
    if (!focused.has_value()) {
        return;
    }

    const std::string text = memberText(*focused->member);
    input_state_->setSelStart(0);
    input_state_->setSelEnd(static_cast<int>(text.size()));
    input_state_->resetCaretBlink();
}

bool InputHandler::processInputEvents() {
    if (input_state_ == nullptr) {
        return false;
    }

    bool had_events = false;
    while (auto queued = input_state_->pollEvent()) {
        dispatchInputEvent(*queued);
        had_events = true;
    }

    dispatchRolloverEvents();

    if (had_events && stage_renderer_ != nullptr) {
        stage_renderer_->spriteRegistry().bumpRevision();
    }

    return had_events;
}

event::EventDispatcher* InputHandler::eventDispatcher() const {
    if (event_dispatcher_supplier_) {
        return event_dispatcher_supplier_();
    }
    return event_dispatcher_;
}

std::vector<render::pipeline::RenderSprite> InputHandler::hitSprites() const {
    if (hit_sprites_supplier_) {
        return hit_sprites_supplier_();
    }
    if (stage_renderer_ == nullptr) {
        return {};
    }

    const auto& baked = stage_renderer_->lastBakedSprites();
    if (!baked.empty()) {
        return baked;
    }

    if (current_frame_supplier_) {
        const int frame = current_frame_supplier_();
        if (frame > 0) {
            return stage_renderer_->getSpritesForFrame(frame);
        }
    }
    return {};
}

std::vector<int> InputHandler::getInteractiveHits(int stage_x, int stage_y, bool force_bounding_box) const {
    auto* dispatcher = eventDispatcher();
    if (dispatcher == nullptr) {
        return {};
    }

    auto sprites = hitSprites();
    auto has_custom_cursor = [this](int channel) {
        if (stage_renderer_ == nullptr) {
            return false;
        }
        const auto sprite = stage_renderer_->spriteRegistry().get(channel);
        return sprite != nullptr &&
               (sprite->hasBitmapCursor() ||
                (sprite->cursor() != CursorManager::DEFAULT_CURSOR && sprite->cursor() != CursorManager::ARROW_CURSOR));
    };
    auto has_authored_cursor_behavior = [&sprites, &has_custom_cursor](int channel) {
        const bool has_behavior = std::ranges::any_of(sprites, [channel](const auto& sprite) {
            return sprite.channel() == channel && sprite.hasBehaviors();
        });
        return has_behavior && has_custom_cursor(channel);
    };
    auto is_interactive = [dispatcher, &has_authored_cursor_behavior](int channel) {
        return dispatcher->isSpriteMouseInteractive(channel) || has_authored_cursor_behavior(channel);
    };
    auto hit_channels = input::HitTester::hitTestAll(
        sprites,
        stage_x,
        stage_y,
        [force_bounding_box, &is_interactive](int channel) {
            return force_bounding_box && is_interactive(channel);
        });
    if (force_bounding_box && stage_renderer_ != nullptr) {
        for (const auto& [channel, sprite] : stage_renderer_->spriteRegistry().getAll()) {
            if (sprite == nullptr || !sprite->isVisible() || !is_interactive(channel)) {
                continue;
            }
            const int left = sprite->locH();
            const int top = sprite->locV();
            const int right = left + sprite->width();
            const int bottom = top + sprite->height();
            if (stage_x >= left && stage_x < right && stage_y >= top && stage_y < bottom &&
                std::ranges::find(hit_channels, channel) == hit_channels.end()) {
                hit_channels.push_back(channel);
            }
        }
    }

    std::vector<int> interactive;
    interactive.reserve(hit_channels.size());
    std::copy_if(
        hit_channels.begin(),
        hit_channels.end(),
        std::back_inserter(interactive),
        is_interactive);
    return interactive;
}

std::vector<int> InputHandler::getInteractiveHitPath(int stage_x, int stage_y) const {
    auto exact_hits = getInteractiveHits(stage_x, stage_y, false);
    auto bounding_hits = getInteractiveHits(stage_x, stage_y, true);
    auto has_pointer_cursor = [this](int channel) {
        if (stage_renderer_ == nullptr) {
            return false;
        }
        const auto sprite = stage_renderer_->spriteRegistry().get(channel);
        return sprite != nullptr &&
               (sprite->hasBitmapCursor() ||
                (sprite->cursor() != CursorManager::DEFAULT_CURSOR && sprite->cursor() != CursorManager::ARROW_CURSOR));
    };
    std::vector<int> hits;
    hits.reserve(exact_hits.size() + bounding_hits.size());
    auto append_unique = [&hits](int channel) {
        if (std::ranges::find(hits, channel) == hits.end()) {
            hits.push_back(channel);
        }
    };

    for (const int channel : exact_hits) {
        if (has_pointer_cursor(channel)) {
            append_unique(channel);
        }
    }
    for (const int channel : bounding_hits) {
        if (has_pointer_cursor(channel)) {
            append_unique(channel);
        }
    }
    for (const int channel : exact_hits) {
        append_unique(channel);
    }
    for (const int channel : bounding_hits) {
        append_unique(channel);
    }
    return hits;
}

void InputHandler::dispatchRolloverEvents() {
    if (input_state_ == nullptr) {
        return;
    }

    auto* dispatcher = eventDispatcher();
    if (dispatcher == nullptr) {
        return;
    }

    const int current_rollover = input_state_->rolloverSprite();
    if (current_rollover != previous_rollover_sprite_) {
        if (previous_rollover_sprite_ > 0) {
            dispatcher->dispatchSpriteEvent(previous_rollover_sprite_, PlayerEvent::MouseLeave);
        }
        if (current_rollover > 0) {
            dispatcher->dispatchSpriteEvent(current_rollover, PlayerEvent::MouseEnter);
        }
        previous_rollover_sprite_ = current_rollover;
    }

    if (current_rollover > 0) {
        dispatcher->dispatchSpriteEvent(current_rollover, PlayerEvent::MouseWithin);
    }
}

void InputHandler::dispatchInputEvent(const input::InputEvent& input_event) {
    if (input_state_ == nullptr) {
        return;
    }

    auto* dispatcher = eventDispatcher();
    if (dispatcher == nullptr) {
        return;
    }

    switch (input_event.type) {
        case input::InputEventType::MouseDown: {
            const auto hit_path = getInteractiveHitPath(input_event.stage_x, input_event.stage_y);
            const int hit_sprite = hit_path.empty() ? 0 : hit_path.front();
            const int stage_hit_sprite = hitTestStage(input_event.stage_x, input_event.stage_y);
            const int editable_hit_sprite = hitTestEditableField(input_event.stage_x, input_event.stage_y);
            const int last_clicked = input_state_->clickOnSprite();
            if (last_clicked > 0 && std::ranges::find(hit_path, last_clicked) == hit_path.end()) {
                dispatcher->dispatchSpriteEvent(last_clicked, "mouseUpOutSide");
            }

            input_state_->setClickOnSprite(hit_sprite);
            autoFocusEditableField(editable_hit_sprite > 0 ? editable_hit_sprite : stage_hit_sprite,
                                   input_event.stage_x,
                                   input_event.stage_y);
            dispatcher->resetEventStopped();
            if (!dispatchLegacyEventScript(PlayerEvent::MouseDown) && !dispatcher->isEventStopped()) {
                for (const int channel : hit_path) {
                    dispatcher->dispatchSpriteEvent(channel, PlayerEvent::MouseDown);
                    if (dispatcher->isEventStopped() || dispatcher->isPropagationStopped()) {
                        break;
                    }
                }
            }
            if (!dispatcher->isEventStopped() && !dispatcher->isPropagationStopped()) {
                dispatcher->dispatchFrameAndMovieEvent(PlayerEvent::MouseDown);
            }
            break;
        }
        case input::InputEventType::MouseUp: {
            const int pressed_sprite = input_state_->clickOnSprite();
            const auto release_path = getInteractiveHitPath(input_event.stage_x, input_event.stage_y);
            const int release_sprite = release_path.empty() ? 0 : release_path.front();
            dispatcher->resetEventStopped();
            const bool legacy_consumed = dispatchLegacyEventScript(PlayerEvent::MouseUp);
            if (!legacy_consumed && !dispatcher->isEventStopped()) {
                if (pressed_sprite > 0 && std::ranges::find(release_path, pressed_sprite) != release_path.end()) {
                    for (const int channel : release_path) {
                        dispatcher->dispatchSpriteEvent(channel, PlayerEvent::MouseUp);
                        if (dispatcher->isEventStopped() || dispatcher->isPropagationStopped()) {
                            break;
                        }
                    }
                } else if (pressed_sprite > 0 && dispatcher->spriteHasHandler(pressed_sprite, "mouseUpOutSide")) {
                    dispatcher->dispatchSpriteEvent(pressed_sprite, "mouseUpOutSide");
                } else if (pressed_sprite > 0 && dispatcher->spriteHasHandler(pressed_sprite, handlerName(PlayerEvent::MouseUp))) {
                    dispatcher->dispatchSpriteEvent(pressed_sprite, PlayerEvent::MouseUp);
                } else if (release_sprite > 0) {
                    for (const int channel : release_path) {
                        dispatcher->dispatchSpriteEvent(channel, PlayerEvent::MouseUp);
                        if (dispatcher->isEventStopped() || dispatcher->isPropagationStopped()) {
                            break;
                        }
                    }
                }
            }
            input_state_->setClickOnSprite(0);
            if (!legacy_consumed && !dispatcher->isEventStopped() && !dispatcher->isPropagationStopped()) {
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
            input_state_->setLastKey(input_event.key_char);
            input_state_->setLastKeyCode(input_event.key_code);
            const int focus_sprite = input_state_->keyboardFocusSprite();
            if (focus_sprite > 0) {
                handleEditableFieldInput(focus_sprite, input_event.key_char);
            }
            if (!dispatchLegacyEventScript(PlayerEvent::KeyDown) && !dispatcher->isEventStopped()) {
                if (focus_sprite > 0) {
                    dispatcher->dispatchSpriteEvent(focus_sprite, PlayerEvent::KeyDown);
                }
                dispatcher->dispatchFrameAndMovieEvent(PlayerEvent::KeyDown);
            }
            break;
        }
        case input::InputEventType::KeyUp: {
            dispatcher->resetEventStopped();
            input_state_->setLastKey(input_event.key_char);
            input_state_->setLastKeyCode(input_event.key_code);
            if (!dispatchLegacyEventScript(PlayerEvent::KeyUp) && !dispatcher->isEventStopped()) {
                const int focus_sprite = input_state_->keyboardFocusSprite();
                if (focus_sprite > 0) {
                    dispatcher->dispatchSpriteEvent(focus_sprite, PlayerEvent::KeyUp);
                }
                dispatcher->dispatchFrameAndMovieEvent(PlayerEvent::KeyUp);
            }
            break;
        }
    }
}

bool InputHandler::dispatchLegacyEventScript(PlayerEvent event) {
    if (!legacy_event_script_dispatcher_) {
        return false;
    }

    const LegacyEventScriptResult result = legacy_event_script_dispatcher_(event);
    return result.handled && !result.passed;
}

int InputHandler::hitTestStage(int stage_x, int stage_y) const {
    return input::HitTester::hitTest(hitSprites(), stage_x, stage_y);
}

int InputHandler::hitTestEditableField(int stage_x, int stage_y) const {
    const auto sprites = hitSprites();
    for (auto it = sprites.rbegin(); it != sprites.rend(); ++it) {
        const auto& sprite = *it;
        if (!sprite.isVisible() || sprite.channel() <= 0 || sprite.width() <= 0 || sprite.height() <= 0) {
            continue;
        }
        if (stage_x < sprite.x() || stage_y < sprite.y() ||
            stage_x >= sprite.x() + sprite.width() ||
            stage_y >= sprite.y() + sprite.height()) {
            continue;
        }
        auto member = resolveSpriteMember(sprite.channel());
        if (member != nullptr && isEditableFieldSprite(sprite.channel(), *member)) {
            return sprite.channel();
        }
    }
    return 0;
}

int InputHandler::hitTestMoveableSprite(int stage_x, int stage_y) const {
    const auto hit_channels = input::HitTester::hitTestAll(hitSprites(), stage_x, stage_y, [](int) {
        return false;
    });
    const auto found = std::ranges::find_if(hit_channels, [this](int channel) {
        return isMoveableSprite(channel);
    });
    return found == hit_channels.end() ? 0 : *found;
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
    if (stage_renderer_ == nullptr || cast_lib_manager_ == nullptr || channel <= 0) {
        return nullptr;
    }
    const auto sprite = stage_renderer_->spriteRegistry().get(channel);
    if (sprite == nullptr || sprite->effectiveCastMember() <= 0) {
        return nullptr;
    }
    return cast_lib_manager_->resolveMember(sprite->effectiveCastLib(), sprite->effectiveCastMember());
}

std::optional<InputHandler::FocusedField> InputHandler::focusedEditableField() const {
    if (input_state_ == nullptr) {
        return std::nullopt;
    }
    const int focus_channel = input_state_->keyboardFocusSprite();
    if (focus_channel <= 0) {
        return std::nullopt;
    }
    auto member = resolveSpriteMember(focus_channel);
    if (member == nullptr || !canReceiveKeyboardTextInput(focus_channel, *member)) {
        return std::nullopt;
    }
    auto sprite = findHitSprite(focus_channel);
    if (!sprite.has_value()) {
        return std::nullopt;
    }
    return FocusedField{*sprite, std::move(member)};
}

std::string InputHandler::memberText(const ::libreshockwave::cast::CastMember& member) const {
    if (cast_lib_manager_ == nullptr) {
        return member.textContent();
    }
    return cast_lib_manager_->getMemberProp(member.castLib(), member.memberNum(), "text").stringValue();
}

std::pair<int, int> InputHandler::clampedSelection(int sel_start, int sel_end, int text_length) {
    const int start = std::clamp(sel_start, 0, text_length);
    const int end = std::clamp(sel_end, 0, text_length);
    return {std::min(start, end), std::max(start, end)};
}

bool InputHandler::isMoveableSprite(int channel) const {
    if (stage_renderer_ == nullptr || channel <= 0) {
        return false;
    }
    const auto sprite = stage_renderer_->spriteRegistry().get(channel);
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
    if (member.rawChunk() != nullptr && member.memberType() == ::libreshockwave::cast::MemberType::Text) {
        return true;
    }
    if (stage_renderer_ == nullptr || channel <= 0) {
        return false;
    }
    const auto sprite = stage_renderer_->spriteRegistry().get(channel);
    if (sprite == nullptr) {
        return false;
    }
    const auto editable = sprite->legacyProperty("editabletext");
    if (editable.has_value() && editable->boolValue()) {
        return true;
    }

    auto* dispatcher = eventDispatcher();
    if (dispatcher != nullptr && dispatcher->spriteHasHandler(channel, handlerName(PlayerEvent::KeyDown))) {
        return true;
    }

    return false;
}

bool InputHandler::canReceiveKeyboardTextInput(int channel, const ::libreshockwave::cast::CastMember& member) const {
    if (!member.isTextLike()) {
        return false;
    }
    if (isEditableFieldSprite(channel, member)) {
        return true;
    }
    return input_state_ != nullptr && input_state_->keyboardFocusSprite() == channel;
}

bool InputHandler::setSpriteLoc(int channel, int loc_h, int loc_v) const {
    if (channel <= 0) {
        return false;
    }
    if (sprite_location_setter_ && sprite_location_setter_(channel, loc_h, loc_v)) {
        return true;
    }
    if (stage_renderer_ == nullptr) {
        return false;
    }
    const auto sprite = stage_renderer_->spriteRegistry().get(channel);
    if (sprite == nullptr) {
        return false;
    }
    sprite->setLocH(loc_h);
    sprite->setLocV(loc_v);
    return true;
}

void InputHandler::bumpSpriteRevision() {
    if (stage_renderer_ != nullptr) {
        stage_renderer_->spriteRegistry().bumpRevision();
    }
}

void InputHandler::beginMoveableSpriteDrag(int stage_x, int stage_y) {
    moveable_drag_.reset();
    if (stage_renderer_ == nullptr) {
        return;
    }

    const int hit_channel = hitTestMoveableSprite(stage_x, stage_y);
    if (hit_channel <= 0) {
        return;
    }

    const auto sprite = stage_renderer_->spriteRegistry().get(hit_channel);
    if (sprite == nullptr) {
        return;
    }

    moveable_drag_ = MoveableDrag{hit_channel, stage_x - sprite->locH(), stage_y - sprite->locV()};
}

void InputHandler::updateMoveableSpriteDrag(int stage_x, int stage_y) {
    if (!moveable_drag_.has_value() || stage_renderer_ == nullptr) {
        return;
    }
    if (!input_state_->isMouseDown() || !isMoveableSprite(moveable_drag_->channel)) {
        endMoveableSpriteDrag();
        return;
    }

    if (setSpriteLoc(moveable_drag_->channel, stage_x - moveable_drag_->offset_h, stage_y - moveable_drag_->offset_v)) {
        bumpSpriteRevision();
    }
}

void InputHandler::endMoveableSpriteDrag() {
    moveable_drag_.reset();
}

void InputHandler::autoFocusEditableField(int hit_channel, int stage_x, int stage_y) {
    if (input_state_ == nullptr) {
        return;
    }
    auto member = resolveSpriteMember(hit_channel);
    auto sprite = findHitSprite(hit_channel);
    if (member != nullptr && sprite.has_value() && isEditableFieldSprite(hit_channel, *member) &&
        member->isTextLike()) {
        input_state_->setKeyboardFocusSprite(hit_channel);
        const int local_x = stage_x - sprite->x();
        const int local_y = stage_y - sprite->y();
        const int char_pos = cast_lib_manager_->locToCharPos(
            member->castLib(),
            member->memberNum(),
            local_x,
            local_y,
            sprite->width());
        input_state_->setSelStart(char_pos);
        input_state_->setSelEnd(char_pos);
        input_state_->resetCaretBlink();
        return;
    }

    input_state_->setKeyboardFocusSprite(0);
}

void InputHandler::handleEditableFieldInput(int channel, const std::string& key_char) {
    if (input_state_ == nullptr || cast_lib_manager_ == nullptr) {
        return;
    }

    auto member = resolveSpriteMember(channel);
    if (member == nullptr || !canReceiveKeyboardTextInput(channel, *member)) {
        return;
    }

    std::string text = cast_lib_manager_->getMemberProp(member->castLib(), member->memberNum(), "text").stringValue();
    int sel_start = std::clamp(input_state_->selStart(), 0, static_cast<int>(text.size()));
    int sel_end = std::clamp(input_state_->selEnd(), 0, static_cast<int>(text.size()));
    const int sel_min = std::min(sel_start, sel_end);
    const int sel_max = std::max(sel_start, sel_end);
    const int key_code = input_state_->lastKeyCode();

    // Printable text wins over key codes. A punctuation code can collide with
    // a navigation or editing code in the host keyboard mapping.
    if (key_char.size() == 1 && std::isprint(static_cast<unsigned char>(key_char.front())) != 0) {
        text.replace(static_cast<std::size_t>(sel_min), static_cast<std::size_t>(sel_max - sel_min), key_char);
        sel_start = sel_end = sel_min + 1;
        member->setDynamicText(text);
    } else if (key_code == 51) {
        if (sel_min != sel_max) {
            text.erase(static_cast<std::size_t>(sel_min), static_cast<std::size_t>(sel_max - sel_min));
            sel_start = sel_end = sel_min;
        } else if (sel_start > 0) {
            text.erase(static_cast<std::size_t>(sel_start - 1), 1);
            sel_start = sel_end = sel_start - 1;
        }
        member->setDynamicText(text);
    } else if (key_code == 123) {
        sel_start = sel_end = std::max(0, sel_min - (sel_min == sel_max ? 1 : 0));
    } else if (key_code == 124) {
        sel_start = sel_end = std::min(static_cast<int>(text.size()), sel_max + (sel_min == sel_max ? 1 : 0));
    } else if (key_code == 48) {
        tabToNextField(channel, input_state_->isShiftDown());
        return;
    } else if (key_code == 36) {
        return;
    } else {
        return;
    }

    input_state_->setSelStart(sel_start);
    input_state_->setSelEnd(sel_end);
    input_state_->resetCaretBlink();
}

void InputHandler::tabToNextField(int current_channel, bool reverse) {
    if (input_state_ == nullptr || stage_renderer_ == nullptr || cast_lib_manager_ == nullptr) {
        return;
    }

    std::vector<int> editable_channels;
    for (const auto& [channel, state] : stage_renderer_->spriteRegistry().getAll()) {
        if (state == nullptr || state->effectiveCastMember() <= 0) {
            continue;
        }
        auto member = cast_lib_manager_->resolveMember(state->effectiveCastLib(), state->effectiveCastMember());
        if (member != nullptr && isEditableFieldSprite(channel, *member)) {
            editable_channels.push_back(channel);
        }
    }
    if (editable_channels.empty()) {
        return;
    }
    std::ranges::sort(editable_channels);

    const auto found = std::ranges::find(editable_channels, current_channel);
    const int index = found == editable_channels.end()
        ? -1
        : static_cast<int>(std::distance(editable_channels.begin(), found));
    int next_index = 0;
    if (reverse) {
        next_index = index <= 0 ? static_cast<int>(editable_channels.size()) - 1 : index - 1;
    } else {
        next_index = index < 0 || index >= static_cast<int>(editable_channels.size()) - 1 ? 0 : index + 1;
    }

    const int next_channel = editable_channels[static_cast<std::size_t>(next_index)];
    input_state_->setKeyboardFocusSprite(next_channel);
    auto member = resolveSpriteMember(next_channel);
    const std::string text = member != nullptr
        ? cast_lib_manager_->getMemberProp(member->castLib(), member->memberNum(), "text").stringValue()
        : "";
    input_state_->setSelStart(0);
    input_state_->setSelEnd(static_cast<int>(text.size()));
    input_state_->resetCaretBlink();
}

} // namespace libreshockwave::player
