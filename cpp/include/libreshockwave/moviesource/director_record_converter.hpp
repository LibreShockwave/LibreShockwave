#pragma once

#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/moviesource/records/cast_asset.hpp"
#include "libreshockwave/moviesource/records/movie_asset.hpp"
#include "libreshockwave/moviesource/records/script_asset.hpp"
#include "libreshockwave/moviesource/records/score_asset.hpp"

namespace libreshockwave::chunks {
class ScriptChunk;
class ScriptNamesChunk;
class CastMemberChunk;
class CastChunk;
class CastListChunk;
class ConfigChunk;
class KeyTableChunk;
class ScoreChunk;
class FrameLabelsChunk;
class TextChunk;
class SoundChunk;
}

namespace libreshockwave::moviesource {

// Converts Director chunks into the store-owned record types. Stateless: it
// reads chunk data and produces self-contained records. The caller resolves
// cross-chunk data (name tables, linked assets, resolved types) and passes it in.
class DirectorRecordConverter {
public:
    [[nodiscard]] static ScriptAsset toScriptAsset(
        const chunks::ScriptChunk& chunk,
        const chunks::ScriptNamesChunk* names,
        ScriptType resolved_type,
        const std::string& resolved_name);

    [[nodiscard]] static CastMemberAsset toCastMemberAsset(
        const chunks::CastMemberChunk& chunk, int cast_lib, int member_num,
        const std::vector<LinkedAsset>& linked);

    [[nodiscard]] static CastAsset toCastAsset(const chunks::CastChunk& chunk);
    [[nodiscard]] static CastListAsset toCastListAsset(const chunks::CastListChunk& chunk);
    [[nodiscard]] static ScoreAsset toScoreAsset(const chunks::ScoreChunk& chunk);
    [[nodiscard]] static ConfigAsset toConfigAsset(const chunks::ConfigChunk& chunk);
    [[nodiscard]] static KeyTableAsset toKeyTableAsset(const chunks::KeyTableChunk& chunk);
    [[nodiscard]] static FrameLabelAsset toFrameLabelAsset(const chunks::FrameLabelsChunk& chunk);
    [[nodiscard]] static TextAsset toTextAsset(const chunks::TextChunk& chunk);
    [[nodiscard]] static SoundAsset toSoundAsset(const chunks::SoundChunk& chunk);
};

} // namespace libreshockwave::moviesource
