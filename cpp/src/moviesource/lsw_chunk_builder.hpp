#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/chunks/cast_chunk.hpp"
#include "libreshockwave/chunks/cast_member_chunk.hpp"
#include "libreshockwave/chunks/frame_labels_chunk.hpp"
#include "libreshockwave/chunks/palette_chunk.hpp"
#include "libreshockwave/chunks/script_chunk.hpp"
#include "libreshockwave/chunks/script_names_chunk.hpp"
#include "libreshockwave/chunks/text_chunk.hpp"
#include "libreshockwave/moviesource/movie_source.hpp"
#include "libreshockwave/moviesource/records/script_asset.hpp"

namespace libreshockwave::moviesource {

// Chunk views synthesized from .lsw records. The Player and debugger use the
// same chunk-based code paths as a Director movie, so the .lsw loader builds
// these from its records. The chunks borrow no data: each owns its payload.

[[nodiscard]] std::shared_ptr<chunks::ScriptChunk> buildScriptChunk(
    const MovieSource* file, const ScriptAsset& asset, int chunk_id);

[[nodiscard]] std::shared_ptr<chunks::ScriptNamesChunk> buildScriptNamesChunk(
    const MovieSource* file, const ScriptAsset& asset, int chunk_id);

[[nodiscard]] std::shared_ptr<chunks::CastMemberChunk> buildCastMemberChunk(
    const MovieSource* file, const CastMemberAsset& asset);

[[nodiscard]] std::shared_ptr<chunks::CastChunk> buildCastChunk(
    const MovieSource* file, int cast_lib, const std::vector<int>& member_ids);

[[nodiscard]] std::shared_ptr<chunks::TextChunk> buildTextChunk(
    const MovieSource* file, int chunk_id, const std::string& text,
    const std::vector<TextAsset::TextRun>& runs);

[[nodiscard]] std::shared_ptr<chunks::PaletteChunk> buildPaletteChunk(
    const MovieSource* file, int chunk_id, const std::vector<std::uint32_t>& colors);

[[nodiscard]] std::shared_ptr<chunks::FrameLabelsChunk> buildFrameLabelsChunk(
    const MovieSource* file, int chunk_id, const FrameLabelAsset& labels);

} // namespace libreshockwave::moviesource
