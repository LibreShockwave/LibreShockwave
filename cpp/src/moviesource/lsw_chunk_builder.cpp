#include "lsw_chunk_builder.hpp"

namespace libreshockwave::moviesource {
namespace {

chunks::ScriptChunkType toChunkScriptType(ScriptType type) {
    switch (type) {
        case ScriptType::Score: return chunks::ScriptChunkType::Score;
        case ScriptType::Behavior: return chunks::ScriptChunkType::Behavior;
        case ScriptType::MovieScript: return chunks::ScriptChunkType::MovieScript;
        case ScriptType::Parent: return chunks::ScriptChunkType::Parent;
        default: return chunks::ScriptChunkType::Unknown;
    }
}

} // namespace

std::shared_ptr<chunks::ScriptChunk> buildScriptChunk(const MovieSource* file,
                                                      const ScriptAsset& asset,
                                                      int chunk_id) {
    std::vector<chunks::ScriptChunk::Handler> handlers;
    handlers.reserve(asset.handlers.size());
    for (const auto& source : asset.handlers) {
        chunks::ScriptChunk::Handler handler;
        handler.name_id = source.name_id;
        handler.handler_vector_pos = source.handler_vector_pos;
        handler.bytecode_length = source.bytecode_length;
        handler.bytecode_offset = source.bytecode_offset;
        handler.arg_count = source.arg_count;
        handler.local_count = source.local_count;
        handler.globals_count = source.globals_count;
        handler.line_count = source.line_count;
        handler.arg_name_ids = source.arg_name_ids;
        handler.local_name_ids = source.local_name_ids;
        handler.global_name_ids = source.global_name_ids;
        handler.bytecode_index_map = source.bytecode_index_map;
        handler.instructions.reserve(source.instructions.size());
        for (const auto& instruction : source.instructions) {
            handler.instructions.push_back(chunks::ScriptChunk::Instruction{
                instruction.offset, instruction.opcode, instruction.raw_opcode,
                instruction.argument});
        }
        handlers.push_back(std::move(handler));
    }

    std::vector<chunks::ScriptChunk::LiteralEntry> literals;
    literals.reserve(asset.literals.size());
    for (const auto& source : asset.literals) {
        literals.push_back(chunks::ScriptChunk::LiteralEntry{
            source.type, source.offset, source.value, source.numeric_value});
    }

    std::vector<chunks::ScriptChunk::PropertyEntry> properties;
    properties.reserve(asset.property_name_ids.size());
    for (const int name_id : asset.property_name_ids) {
        properties.push_back(chunks::ScriptChunk::PropertyEntry{name_id});
    }
    std::vector<chunks::ScriptChunk::GlobalEntry> globals;
    globals.reserve(asset.global_name_ids.size());
    for (const int name_id : asset.global_name_ids) {
        globals.push_back(chunks::ScriptChunk::GlobalEntry{name_id});
    }

    return std::make_shared<chunks::ScriptChunk>(file, id::ChunkId(chunk_id),
                                                 toChunkScriptType(asset.type),
                                                 asset.behavior_flags, std::move(handlers),
                                                 std::move(literals), std::move(properties),
                                                 std::move(globals), asset.raw_bytecode);
}

std::shared_ptr<chunks::ScriptNamesChunk> buildScriptNamesChunk(const MovieSource* file,
                                                                const ScriptAsset& asset,
                                                                int chunk_id) {
    return std::make_shared<chunks::ScriptNamesChunk>(file, id::ChunkId(chunk_id), asset.names);
}

std::shared_ptr<chunks::CastMemberChunk> buildCastMemberChunk(const MovieSource* file,
                                                              const CastMemberAsset& asset) {
    return std::make_shared<chunks::CastMemberChunk>(
        file, id::ChunkId(asset.chunk_id), asset.type,
        static_cast<int>(asset.info.size()), static_cast<int>(asset.specific_data.size()),
        asset.info, asset.specific_data, asset.name, asset.script_id, asset.reg_point_x,
        asset.reg_point_y);
}

std::shared_ptr<chunks::CastChunk> buildCastChunk(const MovieSource* file, int cast_lib,
                                                  const std::vector<int>& member_ids) {
    return std::make_shared<chunks::CastChunk>(file, id::ChunkId(cast_lib), member_ids);
}

std::shared_ptr<chunks::TextChunk> buildTextChunk(const MovieSource* file, int chunk_id,
                                                  const std::string& text,
                                                  const std::vector<TextAsset::TextRun>& runs) {
    std::vector<chunks::TextChunk::TextRun> chunk_runs;
    chunk_runs.reserve(runs.size());
    for (const auto& run : runs) {
        chunks::TextChunk::TextRun out;
        out.start_offset = run.start_offset;
        out.end_offset = run.end_offset;
        out.font_id = run.font_id;
        out.font_size = run.font_size;
        out.font_style = run.font_style;
        out.color_r = run.color_r;
        out.color_g = run.color_g;
        out.color_b = run.color_b;
        chunk_runs.push_back(out);
    }
    return std::make_shared<chunks::TextChunk>(file, id::ChunkId(chunk_id), text,
                                               std::move(chunk_runs));
}

std::shared_ptr<chunks::PaletteChunk> buildPaletteChunk(const MovieSource* file, int chunk_id,
                                                        const std::vector<std::uint32_t>& colors) {
    return std::make_shared<chunks::PaletteChunk>(file, id::ChunkId(chunk_id), colors);
}

std::shared_ptr<chunks::FrameLabelsChunk> buildFrameLabelsChunk(const MovieSource* file,
                                                                int chunk_id,
                                                                const FrameLabelAsset& labels) {
    std::vector<chunks::FrameLabelsChunk::FrameLabel> chunk_labels;
    chunk_labels.reserve(labels.labels.size());
    for (const auto& label : labels.labels) {
        chunk_labels.push_back(
            chunks::FrameLabelsChunk::FrameLabel{label.frame_num, label.label});
    }
    return std::make_shared<chunks::FrameLabelsChunk>(file, id::ChunkId(chunk_id),
                                                      std::move(chunk_labels));
}

} // namespace libreshockwave::moviesource
