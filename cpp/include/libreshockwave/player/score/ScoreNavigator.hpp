#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/player/score/ScoreBehaviorRef.hpp"
#include "libreshockwave/player/score/SpriteSpan.hpp"

namespace libreshockwave {
class DirectorFile;
}

namespace libreshockwave::chunks {
class FrameLabelsChunk;
class ScoreChunk;
}

namespace libreshockwave::player::score {

class ScoreNavigator {
public:
    explicit ScoreNavigator(const DirectorFile* file);
    ScoreNavigator(std::shared_ptr<chunks::ScoreChunk> score,
                   std::shared_ptr<chunks::FrameLabelsChunk> labels);

    [[nodiscard]] const ScoreBehaviorRef* getFrameScript(int frame) const;
    [[nodiscard]] std::vector<ScoreBehaviorRef> getSpriteBehaviors(int frame, int channel) const;
    [[nodiscard]] std::vector<SpriteSpan> getActiveSprites(int frame) const;
    [[nodiscard]] std::set<int> getActiveChannels(int frame) const;
    [[nodiscard]] int getFrameForLabel(std::string_view label) const;
    [[nodiscard]] std::set<std::string> getFrameLabels() const;
    [[nodiscard]] int getMarkerFrame(int currentFrame, int markerOffset) const;
    [[nodiscard]] int getFrameCount() const;
    [[nodiscard]] const std::vector<SpriteSpan>& getAllSpans() const;

    [[nodiscard]] static int resolveMarkerFrame(const std::vector<int>& markerFrames,
                                                int currentFrame,
                                                int markerOffset);
    [[nodiscard]] static std::vector<lingo::Datum> parseBehaviorParameters(
        const std::vector<std::vector<std::uint8_t>>& entries,
        int parameterEntryIndex);

private:
    void buildSpriteSpans();
    void buildFrameLabels();

    std::shared_ptr<chunks::ScoreChunk> score_;
    std::shared_ptr<chunks::FrameLabelsChunk> labels_;
    std::vector<SpriteSpan> spriteSpans_;
    std::map<std::string, int> frameLabels_;
    std::vector<int> markerFrames_;
};

} // namespace libreshockwave::player::score
