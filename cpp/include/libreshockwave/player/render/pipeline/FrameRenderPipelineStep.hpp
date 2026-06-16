#pragma once

#include <string_view>

namespace libreshockwave::player::render::pipeline {

class FrameRenderPipelineContext;

class FrameRenderPipelineStep {
public:
    virtual ~FrameRenderPipelineStep() = default;

    [[nodiscard]] virtual std::string_view name() const = 0;
    virtual void execute(FrameRenderPipelineContext& context) = 0;
};

} // namespace libreshockwave::player::render::pipeline
