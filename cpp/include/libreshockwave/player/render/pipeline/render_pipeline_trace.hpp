#pragma once

#include <string>
#include <vector>

namespace libreshockwave::player::render::pipeline {

struct RenderPipelineStepTrace {
    std::string step_name;
    std::string summary;
    int sprite_count{0};

    friend bool operator==(const RenderPipelineStepTrace&, const RenderPipelineStepTrace&) = default;
};

class RenderPipelineTrace {
public:
    RenderPipelineTrace() = default;
    explicit RenderPipelineTrace(std::vector<RenderPipelineStepTrace> steps);

    [[nodiscard]] const std::vector<RenderPipelineStepTrace>& steps() const;
    [[nodiscard]] static const RenderPipelineTrace& empty();

    friend bool operator==(const RenderPipelineTrace&, const RenderPipelineTrace&) = default;

private:
    std::vector<RenderPipelineStepTrace> steps_;
};

} // namespace libreshockwave::player::render::pipeline
