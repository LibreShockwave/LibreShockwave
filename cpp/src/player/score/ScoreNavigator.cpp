#include "libreshockwave/player/score/ScoreNavigator.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/chunks/FrameLabelsChunk.hpp"
#include "libreshockwave/chunks/ScoreChunk.hpp"
#include "libreshockwave/lingo/LingoValueParser.hpp"

namespace libreshockwave::player::score {
namespace {

std::string lowerAscii(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

std::string decodeScoreTextEntry(const std::vector<std::uint8_t>& data) {
    int end = static_cast<int>(data.size());
    while (end > 0 && data[static_cast<std::size_t>(end - 1)] == 0) {
        --end;
    }

    int begin = 0;
    while (begin < end && std::isspace(static_cast<unsigned char>(data[static_cast<std::size_t>(begin)]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(data[static_cast<std::size_t>(end - 1)]))) {
        --end;
    }
    return std::string(data.begin() + begin, data.begin() + end);
}

} // namespace

ScoreNavigator::ScoreNavigator(const DirectorFile* file)
    : score_(file ? file->scoreChunk() : nullptr),
      labels_(file ? file->frameLabelsChunk() : nullptr) {
    buildSpriteSpans();
    buildFrameLabels();
}

ScoreNavigator::ScoreNavigator(std::shared_ptr<chunks::ScoreChunk> score,
                               std::shared_ptr<chunks::FrameLabelsChunk> labels)
    : score_(std::move(score)), labels_(std::move(labels)) {
    buildSpriteSpans();
    buildFrameLabels();
}

void ScoreNavigator::buildSpriteSpans() {
    if (!score_) {
        return;
    }

    for (const auto& interval : score_->frameIntervals()) {
        const auto& primary = interval.primary;
        SpriteSpan span(primary.channelIndex, primary.startFrame, primary.endFrame);

        if (interval.secondary.has_value()) {
            const auto& secondary = interval.secondary.value();
            if (secondary.castLib >= 1 && secondary.castMember >= 1) {
                span.addBehavior(ScoreBehaviorRef(
                    secondary.castLib,
                    secondary.castMember,
                    parseBehaviorParameters(score_->entries(), secondary.unk0)));
            }
        }
        spriteSpans_.push_back(std::move(span));
    }
}

std::vector<lingo::Datum> ScoreNavigator::parseBehaviorParameters(
    const std::vector<std::vector<std::uint8_t>>& entries,
    int parameterEntryIndex) {
    if (parameterEntryIndex <= 0 || parameterEntryIndex >= static_cast<int>(entries.size())) {
        return {};
    }

    const auto text = decodeScoreTextEntry(entries[static_cast<std::size_t>(parameterEntryIndex)]);
    if (text.empty() || text.front() != '[') {
        return {};
    }

    const auto parsed = lingo::LingoValueParser::parseLiteral(text);
    return parsed.isPropList() ? std::vector<lingo::Datum>{parsed} : std::vector<lingo::Datum>{};
}

void ScoreNavigator::buildFrameLabels() {
    if (!labels_) {
        return;
    }

    for (const auto& label : labels_->labels()) {
        const std::string key = lowerAscii(label.label);
        const int value = label.frameNum.value();
        frameLabels_[key] = value;
        markerFrames_.push_back(value);
    }
    std::sort(markerFrames_.begin(), markerFrames_.end());
}

const ScoreBehaviorRef* ScoreNavigator::getFrameScript(int frame) const {
    for (const auto& span : spriteSpans_) {
        if (span.isFrameBehavior() && span.containsFrame(frame)) {
            return span.firstBehavior();
        }
    }
    return nullptr;
}

std::vector<ScoreBehaviorRef> ScoreNavigator::getSpriteBehaviors(int frame, int channel) const {
    std::vector<ScoreBehaviorRef> behaviors;
    for (const auto& span : spriteSpans_) {
        if (span.channel() == channel && span.containsFrame(frame)) {
            behaviors.insert(behaviors.end(), span.behaviors().begin(), span.behaviors().end());
        }
    }
    return behaviors;
}

std::vector<SpriteSpan> ScoreNavigator::getActiveSprites(int frame) const {
    std::vector<SpriteSpan> active;
    for (const auto& span : spriteSpans_) {
        if (!span.isFrameBehavior() && span.containsFrame(frame)) {
            active.push_back(span);
        }
    }
    return active;
}

std::set<int> ScoreNavigator::getActiveChannels(int frame) const {
    std::set<int> channels;
    for (const auto& span : spriteSpans_) {
        if (!span.isFrameBehavior() && span.containsFrame(frame)) {
            channels.insert(span.channel());
        }
    }
    return channels;
}

int ScoreNavigator::getFrameForLabel(std::string_view label) const {
    const auto it = frameLabels_.find(lowerAscii(label));
    return it == frameLabels_.end() ? -1 : it->second;
}

std::set<std::string> ScoreNavigator::getFrameLabels() const {
    std::set<std::string> labels;
    for (const auto& [label, frame] : frameLabels_) {
        (void)frame;
        labels.insert(label);
    }
    return labels;
}

int ScoreNavigator::getMarkerFrame(int currentFrame, int markerOffset) const {
    return resolveMarkerFrame(markerFrames_, currentFrame, markerOffset);
}

int ScoreNavigator::resolveMarkerFrame(const std::vector<int>& markerFrames, int currentFrame, int markerOffset) {
    if (markerFrames.empty()) {
        return 0;
    }

    if (markerOffset > 0) {
        int seen = 0;
        for (const int markerFrame : markerFrames) {
            if (markerFrame > currentFrame && ++seen == markerOffset) {
                return markerFrame;
            }
        }
        return 0;
    }

    int markerZero = 0;
    for (const int markerFrame : markerFrames) {
        if (markerFrame > currentFrame) {
            break;
        }
        markerZero = markerFrame;
    }

    if (markerOffset == 0) {
        return markerZero;
    }
    if (markerZero == 0) {
        return 0;
    }

    const int targetIndex = -markerOffset;
    int seen = 0;
    for (auto it = markerFrames.rbegin(); it != markerFrames.rend(); ++it) {
        if (*it >= markerZero) {
            continue;
        }
        if (++seen == targetIndex) {
            return *it;
        }
    }
    return 0;
}

int ScoreNavigator::getFrameCount() const {
    return score_ ? score_->getFrameCount() : 0;
}

const std::vector<SpriteSpan>& ScoreNavigator::getAllSpans() const {
    return spriteSpans_;
}

} // namespace libreshockwave::player::score
