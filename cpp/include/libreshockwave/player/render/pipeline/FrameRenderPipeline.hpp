#pragma once

#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/player/render/pipeline/FrameRenderPipelineContext.hpp"
#include "libreshockwave/player/render/pipeline/FrameRenderPipelineStep.hpp"
#include "libreshockwave/player/render/pipeline/FrameSnapshot.hpp"

namespace libreshockwave::player::render::pipeline {

class SpriteBaker;
class StageRenderer;

class FrameRenderPipeline {
public:
    FrameRenderPipeline() = default;
    FrameRenderPipeline(StageRenderer* stageRenderer, SpriteBaker* spriteBaker);

    void registerStep(std::shared_ptr<FrameRenderPipelineStep> step);
    [[nodiscard]] const std::vector<std::shared_ptr<FrameRenderPipelineStep>>& steps() const;
    [[nodiscard]] int stepCount() const;

    [[nodiscard]] FrameSnapshot renderFrame(int frameNumber) const;
    [[nodiscard]] FrameSnapshot renderFrame(int frameNumber,
                                            int stageWidth,
                                            int stageHeight,
                                            int backgroundColor,
                                            std::shared_ptr<const bitmap::Bitmap> stageImage,
                                            std::string debugInfo) const;

private:
    void registerDefaultSteps();

    StageRenderer* stageRenderer_{nullptr};
    SpriteBaker* spriteBaker_{nullptr};
    std::vector<std::shared_ptr<FrameRenderPipelineStep>> steps_;
};

} // namespace libreshockwave::player::render::pipeline
