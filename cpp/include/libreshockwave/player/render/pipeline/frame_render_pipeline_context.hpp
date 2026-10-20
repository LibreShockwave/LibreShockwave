#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/player/render/pipeline/frame_snapshot.hpp"
#include "libreshockwave/player/render/pipeline/render_pipeline_trace.hpp"
#include "libreshockwave/player/render/pipeline/render_sprite.hpp"

namespace libreshockwave::player::render::pipeline {

class FrameRenderPipelineContext {
public:
    FrameRenderPipelineContext(int frame_number,
                               int stage_width,
                               int stage_height,
                               int background_color,
                               std::shared_ptr<const bitmap::Bitmap> stage_image,
                               std::string debug_info);

    [[nodiscard]] int frameNumber() const;
    [[nodiscard]] int stageWidth() const;
    [[nodiscard]] int stageHeight() const;
    [[nodiscard]] int backgroundColor() const;
    [[nodiscard]] std::shared_ptr<const bitmap::Bitmap> stageImage() const;
    [[nodiscard]] const std::string& debugInfo() const;
    [[nodiscard]] std::vector<RenderSprite>& sprites();
    [[nodiscard]] const std::vector<RenderSprite>& sprites() const;
    [[nodiscard]] std::set<int>& renderedChannels();
    [[nodiscard]] const std::set<int>& renderedChannels() const;

    void addTrace(std::string step_name, std::string summary);
    [[nodiscard]] RenderPipelineTrace buildTrace() const;
    [[nodiscard]] const std::optional<FrameSnapshot>& snapshot() const;
    void setSnapshot(FrameSnapshot snapshot);

private:
    int frame_number_;
    int stage_width_;
    int stage_height_;
    int background_color_;
    std::shared_ptr<const bitmap::Bitmap> stage_image_;
    std::string debug_info_;
    std::vector<RenderSprite> sprites_;
    std::set<int> rendered_channels_;
    std::vector<RenderPipelineStepTrace> trace_;
    std::optional<FrameSnapshot> snapshot_;
};

} // namespace libreshockwave::player::render::pipeline
