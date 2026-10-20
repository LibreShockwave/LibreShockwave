#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/player/player_event.hpp"
#include "libreshockwave/player/input/editable_field_overlay.hpp"
#include "libreshockwave/player/input/input_event.hpp"
#include "libreshockwave/player/render/pipeline/render_sprite.hpp"

namespace libreshockwave::cast {
class CastMember;
}

namespace libreshockwave::player::event {
class EventDispatcher;
}

namespace libreshockwave::player::cast {
class CastLibManager;
}

namespace libreshockwave::player::input {
class InputState;
}

namespace libreshockwave::player::render::pipeline {
class StageRenderer;
}

namespace libreshockwave::player {

class InputHandler {
public:
    using CurrentFrameSupplier = std::function<int()>;
    using EventDispatcherSupplier = std::function<event::EventDispatcher*()>;
    using HitSpritesSupplier = std::function<std::vector<render::pipeline::RenderSprite>()>;
    struct LegacyEventScriptResult {
        bool handled{false};
        bool passed{false};
    };
    using LegacyEventScriptDispatcher = std::function<LegacyEventScriptResult(PlayerEvent event)>;
    using SpriteLocationSetter = std::function<bool(int channel, int loc_h, int loc_v)>;

    InputHandler(input::InputState* input_state = nullptr,
                 render::pipeline::StageRenderer* stage_renderer = nullptr,
                 event::EventDispatcher* event_dispatcher = nullptr,
                 cast::CastLibManager* cast_lib_manager = nullptr);

    void setInputState(input::InputState* input_state);
    void setStageRenderer(render::pipeline::StageRenderer* stage_renderer);
    void setEventDispatcher(event::EventDispatcher* event_dispatcher);
    void setCastLibManager(cast::CastLibManager* cast_lib_manager);
    void setEventDispatcherSupplier(EventDispatcherSupplier supplier);
    void setCurrentFrameSupplier(CurrentFrameSupplier supplier);
    void setHitSpritesSupplier(HitSpritesSupplier supplier);
    void setLegacyEventScriptDispatcher(LegacyEventScriptDispatcher dispatcher);
    void setSpriteLocationSetter(SpriteLocationSetter setter);

    [[nodiscard]] int previousRolloverSprite() const;
    [[nodiscard]] int hitTestExact(int stage_x, int stage_y) const;

    void onMouseMove(int stage_x, int stage_y);
    void onMouseDown(int stage_x, int stage_y, bool right_button = false);
    void onMouseUp(int stage_x, int stage_y, bool right_button = false);
    void onBlur();
    void onKeyDown(int director_key_code, std::string key_char, bool shift, bool ctrl, bool alt);
    void onKeyUp(int director_key_code, std::string key_char, bool shift, bool ctrl, bool alt);
    [[nodiscard]] std::optional<input::CaretInfo> getCaretInfo() const;
    [[nodiscard]] std::vector<input::SelectionRect> getSelectionInfo() const;
    [[nodiscard]] input::EditableFieldOverlay editableFieldOverlay() const;
    void onPasteText(std::string paste_text);
    [[nodiscard]] std::optional<std::string> getSelectedText() const;
    [[nodiscard]] std::optional<std::string> cutSelectedText();
    void selectAll();

    [[nodiscard]] bool processInputEvents();

private:
    struct FocusedField {
        render::pipeline::RenderSprite sprite;
        std::shared_ptr<::libreshockwave::cast::CastMember> member;
    };

    struct MoveableDrag {
        int channel{0};
        int offset_h{0};
        int offset_v{0};
    };

    [[nodiscard]] event::EventDispatcher* eventDispatcher() const;
    [[nodiscard]] std::vector<render::pipeline::RenderSprite> hitSprites() const;
    [[nodiscard]] int hitTestStage(int stage_x, int stage_y) const;
    [[nodiscard]] int hitTestEditableField(int stage_x, int stage_y) const;
    [[nodiscard]] int hitTestMoveableSprite(int stage_x, int stage_y) const;
    [[nodiscard]] std::vector<int> getInteractiveHits(int stage_x, int stage_y, bool force_bounding_box) const;
    [[nodiscard]] std::vector<int> getInteractiveHitPath(int stage_x, int stage_y) const;
    [[nodiscard]] std::shared_ptr<::libreshockwave::cast::CastMember> resolveSpriteMember(int channel) const;
    [[nodiscard]] std::optional<render::pipeline::RenderSprite> findHitSprite(int channel) const;
    [[nodiscard]] std::optional<FocusedField> focusedEditableField() const;
    [[nodiscard]] std::string memberText(const ::libreshockwave::cast::CastMember& member) const;
    [[nodiscard]] static std::pair<int, int> clampedSelection(int sel_start, int sel_end, int text_length);
    [[nodiscard]] bool isMoveableSprite(int channel) const;
    [[nodiscard]] bool isEditableFieldSprite(int channel, const ::libreshockwave::cast::CastMember& member) const;
    [[nodiscard]] bool canReceiveKeyboardTextInput(int channel, const ::libreshockwave::cast::CastMember& member) const;
    [[nodiscard]] bool setSpriteLoc(int channel, int loc_h, int loc_v) const;
    void bumpSpriteRevision();
    void beginMoveableSpriteDrag(int stage_x, int stage_y);
    void updateMoveableSpriteDrag(int stage_x, int stage_y);
    void endMoveableSpriteDrag();
    void autoFocusEditableField(int hit_channel, int stage_x, int stage_y);
    void handleEditableFieldInput(int channel, const std::string& key_char);
    void tabToNextField(int current_channel, bool reverse);
    void dispatchRolloverEvents();
    void dispatchInputEvent(const input::InputEvent& input_event);
    [[nodiscard]] bool dispatchLegacyEventScript(PlayerEvent event);

    input::InputState* input_state_{nullptr};
    render::pipeline::StageRenderer* stage_renderer_{nullptr};
    event::EventDispatcher* event_dispatcher_{nullptr};
    cast::CastLibManager* cast_lib_manager_{nullptr};
    EventDispatcherSupplier event_dispatcher_supplier_;
    CurrentFrameSupplier current_frame_supplier_;
    HitSpritesSupplier hit_sprites_supplier_;
    LegacyEventScriptDispatcher legacy_event_script_dispatcher_;
    SpriteLocationSetter sprite_location_setter_;
    std::optional<MoveableDrag> moveable_drag_;
    int previous_rollover_sprite_{0};
};

} // namespace libreshockwave::player
