#pragma once

#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/player/render/pipeline/frame_render_pipeline_context.hpp"
#include "libreshockwave/player/render/pipeline/frame_render_pipeline_step.hpp"
#include "libreshockwave/player/render/pipeline/frame_snapshot.hpp"

namespace libreshockwave::player::render::pipeline {

class SpriteBaker;
class StageRenderer;

class FrameRenderPipeline {
public:
    FrameRenderPipeline() = default;
    FrameRenderPipeline(StageRenderer* stage_renderer, SpriteBaker* sprite_baker);

    void registerStep(std::shared_ptr<FrameRenderPipelineStep> step);
    [[nodiscard]] const std::vector<std::shared_ptr<FrameRenderPipelineStep>>& steps() const;
    [[nodiscard]] int stepCount() const;

    [[nodiscard]] FrameSnapshot renderFrame(int frame_number) const;
    [[nodiscard]] FrameSnapshot renderFrame(int frame_number,
                                            int stage_width,
                                            int stage_height,
                                            int background_color,
                                            std::shared_ptr<const bitmap::Bitmap> stage_image,
                                            std::string debug_info) const;

private:
    void registerDefaultSteps();

    StageRenderer* stage_renderer_{nullptr};
    SpriteBaker* sprite_baker_{nullptr};
    std::vector<std::shared_ptr<FrameRenderPipelineStep>> steps_;
};

} // namespace libreshockwave::player::render::pipeline
