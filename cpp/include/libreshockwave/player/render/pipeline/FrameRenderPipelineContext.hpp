#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/player/render/pipeline/FrameSnapshot.hpp"
#include "libreshockwave/player/render/pipeline/RenderPipelineTrace.hpp"
#include "libreshockwave/player/render/pipeline/RenderSprite.hpp"

namespace libreshockwave::player::render::pipeline {

class FrameRenderPipelineContext {
public:
    FrameRenderPipelineContext(int frameNumber,
                               int stageWidth,
                               int stageHeight,
                               int backgroundColor,
                               std::shared_ptr<const bitmap::Bitmap> stageImage,
                               std::string debugInfo);

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

    void addTrace(std::string stepName, std::string summary);
    [[nodiscard]] RenderPipelineTrace buildTrace() const;
    [[nodiscard]] const std::optional<FrameSnapshot>& snapshot() const;
    void setSnapshot(FrameSnapshot snapshot);

private:
    int frameNumber_;
    int stageWidth_;
    int stageHeight_;
    int backgroundColor_;
    std::shared_ptr<const bitmap::Bitmap> stageImage_;
    std::string debugInfo_;
    std::vector<RenderSprite> sprites_;
    std::set<int> renderedChannels_;
    std::vector<RenderPipelineStepTrace> trace_;
    std::optional<FrameSnapshot> snapshot_;
};

} // namespace libreshockwave::player::render::pipeline
