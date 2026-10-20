#include "libreshockwave/director_file.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <string>
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
#include "libreshockwave/format/afterburner_reader.hpp"
#include "libreshockwave/lookup/cast_member_lookup.hpp"
#include "libreshockwave/lookup/palette_resolver.hpp"
#include "libreshockwave/lookup/script_lookup.hpp"

namespace libreshockwave {
namespace {

DirectorFile::JpegDecoder& jpegDecoder() {
    static DirectorFile::JpegDecoder decoder;
    return decoder;
}

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
            chunk->colors(),
            "Custom Palette #" + std::to_string(chunk->id().value()));
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

bool& jpegDecodePending() {
    static bool pending = false;
    return pending;
}

std::optional<chunks::ScriptChunkType> scriptChunkTypeForCastMemberScriptType(chunks::CastMemberScriptType type) {
    switch (type) {
        case chunks::CastMemberScriptType::Score: return chunks::ScriptChunkType::Score;
        case chunks::CastMemberScriptType::Behavior: return chunks::ScriptChunkType::Behavior;
        case chunks::CastMemberScriptType::MovieScript: return chunks::ScriptChunkType::MovieScript;
        case chunks::CastMemberScriptType::Parent: return chunks::ScriptChunkType::Parent;
        case chunks::CastMemberScriptType::Unknown: return chunks::ScriptChunkType::Unknown;
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

std::uint32_t readContainerFourCC(io::BinaryReader& reader, io::ByteOrder endian) {
    if (endian == io::ByteOrder::LittleEndian) {
        return reader.readU32();
    }
    return reader.readFourCC();
}

bool shouldReplaceSelectedScore(const std::shared_ptr<chunks::ScoreChunk>& current,
                                const std::shared_ptr<chunks::ScoreChunk>& candidate) {
    if (!candidate) {
        return false;
    }
    if (!current) {
        return true;
    }
    if (candidate->frameIntervals().size() != current->frameIntervals().size()) {
        return candidate->frameIntervals().size() > current->frameIntervals().size();
    }
    if (candidate->getFrameCount() != current->getFrameCount()) {
        return candidate->getFrameCount() > current->getFrameCount();
    }
    if (candidate->frameData().frame_channel_data.size() != current->frameData().frame_channel_data.size()) {
        return candidate->frameData().frame_channel_data.size() > current->frameData().frame_channel_data.size();
    }
    return candidate->entries().size() > current->entries().size();
}

bool matchesLinkedChunkFourCC(const chunks::KeyTableChunk::KeyTableEntry& entry,
                              std::optional<std::uint32_t> fourcc) {
    return !fourcc.has_value() || entry.fourcc == *fourcc;
}

} // namespace

format::ChunkType DirectorChunkInfo::type() const {
    return format::chunkTypeFromFourCC(fourcc);
}

DirectorFile::DirectorFile(io::ByteOrder endian, bool afterburner, int version, format::ChunkType movie_type)
    : endian_(endian), afterburner_(afterburner), version_(version), movie_type_(movie_type) {}

DirectorFile::~DirectorFile() = default;

void DirectorFile::setJpegDecoder(JpegDecoder decoder) {
    jpegDecoder() = std::move(decoder);
}

void DirectorFile::markJpegDecodePending() {
    jpegDecodePending() = true;
}

void DirectorFile::clearJpegDecodePending() {
    jpegDecodePending() = false;
}

bool DirectorFile::consumeJpegDecodePending() {
    const bool pending = jpegDecodePending();
    jpegDecodePending() = false;
    return pending;
}

io::ByteOrder DirectorFile::endian() const { return endian_; }
bool DirectorFile::isAfterburner() const { return afterburner_; }
int DirectorFile::version() const { return version_; }
format::ChunkType DirectorFile::movieType() const { return movie_type_; }
bool DirectorFile::isCapitalX() const { return capital_x_; }
std::shared_ptr<chunks::ConfigChunk> DirectorFile::config() const { return config_; }
std::shared_ptr<chunks::KeyTableChunk> DirectorFile::keyTable() const { return key_table_; }
std::shared_ptr<chunks::CastListChunk> DirectorFile::castList() const { return cast_list_; }
std::shared_ptr<chunks::ScriptContextChunk> DirectorFile::scriptContext() const { return script_context_; }
std::shared_ptr<chunks::ScriptNamesChunk> DirectorFile::scriptNames() const { return script_names_; }
std::shared_ptr<chunks::ScoreChunk> DirectorFile::scoreChunk() const { return score_chunk_; }
std::shared_ptr<chunks::FrameLabelsChunk> DirectorFile::frameLabelsChunk() const { return frame_labels_chunk_; }
const std::string& DirectorFile::basePath() const { return base_path_; }
void DirectorFile::setBasePath(std::string base_path) { base_path_ = std::move(base_path); }
const std::map<int, DirectorChunkInfo>& DirectorFile::chunkInfo() const { return chunk_info_; }
const std::map<int, std::shared_ptr<chunks::Chunk>>& DirectorFile::chunks() const { return chunks_; }
const std::vector<std::shared_ptr<chunks::CastChunk>>& DirectorFile::casts() const { return casts_; }
const std::vector<std::shared_ptr<chunks::CastMemberChunk>>& DirectorFile::castMembers() const { return cast_members_; }
const std::vector<std::shared_ptr<chunks::ScriptChunk>>& DirectorFile::scripts() const { return scripts_; }
const std::vector<std::shared_ptr<chunks::PaletteChunk>>& DirectorFile::palettes() const { return palettes_; }
const std::vector<std::shared_ptr<chunks::FontMapChunk>>& DirectorFile::fontMaps() const { return font_maps_; }

std::shared_ptr<chunks::CastChunk> DirectorFile::getMappedCastChunk(int cast_lib) {
    return castMemberLookup().getMappedCast(cast_lib);
}

std::shared_ptr<chunks::CastMemberChunk> DirectorFile::getCastMemberByIndex(int cast_lib, int cast_member_index) {
    return castMemberLookup().getByIndex(cast_lib, cast_member_index);
}

std::shared_ptr<chunks::CastMemberChunk> DirectorFile::getCastMemberByNumber(int cast_lib, int member_number) {
    return castMemberLookup().getByNumber(cast_lib, member_number);
}

std::shared_ptr<chunks::ScriptChunk> DirectorFile::getScriptByContextId(int script_id) {
    return scriptLookup().getByContextId(script_id);
}

std::vector<std::shared_ptr<chunks::ScriptChunk>> DirectorFile::getScriptsByContextId(int script_id) {
    return scriptLookup().getAllByContextId(script_id);
}

std::shared_ptr<chunks::ScriptChunk> DirectorFile::getScriptForCastMember(
    const std::shared_ptr<chunks::CastMemberChunk>& member) {
    return getScriptForCastMember(member, nullptr);
}

std::shared_ptr<chunks::ScriptChunk> DirectorFile::getScriptForCastMember(
    const std::shared_ptr<chunks::CastMemberChunk>& member,
    const std::shared_ptr<chunks::CastChunk>& cast_chunk) {
    if (!member || !member->isScript() || member->scriptId() <= 0) {
        return nullptr;
    }

    const auto matches = getScriptsByContextId(member->scriptId());
    if (matches.empty()) {
        return nullptr;
    }
    if (matches.size() == 1) {
        return matches.front();
    }

    std::optional<id::ChunkId> preferred_context_owner;
    if (cast_chunk && key_table_) {
        preferred_context_owner = key_table_->getOwnerCastId(cast_chunk->id());
    }
    if (preferred_context_owner.has_value() && key_table_) {
        for (const auto& context : all_script_contexts_) {
            if (!context) {
                continue;
            }
            const auto context_owner = key_table_->getOwnerCastId(context->id());
            if (!context_owner.has_value() || context_owner->value() != preferred_context_owner->value()) {
                continue;
            }
            const int index = member->scriptId() - 1;
            if (index < 0 || index >= static_cast<int>(context->entries().size())) {
                break;
            }
            const auto script_id = context->entries()[static_cast<std::size_t>(index)].id;
            if (script_id.value() <= 0) {
                break;
            }
            for (const auto& script : matches) {
                if (script && script->id().value() == script_id.value()) {
                    return script;
                }
            }
            break;
        }
    }

    if (key_table_) {
        for (const auto& script : matches) {
            if (!script) {
                continue;
            }
            const auto owner_id = key_table_->getOwnerCastId(script->id());
            if (owner_id.has_value() && owner_id->value() == member->id().value()) {
                return script;
            }
        }
    }

    if (const auto member_type = member->getScriptType(); member_type.has_value()) {
        const auto script_type = scriptChunkTypeForCastMemberScriptType(member_type.value());
        if (script_type.has_value()) {
            for (const auto& script : matches) {
                if (script && script->scriptType() == script_type.value()) {
                    return script;
                }
            }
        }
    }

    return matches.front();
}

std::optional<chunks::CastMemberScriptType> DirectorFile::getScriptType(const std::shared_ptr<chunks::ScriptChunk>& script) {
    if (!script) {
        return std::nullopt;
    }

    if (key_table_) {
        const auto owner_id = key_table_->getOwnerCastId(script->id());
        if (owner_id.has_value()) {
            for (const auto& member : cast_members_) {
                if (member && member->id().value() == owner_id->value() && member->isScript()) {
                    return member->getScriptType();
                }
            }
        }
    }

    return scriptLookup().getScriptType(script);
}

std::string DirectorFile::getScriptName(const std::shared_ptr<chunks::ScriptChunk>& script) {
    if (!script) {
        return "";
    }

    if (key_table_) {
        const auto owner_id = key_table_->getOwnerCastId(script->id());
        if (owner_id.has_value()) {
            for (const auto& member : cast_members_) {
                if (member && member->id().value() == owner_id->value() && member->isScript()) {
                    return member->name();
                }
            }
        }
    }

    for (const auto& member : cast_members_) {
        if (!member || !member->isScript()) {
            continue;
        }
        auto resolved = getScriptForCastMember(member);
        if (resolved && resolved->id().value() == script->id().value()) {
            return member->name();
        }
    }
    return "";
}

std::shared_ptr<chunks::ScriptNamesChunk> DirectorFile::getScriptNamesById(id::ChunkId id) const {
    if (const auto found = script_names_by_id_.find(id.value()); found != script_names_by_id_.end()) {
        return found->second;
    }
    return nullptr;
}

std::shared_ptr<chunks::ScriptNamesChunk> DirectorFile::getScriptNamesForScript(
    const std::shared_ptr<chunks::ScriptChunk>& script) {
    if (!script) {
        return script_names_;
    }
    if (const auto cached = script_names_for_script_cache_.find(script->id().value()); cached != script_names_for_script_cache_.end()) {
        return cached->second;
    }

    for (const auto& context : all_script_contexts_) {
        if (!context) {
            continue;
        }
        for (const auto& entry : context->entries()) {
            if (entry.id.value() != script->id().value()) {
                continue;
            }
            auto names = getScriptNamesById(context->lnamSectionId());
            auto resolved = names ? names : script_names_;
            if (resolved) {
                script_names_for_script_cache_[script->id().value()] = resolved;
            }
            return resolved;
        }
    }

    if (script_names_) {
        script_names_for_script_cache_[script->id().value()] = script_names_;
    }
    return script_names_;
}

std::vector<std::string> DirectorFile::getAllGlobalNames() {
    std::vector<std::string> result;
    if (!script_names_) {
        return result;
    }
    for (const auto& script : scripts_) {
        appendUnique(result, getScriptGlobals(script));
    }
    return result;
}

std::vector<std::string> DirectorFile::getAllPropertyNames() {
    std::vector<std::string> result;
    if (!script_names_) {
        return result;
    }
    for (const auto& script : scripts_) {
        appendUnique(result, getScriptProperties(script));
    }
    return result;
}

std::vector<std::string> DirectorFile::getScriptGlobals(const std::shared_ptr<chunks::ScriptChunk>& script) {
    auto names = getScriptNamesForScript(script);
    return script && names ? script->getGlobalNames(names.get()) : std::vector<std::string>{};
}

std::vector<std::string> DirectorFile::getScriptProperties(const std::shared_ptr<chunks::ScriptChunk>& script) {
    auto names = getScriptNamesForScript(script);
    return script && names ? script->getPropertyNames(names.get()) : std::vector<std::string>{};
}

std::vector<ScriptInfo> DirectorFile::getScriptInfoList() {
    std::vector<ScriptInfo> result;
    if (!script_names_) {
        return result;
    }

    for (const auto& script : scripts_) {
        if (!script) {
            continue;
        }
        auto names = getScriptNamesForScript(script);
        std::vector<std::string> handler_names;
        if (names) {
            handler_names.reserve(script->handlers().size());
            for (const auto& handler : script->handlers()) {
                handler_names.push_back(names->getName(handler.name_id));
            }
        }

        auto script_type = script->scriptType();
        if (const auto cast_type = getScriptType(script); cast_type.has_value()) {
            if (const auto converted = scriptChunkTypeForCastMemberScriptType(cast_type.value()); converted.has_value()) {
                script_type = converted.value();
            }
        }

        result.push_back(ScriptInfo{script->id().value(),
                                    getScriptName(script),
                                    script_type,
                                    names ? script->getGlobalNames(names.get()) : std::vector<std::string>{},
                                    names ? script->getPropertyNames(names.get()) : std::vector<std::string>{},
                                    std::move(handler_names)});
    }
    return result;
}

std::vector<std::string> DirectorFile::getExternalCastPaths() const {
    std::vector<std::string> paths;
    if (!cast_list_) {
        return paths;
    }

    for (const auto& entry : cast_list_->entries()) {
        if (!entry.path.empty()) {
            paths.push_back(entry.path);
        }
    }
    return paths;
}

bool DirectorFile::hasExternalCasts() const {
    if (!cast_list_) {
        return false;
    }
    return std::any_of(cast_list_->entries().begin(), cast_list_->entries().end(), [](const auto& entry) {
        return !entry.path.empty();
    });
}

bool DirectorFile::hasScore() const {
    return score_chunk_ && score_chunk_->getFrameCount() > 0;
}

std::optional<std::string> DirectorFile::getFontNameForId(int font_id) const {
    auto maps = font_maps_;
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

std::optional<cast::XmedStyledText> DirectorFile::getXmedStyledTextForMember(
    const std::shared_ptr<chunks::CastMemberChunk>& member) {
    for (const auto& chunk : getLinkedChunksForMember(member, format::fourCC(format::ChunkType::XMED))) {
        if (auto raw = std::dynamic_pointer_cast<chunks::RawChunk>(chunk)) {
            return cast::XmedTextParser::parseStyled(raw->data(), member->specificData());
        }
    }
    return std::nullopt;
}

std::vector<DirectorChunkInfo> DirectorFile::getLinkedChunkInfoForMember(
    const std::shared_ptr<chunks::CastMemberChunk>& member) const {
    return getLinkedChunkInfoForMember(member, 0);
}

std::vector<DirectorChunkInfo> DirectorFile::getLinkedChunkInfoForMember(
    const std::shared_ptr<chunks::CastMemberChunk>& member,
    std::uint32_t fourcc) const {
    if (!member || !key_table_) {
        return {};
    }

    const std::optional<std::uint32_t> filter = fourcc == 0 ? std::nullopt : std::make_optional(fourcc);
    std::vector<DirectorChunkInfo> result;
    for (const auto& entry : key_table_->getEntriesForOwner(member->id())) {
        if (!matchesLinkedChunkFourCC(entry, filter)) {
            continue;
        }
        if (const auto* info = getChunkInfo(entry.section_id)) {
            result.push_back(*info);
        }
    }
    return result;
}

std::vector<std::shared_ptr<chunks::Chunk>> DirectorFile::getLinkedChunksForMember(
    const std::shared_ptr<chunks::CastMemberChunk>& member) {
    return getLinkedChunksForMember(member, 0);
}

std::vector<std::shared_ptr<chunks::Chunk>> DirectorFile::getLinkedChunksForMember(
    const std::shared_ptr<chunks::CastMemberChunk>& member,
    std::uint32_t fourcc) {
    if (!member || !key_table_) {
        return {};
    }

    const std::optional<std::uint32_t> filter = fourcc == 0 ? std::nullopt : std::make_optional(fourcc);
    std::vector<std::shared_ptr<chunks::Chunk>> result;
    for (const auto& entry : key_table_->getEntriesForOwner(member->id())) {
        if (!matchesLinkedChunkFourCC(entry, filter)) {
            continue;
        }
        if (auto chunk = getChunk(entry.section_id)) {
            result.push_back(std::move(chunk));
        }
    }
    return result;
}

std::optional<W3DFile> DirectorFile::getW3DFileForMember(
    const std::shared_ptr<chunks::CastMemberChunk>& member) {
    if (!member || !member->isShockwave3D()) {
        return std::nullopt;
    }

    for (const auto& chunk : getLinkedChunksForMember(member)) {
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

std::shared_ptr<chunks::ScoreChunk> DirectorFile::getScoreForMember(
    const std::shared_ptr<chunks::CastMemberChunk>& member) {
    for (const auto& chunk : getLinkedChunksForMember(member)) {
        if (auto score = std::dynamic_pointer_cast<chunks::ScoreChunk>(chunk)) {
            return score;
        }
    }
    return nullptr;
}

std::vector<std::shared_ptr<chunks::TextChunk>> DirectorFile::getTextChunksForMember(
    const std::shared_ptr<chunks::CastMemberChunk>& member) {
    std::vector<std::shared_ptr<chunks::TextChunk>> result;
    for (const auto& chunk : getLinkedChunksForMember(member, format::fourCC(format::ChunkType::STXT))) {
        if (auto text = std::dynamic_pointer_cast<chunks::TextChunk>(chunk)) {
            result.push_back(text);
        }
    }
    return result;
}

std::shared_ptr<chunks::TextChunk> DirectorFile::getTextForMember(
    const std::shared_ptr<chunks::CastMemberChunk>& member) {
    std::shared_ptr<chunks::TextChunk> first_match;
    for (const auto& text : getTextChunksForMember(member)) {
        if (!first_match) {
            first_match = text;
        }
        if (text && !text->text().empty()) {
            return text;
        }
    }
    if (first_match) {
        return first_match;
    }
    if (!member) {
        return nullptr;
    }
    return std::dynamic_pointer_cast<chunks::TextChunk>(getChunk(member->id()));
}

int DirectorFile::stageWidth() const {
    return config_ ? config_->stageWidth() : 0;
}

int DirectorFile::stageHeight() const {
    return config_ ? config_->stageHeight() : 0;
}

int DirectorFile::channelCount() const {
    if (!config_) {
        return 120;
    }

    const int human_version = (config_->directorVersion() / 100) * 100;
    if (human_version >= 1300) {
        return 1000;
    }
    if (human_version >= 1200) {
        return 120;
    }
    return 48;
}

int DirectorFile::tempo() const {
    return config_ ? config_->tempo() : 15;
}

int DirectorFile::getScoreTempo(int frame) const {
    return score_chunk_ ? score_chunk_->getFrameTempo(frame) : -1;
}

std::optional<chunks::ScoreChunk::PaletteChannelData> DirectorFile::getScorePalette(int frame) const {
    return score_chunk_ ? score_chunk_->getFramePalette(frame) : std::nullopt;
}

std::shared_ptr<const bitmap::Palette> DirectorFile::resolvePalette(int palette_id) {
    return paletteResolver().resolve(palette_id);
}

std::shared_ptr<const bitmap::Palette> DirectorFile::resolvePaletteExact(int palette_id) {
    return paletteResolver().resolveExact(palette_id);
}

std::shared_ptr<const bitmap::Palette> DirectorFile::resolvePaletteByMemberNumber(int member_number) {
    return paletteResolver().resolve(member_number - 1);
}

std::optional<bitmap::Bitmap> DirectorFile::decodeBitmap(const std::shared_ptr<chunks::CastMemberChunk>& member) {
    return decodeBitmap(member, nullptr);
}

std::optional<bitmap::Bitmap> DirectorFile::decodeBitmap(const std::shared_ptr<chunks::CastMemberChunk>& member,
                                                         const bitmap::Palette* palette_override) {
    if (!member || !member->isBitmap() || !key_table_) {
        return std::nullopt;
    }

    try {
        const int director_version = config_ ? config_->directorVersion() : 1200;
        const auto info = cast::BitmapInfo::parse(member->specificData(), director_version);

        std::shared_ptr<chunks::BitmapChunk> bitmap_chunk;
        const std::vector<std::uint8_t>* edi_m_data = nullptr;
        const std::vector<std::uint8_t>* alfa_data = nullptr;
        for (const auto& chunk : getLinkedChunksForMember(member, format::fourCC(format::ChunkType::BITD))) {
            if (auto bitmap = std::dynamic_pointer_cast<chunks::BitmapChunk>(chunk)) {
                bitmap_chunk = std::move(bitmap);
                break;
            }
        }
        for (const auto& chunk : getLinkedChunksForMember(member, format::fourCC(format::ChunkType::ediM))) {
            if (auto media = std::dynamic_pointer_cast<chunks::MediaChunk>(chunk)) {
                edi_m_data = &media->audioData();
                break;
            }
            if (auto raw = std::dynamic_pointer_cast<chunks::RawChunk>(chunk)) {
                edi_m_data = &raw->data();
                break;
            }
        }
        for (const auto& chunk : getLinkedChunksForMember(member, format::fourCC(format::ChunkType::ALFA))) {
            if (auto raw = std::dynamic_pointer_cast<chunks::RawChunk>(chunk)) {
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
                const auto* fallback_palette = config_ != nullptr && config_->platform() == 2
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
                                                    endian_ == io::ByteOrder::BigEndian,
                                                    director_version,
                                                    info.pitch);
        if (should_resolve_unlinked_mac_like_palette) {
            if (auto closest_palette = closestMacLikePaletteForIndexedBitmap(bitmap, palettes_)) {
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

std::optional<bitmap::Bitmap> DirectorFile::decodeEdiMBitmap(const cast::BitmapInfo& info,
                                                             const std::vector<std::uint8_t>& jpeg_data,
                                                             const std::vector<std::uint8_t>* alfa_data) {
    try {
        auto& decoder = jpegDecoder();
        if (!decoder) {
            return std::nullopt;
        }

        auto jpeg_bitmap = decoder(jpeg_data);
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

std::shared_ptr<chunks::Chunk> DirectorFile::getChunk(id::ChunkId id) {
    if (const auto found = chunks_.find(id.value()); found != chunks_.end()) {
        return found->second;
    }
    return reparseChunk(id);
}

const DirectorChunkInfo* DirectorFile::getChunkInfo(id::ChunkId id) const {
    if (const auto found = chunk_info_.find(id.value()); found != chunk_info_.end()) {
        return &found->second;
    }
    return nullptr;
}

void DirectorFile::releaseNonEssentialChunks() {
    for (auto it = chunks_.begin(); it != chunks_.end();) {
        const auto& chunk = it->second;
        if (std::dynamic_pointer_cast<chunks::SoundChunk>(chunk) ||
            std::dynamic_pointer_cast<chunks::MediaChunk>(chunk) ||
            std::dynamic_pointer_cast<chunks::RawChunk>(chunk)) {
            it = chunks_.erase(it);
        } else {
            ++it;
        }
    }
}

std::shared_ptr<DirectorFile> DirectorFile::load(const std::vector<std::uint8_t>& data) {
    io::BinaryReader reader(data, io::ByteOrder::BigEndian);
    const auto container_four_cc = reader.readFourCC();
    const auto container = format::chunkTypeFromFourCC(container_four_cc);

    io::ByteOrder endian = io::ByteOrder::BigEndian;
    bool is_riff = false;
    if (container == format::ChunkType::RIFX) {
        endian = io::ByteOrder::BigEndian;
    } else if (container == format::ChunkType::XFIR) {
        endian = io::ByteOrder::LittleEndian;
    } else if (container == format::ChunkType::RIFF || container == format::ChunkType::FFIR) {
        endian = io::ByteOrder::LittleEndian;
        is_riff = true;
    } else {
        throw DirectorFileLoadError("Not a supported Director RIFX/XFIR/RIFF file: " +
                                    io::BinaryReader::fourCCToString(container_four_cc));
    }

    reader.setOrder(endian);
    (void)reader.readI32();
    if (is_riff) {
        const auto rmmp_four_cc = reader.readFourCC();
        if (format::chunkTypeFromFourCC(rmmp_four_cc) != format::ChunkType::RMMP) {
            throw DirectorFileLoadError("Expected RMMP marker in RIFF file, got " +
                                        io::BinaryReader::fourCCToString(rmmp_four_cc));
        }
        auto file = loadRIFF(reader, endian);
        file->data_store_ = data;
        return file;
    }

    const auto movie_type = format::chunkTypeFromFourCC(reader.readI32());
    if (format::isAfterburner(movie_type)) {
        auto file = loadAfterburner(reader, endian, movie_type);
        file->data_store_ = data;
        return file;
    }

    auto file = loadRIFX(reader, endian, movie_type);
    file->data_store_ = data;
    return file;
}

std::shared_ptr<DirectorFile> DirectorFile::loadRIFX(io::BinaryReader& reader,
                                                     io::ByteOrder endian,
                                                     format::ChunkType movie_type) {
    auto file = std::make_shared<DirectorFile>(endian, false, 0, movie_type);

    const auto imap_four_cc = readContainerFourCC(reader, endian);
    if (format::chunkTypeFromFourCC(imap_four_cc) != format::ChunkType::IMAP) {
        throw DirectorFileLoadError("Expected imap chunk, got " + io::BinaryReader::fourCCToString(imap_four_cc));
    }

    const int imap_len = reader.readI32();
    (void)reader.readI32();
    const int mmap_offset = reader.readI32();
    int imap_director_version = 0;
    if (imap_len >= 12 && reader.bytesLeft() >= 4) {
        imap_director_version = reader.readI32();
    }
    file->setVersion(imap_director_version);

    reader.seek(static_cast<std::size_t>(mmap_offset));
    const auto mmap_four_cc = readContainerFourCC(reader, endian);
    if (format::chunkTypeFromFourCC(mmap_four_cc) != format::ChunkType::MMAP) {
        throw DirectorFileLoadError("Expected mmap chunk, got " + io::BinaryReader::fourCCToString(mmap_four_cc));
    }

    (void)reader.readI32();
    (void)reader.readI16();
    (void)reader.readI16();
    (void)reader.readI32();
    const int chunk_count_used = reader.readI32();
    (void)reader.readI32();
    reader.skip(4);
    (void)reader.readI32();

    for (int index = 0; index < chunk_count_used && reader.bytesLeft() >= 20; ++index) {
        const auto fourcc = readContainerFourCC(reader, endian);
        const int length = reader.readI32();
        const int offset = reader.readI32();
        (void)reader.readI16();
        reader.skip(2);
        (void)reader.readI32();

        if (fourcc != 0 && offset > 0 && length >= 0) {
            auto chunk_id = id::ChunkId(index);
            file->chunk_info_.insert_or_assign(chunk_id.value(), DirectorChunkInfo{chunk_id, fourcc, offset + 8, length, length});
        }
    }

    for (const auto& [chunk_id_value, info] : file->chunk_info_) {
        if (info.type() == format::ChunkType::DRCF || info.type() == format::ChunkType::VWCF) {
            auto chunk_reader = reader.sliceReaderAt(static_cast<std::size_t>(info.offset), static_cast<std::size_t>(info.length));
            auto config = chunks::ConfigChunk::read(file.get(), chunk_reader, info.id, 0);
            file->config_ = std::make_shared<chunks::ConfigChunk>(std::move(config));
            file->setVersion(file->config_->directorVersion());
            break;
        }
    }

    const int version = file->config_ ? file->config_->directorVersion() : imap_director_version;
    const bool capital_x = std::any_of(file->chunk_info_.begin(), file->chunk_info_.end(), [](const auto& entry) {
        return entry.second.fourcc == io::BinaryReader::fourCC("LctX");
    });
    file->setCapitalX(capital_x);

    for (const auto& [chunk_id_value, info] : file->chunk_info_) {
        try {
            auto chunk_reader = reader.sliceReaderAt(static_cast<std::size_t>(info.offset), static_cast<std::size_t>(info.length));
            auto chunk = file->parseChunkFromReader(chunk_reader, info, version, file->capital_x_);
            if (chunk) {
                file->chunks_[info.id.value()] = chunk;
                file->categorizeChunk(chunk);
                if (std::dynamic_pointer_cast<chunks::ScriptContextChunk>(chunk)) {
                    file->setCapitalX(info.fourcc == io::BinaryReader::fourCC("LctX"));
                }
            }
        } catch (const std::exception&) {
        }
    }

    return file;
}

std::shared_ptr<DirectorFile> DirectorFile::loadRIFF(io::BinaryReader& reader, io::ByteOrder endian) {
    auto file = std::make_shared<DirectorFile>(endian, false, 0, format::ChunkType::MV93);

    const auto cftc_four_cc = reader.readFourCC();
    if (io::BinaryReader::fourCCToString(cftc_four_cc) != "CFTC") {
        throw DirectorFileLoadError("Expected CFTC in RIFF file, got " + io::BinaryReader::fourCCToString(cftc_four_cc));
    }

    const int cftc_size = reader.readI32();
    const auto cftc_start = reader.position();
    if (reader.bytesLeft() < 4) {
        return file;
    }
    reader.skip(4);

    int chunk_index = 0;
    const auto cftc_end = std::min(reader.length(), cftc_start + static_cast<std::size_t>(std::max(0, cftc_size)));
    while (reader.position() < cftc_end && reader.bytesLeft() >= 16) {
        const auto tag = reader.readFourCC();
        if (tag == 0) {
            break;
        }

        const int size = reader.readI32();
        (void)reader.readI32();
        const int offset = reader.readI32();
        const auto saved_position = reader.position();
        const int resource_header_offset = offset + 12;

        if (resource_header_offset >= 0 && static_cast<std::size_t>(resource_header_offset) < reader.length()) {
            reader.seek(static_cast<std::size_t>(resource_header_offset));
            if (reader.bytesLeft() >= 1) {
                const int name_length = reader.readU8();
                int data_offset = resource_header_offset + 1 + name_length;
                if ((data_offset % 2) != 0) {
                    ++data_offset;
                }
                int data_size = size - 4 - 1 - name_length;
                if (data_size < 0) {
                    data_size = size;
                }

                if (data_offset >= 0 && data_size >= 0 &&
                    static_cast<std::size_t>(data_offset) <= reader.length()) {
                    auto chunk_id = id::ChunkId(chunk_index);
                    file->chunk_info_.insert_or_assign(chunk_id.value(),
                                                      DirectorChunkInfo{chunk_id, tag, data_offset, data_size, data_size});
                    ++chunk_index;
                }
            }
        }

        reader.seek(saved_position);
    }

    for (const auto& [chunk_id_value, info] : file->chunk_info_) {
        if (info.type() == format::ChunkType::DRCF || info.type() == format::ChunkType::VWCF) {
            try {
                auto chunk_reader = reader.sliceReaderAt(static_cast<std::size_t>(info.offset), static_cast<std::size_t>(info.length));
                auto config = chunks::ConfigChunk::read(file.get(), chunk_reader, info.id, 0);
                file->config_ = std::make_shared<chunks::ConfigChunk>(std::move(config));
                file->setVersion(file->config_->directorVersion());
                break;
            } catch (const std::exception&) {
            }
        }
    }

    const int version = file->config_ ? file->config_->directorVersion() : 0;
    const bool capital_x = std::any_of(file->chunk_info_.begin(), file->chunk_info_.end(), [](const auto& entry) {
        return entry.second.fourcc == io::BinaryReader::fourCC("LctX");
    });
    file->setCapitalX(capital_x);

    for (const auto& [chunk_id_value, info] : file->chunk_info_) {
        try {
            auto chunk_reader = reader.sliceReaderAt(static_cast<std::size_t>(info.offset), static_cast<std::size_t>(info.length));
            auto chunk = file->parseChunkFromReader(chunk_reader, info, version, file->capital_x_);
            if (chunk) {
                file->chunks_[info.id.value()] = chunk;
                file->categorizeChunk(chunk);
                if (std::dynamic_pointer_cast<chunks::ScriptContextChunk>(chunk)) {
                    file->setCapitalX(info.fourcc == io::BinaryReader::fourCC("LctX"));
                }
            }
        } catch (const std::exception&) {
        }
    }

    return file;
}

std::shared_ptr<DirectorFile> DirectorFile::loadAfterburner(io::BinaryReader& reader,
                                                            io::ByteOrder endian,
                                                            format::ChunkType movie_type) {
    auto file = std::make_shared<DirectorFile>(endian, true, 0, movie_type);
    format::AfterburnerReader afterburner_reader(reader, endian);
    afterburner_reader.parse();

    int version = afterburner_reader.directorVersion();
    file->setVersion(version);
    const auto ab_infos = afterburner_reader.chunkInfos();
    const bool capital_x = std::any_of(ab_infos.begin(), ab_infos.end(), [](const auto& info) {
        return info.four_cc == "LctX";
    });
    file->setCapitalX(capital_x);

    for (const auto& ab_info : ab_infos) {
        const auto fourcc = io::BinaryReader::fourCC(ab_info.four_cc);
        auto chunk_id = id::ChunkId(ab_info.resource_id);
        file->chunk_info_.insert_or_assign(chunk_id.value(),
                                          DirectorChunkInfo{chunk_id, fourcc, ab_info.offset, ab_info.compressed_size, ab_info.uncompressed_size});
    }

    for (const auto& ab_info : ab_infos) {
        const auto type = format::chunkTypeFromString(ab_info.four_cc);
        if (type == format::ChunkType::DRCF || type == format::ChunkType::VWCF) {
            const auto chunk_data = afterburner_reader.getChunkData(ab_info.resource_id);
            if (!chunk_data.has_value()) {
                continue;
            }

            auto chunk_reader = io::BinaryReader(chunk_data.value(), endian);
            auto chunk_id = id::ChunkId(ab_info.resource_id);
            auto config = chunks::ConfigChunk::read(file.get(), chunk_reader, chunk_id, 0);
            file->config_ = std::make_shared<chunks::ConfigChunk>(std::move(config));
            version = file->config_->directorVersion();
            file->setVersion(version);
            break;
        }
    }

    for (const auto& ab_info : ab_infos) {
        if (ab_info.resource_id == 2 && ab_info.four_cc == "ILS ") {
            continue;
        }

        const auto chunk_data = afterburner_reader.getChunkData(ab_info.resource_id);
        if (!chunk_data.has_value()) {
            continue;
        }

        auto info_it = file->chunk_info_.find(ab_info.resource_id);
        if (info_it == file->chunk_info_.end()) {
            continue;
        }

        try {
            auto chunk_reader = io::BinaryReader(chunk_data.value(), endian);
            auto chunk = file->parseChunkFromReader(chunk_reader, info_it->second, version, file->capital_x_);
            if (chunk) {
                file->chunks_[info_it->second.id.value()] = chunk;
                file->categorizeChunk(chunk);
                if (std::dynamic_pointer_cast<chunks::ScriptContextChunk>(chunk)) {
                    file->setCapitalX(ab_info.four_cc == "LctX");
                }
            }
        } catch (const std::exception&) {
        }
    }

    file->afterburner_reader_ = std::make_unique<format::AfterburnerReader>(std::move(afterburner_reader));
    return file;
}

std::shared_ptr<chunks::Chunk> DirectorFile::parseChunkFromReader(io::BinaryReader& reader,
                                                                  const DirectorChunkInfo& info,
                                                                  int version,
                                                                  bool capital_x) {
    reader.setOrder(endian_);
    switch (info.type()) {
        case format::ChunkType::DRCF:
        case format::ChunkType::VWCF:
            return std::make_shared<chunks::ConfigChunk>(chunks::ConfigChunk::read(this, reader, info.id, version));
        case format::ChunkType::KEYp:
            return std::make_shared<chunks::KeyTableChunk>(chunks::KeyTableChunk::read(this, reader, info.id, version));
        case format::ChunkType::MCsL:
            return std::make_shared<chunks::CastListChunk>(chunks::CastListChunk::read(this, reader, info.id, version));
        case format::ChunkType::CASp:
            return std::make_shared<chunks::CastChunk>(chunks::CastChunk::read(this, reader, info.id, version));
        case format::ChunkType::CASt:
            return std::make_shared<chunks::CastMemberChunk>(chunks::CastMemberChunk::read(this, reader, info.id, version));
        case format::ChunkType::Lctx:
        case format::ChunkType::LctX:
            return std::make_shared<chunks::ScriptContextChunk>(chunks::ScriptContextChunk::read(this, reader, info.id, version));
        case format::ChunkType::Lnam:
            return std::make_shared<chunks::ScriptNamesChunk>(chunks::ScriptNamesChunk::read(this, reader, info.id, version));
        case format::ChunkType::Lscr:
            return std::make_shared<chunks::ScriptChunk>(chunks::ScriptChunk::read(this, reader, info.id, version, capital_x));
        case format::ChunkType::VWSC:
        case format::ChunkType::SCVW:
            return std::make_shared<chunks::ScoreChunk>(chunks::ScoreChunk::read(this, reader, info.id, version));
        case format::ChunkType::VWLB:
            return std::make_shared<chunks::FrameLabelsChunk>(chunks::FrameLabelsChunk::read(this, reader, info.id, version));
        case format::ChunkType::BITD:
            return std::make_shared<chunks::BitmapChunk>(chunks::BitmapChunk::read(this, reader, info.id, version));
        case format::ChunkType::CLUT:
            return std::make_shared<chunks::PaletteChunk>(chunks::PaletteChunk::read(this, reader, info.id, version));
        case format::ChunkType::STXT:
            return std::make_shared<chunks::TextChunk>(chunks::TextChunk::read(this, reader, info.id, version));
        case format::ChunkType::Fmap:
            return std::make_shared<chunks::FontMapChunk>(chunks::FontMapChunk::read(this, reader, info.id));
        case format::ChunkType::snd_:
            return std::make_shared<chunks::SoundChunk>(chunks::SoundChunk::read(this, reader, info.id));
        case format::ChunkType::ediM:
            return std::make_shared<chunks::MediaChunk>(chunks::MediaChunk::read(this, reader, info.id));
        default:
            return std::make_shared<chunks::RawChunk>(this, info.id, info.type(), reader.readBytes(reader.bytesLeft()));
    }
}

std::shared_ptr<chunks::Chunk> DirectorFile::reparseChunk(id::ChunkId id) {
    if (const auto found = chunks_.find(id.value()); found != chunks_.end()) {
        return found->second;
    }

    const auto* info = getChunkInfo(id);
    if (!info) {
        return nullptr;
    }

    try {
        io::BinaryReader chunk_reader({}, endian_);
        if (afterburner_) {
            if (!afterburner_reader_) {
                return nullptr;
            }
            const auto chunk_data = afterburner_reader_->getChunkData(id.value());
            if (!chunk_data.has_value()) {
                return nullptr;
            }
            chunk_reader = io::BinaryReader(chunk_data.value(), endian_);
        } else {
            if (data_store_.empty()) {
                return nullptr;
            }
            chunk_reader = io::BinaryReader(data_store_, endian_).sliceReaderAt(static_cast<std::size_t>(info->offset),
                                                                              static_cast<std::size_t>(info->length));
        }

        const int version = config_ ? config_->directorVersion() : version_;
        auto chunk = parseChunkFromReader(chunk_reader, *info, version, capital_x_);
        if (chunk) {
            chunks_[id.value()] = chunk;
            categorizeChunk(chunk);
        }
        return chunk;
    } catch (const std::exception&) {
        return nullptr;
    }
}

void DirectorFile::categorizeChunk(const std::shared_ptr<chunks::Chunk>& chunk) {
    cast_member_lookup_.reset();
    palette_resolver_.reset();
    script_lookup_.reset();
    script_names_for_script_cache_.clear();

    if (auto config = std::dynamic_pointer_cast<chunks::ConfigChunk>(chunk)) {
        config_ = config;
        version_ = config->directorVersion();
    } else if (auto key_table = std::dynamic_pointer_cast<chunks::KeyTableChunk>(chunk)) {
        key_table_ = key_table;
    } else if (auto cast_list = std::dynamic_pointer_cast<chunks::CastListChunk>(chunk)) {
        cast_list_ = cast_list;
    } else if (auto script_context = std::dynamic_pointer_cast<chunks::ScriptContextChunk>(chunk)) {
        all_script_contexts_.push_back(script_context);
        if (!script_context_ || !script_context->entries().empty()) {
            script_context_ = script_context;
        }
    } else if (auto script_names = std::dynamic_pointer_cast<chunks::ScriptNamesChunk>(chunk)) {
        script_names_by_id_[script_names->id().value()] = script_names;
        if (!script_names->names().empty()) {
            script_names_ = script_names;
        }
    } else if (auto cast = std::dynamic_pointer_cast<chunks::CastChunk>(chunk)) {
        casts_.push_back(cast);
    } else if (auto cast_member = std::dynamic_pointer_cast<chunks::CastMemberChunk>(chunk)) {
        cast_members_.push_back(cast_member);
    } else if (auto script = std::dynamic_pointer_cast<chunks::ScriptChunk>(chunk)) {
        scripts_.push_back(script);
    } else if (auto score = std::dynamic_pointer_cast<chunks::ScoreChunk>(chunk)) {
        if (shouldReplaceSelectedScore(score_chunk_, score)) {
            score_chunk_ = score;
        }
    } else if (auto frame_labels = std::dynamic_pointer_cast<chunks::FrameLabelsChunk>(chunk)) {
        frame_labels_chunk_ = frame_labels;
    } else if (auto palette = std::dynamic_pointer_cast<chunks::PaletteChunk>(chunk)) {
        palettes_.push_back(palette);
    } else if (auto font_map = std::dynamic_pointer_cast<chunks::FontMapChunk>(chunk)) {
        font_maps_.push_back(font_map);
    }
}

lookup::CastMemberLookup& DirectorFile::castMemberLookup() {
    if (!cast_member_lookup_) {
        cast_member_lookup_ = std::make_unique<lookup::CastMemberLookup>(casts_, cast_members_, cast_list_, config_);
    }
    return *cast_member_lookup_;
}

lookup::PaletteResolver& DirectorFile::paletteResolver() {
    if (!palette_resolver_) {
        palette_resolver_ = std::make_unique<lookup::PaletteResolver>(
            casts_, cast_members_, palettes_, cast_list_, config_, key_table_, [this](id::ChunkId chunk_id) {
                return getChunk(chunk_id);
            });
    }
    return *palette_resolver_;
}

lookup::ScriptLookup& DirectorFile::scriptLookup() {
    if (!script_lookup_) {
        script_lookup_ = std::make_unique<lookup::ScriptLookup>(scripts_, all_script_contexts_, cast_members_);
    }
    return *script_lookup_;
}

void DirectorFile::setVersion(int version) {
    version_ = version;
}

void DirectorFile::setCapitalX(bool capital_x) {
    capital_x_ = capital_x;
}

} // namespace libreshockwave
