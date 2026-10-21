#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/player/score/score_behavior_ref.hpp"
#include "libreshockwave/player/score/sprite_span.hpp"

namespace libreshockwave {
namespace moviesource {
class MovieSource;
}

}

namespace libreshockwave::chunks {
class FrameLabelsChunk;
class ScoreChunk;
}

namespace libreshockwave::player::score {

class ScoreNavigator {
public:
    explicit ScoreNavigator(const moviesource::MovieSource* file);
    ScoreNavigator(std::shared_ptr<chunks::ScoreChunk> score,
                   std::shared_ptr<chunks::FrameLabelsChunk> labels);

    [[nodiscard]] const ScoreBehaviorRef* getFrameScript(int frame) const;
    [[nodiscard]] std::vector<ScoreBehaviorRef> getSpriteBehaviors(int frame, int channel) const;
    [[nodiscard]] std::vector<SpriteSpan> getActiveSprites(int frame) const;
    [[nodiscard]] std::set<int> getActiveChannels(int frame) const;
    [[nodiscard]] int getFrameForLabel(std::string_view label) const;
    [[nodiscard]] std::set<std::string> getFrameLabels() const;
    [[nodiscard]] int getMarkerFrame(int current_frame, int marker_offset) const;
    [[nodiscard]] int getFrameCount() const;
    [[nodiscard]] const std::vector<SpriteSpan>& getAllSpans() const;

    [[nodiscard]] static int resolveMarkerFrame(const std::vector<int>& marker_frames,
                                                int current_frame,
                                                int marker_offset);
    [[nodiscard]] static std::vector<lingo::Datum> parseBehaviorParameters(
        const std::vector<std::vector<std::uint8_t>>& entries,
        int parameter_entry_index);

private:
    void buildSpriteSpans();
    void buildFrameLabels();

    std::shared_ptr<chunks::ScoreChunk> score_;
    std::shared_ptr<chunks::FrameLabelsChunk> labels_;
    std::vector<SpriteSpan> sprite_spans_;
    std::map<std::string, int> frame_labels_;
    std::vector<int> marker_frames_;
};

} // namespace libreshockwave::player::score
