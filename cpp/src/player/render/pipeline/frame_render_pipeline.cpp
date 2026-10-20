#include "libreshockwave/player/render/pipeline/frame_render_pipeline.hpp"

#include <stdexcept>
#include <string>
#include <utility>

#include "libreshockwave/player/render/pipeline/sprite_baker.hpp"
#include "libreshockwave/player/render/pipeline/stage_renderer.hpp"

namespace libreshockwave::player::render::pipeline {
namespace {

class CollectScoreSpritesStep final : public FrameRenderPipelineStep {
public:
    explicit CollectScoreSpritesStep(StageRenderer& stage_renderer)
        : stage_renderer_(stage_renderer) {}

    std::string_view name() const override { return "collect-score-sprites"; }

    void execute(FrameRenderPipelineContext& context) override {
        const int before = static_cast<int>(context.sprites().size());
        stage_renderer_.collectScoreSprites(context.frameNumber(), context.sprites(), context.renderedChannels());
        const int added = static_cast<int>(context.sprites().size()) - before;
        context.addTrace(std::string(name()), "Collected " + std::to_string(added) + " score sprites");
    }

private:
    StageRenderer& stage_renderer_;
};

class CollectDynamicSpritesStep final : public FrameRenderPipelineStep {
public:
    explicit CollectDynamicSpritesStep(StageRenderer& stage_renderer)
        : stage_renderer_(stage_renderer) {}

    std::string_view name() const override { return "collect-dynamic-sprites"; }

    void execute(FrameRenderPipelineContext& context) override {
        const int before = static_cast<int>(context.sprites().size());
        stage_renderer_.collectDynamicSprites(context.sprites(), context.renderedChannels());
        const int added = static_cast<int>(context.sprites().size()) - before;
        context.addTrace(std::string(name()), "Collected " + std::to_string(added) + " dynamic sprites");
    }

private:
    StageRenderer& stage_renderer_;
};

class OrderSpritesStep final : public FrameRenderPipelineStep {
public:
    std::string_view name() const override { return "order-sprites"; }

    void execute(FrameRenderPipelineContext& context) override {
        StageRenderer::sortSprites(context.sprites());
        context.addTrace(std::string(name()), "Ordered sprites by locZ then channel");
    }
};

class BakeSpritesStep final : public FrameRenderPipelineStep {
public:
    explicit BakeSpritesStep(SpriteBaker& sprite_baker)
        : sprite_baker_(sprite_baker) {}

    std::string_view name() const override { return "bake-sprites"; }

    void execute(FrameRenderPipelineContext& context) override {
        context.sprites() = sprite_baker_.bakeSprites(context.sprites());
        context.addTrace(std::string(name()), "Baked sprites into renderable bitmaps");
    }

private:
    SpriteBaker& sprite_baker_;
};

class PublishBakedSpritesStep final : public FrameRenderPipelineStep {
public:
    explicit PublishBakedSpritesStep(StageRenderer& stage_renderer)
        : stage_renderer_(stage_renderer) {}

    std::string_view name() const override { return "publish-baked-sprites"; }

    void execute(FrameRenderPipelineContext& context) override {
        stage_renderer_.setLastBakedSprites(context.sprites());
        context.addTrace(std::string(name()), "Published baked sprites for hit testing");
    }

private:
    StageRenderer& stage_renderer_;
};

class BuildSnapshotStep final : public FrameRenderPipelineStep {
public:
    explicit BuildSnapshotStep(SpriteBaker& sprite_baker)
        : sprite_baker_(sprite_baker) {}

    std::string_view name() const override { return "build-frame-snapshot"; }

    void execute(FrameRenderPipelineContext& context) override {
        context.addTrace(std::string(name()), "Built immutable frame snapshot");
        context.setSnapshot(FrameSnapshot{
            context.frameNumber(),
            context.stageWidth(),
            context.stageHeight(),
            context.backgroundColor(),
            context.sprites(),
            context.debugInfo(),
            context.stageImage(),
            sprite_baker_.tickCounter(),
            context.buildTrace()
        });
    }

private:
    SpriteBaker& sprite_baker_;
};

} // namespace

FrameRenderPipeline::FrameRenderPipeline(StageRenderer* stage_renderer, SpriteBaker* sprite_baker)
    : stage_renderer_(stage_renderer), sprite_baker_(sprite_baker) {
    if (stage_renderer_ == nullptr || sprite_baker_ == nullptr) {
        throw std::invalid_argument("FrameRenderPipeline requires StageRenderer and SpriteBaker");
    }
    registerDefaultSteps();
}

void FrameRenderPipeline::registerStep(std::shared_ptr<FrameRenderPipelineStep> step) {
    if (!step) {
        throw std::invalid_argument("FrameRenderPipeline step cannot be null");
    }
    steps_.push_back(std::move(step));
}

const std::vector<std::shared_ptr<FrameRenderPipelineStep>>& FrameRenderPipeline::steps() const {
    return steps_;
}

int FrameRenderPipeline::stepCount() const {
    return static_cast<int>(steps_.size());
}

FrameSnapshot FrameRenderPipeline::renderFrame(int frame_number) const {
    if (stage_renderer_ == nullptr || sprite_baker_ == nullptr) {
        throw std::runtime_error("FrameRenderPipeline was not constructed with default render components");
    }

    return renderFrame(frame_number,
                       stage_renderer_->stageWidth(),
                       stage_renderer_->stageHeight(),
                       stage_renderer_->backgroundColor(),
                       stage_renderer_->renderableStageImage(),
                       "Frame " + std::to_string(frame_number));
}

FrameSnapshot FrameRenderPipeline::renderFrame(int frame_number,
                                               int stage_width,
                                               int stage_height,
                                               int background_color,
                                               std::shared_ptr<const bitmap::Bitmap> stage_image,
                                               std::string debug_info) const {
    FrameRenderPipelineContext context(frame_number,
                                       stage_width,
                                       stage_height,
                                       background_color,
                                       std::move(stage_image),
                                       std::move(debug_info));

    for (const auto& step : steps_) {
        step->execute(context);
    }

    if (!context.snapshot().has_value()) {
        throw std::runtime_error("Frame render pipeline did not produce a snapshot");
    }
    return *context.snapshot();
}

void FrameRenderPipeline::registerDefaultSteps() {
    registerStep(std::make_shared<CollectScoreSpritesStep>(*stage_renderer_));
    registerStep(std::make_shared<CollectDynamicSpritesStep>(*stage_renderer_));
    registerStep(std::make_shared<OrderSpritesStep>());
    registerStep(std::make_shared<BakeSpritesStep>(*sprite_baker_));
    registerStep(std::make_shared<PublishBakedSpritesStep>(*stage_renderer_));
    registerStep(std::make_shared<BuildSnapshotStep>(*sprite_baker_));
}

} // namespace libreshockwave::player::render::pipeline
