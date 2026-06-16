#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "libreshockwave/chunks/ScoreChunk.hpp"
#include "libreshockwave/player/sprite/SpriteState.hpp"

namespace libreshockwave::player::render {

class SpriteRegistry {
public:
    using SpritePtr = std::shared_ptr<sprite::SpriteState>;
    using SpriteMap = std::unordered_map<int, SpritePtr>;

    [[nodiscard]] SpritePtr getOrCreate(int channel, const chunks::ScoreChunk::ChannelData& data);
    [[nodiscard]] SpritePtr getOrCreateDynamic(int channel);

    void markScoreBehaviorChannel(int channel);
    [[nodiscard]] bool hasScoreBehaviorChannel(int channel) const;

    [[nodiscard]] SpritePtr get(int channel);
    [[nodiscard]] std::shared_ptr<const sprite::SpriteState> get(int channel) const;

    void updateFromScore(int channel, const chunks::ScoreChunk::ChannelData& data);
    void remove(int channel);
    void clear();

    [[nodiscard]] bool clearDynamicMemberBindings(int castLib, int memberNum);
    [[nodiscard]] bool contains(int channel) const;
    [[nodiscard]] std::vector<SpritePtr> getDynamicSprites() const;
    [[nodiscard]] const SpriteMap& getAll() const;

    void bumpRevision();
    [[nodiscard]] int revision() const;
    [[nodiscard]] int getRevision() const;

private:
    static void resetRetiredDynamicBinding(sprite::SpriteState& state);

    SpriteMap sprites_;
    std::unordered_set<int> scoreBehaviorChannels_;
    int revision_{0};
};

} // namespace libreshockwave::player::render
