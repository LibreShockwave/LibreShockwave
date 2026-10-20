#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "libreshockwave/format/chunk_type.hpp"
#include "libreshockwave/id/ids.hpp"
#include "libreshockwave/io/binary_reader.hpp"
#include "libreshockwave/w3d_file.hpp"
#include "libreshockwave/chunks/score_chunk.hpp"
#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/cast/xmed_styled_text.hpp"

namespace libreshockwave::chunks {
class CastChunk;
class CastListChunk;
class CastMemberChunk;
class Chunk;
class ConfigChunk;
class FontMapChunk;
class FrameLabelsChunk;
class KeyTableChunk;
class PaletteChunk;
class ScoreChunk;
class ScriptChunk;
class ScriptContextChunk;
class ScriptNamesChunk;
class TextChunk;
enum class CastMemberScriptType;
enum class ScriptChunkType;
}

namespace libreshockwave::format {
class AfterburnerReader;
}

namespace libreshockwave::bitmap {
class Palette;
}

namespace libreshockwave::cast {
struct BitmapInfo;
}

namespace libreshockwave::lookup {
class CastMemberLookup;
class PaletteResolver;
class ScriptLookup;
}

namespace libreshockwave {

class DirectorFileLoadError : public std::runtime_error {
public:
    explicit DirectorFileLoadError(const std::string& message) : std::runtime_error(message) {}
};

struct DirectorChunkInfo {
    id::ChunkId id;
    std::uint32_t fourcc;
    int offset;
    int length;
    int uncompressed_length;

    [[nodiscard]] format::ChunkType type() const;
};

struct ScriptInfo {
    int script_id;
    std::string script_name;
    chunks::ScriptChunkType script_type;
    std::vector<std::string> globals;
    std::vector<std::string> properties;
    std::vector<std::string> handlers;
};

class DirectorFile {
public:
    using JpegDecoder = std::function<std::optional<bitmap::Bitmap>(const std::vector<std::uint8_t>&)>;

    DirectorFile(io::ByteOrder endian, bool afterburner, int version, format::ChunkType movie_type);
    ~DirectorFile();

    static void setJpegDecoder(JpegDecoder decoder);
    static void markJpegDecodePending();
    static void clearJpegDecodePending();
    [[nodiscard]] static bool consumeJpegDecodePending();

    [[nodiscard]] io::ByteOrder endian() const;
    [[nodiscard]] bool isAfterburner() const;
    [[nodiscard]] int version() const;
    [[nodiscard]] format::ChunkType movieType() const;
    [[nodiscard]] bool isCapitalX() const;

    [[nodiscard]] std::shared_ptr<chunks::ConfigChunk> config() const;
    [[nodiscard]] std::shared_ptr<chunks::KeyTableChunk> keyTable() const;
    [[nodiscard]] std::shared_ptr<chunks::CastListChunk> castList() const;
    [[nodiscard]] std::shared_ptr<chunks::ScriptContextChunk> scriptContext() const;
    [[nodiscard]] std::shared_ptr<chunks::ScriptNamesChunk> scriptNames() const;
    [[nodiscard]] std::shared_ptr<chunks::ScoreChunk> scoreChunk() const;
    [[nodiscard]] std::shared_ptr<chunks::FrameLabelsChunk> frameLabelsChunk() const;
    [[nodiscard]] const std::string& basePath() const;
    void setBasePath(std::string base_path);

    [[nodiscard]] const std::map<int, DirectorChunkInfo>& chunkInfo() const;
    [[nodiscard]] const std::map<int, std::shared_ptr<chunks::Chunk>>& chunks() const;
    [[nodiscard]] std::shared_ptr<chunks::Chunk> getChunk(id::ChunkId id);
    [[nodiscard]] const DirectorChunkInfo* getChunkInfo(id::ChunkId id) const;
    void releaseNonEssentialChunks();

    [[nodiscard]] const std::vector<std::shared_ptr<chunks::CastChunk>>& casts() const;
    [[nodiscard]] const std::vector<std::shared_ptr<chunks::CastMemberChunk>>& castMembers() const;
    [[nodiscard]] const std::vector<std::shared_ptr<chunks::ScriptChunk>>& scripts() const;
    [[nodiscard]] const std::vector<std::shared_ptr<chunks::PaletteChunk>>& palettes() const;
    [[nodiscard]] const std::vector<std::shared_ptr<chunks::FontMapChunk>>& fontMaps() const;
    [[nodiscard]] std::shared_ptr<chunks::CastChunk> getMappedCastChunk(int cast_lib);
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> getCastMemberByIndex(int cast_lib, int cast_member_index);
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> getCastMemberByNumber(int cast_lib, int member_number);
    [[nodiscard]] std::shared_ptr<chunks::ScriptChunk> getScriptByContextId(int script_id);
    [[nodiscard]] std::vector<std::shared_ptr<chunks::ScriptChunk>> getScriptsByContextId(int script_id);
    [[nodiscard]] std::shared_ptr<chunks::ScriptChunk> getScriptForCastMember(
        const std::shared_ptr<chunks::CastMemberChunk>& member);
    [[nodiscard]] std::shared_ptr<chunks::ScriptChunk> getScriptForCastMember(
        const std::shared_ptr<chunks::CastMemberChunk>& member,
        const std::shared_ptr<chunks::CastChunk>& cast_chunk);
    [[nodiscard]] std::optional<chunks::CastMemberScriptType> getScriptType(const std::shared_ptr<chunks::ScriptChunk>& script);
    [[nodiscard]] std::string getScriptName(const std::shared_ptr<chunks::ScriptChunk>& script);
    [[nodiscard]] std::shared_ptr<chunks::ScriptNamesChunk> getScriptNamesById(id::ChunkId id) const;
    [[nodiscard]] std::shared_ptr<chunks::ScriptNamesChunk> getScriptNamesForScript(
        const std::shared_ptr<chunks::ScriptChunk>& script);
    [[nodiscard]] std::vector<std::string> getAllGlobalNames();
    [[nodiscard]] std::vector<std::string> getAllPropertyNames();
    [[nodiscard]] std::vector<std::string> getScriptGlobals(const std::shared_ptr<chunks::ScriptChunk>& script);
    [[nodiscard]] std::vector<std::string> getScriptProperties(const std::shared_ptr<chunks::ScriptChunk>& script);
    [[nodiscard]] std::vector<ScriptInfo> getScriptInfoList();
    [[nodiscard]] std::vector<std::string> getExternalCastPaths() const;
    [[nodiscard]] bool hasExternalCasts() const;
    [[nodiscard]] bool hasScore() const;
    [[nodiscard]] std::optional<std::string> getFontNameForId(int font_id) const;
    [[nodiscard]] std::optional<cast::XmedStyledText> getXmedStyledTextForMember(
        const std::shared_ptr<chunks::CastMemberChunk>& member);
    [[nodiscard]] std::vector<DirectorChunkInfo> getLinkedChunkInfoForMember(
        const std::shared_ptr<chunks::CastMemberChunk>& member) const;
    [[nodiscard]] std::vector<DirectorChunkInfo> getLinkedChunkInfoForMember(
        const std::shared_ptr<chunks::CastMemberChunk>& member,
        std::uint32_t fourcc) const;
    [[nodiscard]] std::vector<std::shared_ptr<chunks::Chunk>> getLinkedChunksForMember(
        const std::shared_ptr<chunks::CastMemberChunk>& member);
    [[nodiscard]] std::vector<std::shared_ptr<chunks::Chunk>> getLinkedChunksForMember(
        const std::shared_ptr<chunks::CastMemberChunk>& member,
        std::uint32_t fourcc);
    [[nodiscard]] std::optional<W3DFile> getW3DFileForMember(
        const std::shared_ptr<chunks::CastMemberChunk>& member);
    [[nodiscard]] std::shared_ptr<chunks::ScoreChunk> getScoreForMember(const std::shared_ptr<chunks::CastMemberChunk>& member);
    [[nodiscard]] std::vector<std::shared_ptr<chunks::TextChunk>> getTextChunksForMember(
        const std::shared_ptr<chunks::CastMemberChunk>& member);
    [[nodiscard]] std::shared_ptr<chunks::TextChunk> getTextForMember(const std::shared_ptr<chunks::CastMemberChunk>& member);
    [[nodiscard]] int stageWidth() const;
    [[nodiscard]] int stageHeight() const;
    [[nodiscard]] int channelCount() const;
    [[nodiscard]] int tempo() const;
    [[nodiscard]] int getScoreTempo(int frame) const;
    [[nodiscard]] std::optional<chunks::ScoreChunk::PaletteChannelData> getScorePalette(int frame) const;
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolvePalette(int palette_id);
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolvePaletteExact(int palette_id);
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolvePaletteByMemberNumber(int member_number);
    [[nodiscard]] std::optional<bitmap::Bitmap> decodeBitmap(const std::shared_ptr<chunks::CastMemberChunk>& member);
    [[nodiscard]] std::optional<bitmap::Bitmap> decodeBitmap(const std::shared_ptr<chunks::CastMemberChunk>& member,
                                                             const bitmap::Palette* palette_override);

    [[nodiscard]] static std::shared_ptr<DirectorFile> load(const std::vector<std::uint8_t>& data);

private:
    [[nodiscard]] static std::shared_ptr<DirectorFile> loadRIFX(io::BinaryReader& reader,
                                                                io::ByteOrder endian,
                                                                format::ChunkType movie_type);
    [[nodiscard]] static std::shared_ptr<DirectorFile> loadRIFF(io::BinaryReader& reader, io::ByteOrder endian);
    [[nodiscard]] static std::shared_ptr<DirectorFile> loadAfterburner(io::BinaryReader& reader,
                                                                       io::ByteOrder endian,
                                                                       format::ChunkType movie_type);
    [[nodiscard]] std::shared_ptr<chunks::Chunk> parseChunkFromReader(io::BinaryReader& reader,
                                                                      const DirectorChunkInfo& info,
                                                                      int version,
                                                                      bool capital_x);
    [[nodiscard]] std::shared_ptr<chunks::Chunk> reparseChunk(id::ChunkId id);
    void categorizeChunk(const std::shared_ptr<chunks::Chunk>& chunk);
    [[nodiscard]] lookup::CastMemberLookup& castMemberLookup();
    [[nodiscard]] lookup::PaletteResolver& paletteResolver();
    [[nodiscard]] lookup::ScriptLookup& scriptLookup();
    [[nodiscard]] std::optional<bitmap::Bitmap> decodeEdiMBitmap(const cast::BitmapInfo& info,
                                                                  const std::vector<std::uint8_t>& jpeg_data,
                                                                  const std::vector<std::uint8_t>* alfa_data);
    void setVersion(int version);
    void setCapitalX(bool capital_x);

    io::ByteOrder endian_;
    bool afterburner_;
    int version_;
    format::ChunkType movie_type_;
    bool capital_x_ = false;
    std::string base_path_;
    std::vector<std::uint8_t> data_store_;
    std::unique_ptr<format::AfterburnerReader> afterburner_reader_;

    std::shared_ptr<chunks::ConfigChunk> config_;
    std::shared_ptr<chunks::KeyTableChunk> key_table_;
    std::shared_ptr<chunks::CastListChunk> cast_list_;
    std::shared_ptr<chunks::ScriptContextChunk> script_context_;
    std::vector<std::shared_ptr<chunks::ScriptContextChunk>> all_script_contexts_;
    std::shared_ptr<chunks::ScriptNamesChunk> script_names_;
    std::map<int, std::shared_ptr<chunks::ScriptNamesChunk>> script_names_by_id_;
    std::map<int, std::shared_ptr<chunks::ScriptNamesChunk>> script_names_for_script_cache_;
    std::shared_ptr<chunks::ScoreChunk> score_chunk_;
    std::shared_ptr<chunks::FrameLabelsChunk> frame_labels_chunk_;

    std::map<int, std::shared_ptr<chunks::Chunk>> chunks_;
    std::map<int, DirectorChunkInfo> chunk_info_;
    std::vector<std::shared_ptr<chunks::CastChunk>> casts_;
    std::vector<std::shared_ptr<chunks::CastMemberChunk>> cast_members_;
    std::vector<std::shared_ptr<chunks::ScriptChunk>> scripts_;
    std::vector<std::shared_ptr<chunks::PaletteChunk>> palettes_;
    std::vector<std::shared_ptr<chunks::FontMapChunk>> font_maps_;
    std::unique_ptr<lookup::CastMemberLookup> cast_member_lookup_;
    std::unique_ptr<lookup::PaletteResolver> palette_resolver_;
    std::unique_ptr<lookup::ScriptLookup> script_lookup_;
};

} // namespace libreshockwave
