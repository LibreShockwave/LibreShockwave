#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/bitmap/palette.hpp"
#include "libreshockwave/chunks/cast_member_chunk.hpp"
#include "libreshockwave/chunks/script_chunk.hpp"
#include "libreshockwave/chunks/script_names_chunk.hpp"
#include "libreshockwave/moviesource/movie_source.hpp"
#include "libreshockwave/moviesource/zip_archive.hpp"

namespace libreshockwave::chunks {
class CastChunk;
class ConfigChunk;
class FrameLabelsChunk;
class PaletteChunk;
class ScoreChunk;
class TextChunk;
}

namespace libreshockwave::moviesource {

struct LswManifest;

// The .lsw implementation of MovieSource. It reads a ZIP archive whose manifest
// describes scripts, casts, bitmaps, text, sound, fonts, and the score. Scripts
// are stored as Lingo text and compiled at load time by lingo/compiler.
//
// Loading is strict: a malformed archive, an unknown format version, or a
// script that fails to compile throws MovieSourceLoadError. Chunk views are
// synthesized from the records so the Player and debugger can use the same
// code paths as a Director movie.
class LswMovieSource : public MovieSource {
public:
    LswMovieSource();
    ~LswMovieSource() override;

    // Parse a .lsw archive into this source. Throws MovieSourceLoadError.
    void load(const std::vector<std::uint8_t>& data);

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
    [[nodiscard]] std::vector<std::shared_ptr<chunks::ScriptChunk>> scriptChunksByContextId(
        int context_id) const override;
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
    [[nodiscard]] std::shared_ptr<ScriptAsset> scriptByContextId(int context_id) override;
    [[nodiscard]] std::vector<std::shared_ptr<ScriptAsset>> scriptsByContextId(int context_id) override;
    [[nodiscard]] std::shared_ptr<ScriptAsset> scriptByChunkId(int chunk_id) const override;
    [[nodiscard]] std::shared_ptr<ScriptAsset> scriptForCastMember(
        const std::shared_ptr<CastMemberAsset>& member) override;
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
        const std::shared_ptr<CastMemberAsset>& member,
        const bitmap::Palette* palette_override) override;
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
    void loadScripts();
    void loadCasts();
    void loadConfig();
    void loadScore();
    void loadFontMap();

    [[nodiscard]] std::shared_ptr<ScriptAsset> scriptAssetForChunkId(int chunk_id) const;
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> castMemberChunkById(int chunk_id) const;

    ZipArchive archive_;
    std::unique_ptr<LswManifest> manifest_;
    std::string base_path_;
    JpegDecoder jpeg_decoder_;
    bool jpeg_decode_pending_ = false;

    std::vector<std::shared_ptr<ScriptAsset>> scripts_;
    std::vector<std::shared_ptr<chunks::ScriptChunk>> script_chunks_;
    std::vector<std::shared_ptr<chunks::ScriptNamesChunk>> script_names_;
    std::shared_ptr<chunks::ScriptNamesChunk> movie_names_;
    std::unordered_map<int, std::shared_ptr<ScriptAsset>> scripts_by_chunk_;
    std::unordered_map<int, std::shared_ptr<chunks::ScriptChunk>> script_chunks_by_id_;

    std::vector<std::shared_ptr<CastMemberAsset>> cast_members_;
    std::vector<std::shared_ptr<chunks::CastMemberChunk>> cast_member_chunks_;
    std::unordered_map<int, std::shared_ptr<CastMemberAsset>> cast_members_by_id_;
    std::unordered_map<int, std::shared_ptr<chunks::CastMemberChunk>> cast_member_chunks_by_id_;
    std::unordered_map<int, std::shared_ptr<CastAsset>> casts_;
    std::unordered_map<int, std::shared_ptr<chunks::CastChunk>> cast_chunks_;
    std::shared_ptr<CastListAsset> cast_list_;

    std::shared_ptr<ScoreAsset> score_;
    std::shared_ptr<ConfigAsset> config_asset_;
    std::shared_ptr<KeyTableAsset> key_table_;
    std::shared_ptr<FrameLabelAsset> frame_label_asset_;
    std::unordered_map<int, std::shared_ptr<const bitmap::Palette>> palettes_;
    std::unordered_map<int, std::string> font_names_;
    std::unordered_map<int, std::vector<std::uint8_t>> member_payloads_;
    std::unordered_map<int, std::string> member_text_;
};

} // namespace libreshockwave::moviesource
