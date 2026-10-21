#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/cast/xmed_styled_text.hpp"
#include "libreshockwave/format/chunk_type.hpp"
#include "libreshockwave/io/binary_reader.hpp"
#include "libreshockwave/moviesource/records/cast_asset.hpp"
#include "libreshockwave/moviesource/records/movie_asset.hpp"
#include "libreshockwave/moviesource/records/script_asset.hpp"
#include "libreshockwave/moviesource/records/score_asset.hpp"
#include "libreshockwave/w3d_file.hpp"

namespace libreshockwave::chunks {
class ScriptChunk;
class ScriptNamesChunk;
class CastMemberChunk;
class CastChunk;
class ScoreChunk;
class FrameLabelsChunk;
class Chunk;
class PaletteChunk;
class TextChunk;
enum class ScriptChunkType;
}

namespace libreshockwave::moviesource {

// Thrown when a movie source cannot be loaded (bad extension, bad container,
// or a parse failure).
class MovieSourceLoadError : public std::runtime_error {
public:
    explicit MovieSourceLoadError(const std::string& message) : std::runtime_error(message) {}
};

// A format-agnostic source of movie assets. The VM, Player, and SDK depend on
// this interface. Concrete format parsers (e.g. DirectorMovieSource) implement
// it and populate the store-owned record types lazily.
//
// One source per session. The VM and scopes hold a MovieSource* and look scripts
// up across sources for cross-file name resolution.
class MovieSource {
public:
    using JpegDecoder = std::function<std::optional<bitmap::Bitmap>(const std::vector<std::uint8_t>&)>;

    virtual ~MovieSource() = default;

    // Load a movie source from raw bytes. The file name selects the parser via
    // the format registry. An unknown or missing extension is a
    // MovieSourceLoadError.
    [[nodiscard]] static std::shared_ptr<MovieSource> load(const std::string& name,
                                                          const std::vector<std::uint8_t>& data);

    // Metadata.
    [[nodiscard]] virtual io::ByteOrder endian() const = 0;
    [[nodiscard]] virtual int version() const = 0;
    [[nodiscard]] virtual format::ChunkType movieType() const = 0;
    [[nodiscard]] virtual bool isAfterburner() const = 0;
    [[nodiscard]] virtual bool isCapitalX() const = 0;
    [[nodiscard]] virtual const std::string& basePath() const = 0;
    virtual void setBasePath(std::string base_path) = 0;

    // Scripts.
    [[nodiscard]] virtual std::vector<std::shared_ptr<ScriptAsset>> scripts() = 0;
    // Resolve the script type of a chunk. Director resolves it through the
    // owning cast member; a format that stores the type directly returns it.
    [[nodiscard]] virtual chunks::ScriptChunkType scriptTypeForChunk(
        const chunks::ScriptChunk& script) const = 0;
    [[nodiscard]] virtual std::vector<std::shared_ptr<chunks::ScriptChunk>> scriptChunks() = 0;
    [[nodiscard]] virtual std::shared_ptr<const chunks::ScriptNamesChunk> scriptNamesForScript(
        const chunks::ScriptChunk& script) const = 0;
    [[nodiscard]] virtual std::string scriptNameForScript(const chunks::ScriptChunk& script) const = 0;
    [[nodiscard]] virtual std::shared_ptr<chunks::ScriptChunk> scriptChunkForCastMember(
        const std::shared_ptr<CastMemberAsset>& member) const = 0;
    [[nodiscard]] virtual std::shared_ptr<CastMemberAsset> castMemberAssetForChunk(
        const std::shared_ptr<chunks::CastMemberChunk>& chunk) = 0;
    [[nodiscard]] virtual std::shared_ptr<chunks::Chunk> chunkById(int chunk_id) = 0;
    [[nodiscard]] virtual std::shared_ptr<chunks::ScriptNamesChunk> scriptNames() = 0;
    [[nodiscard]] virtual std::vector<std::shared_ptr<chunks::CastChunk>> castChunks() = 0;
    [[nodiscard]] virtual std::vector<std::shared_ptr<chunks::ScriptChunk>> scriptChunksByContextId(int context_id) const = 0;
    [[nodiscard]] virtual std::shared_ptr<chunks::CastMemberChunk> castMemberChunkForAsset(
        const std::shared_ptr<CastMemberAsset>& member) = 0;
    [[nodiscard]] virtual std::shared_ptr<chunks::CastChunk> mappedCastChunk(int cast_lib) = 0;
    [[nodiscard]] virtual std::shared_ptr<chunks::CastMemberChunk> castMemberChunkByNumber(
        int cast_lib, int member_num) = 0;
    [[nodiscard]] virtual std::shared_ptr<chunks::CastMemberChunk> castMemberChunkByIndex(
        int cast_lib, int member_num) = 0;
    [[nodiscard]] virtual std::shared_ptr<chunks::ScoreChunk> scoreChunk() const = 0;
    [[nodiscard]] virtual std::shared_ptr<chunks::FrameLabelsChunk> frameLabelsChunk() const = 0;
    [[nodiscard]] virtual int chunkCount() const = 0;
    [[nodiscard]] virtual int paletteCount() const = 0;
    [[nodiscard]] virtual std::vector<std::shared_ptr<chunks::PaletteChunk>> paletteChunks() = 0;
    [[nodiscard]] virtual std::shared_ptr<ScriptAsset> scriptByContextId(int context_id) = 0;
    [[nodiscard]] virtual std::vector<std::shared_ptr<ScriptAsset>> scriptsByContextId(int context_id) = 0;
    [[nodiscard]] virtual std::shared_ptr<ScriptAsset> scriptByChunkId(int chunk_id) const = 0;
    [[nodiscard]] virtual std::shared_ptr<ScriptAsset> scriptForCastMember(
        const std::shared_ptr<CastMemberAsset>& member) = 0;
    [[nodiscard]] virtual std::vector<std::string> allGlobalNames() = 0;
    [[nodiscard]] virtual std::vector<std::string> allPropertyNames() = 0;

    // Casts.
    [[nodiscard]] virtual std::shared_ptr<CastListAsset> castList() = 0;
    [[nodiscard]] virtual std::vector<std::shared_ptr<CastMemberAsset>> castMembers() = 0;
    [[nodiscard]] virtual std::shared_ptr<CastAsset> mappedCast(int cast_lib) = 0;
    [[nodiscard]] virtual std::shared_ptr<CastMemberAsset> castMemberByNumber(
        int cast_lib, int member_number) = 0;
    [[nodiscard]] virtual std::vector<LinkedAsset> linkedAssetsForMember(
        const std::shared_ptr<CastMemberAsset>& member) = 0;
    [[nodiscard]] virtual std::vector<LinkedAsset> linkedAssetsForMember(
        const std::shared_ptr<CastMemberAsset>& member, std::uint32_t fourcc) = 0;
    [[nodiscard]] virtual std::shared_ptr<ScoreAsset> scoreForMember(
        const std::shared_ptr<CastMemberAsset>& member) = 0;

    // Palettes.
    [[nodiscard]] virtual std::shared_ptr<const bitmap::Palette> resolvePalette(int palette_id) = 0;
    [[nodiscard]] virtual std::shared_ptr<const bitmap::Palette> resolvePaletteExact(int palette_id) = 0;
    [[nodiscard]] virtual std::shared_ptr<const bitmap::Palette> resolvePaletteByMemberNumber(
        int member_number) = 0;

    // Lazy decodes.
    [[nodiscard]] virtual std::optional<bitmap::Bitmap> decodeBitmap(
        const std::shared_ptr<CastMemberAsset>& member) = 0;
    [[nodiscard]] virtual std::optional<bitmap::Bitmap> decodeBitmap(
        const std::shared_ptr<CastMemberAsset>& member, const bitmap::Palette* palette_override) = 0;
    [[nodiscard]] virtual std::optional<W3DFile> w3dForMember(
        const std::shared_ptr<CastMemberAsset>& member) = 0;
    [[nodiscard]] virtual std::shared_ptr<TextAsset> textForMember(
        const std::shared_ptr<CastMemberAsset>& member) = 0;
    [[nodiscard]] virtual std::vector<std::shared_ptr<chunks::TextChunk>> textChunksForMember(
        const std::shared_ptr<CastMemberAsset>& member) = 0;
    [[nodiscard]] virtual std::optional<cast::XmedStyledText> xmedStyledTextForMember(
        const std::shared_ptr<CastMemberAsset>& member) = 0;
    [[nodiscard]] virtual std::shared_ptr<SoundAsset> soundForMember(
        const std::shared_ptr<CastMemberAsset>& member) = 0;
    [[nodiscard]] virtual std::optional<std::string> fontNameForId(int font_id) = 0;

    // Score and movie info.
    [[nodiscard]] virtual std::shared_ptr<ScoreAsset> score() = 0;
    [[nodiscard]] virtual int stageWidth() const = 0;
    [[nodiscard]] virtual int stageHeight() const = 0;
    [[nodiscard]] virtual int channelCount() const = 0;
    [[nodiscard]] virtual int tempo() const = 0;
    [[nodiscard]] virtual int scoreTempo(int frame) const = 0;
    [[nodiscard]] virtual std::optional<ScoreAsset::PaletteChannelData> scorePalette(int frame) const = 0;
    [[nodiscard]] virtual std::shared_ptr<ConfigAsset> config() const = 0;
    [[nodiscard]] virtual std::shared_ptr<KeyTableAsset> keyTable() = 0;
    [[nodiscard]] virtual std::shared_ptr<FrameLabelAsset> frameLabels() = 0;

    // JPEG decode hooks. Instance-level replacements for the former statics.
    virtual void setJpegDecoder(JpegDecoder decoder) = 0;
    virtual void markJpegDecodePending() = 0;
    virtual void clearJpegDecodePending() = 0;
    [[nodiscard]] virtual bool consumeJpegDecodePending() = 0;
};

} // namespace libreshockwave::moviesource
