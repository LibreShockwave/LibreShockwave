#pragma once

#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/player/input/editable_field_overlay.hpp"
#include "libreshockwave/player/render/pipeline/render_pipeline_trace.hpp"
#include "libreshockwave/player/render/pipeline/render_sprite.hpp"

namespace libreshockwave::player::render::pipeline {

struct FrameSnapshot {
    int frame_number{0};
    int stage_width{0};
    int stage_height{0};
    int background_color{0};
    std::vector<RenderSprite> sprites;
    std::string debug_info;
    std::shared_ptr<const bitmap::Bitmap> stage_image;
    int bake_tick{0};
    RenderPipelineTrace pipeline_trace;
    // Editing chrome (typing caret, selection) captured with this snapshot on
    // the thread that owns the input state. Presentation paths compose it via
    // renderPresentableFrame(); Lingo-visible stage images must ignore it.
    input::EditableFieldOverlay editable_overlay;

    [[nodiscard]] bitmap::Bitmap renderFrame() const;

    /// Composed frame plus the captured editable-field overlay, ready to show
    /// on a host screen. Example: widget->show(snapshot.renderPresentableFrame()).
    [[nodiscard]] bitmap::Bitmap renderPresentableFrame() const;
};

} // namespace libreshockwave::player::render::pipeline
