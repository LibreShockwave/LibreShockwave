#pragma once

#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/player/input/EditableFieldOverlay.hpp"
#include "libreshockwave/player/render/pipeline/RenderPipelineTrace.hpp"
#include "libreshockwave/player/render/pipeline/RenderSprite.hpp"

namespace libreshockwave::player::render::pipeline {

struct FrameSnapshot {
    int frameNumber{0};
    int stageWidth{0};
    int stageHeight{0};
    int backgroundColor{0};
    std::vector<RenderSprite> sprites;
    std::string debugInfo;
    std::shared_ptr<const bitmap::Bitmap> stageImage;
    int bakeTick{0};
    RenderPipelineTrace pipelineTrace;
    // Editing chrome (typing caret, selection) captured with this snapshot on
    // the thread that owns the input state. Presentation paths compose it via
    // renderPresentableFrame(); Lingo-visible stage images must ignore it.
    input::EditableFieldOverlay editableOverlay;

    [[nodiscard]] bitmap::Bitmap renderFrame() const;

    /// Composed frame plus the captured editable-field overlay, ready to show
    /// on a host screen. Example: widget->show(snapshot.renderPresentableFrame()).
    [[nodiscard]] bitmap::Bitmap renderPresentableFrame() const;
};

} // namespace libreshockwave::player::render::pipeline
