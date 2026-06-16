#include "libreshockwave/player/render/pipeline/RenderPipelineTrace.hpp"

#include <utility>

namespace libreshockwave::player::render::pipeline {

RenderPipelineTrace::RenderPipelineTrace(std::vector<RenderPipelineStepTrace> steps)
    : steps_(std::move(steps)) {}

const std::vector<RenderPipelineStepTrace>& RenderPipelineTrace::steps() const {
    return steps_;
}

const RenderPipelineTrace& RenderPipelineTrace::empty() {
    static const RenderPipelineTrace trace;
    return trace;
}

} // namespace libreshockwave::player::render::pipeline
