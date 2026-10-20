#include "libreshockwave/player/render/pipeline/frame_render_pipeline_context.hpp"

#include <utility>

namespace libreshockwave::player::render::pipeline {

FrameRenderPipelineContext::FrameRenderPipelineContext(int frame_number,
                                                       int stage_width,
                                                       int stage_height,
                                                       int background_color,
                                                       std::shared_ptr<const bitmap::Bitmap> stage_image,
                                                       std::string debug_info)
    : frame_number_(frame_number),
      stage_width_(stage_width),
      stage_height_(stage_height),
      background_color_(background_color),
      stage_image_(std::move(stage_image)),
      debug_info_(std::move(debug_info)) {}

int FrameRenderPipelineContext::frameNumber() const { return frame_number_; }
int FrameRenderPipelineContext::stageWidth() const { return stage_width_; }
int FrameRenderPipelineContext::stageHeight() const { return stage_height_; }
int FrameRenderPipelineContext::backgroundColor() const { return background_color_; }
std::shared_ptr<const bitmap::Bitmap> FrameRenderPipelineContext::stageImage() const { return stage_image_; }
const std::string& FrameRenderPipelineContext::debugInfo() const { return debug_info_; }
std::vector<RenderSprite>& FrameRenderPipelineContext::sprites() { return sprites_; }
const std::vector<RenderSprite>& FrameRenderPipelineContext::sprites() const { return sprites_; }
std::set<int>& FrameRenderPipelineContext::renderedChannels() { return rendered_channels_; }
const std::set<int>& FrameRenderPipelineContext::renderedChannels() const { return rendered_channels_; }

void FrameRenderPipelineContext::addTrace(std::string step_name, std::string summary) {
    trace_.push_back(RenderPipelineStepTrace{std::move(step_name), std::move(summary), static_cast<int>(sprites_.size())});
}

RenderPipelineTrace FrameRenderPipelineContext::buildTrace() const {
    return trace_.empty() ? RenderPipelineTrace::empty() : RenderPipelineTrace(trace_);
}

const std::optional<FrameSnapshot>& FrameRenderPipelineContext::snapshot() const {
    return snapshot_;
}

void FrameRenderPipelineContext::setSnapshot(FrameSnapshot snapshot) {
    snapshot_ = std::move(snapshot);
}

} // namespace libreshockwave::player::render::pipeline
