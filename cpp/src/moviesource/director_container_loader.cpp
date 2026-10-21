#include "libreshockwave/moviesource/director_container_loader.hpp"

#include <algorithm>
#include <exception>
#include <utility>

#include "libreshockwave/moviesource/movie_source.hpp"
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

namespace libreshockwave::moviesource {
namespace {

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

void categorizeChunk(DirectorContainerData& container, const std::shared_ptr<chunks::Chunk>& chunk) {
    if (auto config = std::dynamic_pointer_cast<chunks::ConfigChunk>(chunk)) {
        container.config = config;
        container.version = config->directorVersion();
    } else if (auto key_table = std::dynamic_pointer_cast<chunks::KeyTableChunk>(chunk)) {
        container.key_table = key_table;
    } else if (auto cast_list = std::dynamic_pointer_cast<chunks::CastListChunk>(chunk)) {
        container.cast_list = cast_list;
    } else if (auto script_context = std::dynamic_pointer_cast<chunks::ScriptContextChunk>(chunk)) {
        container.all_script_contexts.push_back(script_context);
        if (!container.script_context || !script_context->entries().empty()) {
            container.script_context = script_context;
        }
    } else if (auto script_names = std::dynamic_pointer_cast<chunks::ScriptNamesChunk>(chunk)) {
        container.script_names_by_id[script_names->id().value()] = script_names;
        if (!script_names->names().empty()) {
            container.script_names = script_names;
        }
    } else if (auto cast = std::dynamic_pointer_cast<chunks::CastChunk>(chunk)) {
        container.casts.push_back(cast);
    } else if (auto cast_member = std::dynamic_pointer_cast<chunks::CastMemberChunk>(chunk)) {
        container.cast_members.push_back(cast_member);
    } else if (auto script = std::dynamic_pointer_cast<chunks::ScriptChunk>(chunk)) {
        container.scripts.push_back(script);
    } else if (auto score = std::dynamic_pointer_cast<chunks::ScoreChunk>(chunk)) {
        if (shouldReplaceSelectedScore(container.score_chunk, score)) {
            container.score_chunk = score;
        }
    } else if (auto frame_labels = std::dynamic_pointer_cast<chunks::FrameLabelsChunk>(chunk)) {
        container.frame_labels_chunk = frame_labels;
    } else if (auto palette = std::dynamic_pointer_cast<chunks::PaletteChunk>(chunk)) {
        container.palettes.push_back(palette);
    } else if (auto font_map = std::dynamic_pointer_cast<chunks::FontMapChunk>(chunk)) {
        container.font_maps.push_back(font_map);
    }
}

std::shared_ptr<chunks::Chunk> parseChunkFromReader(const MovieSource* owner,
                                                      DirectorContainerData& container,
                                                      io::BinaryReader& reader,
                                                      const DirectorChunkInfo& info,
                                                      int version,
                                                      bool capital_x) {
    reader.setOrder(container.endian);
    switch (format::chunkTypeFromFourCC(info.fourcc)) {
        case format::ChunkType::DRCF:
        case format::ChunkType::VWCF:
            return std::make_shared<chunks::ConfigChunk>(chunks::ConfigChunk::read(owner, reader, info.id, version));
        case format::ChunkType::KEYp:
            return std::make_shared<chunks::KeyTableChunk>(chunks::KeyTableChunk::read(owner, reader, info.id, version));
        case format::ChunkType::MCsL:
            return std::make_shared<chunks::CastListChunk>(chunks::CastListChunk::read(owner, reader, info.id, version));
        case format::ChunkType::CASp:
            // CAS* is a cast array (member-id list), not a cast list. The cast
            // list is the MCsL chunk. Parse it as a CastChunk.
            return std::make_shared<chunks::CastChunk>(chunks::CastChunk::read(owner, reader, info.id, version));
        case format::ChunkType::CASt:
            return std::make_shared<chunks::CastMemberChunk>(chunks::CastMemberChunk::read(owner, reader, info.id, version));
        case format::ChunkType::Lctx:
        case format::ChunkType::LctX:
            return std::make_shared<chunks::ScriptContextChunk>(chunks::ScriptContextChunk::read(owner, reader, info.id, version));
        case format::ChunkType::Lnam:
            return std::make_shared<chunks::ScriptNamesChunk>(chunks::ScriptNamesChunk::read(owner, reader, info.id, version));
        case format::ChunkType::Lscr:
            return std::make_shared<chunks::ScriptChunk>(chunks::ScriptChunk::read(owner, reader, info.id, version, capital_x));
        case format::ChunkType::VWSC:
        case format::ChunkType::SCVW:
            return std::make_shared<chunks::ScoreChunk>(chunks::ScoreChunk::read(owner, reader, info.id, version));
        case format::ChunkType::VWLB:
            return std::make_shared<chunks::FrameLabelsChunk>(chunks::FrameLabelsChunk::read(owner, reader, info.id, version));
        case format::ChunkType::BITD:
            return std::make_shared<chunks::BitmapChunk>(chunks::BitmapChunk::read(owner, reader, info.id, version));
        case format::ChunkType::CLUT:
            return std::make_shared<chunks::PaletteChunk>(chunks::PaletteChunk::read(owner, reader, info.id, version));
        case format::ChunkType::STXT:
            return std::make_shared<chunks::TextChunk>(chunks::TextChunk::read(owner, reader, info.id, version));
        case format::ChunkType::Fmap:
            return std::make_shared<chunks::FontMapChunk>(chunks::FontMapChunk::read(owner, reader, info.id));
        case format::ChunkType::snd_:
            return std::make_shared<chunks::SoundChunk>(chunks::SoundChunk::read(owner, reader, info.id));
        case format::ChunkType::ediM:
            return std::make_shared<chunks::MediaChunk>(chunks::MediaChunk::read(owner, reader, info.id));
        default:
            return std::make_shared<chunks::RawChunk>(owner, info.id, format::chunkTypeFromFourCC(info.fourcc), reader.readBytes(reader.bytesLeft()));
    }
}

std::shared_ptr<chunks::Chunk> loadRIFX(const MovieSource* owner, DirectorContainerData& container, io::BinaryReader& reader,
                                          format::ChunkType movie_type) {
    const auto imap_four_cc = readContainerFourCC(reader, container.endian);
    if (format::chunkTypeFromFourCC(imap_four_cc) != format::ChunkType::IMAP) {
        throw MovieSourceLoadError("Expected imap chunk, got " + io::BinaryReader::fourCCToString(imap_four_cc));
    }

    const int imap_len = reader.readI32();
    (void)reader.readI32();
    const int mmap_offset = reader.readI32();
    int imap_director_version = 0;
    if (imap_len >= 12 && reader.bytesLeft() >= 4) {
        imap_director_version = reader.readI32();
    }
    container.version = imap_director_version;

    reader.seek(static_cast<std::size_t>(mmap_offset));
    const auto mmap_four_cc = readContainerFourCC(reader, container.endian);
    if (format::chunkTypeFromFourCC(mmap_four_cc) != format::ChunkType::MMAP) {
        throw MovieSourceLoadError("Expected mmap chunk, got " + io::BinaryReader::fourCCToString(mmap_four_cc));
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
        const auto fourcc = readContainerFourCC(reader, container.endian);
        const int length = reader.readI32();
        const int offset = reader.readI32();
        (void)reader.readI16();
        reader.skip(2);
        (void)reader.readI32();

        if (fourcc != 0 && offset > 0 && length >= 0) {
            auto chunk_id = id::ChunkId(index);
            container.chunk_info.insert_or_assign(chunk_id.value(), DirectorChunkInfo{chunk_id, fourcc, offset + 8, length, length});
        }
    }

    for (const auto& [chunk_id_value, info] : container.chunk_info) {
        if (format::chunkTypeFromFourCC(info.fourcc) == format::ChunkType::DRCF ||
            format::chunkTypeFromFourCC(info.fourcc) == format::ChunkType::VWCF) {
            auto chunk_reader = reader.sliceReaderAt(static_cast<std::size_t>(info.offset), static_cast<std::size_t>(info.length));
            auto config = chunks::ConfigChunk::read(owner, chunk_reader, info.id, 0);
            container.config = std::make_shared<chunks::ConfigChunk>(std::move(config));
            container.version = container.config->directorVersion();
            break;
        }
    }

    const int version = container.config ? container.config->directorVersion() : imap_director_version;
    container.capital_x = std::any_of(container.chunk_info.begin(), container.chunk_info.end(), [](const auto& entry) {
        return entry.second.fourcc == io::BinaryReader::fourCC("LctX");
    });

    for (const auto& [chunk_id_value, info] : container.chunk_info) {
        try {
            auto chunk_reader = reader.sliceReaderAt(static_cast<std::size_t>(info.offset), static_cast<std::size_t>(info.length));
            auto chunk = parseChunkFromReader(owner, container, chunk_reader, info, version, container.capital_x);
            if (chunk) {
                container.chunks[info.id.value()] = chunk;
                categorizeChunk(container, chunk);
                if (std::dynamic_pointer_cast<chunks::ScriptContextChunk>(chunk)) {
                    container.capital_x = info.fourcc == io::BinaryReader::fourCC("LctX");
                }
            }
        } catch (const std::exception&) {
        }
    }

    return nullptr;
}

std::shared_ptr<chunks::Chunk> loadRIFF(const MovieSource* owner, DirectorContainerData& container, io::BinaryReader& reader) {
    // RIFF Director files are always MV93.
    container.movie_type = format::ChunkType::MV93;
    const auto cftc_four_cc = reader.readFourCC();
    if (io::BinaryReader::fourCCToString(cftc_four_cc) != "CFTC") {
        throw MovieSourceLoadError("Expected CFTC in RIFF file, got " + io::BinaryReader::fourCCToString(cftc_four_cc));
    }

    const int cftc_size = reader.readI32();
    const auto cftc_start = reader.position();
    if (reader.bytesLeft() < 4) {
        return nullptr;
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
                    container.chunk_info.insert_or_assign(chunk_id.value(),
                                                        DirectorChunkInfo{chunk_id, tag, data_offset, data_size, data_size});
                    ++chunk_index;
                }
            }
        }

        reader.seek(saved_position);
    }

    for (const auto& [chunk_id_value, info] : container.chunk_info) {
        if (format::chunkTypeFromFourCC(info.fourcc) == format::ChunkType::DRCF ||
            format::chunkTypeFromFourCC(info.fourcc) == format::ChunkType::VWCF) {
            try {
                auto chunk_reader = reader.sliceReaderAt(static_cast<std::size_t>(info.offset), static_cast<std::size_t>(info.length));
                auto config = chunks::ConfigChunk::read(owner, chunk_reader, info.id, 0);
                container.config = std::make_shared<chunks::ConfigChunk>(std::move(config));
                container.version = container.config->directorVersion();
                break;
            } catch (const std::exception&) {
            }
        }
    }

    const int version = container.config ? container.config->directorVersion() : 0;
    container.capital_x = std::any_of(container.chunk_info.begin(), container.chunk_info.end(), [](const auto& entry) {
        return entry.second.fourcc == io::BinaryReader::fourCC("LctX");
    });

    for (const auto& [chunk_id_value, info] : container.chunk_info) {
        try {
            auto chunk_reader = reader.sliceReaderAt(static_cast<std::size_t>(info.offset), static_cast<std::size_t>(info.length));
            auto chunk = parseChunkFromReader(owner, container, chunk_reader, info, version, container.capital_x);
            if (chunk) {
                container.chunks[info.id.value()] = chunk;
                categorizeChunk(container, chunk);
                if (std::dynamic_pointer_cast<chunks::ScriptContextChunk>(chunk)) {
                    container.capital_x = info.fourcc == io::BinaryReader::fourCC("LctX");
                }
            }
        } catch (const std::exception&) {
        }
    }

    return nullptr;
}

void loadAfterburner(const MovieSource* owner, DirectorContainerData& container, io::BinaryReader& reader,
                       format::ChunkType movie_type) {
    container.afterburner = true;
    format::AfterburnerReader afterburner_reader(reader, container.endian);
    afterburner_reader.parse();

    container.version = afterburner_reader.directorVersion();
    const auto ab_infos = afterburner_reader.chunkInfos();
    container.capital_x = std::any_of(ab_infos.begin(), ab_infos.end(), [](const auto& info) {
        return info.four_cc == "LctX";
    });

    for (const auto& ab_info : ab_infos) {
        const auto fourcc = io::BinaryReader::fourCC(ab_info.four_cc);
        auto chunk_id = id::ChunkId(ab_info.resource_id);
        container.chunk_info.insert_or_assign(chunk_id.value(),
                                            DirectorChunkInfo{chunk_id, fourcc, ab_info.offset, ab_info.compressed_size, ab_info.uncompressed_size});
    }

    for (const auto& ab_info : ab_infos) {
        const auto type = format::chunkTypeFromString(ab_info.four_cc);
        if (type == format::ChunkType::DRCF || type == format::ChunkType::VWCF) {
            const auto chunk_data = afterburner_reader.getChunkData(ab_info.resource_id);
            if (!chunk_data.has_value()) {
                continue;
            }

            auto chunk_reader = io::BinaryReader(chunk_data.value(), container.endian);
            auto chunk_id = id::ChunkId(ab_info.resource_id);
            auto config = chunks::ConfigChunk::read(owner, chunk_reader, chunk_id, 0);
            container.config = std::make_shared<chunks::ConfigChunk>(std::move(config));
            container.version = container.config->directorVersion();
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

        auto info_it = container.chunk_info.find(ab_info.resource_id);
        if (info_it == container.chunk_info.end()) {
            continue;
        }

        try {
            auto chunk_reader = io::BinaryReader(chunk_data.value(), container.endian);
            auto chunk = parseChunkFromReader(owner, container, chunk_reader, info_it->second, container.version, container.capital_x);
            if (chunk) {
                container.chunks[info_it->second.id.value()] = chunk;
                categorizeChunk(container, chunk);
                if (std::dynamic_pointer_cast<chunks::ScriptContextChunk>(chunk)) {
                    container.capital_x = ab_info.four_cc == "LctX";
                }
            }
        } catch (const std::exception&) {
        }
    }

    container.afterburner_reader = std::make_unique<format::AfterburnerReader>(std::move(afterburner_reader));
}

} // namespace

DirectorContainerData DirectorContainerLoader::load(const MovieSource* owner, const std::vector<std::uint8_t>& data) {
    DirectorContainerData container;
    io::BinaryReader reader(data, io::ByteOrder::BigEndian);
    const auto container_four_cc = reader.readFourCC();
    const auto container_type = format::chunkTypeFromFourCC(container_four_cc);

    io::ByteOrder endian = io::ByteOrder::BigEndian;
    bool is_riff = false;
    if (container_type == format::ChunkType::RIFX) {
        endian = io::ByteOrder::BigEndian;
    } else if (container_type == format::ChunkType::XFIR) {
        endian = io::ByteOrder::LittleEndian;
    } else if (container_type == format::ChunkType::RIFF || container_type == format::ChunkType::FFIR) {
        endian = io::ByteOrder::LittleEndian;
        is_riff = true;
    } else {
        throw MovieSourceLoadError("Not a supported Director RIFX/XFIR/RIFF file: " +
                                    io::BinaryReader::fourCCToString(container_four_cc));
    }

    reader.setOrder(endian);
    (void)reader.readI32();
    container.endian = endian;

    if (is_riff) {
        const auto rmmp_four_cc = reader.readFourCC();
        if (format::chunkTypeFromFourCC(rmmp_four_cc) != format::ChunkType::RMMP) {
            throw MovieSourceLoadError("Expected RMMP marker in RIFF file, got " +
                                        io::BinaryReader::fourCCToString(rmmp_four_cc));
        }
        loadRIFF(owner, container, reader);
        container.data_store = data;
        return container;
    }

    const auto movie_type = format::chunkTypeFromFourCC(reader.readI32());
    container.movie_type = movie_type;
    if (isAfterburner(movie_type)) {
        loadAfterburner(owner, container, reader, movie_type);
        container.data_store = data;
        return container;
    }

    loadRIFX(owner, container, reader, movie_type);
    container.data_store = data;
    return container;
}

std::shared_ptr<chunks::Chunk> DirectorContainerLoader::reparseChunk(const MovieSource* owner, DirectorContainerData& container,
                                                                      id::ChunkId id) {
    if (const auto found = container.chunks.find(id.value()); found != container.chunks.end()) {
        return found->second;
    }

    const auto info_it = container.chunk_info.find(id.value());
    if (info_it == container.chunk_info.end()) {
        return nullptr;
    }

    try {
        io::BinaryReader chunk_reader({}, container.endian);
        if (container.afterburner) {
            if (!container.afterburner_reader) {
                return nullptr;
            }
            const auto chunk_data = container.afterburner_reader->getChunkData(id.value());
            if (!chunk_data.has_value()) {
                return nullptr;
            }
            chunk_reader = io::BinaryReader(chunk_data.value(), container.endian);
        } else {
            if (container.data_store.empty()) {
                return nullptr;
            }
            chunk_reader = io::BinaryReader(container.data_store, container.endian)
                             .sliceReaderAt(static_cast<std::size_t>(info_it->second.offset),
                                            static_cast<std::size_t>(info_it->second.length));
        }

        const int version = container.config ? container.config->directorVersion() : container.version;
        auto chunk = parseChunkFromReader(owner, container, chunk_reader, info_it->second, version, container.capital_x);
        if (chunk) {
            container.chunks[id.value()] = chunk;
            categorizeChunk(container, chunk);
        }
        return chunk;
    } catch (const std::exception&) {
        return nullptr;
    }
}

} // namespace libreshockwave::moviesource
