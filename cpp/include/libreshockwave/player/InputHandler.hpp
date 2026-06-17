#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/player/PlayerEvent.hpp"
#include "libreshockwave/player/input/InputEvent.hpp"
#include "libreshockwave/player/render/pipeline/RenderSprite.hpp"

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
    struct CaretInfo {
        int x{0};
        int y{0};
        int height{0};

        friend bool operator==(const CaretInfo&, const CaretInfo&) = default;
    };

    struct SelectionRect {
        int x{0};
        int y{0};
        int width{0};
        int height{0};

        friend bool operator==(const SelectionRect&, const SelectionRect&) = default;
    };

    struct EditableFieldOverlay {
        std::optional<CaretInfo> caret;
        std::vector<SelectionRect> selectionRects;

        [[nodiscard]] bool empty() const {
            return !caret.has_value() && selectionRects.empty();
        }

        friend bool operator==(const EditableFieldOverlay&, const EditableFieldOverlay&) = default;
    };

    using CurrentFrameSupplier = std::function<int()>;
    using EventDispatcherSupplier = std::function<event::EventDispatcher*()>;
    using HitSpritesSupplier = std::function<std::vector<render::pipeline::RenderSprite>()>;
    struct LegacyEventScriptResult {
        bool handled{false};
        bool passed{false};
    };
    using LegacyEventScriptDispatcher = std::function<LegacyEventScriptResult(PlayerEvent event)>;
    using SpriteLocationSetter = std::function<bool(int channel, int locH, int locV)>;

    InputHandler(input::InputState* inputState = nullptr,
                 render::pipeline::StageRenderer* stageRenderer = nullptr,
                 event::EventDispatcher* eventDispatcher = nullptr,
                 cast::CastLibManager* castLibManager = nullptr);

    void setInputState(input::InputState* inputState);
    void setStageRenderer(render::pipeline::StageRenderer* stageRenderer);
    void setEventDispatcher(event::EventDispatcher* eventDispatcher);
    void setCastLibManager(cast::CastLibManager* castLibManager);
    void setEventDispatcherSupplier(EventDispatcherSupplier supplier);
    void setCurrentFrameSupplier(CurrentFrameSupplier supplier);
    void setHitSpritesSupplier(HitSpritesSupplier supplier);
    void setLegacyEventScriptDispatcher(LegacyEventScriptDispatcher dispatcher);
    void setSpriteLocationSetter(SpriteLocationSetter setter);

    [[nodiscard]] int previousRolloverSprite() const;
    [[nodiscard]] int hitTestExact(int stageX, int stageY) const;

    void onMouseMove(int stageX, int stageY);
    void onMouseDown(int stageX, int stageY, bool rightButton = false);
    void onMouseUp(int stageX, int stageY, bool rightButton = false);
    void onBlur();
    void onKeyDown(int directorKeyCode, std::string keyChar, bool shift, bool ctrl, bool alt);
    void onKeyUp(int directorKeyCode, std::string keyChar, bool shift, bool ctrl, bool alt);
    [[nodiscard]] std::optional<CaretInfo> getCaretInfo() const;
    [[nodiscard]] std::vector<SelectionRect> getSelectionInfo() const;
    [[nodiscard]] EditableFieldOverlay editableFieldOverlay() const;
    static void applyEditableFieldOverlay(bitmap::Bitmap& bitmap, const EditableFieldOverlay& overlay);
    [[nodiscard]] static bitmap::Bitmap withEditableFieldOverlay(const bitmap::Bitmap& bitmap,
                                                                 const EditableFieldOverlay& overlay);
    void onPasteText(std::string pasteText);
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
        int offsetH{0};
        int offsetV{0};
    };

    [[nodiscard]] event::EventDispatcher* eventDispatcher() const;
    [[nodiscard]] std::vector<render::pipeline::RenderSprite> hitSprites() const;
    [[nodiscard]] int hitTestStage(int stageX, int stageY) const;
    [[nodiscard]] int hitTestEditableField(int stageX, int stageY) const;
    [[nodiscard]] int hitTestMoveableSprite(int stageX, int stageY) const;
    [[nodiscard]] std::vector<int> getInteractiveHits(int stageX, int stageY, bool forceBoundingBox) const;
    [[nodiscard]] std::shared_ptr<::libreshockwave::cast::CastMember> resolveSpriteMember(int channel) const;
    [[nodiscard]] std::optional<render::pipeline::RenderSprite> findHitSprite(int channel) const;
    [[nodiscard]] std::optional<FocusedField> focusedEditableField() const;
    [[nodiscard]] std::string memberText(const ::libreshockwave::cast::CastMember& member) const;
    [[nodiscard]] static std::pair<int, int> clampedSelection(int selStart, int selEnd, int textLength);
    [[nodiscard]] bool isMoveableSprite(int channel) const;
    [[nodiscard]] bool isEditableFieldSprite(int channel, const ::libreshockwave::cast::CastMember& member) const;
    [[nodiscard]] bool setSpriteLoc(int channel, int locH, int locV) const;
    void bumpSpriteRevision();
    void beginMoveableSpriteDrag(int stageX, int stageY);
    void updateMoveableSpriteDrag(int stageX, int stageY);
    void endMoveableSpriteDrag();
    void autoFocusEditableField(int hitChannel, int stageX, int stageY);
    void handleEditableFieldInput(int channel, const std::string& keyChar);
    void tabToNextField(int currentChannel, bool reverse);
    void dispatchRolloverEvents();
    void dispatchInputEvent(const input::InputEvent& inputEvent);
    [[nodiscard]] bool dispatchLegacyEventScript(PlayerEvent event);

    input::InputState* inputState_{nullptr};
    render::pipeline::StageRenderer* stageRenderer_{nullptr};
    event::EventDispatcher* eventDispatcher_{nullptr};
    cast::CastLibManager* castLibManager_{nullptr};
    EventDispatcherSupplier eventDispatcherSupplier_;
    CurrentFrameSupplier currentFrameSupplier_;
    HitSpritesSupplier hitSpritesSupplier_;
    LegacyEventScriptDispatcher legacyEventScriptDispatcher_;
    SpriteLocationSetter spriteLocationSetter_;
    std::optional<MoveableDrag> moveableDrag_;
    int previousRolloverSprite_{0};
};

} // namespace libreshockwave::player
