#include "libreshockwave/player/render/pipeline/FrameRenderPipelineContext.hpp"

#include <utility>

namespace libreshockwave::player::render::pipeline {

FrameRenderPipelineContext::FrameRenderPipelineContext(int frameNumber,
                                                       int stageWidth,
                                                       int stageHeight,
                                                       int backgroundColor,
                                                       std::shared_ptr<const bitmap::Bitmap> stageImage,
                                                       std::string debugInfo)
    : frameNumber_(frameNumber),
      stageWidth_(stageWidth),
      stageHeight_(stageHeight),
      backgroundColor_(backgroundColor),
      stageImage_(std::move(stageImage)),
      debugInfo_(std::move(debugInfo)) {}

int FrameRenderPipelineContext::frameNumber() const { return frameNumber_; }
int FrameRenderPipelineContext::stageWidth() const { return stageWidth_; }
int FrameRenderPipelineContext::stageHeight() const { return stageHeight_; }
int FrameRenderPipelineContext::backgroundColor() const { return backgroundColor_; }
std::shared_ptr<const bitmap::Bitmap> FrameRenderPipelineContext::stageImage() const { return stageImage_; }
const std::string& FrameRenderPipelineContext::debugInfo() const { return debugInfo_; }
std::vector<RenderSprite>& FrameRenderPipelineContext::sprites() { return sprites_; }
const std::vector<RenderSprite>& FrameRenderPipelineContext::sprites() const { return sprites_; }
std::set<int>& FrameRenderPipelineContext::renderedChannels() { return renderedChannels_; }
const std::set<int>& FrameRenderPipelineContext::renderedChannels() const { return renderedChannels_; }

void FrameRenderPipelineContext::addTrace(std::string stepName, std::string summary) {
    trace_.push_back(RenderPipelineStepTrace{std::move(stepName), std::move(summary), static_cast<int>(sprites_.size())});
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
