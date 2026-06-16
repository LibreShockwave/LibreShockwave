#pragma once

#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/bitmap/Bitmap.hpp"
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

    [[nodiscard]] bitmap::Bitmap renderFrame() const;
};

} // namespace libreshockwave::player::render::pipeline
