#include "libreshockwave/player/render/pipeline/FrameSnapshot.hpp"

#include "libreshockwave/player/render/output/SoftwareFrameRenderer.hpp"

namespace libreshockwave::player::render::pipeline {

bitmap::Bitmap FrameSnapshot::renderFrame() const {
    return libreshockwave::player::render::output::SoftwareFrameRenderer::renderFrame(*this, stageWidth, stageHeight);
}

} // namespace libreshockwave::player::render::pipeline
