#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "libreshockwave/moviesource/director_container_loader.hpp"
#include "libreshockwave/moviesource/movie_source.hpp"
#include "libreshockwave/chunks/script_chunk.hpp"
#include "libreshockwave/chunks/script_names_chunk.hpp"
#include "libreshockwave/cast/bitmap_info.hpp"
#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/bitmap/palette.hpp"

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
class PaletteChunk;
class FontMapChunk;
class TextChunk;
class SoundChunk;
class MediaChunk;
}

namespace libreshockwave::lookup {
class CastMemberLookup;
class PaletteResolver;
class ScriptLookup;
}

namespace libreshockwave::moviesource {

// The Director implementation of MovieSource. Parses a Director container
// (RIFX, RIFF, or Afterburner) and lazily converts its chunks into the
// store-owned record types.
//
// Chunks remain Director-specific parsing detail. The records are self-contained
// and are the execution source of truth for the VM.
class DirectorMovieSource : public MovieSource {
public:
    DirectorMovieSource();
    ~DirectorMovieSource() override;

    // Parse a Director container into this source. Throws MovieSourceLoadError
    // on a bad container.
    void load(const std::vector<std::uint8_t>& data);

    // Register a script chunk in this source. Used by tests and tooling that
    // build a source without parsing a container.
    void addScriptChunk(std::shared_ptr<chunks::ScriptChunk> chunk);

    // Set the script names chunk for this source. Used by tests and tooling
    // that build a source without parsing a container.
    void setScriptNames(std::shared_ptr<chunks::ScriptNamesChunk> names);

    // Metadata.
    [[nodiscard]] io::ByteOrder endian() const override;
    [[nodiscard]] int version() const override;
    [[nodiscard]] format::ChunkType movieType() const override;
    [[nodiscard]] bool isAfterburner() const override;
    [[nodiscard]] bool isCapitalX() const override;
    [[nodiscard]] const std::string& basePath() const override;
    void setBasePath(std::string base_path) override;

    // Scripts.
    [[nodiscard]] std::vector<std::shared_ptr<ScriptAsset>> scripts() override;
    [[nodiscard]] std::vector<std::shared_ptr<chunks::ScriptChunk>> scriptChunks() override;
    [[nodiscard]] std::shared_ptr<ScriptAsset> scriptByContextId(int context_id) override;
    [[nodiscard]] std::vector<std::shared_ptr<ScriptAsset>> scriptsByContextId(int context_id) override;
    [[nodiscard]] std::shared_ptr<ScriptAsset> scriptByChunkId(int chunk_id) const override;
    [[nodiscard]] std::shared_ptr<ScriptAsset> scriptForCastMember(
        const std::shared_ptr<CastMemberAsset>& member) override;
    [[nodiscard]] chunks::ScriptChunkType resolvedScriptType(const chunks::ScriptChunk& script) const;
    [[nodiscard]] std::shared_ptr<const chunks::ScriptNamesChunk> scriptNamesForScript(
        const chunks::ScriptChunk& script) const override;
    [[nodiscard]] std::string scriptNameForScript(const chunks::ScriptChunk& script) const override;
    [[nodiscard]] std::shared_ptr<chunks::ScriptChunk> scriptChunkForCastMember(
        const std::shared_ptr<CastMemberAsset>& member) const override;
    [[nodiscard]] std::shared_ptr<CastMemberAsset> castMemberAssetForChunk(
        const std::shared_ptr<chunks::CastMemberChunk>& chunk) override;
    [[nodiscard]] std::shared_ptr<chunks::Chunk> chunkById(int chunk_id) override;
    [[nodiscard]] std::shared_ptr<chunks::ScriptNamesChunk> scriptNames() override;
    [[nodiscard]] std::vector<std::shared_ptr<chunks::CastChunk>> castChunks() override;
    [[nodiscard]] std::vector<std::shared_ptr<chunks::ScriptChunk>> scriptChunksByContextId(int context_id) const override;
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> castMemberChunkForAsset(
        const std::shared_ptr<CastMemberAsset>& member) override;
    [[nodiscard]] std::shared_ptr<chunks::CastChunk> mappedCastChunk(int cast_lib) override;
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> castMemberChunkByNumber(
        int cast_lib, int member_num) override;
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> castMemberChunkByIndex(
        int cast_lib, int member_num) override;
    [[nodiscard]] std::shared_ptr<chunks::ScoreChunk> scoreChunk() const override;
    [[nodiscard]] std::shared_ptr<chunks::FrameLabelsChunk> frameLabelsChunk() const override;
    [[nodiscard]] int chunkCount() const override;
    [[nodiscard]] int paletteCount() const override;
    [[nodiscard]] std::vector<std::shared_ptr<chunks::PaletteChunk>> paletteChunks() override;
    [[nodiscard]] std::vector<std::string> allGlobalNames() override;
    [[nodiscard]] std::vector<std::string> allPropertyNames() override;

    // Casts.
    [[nodiscard]] std::shared_ptr<CastListAsset> castList() override;
    [[nodiscard]] std::vector<std::shared_ptr<CastMemberAsset>> castMembers() override;
    [[nodiscard]] std::shared_ptr<CastAsset> mappedCast(int cast_lib) override;
    [[nodiscard]] std::shared_ptr<CastMemberAsset> castMemberByNumber(
        int cast_lib, int member_number) override;
    [[nodiscard]] std::vector<LinkedAsset> linkedAssetsForMember(
        const std::shared_ptr<CastMemberAsset>& member) override;
    [[nodiscard]] std::vector<LinkedAsset> linkedAssetsForMember(
        const std::shared_ptr<CastMemberAsset>& member, std::uint32_t fourcc) override;
    [[nodiscard]] std::shared_ptr<ScoreAsset> scoreForMember(
        const std::shared_ptr<CastMemberAsset>& member) override;

    // Palettes.
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolvePalette(int palette_id) override;
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolvePaletteExact(int palette_id) override;
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolvePaletteByMemberNumber(
        int member_number) override;

    // Lazy decodes.
    [[nodiscard]] std::optional<bitmap::Bitmap> decodeBitmap(
        const std::shared_ptr<CastMemberAsset>& member) override;
    [[nodiscard]] std::optional<bitmap::Bitmap> decodeBitmap(
        const std::shared_ptr<CastMemberAsset>& member, const bitmap::Palette* palette_override) override;
    [[nodiscard]] std::optional<W3DFile> w3dForMember(
        const std::shared_ptr<CastMemberAsset>& member) override;
    [[nodiscard]] std::shared_ptr<TextAsset> textForMember(
        const std::shared_ptr<CastMemberAsset>& member) override;
    [[nodiscard]] std::vector<std::shared_ptr<chunks::TextChunk>> textChunksForMember(
        const std::shared_ptr<CastMemberAsset>& member) override;
    [[nodiscard]] std::optional<cast::XmedStyledText> xmedStyledTextForMember(
        const std::shared_ptr<CastMemberAsset>& member) override;
    [[nodiscard]] std::shared_ptr<SoundAsset> soundForMember(
        const std::shared_ptr<CastMemberAsset>& member) override;
    [[nodiscard]] std::optional<std::string> fontNameForId(int font_id) override;

    // Score and movie info.
    [[nodiscard]] std::shared_ptr<ScoreAsset> score() override;
    [[nodiscard]] int stageWidth() const override;
    [[nodiscard]] int stageHeight() const override;
    [[nodiscard]] int channelCount() const override;
    [[nodiscard]] int tempo() const override;
    [[nodiscard]] int scoreTempo(int frame) const override;
    [[nodiscard]] std::optional<ScoreAsset::PaletteChannelData> scorePalette(int frame) const override;
    [[nodiscard]] std::shared_ptr<ConfigAsset> config() const override;
    [[nodiscard]] std::shared_ptr<KeyTableAsset> keyTable() override;
    [[nodiscard]] std::shared_ptr<FrameLabelAsset> frameLabels() override;

    // JPEG decode hooks.
    void setJpegDecoder(JpegDecoder decoder) override;
    void markJpegDecodePending() override;
    void clearJpegDecodePending() override;
    [[nodiscard]] bool consumeJpegDecodePending() override;

private:
    // Chunk access.
    [[nodiscard]] std::shared_ptr<chunks::Chunk> getChunk(id::ChunkId id) const;
    [[nodiscard]] std::shared_ptr<chunks::ScriptNamesChunk> scriptNamesForScript(
        const std::shared_ptr<chunks::ScriptChunk>& script);
    [[nodiscard]] std::shared_ptr<chunks::ScriptChunk> scriptChunkByContextId(int context_id);
    [[nodiscard]] std::shared_ptr<chunks::ScriptChunk> scriptChunkForCastMember(
        const std::shared_ptr<chunks::CastMemberChunk>& member) const;
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> chunkForAsset(
        const std::shared_ptr<CastMemberAsset>& asset) const;
    // Finds the cast member that owns a script, via the key table (KEYp) or,
    // when absent, the script context (Lctx) mapping the script's chunk id to
    // a context index that is the member's script_id. Returns nullptr if none.
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> owningCastMemberForScript(
        const chunks::ScriptChunk& script) const;

    [[nodiscard]] std::optional<bitmap::Bitmap> decodeEdiMBitmap(
        const cast::BitmapInfo& info,
        const std::vector<std::uint8_t>& jpeg_data,
        const std::vector<std::uint8_t>* alfa_data);

    // Record conversion (lazy, cached by chunk id).
    [[nodiscard]] std::shared_ptr<ScriptAsset> toScriptAsset(const std::shared_ptr<chunks::ScriptChunk>& chunk) const;
    [[nodiscard]] std::shared_ptr<CastMemberAsset> toCastMemberAsset(const std::shared_ptr<chunks::CastMemberChunk>& chunk);
    [[nodiscard]] std::shared_ptr<CastAsset> toCastAsset(const std::shared_ptr<chunks::CastChunk>& chunk);
    [[nodiscard]] std::shared_ptr<CastListAsset> toCastListAsset(const std::shared_ptr<chunks::CastListChunk>& chunk);
    [[nodiscard]] std::shared_ptr<ScoreAsset> toScoreAsset(const std::shared_ptr<chunks::ScoreChunk>& chunk);
    [[nodiscard]] std::shared_ptr<ConfigAsset> toConfigAsset(const std::shared_ptr<chunks::ConfigChunk>& chunk) const;
    [[nodiscard]] std::shared_ptr<KeyTableAsset> toKeyTableAsset(const std::shared_ptr<chunks::KeyTableChunk>& chunk);
    [[nodiscard]] std::shared_ptr<FrameLabelAsset> toFrameLabelAsset(const std::shared_ptr<chunks::FrameLabelsChunk>& chunk);
    [[nodiscard]] std::shared_ptr<TextAsset> toTextAsset(const std::shared_ptr<chunks::TextChunk>& chunk);
    [[nodiscard]] std::shared_ptr<SoundAsset> toSoundAsset(const std::shared_ptr<chunks::SoundChunk>& chunk);

    mutable DirectorContainerData container_;
    std::unique_ptr<lookup::CastMemberLookup> cast_member_lookup_;
    std::unique_ptr<lookup::PaletteResolver> palette_resolver_;
    mutable std::unique_ptr<lookup::ScriptLookup> script_lookup_;

    mutable std::unordered_map<int, std::shared_ptr<ScriptAsset>> script_asset_cache_;
    std::unordered_map<int, std::shared_ptr<CastMemberAsset>> cast_member_asset_cache_;
    std::unordered_map<int, std::shared_ptr<CastAsset>> cast_asset_cache_;
    std::shared_ptr<CastListAsset> cast_list_asset_;
    std::shared_ptr<ScoreAsset> score_asset_;
    mutable std::shared_ptr<ConfigAsset> config_asset_;
    std::shared_ptr<KeyTableAsset> key_table_asset_;
    std::shared_ptr<FrameLabelAsset> frame_label_asset_;

    JpegDecoder jpeg_decoder_;
    bool jpeg_decode_pending_ = false;
};

} // namespace libreshockwave::moviesource
