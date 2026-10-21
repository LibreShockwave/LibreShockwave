#include "libreshockwave/moviesource/lsw_movie_source.hpp"

#include "libreshockwave/bitmap/png_decoder.hpp"

#include "json_value.hpp"
#include "lsw_chunk_builder.hpp"
#include "lsw_manifest.hpp"

namespace libreshockwave::moviesource {
namespace {

// Member chunk ids start above the script range so a synthesized cast member
// never collides with a script chunk.
constexpr int kMemberChunkIdBase = 1000000;

cast::MemberType memberTypeFromName(const std::string& name) {
    if (name == "bitmap") return cast::MemberType::Bitmap;
    if (name == "text" || name == "field") return cast::MemberType::Text;
    if (name == "sound") return cast::MemberType::Sound;
    if (name == "script") return cast::MemberType::Script;
    if (name == "font") return cast::MemberType::Font;
    if (name == "palette") return cast::MemberType::Palette;
    if (name == "shape") return cast::MemberType::Shape;
    if (name == "button") return cast::MemberType::Button;
    if (name == "movie") return cast::MemberType::Movie;
    if (name == "picture") return cast::MemberType::Picture;
    if (name == "richText") return cast::MemberType::RichText;
    if (name == "shockwave3d") return cast::MemberType::Shockwave3D;
    return cast::MemberType::Unknown;
}

std::vector<std::uint32_t> parsePaletteColors(const std::vector<std::uint8_t>& bytes,
                                              std::string& error) {
    const std::string text(bytes.begin(), bytes.end());
    const auto json = JsonValue::parse(text, error);
    if (!json) {
        return {};
    }
    const JsonValue* colors = json->find("colors");
    if (colors == nullptr || !colors->isArray()) {
        error = "palette has no colors array";
        return {};
    }
    std::vector<std::uint32_t> result;
    for (const auto& color : colors->asArray()) {
        if (color.isNumber()) {
            result.push_back(static_cast<std::uint32_t>(color.asInt()));
        }
    }
    return result;
}

} // namespace

void LswMovieSource::loadCasts() {
    int next_chunk_id = kMemberChunkIdBase;
    cast_list_ = std::make_shared<CastListAsset>();
    for (const auto& cast_entry : manifest_->casts) {
        auto cast = std::make_shared<CastAsset>();
        cast->cast_lib = cast_entry.cast_lib;
        CastListAsset::Entry list_entry;
        list_entry.name = cast_entry.name;
        list_entry.id = cast_entry.cast_lib;
        list_entry.min_member = 1;
        for (const auto& member_entry : cast_entry.members) {
            auto member = std::make_shared<CastMemberAsset>();
            member->chunk_id = next_chunk_id++;
            member->cast_lib = cast_entry.cast_lib;
            member->member_num = member_entry.member_num;
            member->name = member_entry.name;
            member->type = memberTypeFromName(member_entry.type);
            if (member_entry.script_index >= 0 &&
                member_entry.script_index < static_cast<int>(scripts_.size())) {
                member->type = cast::MemberType::Script;
                member->script_id = scripts_[static_cast<std::size_t>(member_entry.script_index)]->chunk_id;
            }
            if (!member_entry.path.empty()) {
                const auto bytes = archive_.extract(member_entry.path);
                if (!bytes) {
                    throw MovieSourceLoadError("cast member '" + member_entry.name +
                                               "' is missing entry " + member_entry.path);
                }
                member_payloads_[member->chunk_id] = *bytes;
                if (member->type == cast::MemberType::Text) {
                    member_text_[member->chunk_id] =
                        std::string(bytes->begin(), bytes->end());
                }
                if (member->type == cast::MemberType::Palette) {
                    std::string error;
                    const auto colors = parsePaletteColors(*bytes, error);
                    if (colors.empty()) {
                        throw MovieSourceLoadError("palette member '" + member_entry.name +
                                                   "': " + error);
                    }
                    palettes_[member->chunk_id] =
                        std::make_shared<bitmap::Palette>(colors, member_entry.name);
                }
            }
            cast->member_ids.push_back(member->chunk_id);
            cast_members_.push_back(member);
            cast_members_by_id_[member->chunk_id] = member;
            auto chunk = buildCastMemberChunk(this, *member);
            cast_member_chunks_.push_back(chunk);
            cast_member_chunks_by_id_[member->chunk_id] = chunk;
        }
        list_entry.member_count = static_cast<int>(cast->member_ids.size());
        list_entry.max_member = static_cast<int>(cast->member_ids.size());
        cast_list_->entries.push_back(std::move(list_entry));
        casts_[cast_entry.cast_lib] = cast;
        cast_chunks_[cast_entry.cast_lib] =
            buildCastChunk(this, cast_entry.cast_lib, cast->member_ids);
    }
}

std::shared_ptr<CastListAsset> LswMovieSource::castList() { return cast_list_; }

std::vector<std::shared_ptr<CastMemberAsset>> LswMovieSource::castMembers() {
    return cast_members_;
}

std::shared_ptr<CastAsset> LswMovieSource::mappedCast(int cast_lib) {
    const auto found = casts_.find(cast_lib);
    return found != casts_.end() ? found->second : nullptr;
}

std::shared_ptr<CastMemberAsset> LswMovieSource::castMemberByNumber(int cast_lib,
                                                                    int member_number) {
    for (const auto& member : cast_members_) {
        if (member->cast_lib == cast_lib && member->member_num == member_number) {
            return member;
        }
    }
    return nullptr;
}

std::shared_ptr<chunks::CastMemberChunk> LswMovieSource::castMemberChunkForAsset(
    const std::shared_ptr<CastMemberAsset>& member) {
    if (!member) {
        return nullptr;
    }
    const auto found = cast_member_chunks_by_id_.find(member->chunk_id);
    return found != cast_member_chunks_by_id_.end() ? found->second : nullptr;
}

std::shared_ptr<chunks::CastMemberChunk> LswMovieSource::castMemberChunkById(int chunk_id) const {
    const auto found = cast_member_chunks_by_id_.find(chunk_id);
    return found != cast_member_chunks_by_id_.end() ? found->second : nullptr;
}

std::shared_ptr<chunks::CastChunk> LswMovieSource::mappedCastChunk(int cast_lib) {
    const auto found = cast_chunks_.find(cast_lib);
    return found != cast_chunks_.end() ? found->second : nullptr;
}

std::shared_ptr<chunks::CastMemberChunk> LswMovieSource::castMemberChunkByNumber(
    int cast_lib, int member_num) {
    const auto member = castMemberByNumber(cast_lib, member_num);
    return member ? castMemberChunkForAsset(member) : nullptr;
}

std::shared_ptr<chunks::CastMemberChunk> LswMovieSource::castMemberChunkByIndex(
    int cast_lib, int member_num) {
    const auto cast = mappedCast(cast_lib);
    if (!cast || member_num < 1 ||
        member_num > static_cast<int>(cast->member_ids.size())) {
        return nullptr;
    }
    return castMemberChunkById(cast->member_ids[static_cast<std::size_t>(member_num - 1)]);
}

std::vector<LinkedAsset> LswMovieSource::linkedAssetsForMember(
    const std::shared_ptr<CastMemberAsset>& member) {
    return member ? member->linked : std::vector<LinkedAsset>{};
}

std::vector<LinkedAsset> LswMovieSource::linkedAssetsForMember(
    const std::shared_ptr<CastMemberAsset>& member, std::uint32_t fourcc) {
    std::vector<LinkedAsset> result;
    if (!member) {
        return result;
    }
    for (const auto& linked : member->linked) {
        if (linked.fourcc == fourcc) {
            result.push_back(linked);
        }
    }
    return result;
}

std::shared_ptr<ScoreAsset> LswMovieSource::scoreForMember(
    const std::shared_ptr<CastMemberAsset>& member) {
    (void)member;
    return nullptr;
}

std::shared_ptr<const bitmap::Palette> LswMovieSource::resolvePalette(int palette_id) {
    return resolvePaletteExact(palette_id);
}

std::shared_ptr<const bitmap::Palette> LswMovieSource::resolvePaletteExact(int palette_id) {
    const auto found = palettes_.find(palette_id);
    return found != palettes_.end() ? found->second : nullptr;
}

std::shared_ptr<const bitmap::Palette> LswMovieSource::resolvePaletteByMemberNumber(
    int member_number) {
    for (const auto& [chunk_id, palette] : palettes_) {
        const auto member = cast_members_by_id_.find(chunk_id);
        if (member != cast_members_by_id_.end() && member->second->member_num == member_number) {
            return palette;
        }
    }
    return nullptr;
}

std::optional<bitmap::Bitmap> LswMovieSource::decodeBitmap(
    const std::shared_ptr<CastMemberAsset>& member) {
    return decodeBitmap(member, nullptr);
}

std::optional<bitmap::Bitmap> LswMovieSource::decodeBitmap(
    const std::shared_ptr<CastMemberAsset>& member, const bitmap::Palette* palette_override) {
    if (!member) {
        return std::nullopt;
    }
    const auto found = member_payloads_.find(member->chunk_id);
    if (found == member_payloads_.end()) {
        return std::nullopt;
    }
    auto bitmap = bitmap::PngDecoder::decode(found->second);
    if (bitmap && palette_override != nullptr) {
        bitmap->setImagePalette(palette_override);
    }
    return bitmap;
}

std::optional<W3DFile> LswMovieSource::w3dForMember(
    const std::shared_ptr<CastMemberAsset>& member) {
    (void)member;
    return std::nullopt;
}

std::shared_ptr<TextAsset> LswMovieSource::textForMember(
    const std::shared_ptr<CastMemberAsset>& member) {
    if (!member) {
        return nullptr;
    }
    const auto found = member_text_.find(member->chunk_id);
    if (found == member_text_.end()) {
        return nullptr;
    }
    auto asset = std::make_shared<TextAsset>();
    asset->text = found->second;
    return asset;
}

std::vector<std::shared_ptr<chunks::TextChunk>> LswMovieSource::textChunksForMember(
    const std::shared_ptr<CastMemberAsset>& member) {
    std::vector<std::shared_ptr<chunks::TextChunk>> chunks;
    const auto text = textForMember(member);
    if (text) {
        chunks.push_back(buildTextChunk(this, member->chunk_id, text->text, text->runs));
    }
    return chunks;
}

std::optional<cast::XmedStyledText> LswMovieSource::xmedStyledTextForMember(
    const std::shared_ptr<CastMemberAsset>& member) {
    (void)member;
    return std::nullopt;
}

std::shared_ptr<SoundAsset> LswMovieSource::soundForMember(
    const std::shared_ptr<CastMemberAsset>& member) {
    if (!member) {
        return nullptr;
    }
    const auto found = member_payloads_.find(member->chunk_id);
    if (found == member_payloads_.end()) {
        return nullptr;
    }
    auto asset = std::make_shared<SoundAsset>();
    asset->codec = "wav";
    asset->audio_data = found->second;
    return asset;
}

std::optional<std::string> LswMovieSource::fontNameForId(int font_id) {
    const auto found = font_names_.find(font_id);
    if (found == font_names_.end()) {
        return std::nullopt;
    }
    return found->second;
}

} // namespace libreshockwave::moviesource
