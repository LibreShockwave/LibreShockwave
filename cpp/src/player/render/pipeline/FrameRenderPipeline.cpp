#include "libreshockwave/player/render/pipeline/FrameRenderPipeline.hpp"

#include <stdexcept>
#include <string>
#include <utility>

#include "libreshockwave/player/render/pipeline/SpriteBaker.hpp"
#include "libreshockwave/player/render/pipeline/StageRenderer.hpp"

namespace libreshockwave::player::render::pipeline {
namespace {

class CollectScoreSpritesStep final : public FrameRenderPipelineStep {
public:
    explicit CollectScoreSpritesStep(StageRenderer& stageRenderer)
        : stageRenderer_(stageRenderer) {}

    std::string_view name() const override { return "collect-score-sprites"; }

    void execute(FrameRenderPipelineContext& context) override {
        const int before = static_cast<int>(context.sprites().size());
        stageRenderer_.collectScoreSprites(context.frameNumber(), context.sprites(), context.renderedChannels());
        const int added = static_cast<int>(context.sprites().size()) - before;
        context.addTrace(std::string(name()), "Collected " + std::to_string(added) + " score sprites");
    }

private:
    StageRenderer& stageRenderer_;
};

class CollectDynamicSpritesStep final : public FrameRenderPipelineStep {
public:
    explicit CollectDynamicSpritesStep(StageRenderer& stageRenderer)
        : stageRenderer_(stageRenderer) {}

    std::string_view name() const override { return "collect-dynamic-sprites"; }

    void execute(FrameRenderPipelineContext& context) override {
        const int before = static_cast<int>(context.sprites().size());
        stageRenderer_.collectDynamicSprites(context.sprites(), context.renderedChannels());
        const int added = static_cast<int>(context.sprites().size()) - before;
        context.addTrace(std::string(name()), "Collected " + std::to_string(added) + " dynamic sprites");
    }

private:
    StageRenderer& stageRenderer_;
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
    explicit BakeSpritesStep(SpriteBaker& spriteBaker)
        : spriteBaker_(spriteBaker) {}

    std::string_view name() const override { return "bake-sprites"; }

    void execute(FrameRenderPipelineContext& context) override {
        context.sprites() = spriteBaker_.bakeSprites(context.sprites());
        context.addTrace(std::string(name()), "Baked sprites into renderable bitmaps");
    }

private:
    SpriteBaker& spriteBaker_;
};

class PublishBakedSpritesStep final : public FrameRenderPipelineStep {
public:
    explicit PublishBakedSpritesStep(StageRenderer& stageRenderer)
        : stageRenderer_(stageRenderer) {}

    std::string_view name() const override { return "publish-baked-sprites"; }

    void execute(FrameRenderPipelineContext& context) override {
        stageRenderer_.setLastBakedSprites(context.sprites());
        context.addTrace(std::string(name()), "Published baked sprites for hit testing");
    }

private:
    StageRenderer& stageRenderer_;
};

class BuildSnapshotStep final : public FrameRenderPipelineStep {
public:
    explicit BuildSnapshotStep(SpriteBaker& spriteBaker)
        : spriteBaker_(spriteBaker) {}

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
            spriteBaker_.tickCounter(),
            context.buildTrace()
        });
    }

private:
    SpriteBaker& spriteBaker_;
};

} // namespace

FrameRenderPipeline::FrameRenderPipeline(StageRenderer* stageRenderer, SpriteBaker* spriteBaker)
    : stageRenderer_(stageRenderer), spriteBaker_(spriteBaker) {
    if (stageRenderer_ == nullptr || spriteBaker_ == nullptr) {
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

FrameSnapshot FrameRenderPipeline::renderFrame(int frameNumber) const {
    if (stageRenderer_ == nullptr || spriteBaker_ == nullptr) {
        throw std::runtime_error("FrameRenderPipeline was not constructed with default render components");
    }

    return renderFrame(frameNumber,
                       stageRenderer_->stageWidth(),
                       stageRenderer_->stageHeight(),
                       stageRenderer_->backgroundColor(),
                       stageRenderer_->renderableStageImage(),
                       "Frame " + std::to_string(frameNumber));
}

FrameSnapshot FrameRenderPipeline::renderFrame(int frameNumber,
                                               int stageWidth,
                                               int stageHeight,
                                               int backgroundColor,
                                               std::shared_ptr<const bitmap::Bitmap> stageImage,
                                               std::string debugInfo) const {
    FrameRenderPipelineContext context(frameNumber,
                                       stageWidth,
                                       stageHeight,
                                       backgroundColor,
                                       std::move(stageImage),
                                       std::move(debugInfo));

    for (const auto& step : steps_) {
        step->execute(context);
    }

    if (!context.snapshot().has_value()) {
        throw std::runtime_error("Frame render pipeline did not produce a snapshot");
    }
    return *context.snapshot();
}

void FrameRenderPipeline::registerDefaultSteps() {
    registerStep(std::make_shared<CollectScoreSpritesStep>(*stageRenderer_));
    registerStep(std::make_shared<CollectDynamicSpritesStep>(*stageRenderer_));
    registerStep(std::make_shared<OrderSpritesStep>());
    registerStep(std::make_shared<BakeSpritesStep>(*spriteBaker_));
    registerStep(std::make_shared<PublishBakedSpritesStep>(*stageRenderer_));
    registerStep(std::make_shared<BuildSnapshotStep>(*spriteBaker_));
}

} // namespace libreshockwave::player::render::pipeline
