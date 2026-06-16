#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "libreshockwave/format/ChunkType.hpp"
#include "libreshockwave/id/Ids.hpp"
#include "libreshockwave/io/BinaryReader.hpp"
#include "libreshockwave/W3DFile.hpp"
#include "libreshockwave/chunks/ScoreChunk.hpp"
#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/cast/XmedStyledText.hpp"

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
    int uncompressedLength;

    [[nodiscard]] format::ChunkType type() const;
};

struct ScriptInfo {
    int scriptId;
    std::string scriptName;
    chunks::ScriptChunkType scriptType;
    std::vector<std::string> globals;
    std::vector<std::string> properties;
    std::vector<std::string> handlers;
};

class DirectorFile {
public:
    using JpegDecoder = std::function<std::optional<bitmap::Bitmap>(const std::vector<std::uint8_t>&)>;

    DirectorFile(io::ByteOrder endian, bool afterburner, int version, format::ChunkType movieType);
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
    void setBasePath(std::string basePath);

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
    [[nodiscard]] std::shared_ptr<chunks::CastChunk> getMappedCastChunk(int castLib);
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> getCastMemberByIndex(int castLib, int castMemberIndex);
    [[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> getCastMemberByNumber(int castLib, int memberNumber);
    [[nodiscard]] std::shared_ptr<chunks::ScriptChunk> getScriptByContextId(int scriptId);
    [[nodiscard]] std::vector<std::shared_ptr<chunks::ScriptChunk>> getScriptsByContextId(int scriptId);
    [[nodiscard]] std::shared_ptr<chunks::ScriptChunk> getScriptForCastMember(
        const std::shared_ptr<chunks::CastMemberChunk>& member);
    [[nodiscard]] std::shared_ptr<chunks::ScriptChunk> getScriptForCastMember(
        const std::shared_ptr<chunks::CastMemberChunk>& member,
        const std::shared_ptr<chunks::CastChunk>& castChunk);
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
    [[nodiscard]] std::optional<std::string> getFontNameForId(int fontId) const;
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
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolvePalette(int paletteId);
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolvePaletteExact(int paletteId);
    [[nodiscard]] std::shared_ptr<const bitmap::Palette> resolvePaletteByMemberNumber(int memberNumber);
    [[nodiscard]] std::optional<bitmap::Bitmap> decodeBitmap(const std::shared_ptr<chunks::CastMemberChunk>& member);
    [[nodiscard]] std::optional<bitmap::Bitmap> decodeBitmap(const std::shared_ptr<chunks::CastMemberChunk>& member,
                                                             const bitmap::Palette* paletteOverride);

    [[nodiscard]] static std::shared_ptr<DirectorFile> load(const std::vector<std::uint8_t>& data);

private:
    [[nodiscard]] static std::shared_ptr<DirectorFile> loadRIFX(io::BinaryReader& reader,
                                                                io::ByteOrder endian,
                                                                format::ChunkType movieType);
    [[nodiscard]] static std::shared_ptr<DirectorFile> loadRIFF(io::BinaryReader& reader, io::ByteOrder endian);
    [[nodiscard]] static std::shared_ptr<DirectorFile> loadAfterburner(io::BinaryReader& reader,
                                                                       io::ByteOrder endian,
                                                                       format::ChunkType movieType);
    [[nodiscard]] std::shared_ptr<chunks::Chunk> parseChunkFromReader(io::BinaryReader& reader,
                                                                      const DirectorChunkInfo& info,
                                                                      int version,
                                                                      bool capitalX);
    [[nodiscard]] std::shared_ptr<chunks::Chunk> reparseChunk(id::ChunkId id);
    void categorizeChunk(const std::shared_ptr<chunks::Chunk>& chunk);
    [[nodiscard]] lookup::CastMemberLookup& castMemberLookup();
    [[nodiscard]] lookup::PaletteResolver& paletteResolver();
    [[nodiscard]] lookup::ScriptLookup& scriptLookup();
    [[nodiscard]] std::optional<bitmap::Bitmap> decodeEdiMBitmap(const cast::BitmapInfo& info,
                                                                  const std::vector<std::uint8_t>& jpegData,
                                                                  const std::vector<std::uint8_t>* alfaData);
    void setVersion(int version);
    void setCapitalX(bool capitalX);

    io::ByteOrder endian_;
    bool afterburner_;
    int version_;
    format::ChunkType movieType_;
    bool capitalX_ = false;
    std::string basePath_;
    std::vector<std::uint8_t> dataStore_;
    std::unique_ptr<format::AfterburnerReader> afterburnerReader_;

    std::shared_ptr<chunks::ConfigChunk> config_;
    std::shared_ptr<chunks::KeyTableChunk> keyTable_;
    std::shared_ptr<chunks::CastListChunk> castList_;
    std::shared_ptr<chunks::ScriptContextChunk> scriptContext_;
    std::vector<std::shared_ptr<chunks::ScriptContextChunk>> allScriptContexts_;
    std::shared_ptr<chunks::ScriptNamesChunk> scriptNames_;
    std::map<int, std::shared_ptr<chunks::ScriptNamesChunk>> scriptNamesById_;
    std::map<int, std::shared_ptr<chunks::ScriptNamesChunk>> scriptNamesForScriptCache_;
    std::shared_ptr<chunks::ScoreChunk> scoreChunk_;
    std::shared_ptr<chunks::FrameLabelsChunk> frameLabelsChunk_;

    std::map<int, std::shared_ptr<chunks::Chunk>> chunks_;
    std::map<int, DirectorChunkInfo> chunkInfo_;
    std::vector<std::shared_ptr<chunks::CastChunk>> casts_;
    std::vector<std::shared_ptr<chunks::CastMemberChunk>> castMembers_;
    std::vector<std::shared_ptr<chunks::ScriptChunk>> scripts_;
    std::vector<std::shared_ptr<chunks::PaletteChunk>> palettes_;
    std::vector<std::shared_ptr<chunks::FontMapChunk>> fontMaps_;
    std::unique_ptr<lookup::CastMemberLookup> castMemberLookup_;
    std::unique_ptr<lookup::PaletteResolver> paletteResolver_;
    std::unique_ptr<lookup::ScriptLookup> scriptLookup_;
};

} // namespace libreshockwave
