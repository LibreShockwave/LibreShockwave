#include "libreshockwave/player/score/score_navigator.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

#include "libreshockwave/director_file.hpp"
#include "libreshockwave/chunks/frame_labels_chunk.hpp"
#include "libreshockwave/chunks/score_chunk.hpp"
#include "libreshockwave/lingo/lingo_value_parser.hpp"

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
        SpriteSpan span(primary.channel_index, primary.start_frame, primary.end_frame);

        if (interval.secondary.has_value()) {
            const auto& secondary = interval.secondary.value();
            if (secondary.cast_lib >= 1 && secondary.cast_member >= 1) {
                span.addBehavior(ScoreBehaviorRef(
                    secondary.cast_lib,
                    secondary.cast_member,
                    parseBehaviorParameters(score_->entries(), secondary.unk0)));
            }
        }
        sprite_spans_.push_back(std::move(span));
    }
}

std::vector<lingo::Datum> ScoreNavigator::parseBehaviorParameters(
    const std::vector<std::vector<std::uint8_t>>& entries,
    int parameter_entry_index) {
    if (parameter_entry_index <= 0 || parameter_entry_index >= static_cast<int>(entries.size())) {
        return {};
    }

    const auto text = decodeScoreTextEntry(entries[static_cast<std::size_t>(parameter_entry_index)]);
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
        const int value = label.frame_num.value();
        frame_labels_[key] = value;
        marker_frames_.push_back(value);
    }
    std::sort(marker_frames_.begin(), marker_frames_.end());
}

const ScoreBehaviorRef* ScoreNavigator::getFrameScript(int frame) const {
    for (const auto& span : sprite_spans_) {
        if (span.isFrameBehavior() && span.containsFrame(frame)) {
            return span.firstBehavior();
        }
    }
    return nullptr;
}

std::vector<ScoreBehaviorRef> ScoreNavigator::getSpriteBehaviors(int frame, int channel) const {
    std::vector<ScoreBehaviorRef> behaviors;
    for (const auto& span : sprite_spans_) {
        if (span.channel() == channel && span.containsFrame(frame)) {
            behaviors.insert(behaviors.end(), span.behaviors().begin(), span.behaviors().end());
        }
    }
    return behaviors;
}

std::vector<SpriteSpan> ScoreNavigator::getActiveSprites(int frame) const {
    std::vector<SpriteSpan> active;
    for (const auto& span : sprite_spans_) {
        if (!span.isFrameBehavior() && span.containsFrame(frame)) {
            active.push_back(span);
        }
    }
    return active;
}

std::set<int> ScoreNavigator::getActiveChannels(int frame) const {
    std::set<int> channels;
    for (const auto& span : sprite_spans_) {
        if (!span.isFrameBehavior() && span.containsFrame(frame)) {
            channels.insert(span.channel());
        }
    }
    return channels;
}

int ScoreNavigator::getFrameForLabel(std::string_view label) const {
    const auto it = frame_labels_.find(lowerAscii(label));
    return it == frame_labels_.end() ? -1 : it->second;
}

std::set<std::string> ScoreNavigator::getFrameLabels() const {
    std::set<std::string> labels;
    for (const auto& [label, frame] : frame_labels_) {
        (void)frame;
        labels.insert(label);
    }
    return labels;
}

int ScoreNavigator::getMarkerFrame(int current_frame, int marker_offset) const {
    return resolveMarkerFrame(marker_frames_, current_frame, marker_offset);
}

int ScoreNavigator::resolveMarkerFrame(const std::vector<int>& marker_frames, int current_frame, int marker_offset) {
    if (marker_frames.empty()) {
        return 0;
    }

    if (marker_offset > 0) {
        int seen = 0;
        for (const int marker_frame : marker_frames) {
            if (marker_frame > current_frame && ++seen == marker_offset) {
                return marker_frame;
            }
        }
        return 0;
    }

    int marker_zero = 0;
    for (const int marker_frame : marker_frames) {
        if (marker_frame > current_frame) {
            break;
        }
        marker_zero = marker_frame;
    }

    if (marker_offset == 0) {
        return marker_zero;
    }
    if (marker_zero == 0) {
        return 0;
    }

    const int target_index = -marker_offset;
    int seen = 0;
    for (auto it = marker_frames.rbegin(); it != marker_frames.rend(); ++it) {
        if (*it >= marker_zero) {
            continue;
        }
        if (++seen == target_index) {
            return *it;
        }
    }
    return 0;
}

int ScoreNavigator::getFrameCount() const {
    return score_ ? score_->getFrameCount() : 0;
}

const std::vector<SpriteSpan>& ScoreNavigator::getAllSpans() const {
    return sprite_spans_;
}

} // namespace libreshockwave::player::score
