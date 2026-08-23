#include "libreshockwave/player/render/pipeline/FrameSnapshot.hpp"

#include "libreshockwave/player/input/EditableFieldOverlay.hpp"
#include "libreshockwave/player/render/output/SoftwareFrameRenderer.hpp"

namespace libreshockwave::player::render::pipeline {

bitmap::Bitmap FrameSnapshot::renderFrame() const {
    return libreshockwave::player::render::output::SoftwareFrameRenderer::renderFrame(*this, stageWidth, stageHeight);
}

bitmap::Bitmap FrameSnapshot::renderPresentableFrame() const {
    auto frame = renderFrame();
    input::applyEditableFieldOverlay(frame, editableOverlay);
    return frame;
}

} // namespace libreshockwave::player::render::pipeline
