#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/format/afterburner_reader.hpp"
#include "libreshockwave/format/chunk_type.hpp"
#include "libreshockwave/id/ids.hpp"
#include "libreshockwave/io/binary_reader.hpp"

namespace libreshockwave::chunks {
class Chunk;
class ConfigChunk;
class KeyTableChunk;
class CastListChunk;
class CastChunk;
class CastMemberChunk;
class ScriptContextChunk;
class ScriptNamesChunk;
class ScriptChunk;
class ScoreChunk;
class FrameLabelsChunk;
class PaletteChunk;
class FontMapChunk;
}

namespace libreshockwave::moviesource {

class MovieSource;

// Chunk metadata for a parsed Director container.
struct DirectorChunkInfo {
    id::ChunkId id;
    std::uint32_t fourcc;
    int offset;
    int length;
    int uncompressed_length;
};

// The parsed state of a Director container. Produced by DirectorContainerLoader
// and consumed by DirectorMovieSource. Holds the raw chunk store and the
// categorized chunk collections.
struct DirectorContainerData {
    io::ByteOrder endian = io::ByteOrder::BigEndian;
    bool afterburner = false;
    int version = 0;
    format::ChunkType movie_type = format::ChunkType::UNKNOWN;
    bool capital_x = false;
    std::string base_path;
    std::vector<std::uint8_t> data_store;
    std::unique_ptr<format::AfterburnerReader> afterburner_reader;

    std::shared_ptr<chunks::ConfigChunk> config;
    std::shared_ptr<chunks::KeyTableChunk> key_table;
    std::shared_ptr<chunks::CastListChunk> cast_list;
    std::shared_ptr<chunks::ScriptContextChunk> script_context;
    std::vector<std::shared_ptr<chunks::ScriptContextChunk>> all_script_contexts;
    std::shared_ptr<chunks::ScriptNamesChunk> script_names;
    std::map<int, std::shared_ptr<chunks::ScriptNamesChunk>> script_names_by_id;
    std::shared_ptr<chunks::ScoreChunk> score_chunk;
    std::shared_ptr<chunks::FrameLabelsChunk> frame_labels_chunk;

    std::map<int, std::shared_ptr<chunks::Chunk>> chunks;
    std::map<int, DirectorChunkInfo> chunk_info;
    std::vector<std::shared_ptr<chunks::CastChunk>> casts;
    std::vector<std::shared_ptr<chunks::CastMemberChunk>> cast_members;
    std::vector<std::shared_ptr<chunks::ScriptChunk>> scripts;
    std::vector<std::shared_ptr<chunks::PaletteChunk>> palettes;
    std::vector<std::shared_ptr<chunks::FontMapChunk>> font_maps;
};

// Parses a Director container (RIFX, RIFF, or Afterburner) into a
// DirectorContainerData. The owner is the DirectorMovieSource that will hold
// the data; it is passed to the chunk constructors as their back-pointer.
// Throws MovieSourceLoadError on a bad container.
class DirectorContainerLoader {
public:
    [[nodiscard]] static DirectorContainerData load(const MovieSource* owner,
                                                    const std::vector<std::uint8_t>& data);

    // Re-parse a single chunk by id from the container data. Returns nullptr if
    // the chunk cannot be recovered.
    [[nodiscard]] static std::shared_ptr<chunks::Chunk> reparseChunk(const MovieSource* owner,
                                                                    DirectorContainerData& container,
                                                                    id::ChunkId id);
};

} // namespace libreshockwave::moviesource
