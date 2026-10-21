#include "libreshockwave/moviesource/director_movie_source.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <utility>

#include "libreshockwave/bitmap/bitmap_decoder.hpp"
#include "libreshockwave/bitmap/palette.hpp"
#include "libreshockwave/cast/bitmap_info.hpp"
#include "libreshockwave/cast/xmed_text_parser.hpp"
#include "libreshockwave/chunks/bitmap_chunk.hpp"
#include "libreshockwave/chunks/cast_chunk.hpp"
#include "libreshockwave/chunks/cast_list_chunk.hpp"
#include "libreshockwave/chunks/cast_member_chunk.hpp"
#include "libreshockwave/chunks/chunk.hpp"
#include "libreshockwave/chunks/config_chunk.hpp"
#include "libreshockwave/chunks/font_map_chunk.hpp"
#include "libreshockwave/chunks/frame_labels_chunk.hpp"
#include "libreshockwave/chunks/key_table_chunk.hpp"
#include "libreshockwave/chunks/media_chunk.hpp"
#include "libreshockwave/chunks/palette_chunk.hpp"
#include "libreshockwave/chunks/raw_chunk.hpp"
#include "libreshockwave/chunks/score_chunk.hpp"
#include "libreshockwave/chunks/script_chunk.hpp"
#include "libreshockwave/chunks/script_context_chunk.hpp"
#include "libreshockwave/chunks/script_names_chunk.hpp"
#include "libreshockwave/chunks/sound_chunk.hpp"
#include "libreshockwave/chunks/text_chunk.hpp"
#include "libreshockwave/lookup/cast_member_lookup.hpp"
#include "libreshockwave/lookup/palette_resolver.hpp"
#include "libreshockwave/lookup/script_lookup.hpp"
#include "libreshockwave/moviesource/director_record_converter.hpp"

namespace libreshockwave::moviesource {
namespace {

std::shared_ptr<const bitmap::Palette> borrowedPalette(const bitmap::Palette* palette) {
    if (palette == nullptr) {
        return nullptr;
    }
    return std::shared_ptr<const bitmap::Palette>(palette, [](const bitmap::Palette*) {});
}

int colorDistance(std::uint32_t left, std::uint32_t right) {
    const int left_red = static_cast<int>((left >> 16U) & 0xFFU);
    const int left_green = static_cast<int>((left >> 8U) & 0xFFU);
    const int left_blue = static_cast<int>(left & 0xFFU);
    const int right_red = static_cast<int>((right >> 16U) & 0xFFU);
    const int right_green = static_cast<int>((right >> 8U) & 0xFFU);
    const int right_blue = static_cast<int>(right & 0xFFU);
    const int red_delta = left_red - right_red;
    const int green_delta = left_green - right_green;
    const int blue_delta = left_blue - right_blue;
    return (red_delta * red_delta) + (green_delta * green_delta) + (blue_delta * blue_delta);
}

std::shared_ptr<const bitmap::Palette> closestMacLikePaletteForIndexedBitmap(
    const bitmap::Bitmap& bitmap,
    const std::vector<std::shared_ptr<chunks::PaletteChunk>>& palette_chunks) {
    const auto indices = bitmap.paletteIndices();
    if (!indices.has_value() || indices->empty()) {
        return nullptr;
    }

    std::array<bool, 256> used_indices{};
    int used_count = 0;
    for (const auto raw_index : *indices) {
        const int index = raw_index & 0xFF;
        if (!used_indices[static_cast<std::size_t>(index)]) {
            used_indices[static_cast<std::size_t>(index)] = true;
            ++used_count;
        }
    }
    if (used_count == 0) {
        return nullptr;
    }

    const auto& mac_palette = bitmap::Palette::systemMacPalette();
    std::shared_ptr<const bitmap::Palette> best_palette;
    long long best_distance = std::numeric_limits<long long>::max();
    for (const auto& chunk : palette_chunks) {
        if (!chunk || chunk->colorCount() <= 0) {
            continue;
        }
        auto candidate = std::make_shared<bitmap::Palette>(
            chunk->colors(), "Custom Palette #" + std::to_string(chunk->id().value()));
        long long total_distance = 0;
        for (int index = 0; index < 256; ++index) {
            if (!used_indices[static_cast<std::size_t>(index)]) {
                continue;
            }
            if (index >= candidate->size() || index >= mac_palette.size()) {
                total_distance += 255LL * 255LL * 3LL;
                continue;
            }
            total_distance += colorDistance(candidate->getColor(index), mac_palette.getColor(index));
        }
        if (total_distance < best_distance) {
            best_distance = total_distance;
            best_palette = std::move(candidate);
        }
    }

    constexpr long long max_average_channel_distance_squared = 64LL * 64LL;
    const long long max_distance = static_cast<long long>(used_count) * max_average_channel_distance_squared;
    return best_distance <= max_distance ? best_palette : nullptr;
}

std::optional<ScriptType> scriptTypeForCastMemberScriptType(chunks::CastMemberScriptType type) {
    switch (type) {
        case chunks::CastMemberScriptType::Score: return ScriptType::Score;
        case chunks::CastMemberScriptType::Behavior: return ScriptType::Behavior;
        case chunks::CastMemberScriptType::MovieScript: return ScriptType::MovieScript;
        case chunks::CastMemberScriptType::Parent: return ScriptType::Parent;
        case chunks::CastMemberScriptType::Unknown: return ScriptType::Unknown;
    }
    return std::nullopt;
}

void appendUnique(std::vector<std::string>& result, const std::vector<std::string>& names) {
    for (const auto& name : names) {
        if (std::find(result.begin(), result.end(), name) == result.end()) {
            result.push_back(name);
        }
    }
}

bool matchesLinkedChunkFourCC(const chunks::KeyTableChunk::KeyTableEntry& entry,
                              std::optional<std::uint32_t> fourcc) {
    return !fourcc.has_value() || entry.fourcc == *fourcc;
}

} // namespace

DirectorMovieSource::DirectorMovieSource() = default;
DirectorMovieSource::~DirectorMovieSource() = default;

void DirectorMovieSource::load(const std::vector<std::uint8_t>& data) {
    container_ = DirectorContainerLoader::load(this, data);
}

void DirectorMovieSource::addScriptChunk(std::shared_ptr<chunks::ScriptChunk> chunk) {
    if (chunk == nullptr) {
        return;
    }
    container_.scripts.push_back(std::move(chunk));
}

void DirectorMovieSource::setScriptNames(std::shared_ptr<chunks::ScriptNamesChunk> names) {
    container_.script_names = std::move(names);
}

io::ByteOrder DirectorMovieSource::endian() const { return container_.endian; }
int DirectorMovieSource::version() const { return container_.version; }
format::ChunkType DirectorMovieSource::movieType() const { return container_.movie_type; }
bool DirectorMovieSource::isAfterburner() const { return container_.afterburner; }
bool DirectorMovieSource::isCapitalX() const { return container_.capital_x; }
const std::string& DirectorMovieSource::basePath() const { return container_.base_path; }
void DirectorMovieSource::setBasePath(std::string base_path) { container_.base_path = std::move(base_path); }

std::shared_ptr<chunks::Chunk> DirectorMovieSource::getChunk(id::ChunkId id) const {
    if (const auto found = container_.chunks.find(id.value()); found != container_.chunks.end()) {
        return found->second;
    }
    return DirectorContainerLoader::reparseChunk(this, container_, id);
}

std::shared_ptr<chunks::ScriptNamesChunk> DirectorMovieSource::scriptNamesForScript(
    const std::shared_ptr<chunks::ScriptChunk>& script) {
    if (!script) {
        return container_.script_names;
    }
    for (const auto& context : container_.all_script_contexts) {
        if (!context) {
            continue;
        }
        for (const auto& entry : context->entries()) {
            if (entry.id.value() != script->id().value()) {
                continue;
            }
            const auto found = container_.script_names_by_id.find(context->lnamSectionId().value());
            auto names = found != container_.script_names_by_id.end() ? found->second : nullptr;
            return names ? names : container_.script_names;
        }
    }
    return container_.script_names;
}

std::shared_ptr<chunks::ScriptChunk> DirectorMovieSource::scriptChunkByContextId(int context_id) {
    if (!script_lookup_) {
        script_lookup_ = std::make_unique<lookup::ScriptLookup>(
            container_.scripts, container_.all_script_contexts, container_.cast_members);
    }
    return script_lookup_->getByContextId(context_id);
}

std::vector<std::shared_ptr<chunks::ScriptChunk>> DirectorMovieSource::scriptChunksByContextId(int context_id) const {
    if (!script_lookup_) {
        script_lookup_ = std::make_unique<lookup::ScriptLookup>(
            container_.scripts, container_.all_script_contexts, container_.cast_members);
    }
    return script_lookup_->getAllByContextId(context_id);
}

std::shared_ptr<chunks::ScriptChunk> DirectorMovieSource::scriptChunkForCastMember(
    const std::shared_ptr<chunks::CastMemberChunk>& member) const {
    if (!member || !member->isScript() || member->scriptId() <= 0) {
        return nullptr;
    }
    const auto matches = scriptChunksByContextId(member->scriptId());
    if (matches.empty()) {
        return nullptr;
    }
    if (matches.size() == 1) {
        return matches.front();
    }
    if (container_.key_table) {
        for (const auto& script : matches) {
            if (!script) {
                continue;
            }
            const auto owner_id = container_.key_table->getOwnerCastId(script->id());
            if (owner_id.has_value() && owner_id->value() == member->id().value()) {
                return script;
            }
        }
    }
    if (const auto member_type = member->getScriptType(); member_type.has_value()) {
        if (const auto script_type = scriptTypeForCastMemberScriptType(member_type.value());
            script_type.has_value()) {
            for (const auto& script : matches) {
                if (script && script->scriptType() == chunks::scriptChunkTypeFromCode(static_cast<int>(*script_type))) {
                    return script;
                }
            }
        }
    }
    return matches.front();
}

std::shared_ptr<chunks::CastMemberChunk> DirectorMovieSource::chunkForAsset(
    const std::shared_ptr<CastMemberAsset>& asset) const {
    if (!asset) {
        return nullptr;
    }
    const auto chunk = getChunk(id::ChunkId(asset->chunk_id));
    if (!chunk) {
        return nullptr;
    }
    return std::dynamic_pointer_cast<chunks::CastMemberChunk>(chunk);
}

std::shared_ptr<ScriptAsset> DirectorMovieSource::toScriptAsset(
    const std::shared_ptr<chunks::ScriptChunk>& chunk) const {
    if (!chunk) {
        return nullptr;
    }
    if (const auto found = script_asset_cache_.find(chunk->id().value());
        found != script_asset_cache_.end()) {
        return found->second;
    }

    ScriptType resolved_type = scriptTypeFromCode(static_cast<int>(chunk->scriptType()));
    std::string resolved_name;
    if (const auto member = owningCastMemberForScript(*chunk); member) {
        if (const auto name = member->name(); !name.empty()) {
            resolved_name = name;
        }
        if (const auto member_type = member->getScriptType(); member_type.has_value()) {
            if (const auto converted = scriptTypeForCastMemberScriptType(member_type.value());
                converted.has_value() && *converted != ScriptType::Unknown) {
                resolved_type = *converted;
            }
        }
    }
    if (resolved_name.empty()) {
        for (const auto& member : container_.cast_members) {
            if (!member || !member->isScript()) {
                continue;
            }
            const auto resolved = scriptChunkForCastMember(member);
            if (resolved && resolved->id().value() == chunk->id().value()) {
                resolved_name = member->name();
                break;
            }
        }
    }

    const auto names = scriptNamesForScript(*chunk);
    auto asset = std::make_shared<ScriptAsset>(DirectorRecordConverter::toScriptAsset(
        *chunk, names.get(), resolved_type, resolved_name));
    script_asset_cache_[chunk->id().value()] = asset;
    return asset;
}

std::shared_ptr<CastMemberAsset> DirectorMovieSource::toCastMemberAsset(
    const std::shared_ptr<chunks::CastMemberChunk>& chunk) {
    if (!chunk) {
        return nullptr;
    }
    if (const auto found = cast_member_asset_cache_.find(chunk->id().value());
        found != cast_member_asset_cache_.end()) {
        return found->second;
    }

    std::vector<LinkedAsset> linked;
    if (container_.key_table) {
        for (const auto& entry : container_.key_table->getEntriesForOwner(chunk->id())) {
            LinkedAsset linked_asset;
            linked_asset.fourcc = entry.fourcc;
            linked_asset.chunk_id = entry.section_id.value();
            linked.push_back(std::move(linked_asset));
        }
    }

    // cast_lib and member_num are resolved by the caller via the lookup; the
    // chunk id is the canonical key.
    auto asset = std::make_shared<CastMemberAsset>(
        DirectorRecordConverter::toCastMemberAsset(*chunk, 0, 0, linked));
    cast_member_asset_cache_[chunk->id().value()] = asset;
    return asset;
}

std::shared_ptr<CastAsset> DirectorMovieSource::toCastAsset(const std::shared_ptr<chunks::CastChunk>& chunk) {
    if (!chunk) {
        return nullptr;
    }
    if (const auto found = cast_asset_cache_.find(chunk->id().value());
        found != cast_asset_cache_.end()) {
        return found->second;
    }
    auto asset = std::make_shared<CastAsset>(DirectorRecordConverter::toCastAsset(*chunk));
    cast_asset_cache_[chunk->id().value()] = asset;
    return asset;
}

std::shared_ptr<CastListAsset> DirectorMovieSource::castList() {
    if (cast_list_asset_) {
        return cast_list_asset_;
    }
    if (container_.cast_list && !container_.cast_list->entries().empty()) {
        cast_list_asset_ = std::make_shared<CastListAsset>(
            DirectorRecordConverter::toCastListAsset(*container_.cast_list));
        return cast_list_asset_;
    }
    // No explicit cast-list chunk (MCsL). Synthesize an implicit list: one
    // internal entry per cast chunk, in chunk order. This mirrors the implicit
    // cast libraries Director derives from bare cast (CAS*) chunks.
    if (!container_.casts.empty()) {
        auto asset = std::make_shared<CastListAsset>();
        asset->entries.reserve(container_.casts.size());
        for (int index = 0; index < static_cast<int>(container_.casts.size()); ++index) {
            const auto& cast = container_.casts[static_cast<std::size_t>(index)];
            if (!cast) {
                continue;
            }
            CastListAsset::Entry entry;
            entry.min_member = 1;
            entry.max_member = static_cast<int>(cast->memberIds().size());
            entry.member_count = static_cast<int>(cast->memberIds().size());
            entry.id = cast->id().value();
            asset->entries.push_back(std::move(entry));
        }
        cast_list_asset_ = std::move(asset);
        return cast_list_asset_;
    }
    return nullptr;
}

std::shared_ptr<ScoreAsset> DirectorMovieSource::toScoreAsset(const std::shared_ptr<chunks::ScoreChunk>& chunk) {
    if (!chunk) {
        return nullptr;
    }
    if (score_asset_) {
        return score_asset_;
    }
    score_asset_ = std::make_shared<ScoreAsset>(DirectorRecordConverter::toScoreAsset(*chunk));
    return score_asset_;
}

std::shared_ptr<ConfigAsset> DirectorMovieSource::toConfigAsset(const std::shared_ptr<chunks::ConfigChunk>& chunk) const {
    if (!chunk) {
        return nullptr;
    }
    if (config_asset_) {
        return config_asset_;
    }
    config_asset_ = std::make_shared<ConfigAsset>(DirectorRecordConverter::toConfigAsset(*chunk));
    return config_asset_;
}

std::shared_ptr<KeyTableAsset> DirectorMovieSource::toKeyTableAsset(const std::shared_ptr<chunks::KeyTableChunk>& chunk) {
    if (!chunk) {
        return nullptr;
    }
    if (key_table_asset_) {
        return key_table_asset_;
    }
    key_table_asset_ = std::make_shared<KeyTableAsset>(DirectorRecordConverter::toKeyTableAsset(*chunk));
    return key_table_asset_;
}

std::shared_ptr<FrameLabelAsset> DirectorMovieSource::toFrameLabelAsset(
    const std::shared_ptr<chunks::FrameLabelsChunk>& chunk) {
    if (!chunk) {
        return nullptr;
    }
    if (frame_label_asset_) {
        return frame_label_asset_;
    }
    frame_label_asset_ = std::make_shared<FrameLabelAsset>(
        DirectorRecordConverter::toFrameLabelAsset(*chunk));
    return frame_label_asset_;
}

std::shared_ptr<TextAsset> DirectorMovieSource::toTextAsset(const std::shared_ptr<chunks::TextChunk>& chunk) {
    if (!chunk) {
        return nullptr;
    }
    return std::make_shared<TextAsset>(DirectorRecordConverter::toTextAsset(*chunk));
}

std::shared_ptr<SoundAsset> DirectorMovieSource::toSoundAsset(const std::shared_ptr<chunks::SoundChunk>& chunk) {
    if (!chunk) {
        return nullptr;
    }
    return std::make_shared<SoundAsset>(DirectorRecordConverter::toSoundAsset(*chunk));
}

// Scripts.

std::vector<std::shared_ptr<ScriptAsset>> DirectorMovieSource::scripts() {
    std::vector<std::shared_ptr<ScriptAsset>> result;
    result.reserve(container_.scripts.size());
    for (const auto& chunk : container_.scripts) {
        if (auto asset = toScriptAsset(chunk); asset) {
            result.push_back(std::move(asset));
        }
    }
    return result;
}

std::vector<std::shared_ptr<chunks::ScriptChunk>> DirectorMovieSource::scriptChunks() {
    return container_.scripts;
}

std::shared_ptr<ScriptAsset> DirectorMovieSource::scriptByContextId(int context_id) {
    return toScriptAsset(scriptChunkByContextId(context_id));
}

std::vector<std::shared_ptr<ScriptAsset>> DirectorMovieSource::scriptsByContextId(int context_id) {
    std::vector<std::shared_ptr<ScriptAsset>> result;
    for (const auto& chunk : scriptChunksByContextId(context_id)) {
        if (auto asset = toScriptAsset(chunk); asset) {
            result.push_back(std::move(asset));
        }
    }
    return result;
}

std::shared_ptr<ScriptAsset> DirectorMovieSource::scriptByChunkId(int chunk_id) const {
    for (const auto& chunk : container_.scripts) {
        if (chunk->id().value() == chunk_id) {
            return toScriptAsset(chunk);
        }
    }
    return nullptr;
}

std::shared_ptr<ScriptAsset> DirectorMovieSource::scriptForCastMember(
    const std::shared_ptr<CastMemberAsset>& member) {
    const auto chunk = chunkForAsset(member);
    return toScriptAsset(scriptChunkForCastMember(chunk));
}

std::shared_ptr<chunks::ScriptChunk> DirectorMovieSource::scriptChunkForCastMember(
    const std::shared_ptr<CastMemberAsset>& member) const {
    const auto chunk = chunkForAsset(member);
    return scriptChunkForCastMember(chunk);
}

std::shared_ptr<CastMemberAsset> DirectorMovieSource::castMemberAssetForChunk(
    const std::shared_ptr<chunks::CastMemberChunk>& chunk) {
    return toCastMemberAsset(chunk);
}

std::shared_ptr<chunks::Chunk> DirectorMovieSource::chunkById(int chunk_id) {
    return getChunk(id::ChunkId(chunk_id));
}

std::shared_ptr<chunks::ScriptNamesChunk> DirectorMovieSource::scriptNames() {
    return container_.script_names;
}

std::vector<std::shared_ptr<chunks::CastChunk>> DirectorMovieSource::castChunks() {
    return container_.casts;
}

std::shared_ptr<chunks::CastMemberChunk> DirectorMovieSource::castMemberChunkForAsset(
    const std::shared_ptr<CastMemberAsset>& member) {
    return chunkForAsset(member);
}

std::shared_ptr<chunks::CastChunk> DirectorMovieSource::mappedCastChunk(int cast_lib) {
    const auto cast_asset = mappedCast(cast_lib);
    if (!cast_asset) {
        return nullptr;
    }
    for (const auto& cast : container_.casts) {
        if (cast && cast->memberCount() == static_cast<int>(cast_asset->member_ids.size())) {
            return cast;
        }
    }
    return nullptr;
}

std::shared_ptr<chunks::CastMemberChunk> DirectorMovieSource::owningCastMemberForScript(
    const chunks::ScriptChunk& script) const {
    if (container_.key_table != nullptr) {
        if (const auto owner_id = container_.key_table->getOwnerCastId(script.id()); owner_id.has_value()) {
            for (const auto& member : container_.cast_members) {
                if (member && member->id().value() == owner_id->value() && member->isScript()) {
                    return member;
                }
            }
        }
    }
    for (const auto& context : container_.all_script_contexts) {
        if (!context) {
            continue;
        }
        for (int index = 0; index < static_cast<int>(context->entries().size()); ++index) {
            if (context->entries()[static_cast<std::size_t>(index)].id.value() != script.id().value()) {
                continue;
            }
            const int script_id = index + 1;
            for (const auto& member : container_.cast_members) {
                if (member && member->isScript() && member->scriptId() == script_id) {
                    return member;
                }
            }
        }
    }
    return nullptr;
}

chunks::ScriptChunkType DirectorMovieSource::scriptTypeForChunk(
    const chunks::ScriptChunk& script) const {
    return resolvedScriptType(script);
}

chunks::ScriptChunkType DirectorMovieSource::resolvedScriptType(const chunks::ScriptChunk& script) const {
    if (const auto member = owningCastMemberForScript(script); member) {
        if (const auto member_type = member->getScriptType(); member_type.has_value()) {
            switch (member_type.value()) {
                case chunks::CastMemberScriptType::Score: return chunks::ScriptChunkType::Score;
                case chunks::CastMemberScriptType::Behavior: return chunks::ScriptChunkType::Behavior;
                case chunks::CastMemberScriptType::MovieScript: return chunks::ScriptChunkType::MovieScript;
                case chunks::CastMemberScriptType::Parent: return chunks::ScriptChunkType::Parent;
                case chunks::CastMemberScriptType::Unknown: return script.scriptType();
            }
        }
    }
    return script.scriptType();
}

std::shared_ptr<const chunks::ScriptNamesChunk> DirectorMovieSource::scriptNamesForScript(
    const chunks::ScriptChunk& script) const {
    if (container_.script_names_by_id.empty()) {
        return container_.script_names;
    }
    const auto found = container_.script_names_by_id.find(script.id().value());
    if (found != container_.script_names_by_id.end()) {
        return found->second;
    }
    return container_.script_names;
}

std::string DirectorMovieSource::scriptNameForScript(const chunks::ScriptChunk& script) const {
    if (const auto member = owningCastMemberForScript(script); member) {
        if (const auto name = member->name(); !name.empty()) {
            return name;
        }
    }
    if (const auto names = scriptNamesForScript(script); names != nullptr) {
        for (const auto& handler : script.handlers()) {
            if (const auto handler_name = script.getHandlerName(handler, names.get()); !handler_name.empty()) {
                return handler_name;
            }
        }
    }
    return "";
}

std::vector<std::string> DirectorMovieSource::allGlobalNames() {
    std::vector<std::string> result;
    for (const auto& asset : scripts()) {
        if (asset) {
            appendUnique(result, asset->getGlobalNames());
        }
    }
    return result;
}

std::vector<std::string> DirectorMovieSource::allPropertyNames() {
    std::vector<std::string> result;
    for (const auto& asset : scripts()) {
        if (asset) {
            appendUnique(result, asset->getPropertyNames());
        }
    }
    return result;
}

// Casts.

std::vector<std::shared_ptr<CastMemberAsset>> DirectorMovieSource::castMembers() {
    std::vector<std::shared_ptr<CastMemberAsset>> result;
    result.reserve(container_.cast_members.size());
    for (const auto& chunk : container_.cast_members) {
        if (auto asset = toCastMemberAsset(chunk); asset) {
            result.push_back(std::move(asset));
        }
    }
    return result;
}

std::shared_ptr<CastAsset> DirectorMovieSource::mappedCast(int cast_lib) {
    if (!cast_member_lookup_) {
        cast_member_lookup_ = std::make_unique<lookup::CastMemberLookup>(
            container_.casts, container_.cast_members, container_.cast_list, container_.config);
    }
    return toCastAsset(cast_member_lookup_->getMappedCast(cast_lib));
}

std::shared_ptr<CastMemberAsset> DirectorMovieSource::castMemberByNumber(
    int cast_lib, int member_number) {
    if (!cast_member_lookup_) {
        cast_member_lookup_ = std::make_unique<lookup::CastMemberLookup>(
            container_.casts, container_.cast_members, container_.cast_list, container_.config);
    }
    const auto chunk = cast_member_lookup_->getByNumber(cast_lib, member_number);
    if (!chunk) {
        return nullptr;
    }
    auto asset = toCastMemberAsset(chunk);
    if (asset) {
        asset->cast_lib = cast_lib;
        asset->member_num = member_number;
    }
    return asset;
}

std::shared_ptr<chunks::CastMemberChunk> DirectorMovieSource::castMemberChunkByNumber(
    int cast_lib, int member_number) {
    if (!cast_member_lookup_) {
        cast_member_lookup_ = std::make_unique<lookup::CastMemberLookup>(
            container_.casts, container_.cast_members, container_.cast_list, container_.config);
    }
    return cast_member_lookup_->getByNumber(cast_lib, member_number);
}

std::shared_ptr<chunks::CastMemberChunk> DirectorMovieSource::castMemberChunkByIndex(
    int cast_lib, int member_number) {
    if (!cast_member_lookup_) {
        cast_member_lookup_ = std::make_unique<lookup::CastMemberLookup>(
            container_.casts, container_.cast_members, container_.cast_list, container_.config);
    }
    return cast_member_lookup_->getByIndex(cast_lib, member_number);
}

std::shared_ptr<chunks::ScoreChunk> DirectorMovieSource::scoreChunk() const {
    return container_.score_chunk;
}

std::shared_ptr<chunks::FrameLabelsChunk> DirectorMovieSource::frameLabelsChunk() const {
    return container_.frame_labels_chunk;
}

int DirectorMovieSource::chunkCount() const {
    return static_cast<int>(container_.chunks.size());
}

int DirectorMovieSource::paletteCount() const {
    return static_cast<int>(container_.palettes.size());
}

std::vector<std::shared_ptr<chunks::PaletteChunk>> DirectorMovieSource::paletteChunks() {
    return container_.palettes;
}

std::vector<LinkedAsset> DirectorMovieSource::linkedAssetsForMember(
    const std::shared_ptr<CastMemberAsset>& member) {
    return linkedAssetsForMember(member, 0);
}

std::vector<LinkedAsset> DirectorMovieSource::linkedAssetsForMember(
    const std::shared_ptr<CastMemberAsset>& member, std::uint32_t fourcc) {
    if (!member || !container_.key_table) {
        return {};
    }
    const std::optional<std::uint32_t> filter = fourcc == 0 ? std::nullopt : std::make_optional(fourcc);
    std::vector<LinkedAsset> result;
    for (const auto& entry : container_.key_table->getEntriesForOwner(id::ChunkId(member->chunk_id))) {
        if (!matchesLinkedChunkFourCC(entry, filter)) {
            continue;
        }
        LinkedAsset linked_asset;
        linked_asset.fourcc = entry.fourcc;
        linked_asset.chunk_id = entry.section_id.value();
        result.push_back(std::move(linked_asset));
    }
    return result;
}

std::shared_ptr<ScoreAsset> DirectorMovieSource::scoreForMember(
    const std::shared_ptr<CastMemberAsset>& member) {
    for (const auto& linked : linkedAssetsForMember(member)) {
        if (auto chunk = getChunk(id::ChunkId(linked.chunk_id));
            chunk && std::dynamic_pointer_cast<chunks::ScoreChunk>(chunk)) {
            return toScoreAsset(std::dynamic_pointer_cast<chunks::ScoreChunk>(chunk));
        }
    }
    return nullptr;
}

// Palettes.

std::shared_ptr<const bitmap::Palette> DirectorMovieSource::resolvePalette(int palette_id) {
    if (!palette_resolver_) {
        palette_resolver_ = std::make_unique<lookup::PaletteResolver>(
            container_.casts, container_.cast_members, container_.palettes, container_.cast_list,
            container_.config, container_.key_table, [this](id::ChunkId chunk_id) {
                return getChunk(chunk_id);
            });
    }
    return palette_resolver_->resolve(palette_id);
}

std::shared_ptr<const bitmap::Palette> DirectorMovieSource::resolvePaletteExact(int palette_id) {
    if (!palette_resolver_) {
        palette_resolver_ = std::make_unique<lookup::PaletteResolver>(
            container_.casts, container_.cast_members, container_.palettes, container_.cast_list,
            container_.config, container_.key_table, [this](id::ChunkId chunk_id) {
                return getChunk(chunk_id);
            });
    }
    return palette_resolver_->resolveExact(palette_id);
}

std::shared_ptr<const bitmap::Palette> DirectorMovieSource::resolvePaletteByMemberNumber(
    int member_number) {
    return resolvePalette(member_number - 1);
}

// Lazy decodes.

std::optional<bitmap::Bitmap> DirectorMovieSource::decodeBitmap(
    const std::shared_ptr<CastMemberAsset>& member) {
    return decodeBitmap(member, nullptr);
}

std::optional<bitmap::Bitmap> DirectorMovieSource::decodeBitmap(
    const std::shared_ptr<CastMemberAsset>& member, const bitmap::Palette* palette_override) {
    if (!member || !member->isBitmap() || !container_.key_table) {
        return std::nullopt;
    }

    try {
        const int director_version = container_.config ? container_.config->directorVersion() : 1200;
        const auto info = cast::BitmapInfo::parse(member->specific_data, director_version);

        std::shared_ptr<chunks::BitmapChunk> bitmap_chunk;
        const std::vector<std::uint8_t>* edi_m_data = nullptr;
        const std::vector<std::uint8_t>* alfa_data = nullptr;
        for (const auto& linked : linkedAssetsForMember(member, format::fourCC(format::ChunkType::BITD))) {
            if (auto bitmap = std::dynamic_pointer_cast<chunks::BitmapChunk>(getChunk(id::ChunkId(linked.chunk_id)))) {
                bitmap_chunk = std::move(bitmap);
                break;
            }
        }
        for (const auto& linked : linkedAssetsForMember(member, format::fourCC(format::ChunkType::ediM))) {
            if (auto media = std::dynamic_pointer_cast<chunks::MediaChunk>(getChunk(id::ChunkId(linked.chunk_id)))) {
                edi_m_data = &media->audioData();
                break;
            }
            if (auto raw = std::dynamic_pointer_cast<chunks::RawChunk>(getChunk(id::ChunkId(linked.chunk_id)))) {
                edi_m_data = &raw->data();
                break;
            }
        }
        for (const auto& linked : linkedAssetsForMember(member, format::fourCC(format::ChunkType::ALFA))) {
            if (auto raw = std::dynamic_pointer_cast<chunks::RawChunk>(getChunk(id::ChunkId(linked.chunk_id)))) {
                alfa_data = &raw->data();
                break;
            }
        }
        if (!bitmap_chunk) {
            if (edi_m_data != nullptr && info.bit_depth == 32) {
                return decodeEdiMBitmap(info, *edi_m_data, alfa_data);
            }
            return std::nullopt;
        }

        std::shared_ptr<const bitmap::Palette> resolved_palette;
        const bitmap::Palette* palette = palette_override;
        bool should_resolve_unlinked_mac_like_palette = false;
        if (!palette) {
            resolved_palette = info.palette_id >= 0 ? resolvePaletteExact(info.palette_id) : resolvePalette(info.palette_id);
            if (resolved_palette == nullptr && info.palette_id >= 0) {
                const auto* fallback_palette = container_.config != nullptr && container_.config->platform() == 2
                    ? &bitmap::Palette::systemWinPalette()
                    : &bitmap::Palette::systemMacPalette();
                resolved_palette = borrowedPalette(fallback_palette);
                should_resolve_unlinked_mac_like_palette = fallback_palette == &bitmap::Palette::systemMacPalette();
            }
            palette = resolved_palette.get();
        }

        auto bitmap = bitmap::BitmapDecoder::decode(bitmap_chunk->data(),
                                                    info.width,
                                                    info.height,
                                                    info.bit_depth,
                                                    palette,
                                                    container_.endian == io::ByteOrder::BigEndian,
                                                    director_version,
                                                    info.pitch);
        if (should_resolve_unlinked_mac_like_palette) {
            if (auto closest_palette = closestMacLikePaletteForIndexedBitmap(bitmap, container_.palettes)) {
                (void)bitmap.remapImagePalette(closest_palette);
                resolved_palette = std::move(closest_palette);
                palette = resolved_palette.get();
            }
        }
        bitmap.setNativeAlpha(info.use_alpha);
        if (palette) {
            bitmap.setImagePalette(palette);
        }
        return bitmap;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<bitmap::Bitmap> DirectorMovieSource::decodeEdiMBitmap(
    const cast::BitmapInfo& info,
    const std::vector<std::uint8_t>& jpeg_data,
    const std::vector<std::uint8_t>* alfa_data) {
    try {
        if (!jpeg_decoder_) {
            return std::nullopt;
        }

        auto jpeg_bitmap = jpeg_decoder_(jpeg_data);
        if (!jpeg_bitmap.has_value()) {
            return std::nullopt;
        }

        bitmap::Bitmap bitmap(info.width, info.height, 32);
        const int copy_height = std::min(info.height, jpeg_bitmap->height());
        const int copy_width = std::min(info.width, jpeg_bitmap->width());
        for (int y = 0; y < copy_height; ++y) {
            for (int x = 0; x < copy_width; ++x) {
                const auto rgb = jpeg_bitmap->getPixel(x, y);
                bitmap.setPixelRGB(x,
                                   y,
                                   static_cast<int>((rgb >> 16U) & 0xFFU),
                                   static_cast<int>((rgb >> 8U) & 0xFFU),
                                   static_cast<int>(rgb & 0xFFU));
            }
        }

        if (alfa_data != nullptr && !alfa_data->empty()) {
            const int scan_width = bitmap::BitmapDecoder::calculateScanWidth(info.width, 8);
            const int expected_size = scan_width * info.height;
            const auto alpha_channel = bitmap::BitmapDecoder::decompressRLE(*alfa_data, expected_size);
            for (int y = 0; y < info.height; ++y) {
                const int row_offset = y * scan_width;
                for (int x = 0; x < info.width; ++x) {
                    const int byte_index = row_offset + x;
                    if (byte_index < static_cast<int>(alpha_channel.size())) {
                        const auto alpha = static_cast<std::uint32_t>(alpha_channel[static_cast<std::size_t>(byte_index)]);
                        const auto pixel = bitmap.getPixel(x, y);
                        bitmap.setPixel(x, y, (alpha << 24U) | (pixel & 0x00FFFFFFU));
                    }
                }
            }
        } else {
            for (int y = 0; y < info.height; ++y) {
                for (int x = 0; x < info.width; ++x) {
                    const auto pixel = bitmap.getPixel(x, y);
                    bitmap.setPixel(x, y, 0xFF000000U | (pixel & 0x00FFFFFFU));
                }
            }
        }

        bitmap.setNativeAlpha(info.use_alpha || (alfa_data != nullptr && !alfa_data->empty()));
        return bitmap;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<W3DFile> DirectorMovieSource::w3dForMember(const std::shared_ptr<CastMemberAsset>& member) {
    if (!member || !member->isShockwave3D()) {
        return std::nullopt;
    }
    for (const auto& linked : linkedAssetsForMember(member)) {
        const auto chunk = getChunk(id::ChunkId(linked.chunk_id));
        const std::vector<std::uint8_t>* data = nullptr;
        if (const auto raw = std::dynamic_pointer_cast<chunks::RawChunk>(chunk)) {
            data = &raw->data();
        } else if (const auto media = std::dynamic_pointer_cast<chunks::MediaChunk>(chunk)) {
            data = &media->audioData();
        }
        if (data == nullptr || data->empty()) {
            continue;
        }
        try {
            auto w3d = W3DFile::load(*data);
            if (w3d.hasSceneData()) {
                return w3d;
            }
        } catch (const std::exception&) {
        }
    }
    return std::nullopt;
}

std::shared_ptr<TextAsset> DirectorMovieSource::textForMember(const std::shared_ptr<CastMemberAsset>& member) {
    std::shared_ptr<TextAsset> first_match;
    for (const auto& linked : linkedAssetsForMember(member, format::fourCC(format::ChunkType::STXT))) {
        if (auto text = std::dynamic_pointer_cast<chunks::TextChunk>(getChunk(id::ChunkId(linked.chunk_id)))) {
            auto asset = toTextAsset(text);
            if (!first_match) {
                first_match = asset;
            }
            if (asset && !asset->text.empty()) {
                return asset;
            }
        }
    }
    if (first_match) {
        return first_match;
    }
    if (!member) {
        return nullptr;
    }
    if (auto text = std::dynamic_pointer_cast<chunks::TextChunk>(getChunk(id::ChunkId(member->chunk_id)))) {
        return toTextAsset(text);
    }
    return nullptr;
}

std::vector<std::shared_ptr<chunks::TextChunk>> DirectorMovieSource::textChunksForMember(
    const std::shared_ptr<CastMemberAsset>& member) {
    std::vector<std::shared_ptr<chunks::TextChunk>> result;
    for (const auto& linked : linkedAssetsForMember(member, format::fourCC(format::ChunkType::STXT))) {
        if (auto text = std::dynamic_pointer_cast<chunks::TextChunk>(getChunk(id::ChunkId(linked.chunk_id)))) {
            result.push_back(text);
        }
    }
    return result;
}

std::optional<cast::XmedStyledText> DirectorMovieSource::xmedStyledTextForMember(
    const std::shared_ptr<CastMemberAsset>& member) {
    for (const auto& linked : linkedAssetsForMember(member, format::fourCC(format::ChunkType::XMED))) {
        if (auto raw = std::dynamic_pointer_cast<chunks::RawChunk>(getChunk(id::ChunkId(linked.chunk_id)))) {
            return cast::XmedTextParser::parseStyled(raw->data(), member->specific_data);
        }
    }
    return std::nullopt;
}

std::shared_ptr<SoundAsset> DirectorMovieSource::soundForMember(const std::shared_ptr<CastMemberAsset>& member) {
    for (const auto& linked : linkedAssetsForMember(member)) {
        if (auto sound = std::dynamic_pointer_cast<chunks::SoundChunk>(getChunk(id::ChunkId(linked.chunk_id)))) {
            return toSoundAsset(sound);
        }
        if (auto media = std::dynamic_pointer_cast<chunks::MediaChunk>(getChunk(id::ChunkId(linked.chunk_id)))) {
            chunks::SoundChunk converted = media->toSoundChunk();
            return toSoundAsset(std::make_shared<chunks::SoundChunk>(std::move(converted)));
        }
    }
    return nullptr;
}

std::optional<std::string> DirectorMovieSource::fontNameForId(int font_id) {
    auto maps = container_.font_maps;
    std::sort(maps.begin(), maps.end(), [](const auto& left, const auto& right) {
        const auto left_size = left ? left->entries().size() : 0U;
        const auto right_size = right ? right->entries().size() : 0U;
        return left_size > right_size;
    });
    for (const auto& map : maps) {
        if (!map) {
            continue;
        }
        auto font_name = map->fontNameForId(font_id);
        if (font_name.has_value() && !font_name->empty()) {
            return font_name;
        }
    }
    return std::nullopt;
}

// Score and movie info.

std::shared_ptr<ScoreAsset> DirectorMovieSource::score() {
    // A score with no frames is not a usable score. This preserves the old
    // hasScore() semantics (score_chunk && frame_count > 0).
    if (container_.score_chunk && container_.score_chunk->getFrameCount() <= 0) {
        return nullptr;
    }
    return toScoreAsset(container_.score_chunk);
}

int DirectorMovieSource::stageWidth() const {
    return container_.config ? container_.config->stageWidth() : 0;
}

int DirectorMovieSource::stageHeight() const {
    return container_.config ? container_.config->stageHeight() : 0;
}

int DirectorMovieSource::channelCount() const {
    if (!container_.config) {
        return 120;
    }
    const int human_version = (container_.config->directorVersion() / 100) * 100;
    if (human_version >= 1300) {
        return 1000;
    }
    if (human_version >= 1200) {
        return 120;
    }
    return 48;
}

int DirectorMovieSource::tempo() const {
    return container_.config ? container_.config->tempo() : 15;
}

int DirectorMovieSource::scoreTempo(int frame) const {
    return container_.score_chunk ? container_.score_chunk->getFrameTempo(frame) : -1;
}

std::optional<ScoreAsset::PaletteChannelData> DirectorMovieSource::scorePalette(int frame) const {
    if (!container_.score_chunk) {
        return std::nullopt;
    }
    const auto data = container_.score_chunk->getFramePalette(frame);
    if (!data.has_value()) {
        return std::nullopt;
    }
    ScoreAsset::PaletteChannelData out;
    out.frame_index = data->frame_index;
    out.cast_lib = data->cast_lib;
    out.cast_member = data->cast_member;
    return out;
}

std::shared_ptr<ConfigAsset> DirectorMovieSource::config() const {
    return toConfigAsset(container_.config);
}

std::shared_ptr<KeyTableAsset> DirectorMovieSource::keyTable() {
    return toKeyTableAsset(container_.key_table);
}

std::shared_ptr<FrameLabelAsset> DirectorMovieSource::frameLabels() {
    return toFrameLabelAsset(container_.frame_labels_chunk);
}

// JPEG decode hooks.

void DirectorMovieSource::setJpegDecoder(JpegDecoder decoder) {
    jpeg_decoder_ = std::move(decoder);
}

void DirectorMovieSource::markJpegDecodePending() {
    jpeg_decode_pending_ = true;
}

void DirectorMovieSource::clearJpegDecodePending() {
    jpeg_decode_pending_ = false;
}

bool DirectorMovieSource::consumeJpegDecodePending() {
    const bool pending = jpeg_decode_pending_;
    jpeg_decode_pending_ = false;
    return pending;
}

} // namespace libreshockwave::moviesource
