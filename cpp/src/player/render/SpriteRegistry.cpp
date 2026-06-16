#include "libreshockwave/player/render/SpriteRegistry.hpp"

#include <algorithm>

namespace libreshockwave::player::render {

SpriteRegistry::SpritePtr SpriteRegistry::getOrCreate(int channel,
                                                      const chunks::ScoreChunk::ChannelData& data) {
    auto found = sprites_.find(channel);
    if (found == sprites_.end()) {
        auto state = std::make_shared<sprite::SpriteState>(channel, data);
        sprites_.emplace(channel, state);
        return state;
    }

    auto& state = *found->second;
    if (!state.isPuppet() && !state.hasDynamicMember() && !state.matchesScoreIdentity(data)) {
        state.rebindToScorePreservingScriptInstances(data);
    }
    return found->second;
}

SpriteRegistry::SpritePtr SpriteRegistry::getOrCreateDynamic(int channel) {
    auto found = sprites_.find(channel);
    if (found != sprites_.end()) {
        return found->second;
    }

    auto state = std::make_shared<sprite::SpriteState>(channel);
    sprites_.emplace(channel, state);
    return state;
}

void SpriteRegistry::markScoreBehaviorChannel(int channel) {
    scoreBehaviorChannels_.insert(channel);
}

bool SpriteRegistry::hasScoreBehaviorChannel(int channel) const {
    return scoreBehaviorChannels_.contains(channel);
}

SpriteRegistry::SpritePtr SpriteRegistry::get(int channel) {
    auto found = sprites_.find(channel);
    return found == sprites_.end() ? nullptr : found->second;
}

std::shared_ptr<const sprite::SpriteState> SpriteRegistry::get(int channel) const {
    auto found = sprites_.find(channel);
    return found == sprites_.end() ? nullptr : found->second;
}

void SpriteRegistry::updateFromScore(int channel, const chunks::ScoreChunk::ChannelData& data) {
    auto found = sprites_.find(channel);
    if (found == sprites_.end()) {
        return;
    }

    auto& state = *found->second;
    if (state.isPuppet() || state.hasDynamicMember()) {
        return;
    }

    if (state.matchesScoreIdentity(data)) {
        state.syncFromScore(data);
    } else {
        state.rebindToScorePreservingScriptInstances(data);
        bumpRevision();
    }
}

void SpriteRegistry::remove(int channel) {
    sprites_.erase(channel);
    scoreBehaviorChannels_.erase(channel);
}

void SpriteRegistry::clear() {
    sprites_.clear();
    scoreBehaviorChannels_.clear();
}

bool SpriteRegistry::clearDynamicMemberBindings(int castLib, int memberNum) {
    bool changed = false;
    for (auto& [channel, state] : sprites_) {
        (void)channel;
        if (!state || !state->hasDynamicMember()) {
            continue;
        }
        if (state->effectiveCastLib() == castLib && state->effectiveCastMember() == memberNum) {
            resetRetiredDynamicBinding(*state);
            changed = true;
        }
    }
    if (changed) {
        bumpRevision();
    }
    return changed;
}

bool SpriteRegistry::contains(int channel) const {
    return sprites_.contains(channel);
}

std::vector<SpriteRegistry::SpritePtr> SpriteRegistry::getDynamicSprites() const {
    std::vector<SpritePtr> result;
    for (const auto& [channel, state] : sprites_) {
        (void)channel;
        if (!state) {
            continue;
        }
        if (state->hasDynamicMember() || state->isDynamic() || state->isPuppet()) {
            result.push_back(state);
        }
    }
    std::ranges::sort(result, [](const SpritePtr& left, const SpritePtr& right) {
        return left->channel() < right->channel();
    });
    return result;
}

const SpriteRegistry::SpriteMap& SpriteRegistry::getAll() const {
    return sprites_;
}

void SpriteRegistry::bumpRevision() {
    ++revision_;
}

int SpriteRegistry::revision() const {
    return revision_;
}

int SpriteRegistry::getRevision() const {
    return revision_;
}

void SpriteRegistry::resetRetiredDynamicBinding(sprite::SpriteState& state) {
    if (state.isDynamic()) {
        state.clearDynamicMember();
        state.resetReleasedChannelGeometry();
        state.resetReleasedSpriteTransforms();
        return;
    }

    const auto& initialData = state.initialData();
    if (initialData.has_value()) {
        state.rebindToScorePreservingScriptInstances(*initialData);
        return;
    }

    state.clearDynamicMember();
    state.resetReleasedSpriteTransforms();
}

} // namespace libreshockwave::player::render
