#include "libreshockwave/player/render/pipeline/frame_snapshot.hpp"

#include "libreshockwave/player/input/editable_field_overlay.hpp"
#include "libreshockwave/player/render/output/software_frame_renderer.hpp"

namespace libreshockwave::player::render::pipeline {

bitmap::Bitmap FrameSnapshot::renderFrame() const {
    return libreshockwave::player::render::output::SoftwareFrameRenderer::renderFrame(*this, stage_width, stage_height);
}

bitmap::Bitmap FrameSnapshot::renderPresentableFrame() const {
    auto frame = renderFrame();
    input::applyEditableFieldOverlay(frame, editable_overlay);
    return frame;
}

} // namespace libreshockwave::player::render::pipeline
