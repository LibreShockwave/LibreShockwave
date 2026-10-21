#include "libreshockwave/moviesource/lsw_movie_source.hpp"

#include <algorithm>
#include <unordered_set>

#include "json_value.hpp"
#include "lsw_chunk_builder.hpp"
#include "lsw_manifest.hpp"
#include "lsw_script_loader.hpp"

namespace libreshockwave::moviesource {
namespace {

// Member chunk ids start above the script range so a synthesized cast member
// never collides with a script chunk.
constexpr int kMemberChunkIdBase = 1000000;

} // namespace

LswMovieSource::LswMovieSource() = default;

LswMovieSource::~LswMovieSource() = default;

void LswMovieSource::load(const std::vector<std::uint8_t>& data) {
    auto archive = ZipArchive::parse(data);
    if (!archive) {
        throw MovieSourceLoadError(".lsw file is not a ZIP archive");
    }
    archive_ = std::move(*archive);

    const auto manifest_bytes = archive_.extract("manifest.json");
    if (!manifest_bytes) {
        throw MovieSourceLoadError(".lsw archive has no manifest.json");
    }
    const std::string manifest_text(manifest_bytes->begin(), manifest_bytes->end());
    std::string error;
    const auto manifest_json = JsonValue::parse(manifest_text, error);
    if (!manifest_json) {
        throw MovieSourceLoadError("manifest.json: " + error);
    }
    auto manifest = parseLswManifest(*manifest_json, error);
    if (!manifest) {
        throw MovieSourceLoadError("manifest.json: " + error);
    }
    manifest_ = std::make_unique<LswManifest>(std::move(*manifest));

    loadScripts();
    loadCasts();
    loadConfig();
    loadScore();
    loadFontMap();
}

void LswMovieSource::loadScripts() {
    int next_chunk_id = 1;
    std::vector<std::string> union_names;
    std::unordered_set<std::string> seen_names;
    for (const auto& entry : manifest_->scripts) {
        const auto script_bytes = archive_.extract(entry.path);
        if (!script_bytes) {
            throw MovieSourceLoadError("script '" + entry.name + "' is missing entry " + entry.path);
        }
        const std::string script_text(script_bytes->begin(), script_bytes->end());
        const int chunk_id = entry.chunk_id > 0 ? entry.chunk_id : next_chunk_id;
        next_chunk_id = std::max(next_chunk_id, chunk_id + 1);
        std::string error;
        auto asset = compileLswScript(entry.name, script_text, entry.names, entry.type,
                                      entry.behavior_flags, manifest_->director_version,
                                      manifest_->capital_x, error);
        if (!asset) {
            throw MovieSourceLoadError(error);
        }
        asset->chunk_id = chunk_id;
        asset->context_id = entry.context_id != 0 ? entry.context_id : chunk_id;
        scripts_.push_back(asset);
        scripts_by_chunk_[chunk_id] = asset;
        auto script_chunk = buildScriptChunk(this, *asset, chunk_id);
        script_chunks_.push_back(script_chunk);
        script_chunks_by_id_[chunk_id] = script_chunk;
        script_names_.push_back(buildScriptNamesChunk(this, *asset, chunk_id));
        for (const auto& name : asset->names) {
            if (seen_names.insert(name).second) {
                union_names.push_back(name);
            }
        }
    }
    movie_names_ = std::make_shared<chunks::ScriptNamesChunk>(this, id::ChunkId(0),
                                                              std::move(union_names));
}

io::ByteOrder LswMovieSource::endian() const { return io::ByteOrder::BigEndian; }

int LswMovieSource::version() const { return manifest_->director_version; }

format::ChunkType LswMovieSource::movieType() const { return format::ChunkType::UNKNOWN; }

bool LswMovieSource::isAfterburner() const { return false; }

bool LswMovieSource::isCapitalX() const { return manifest_->capital_x; }

const std::string& LswMovieSource::basePath() const { return base_path_; }

void LswMovieSource::setBasePath(std::string base_path) { base_path_ = std::move(base_path); }

std::vector<std::shared_ptr<ScriptAsset>> LswMovieSource::scripts() { return scripts_; }

std::vector<std::shared_ptr<chunks::ScriptChunk>> LswMovieSource::scriptChunks() {
    return script_chunks_;
}

std::shared_ptr<const chunks::ScriptNamesChunk> LswMovieSource::scriptNamesForScript(
    const chunks::ScriptChunk& script) const {
    for (std::size_t index = 0; index < script_chunks_.size(); ++index) {
        if (script_chunks_[index]->id().value() == script.id().value()) {
            return script_names_[index];
        }
    }
    return nullptr;
}

std::string LswMovieSource::scriptNameForScript(const chunks::ScriptChunk& script) const {
    const auto found = scripts_by_chunk_.find(script.id().value());
    return found != scripts_by_chunk_.end() ? found->second->name : std::string();
}

std::shared_ptr<chunks::ScriptChunk> LswMovieSource::scriptChunkForCastMember(
    const std::shared_ptr<CastMemberAsset>& member) const {
    if (!member || member->script_id <= 0) {
        return nullptr;
    }
    const auto found = script_chunks_by_id_.find(member->script_id);
    return found != script_chunks_by_id_.end() ? found->second : nullptr;
}

std::shared_ptr<CastMemberAsset> LswMovieSource::castMemberAssetForChunk(
    const std::shared_ptr<chunks::CastMemberChunk>& chunk) {
    if (!chunk) {
        return nullptr;
    }
    const auto found = cast_members_by_id_.find(chunk->id().value());
    return found != cast_members_by_id_.end() ? found->second : nullptr;
}

std::shared_ptr<chunks::Chunk> LswMovieSource::chunkById(int chunk_id) {
    const auto script = script_chunks_by_id_.find(chunk_id);
    if (script != script_chunks_by_id_.end()) {
        return script->second;
    }
    const auto member = cast_member_chunks_by_id_.find(chunk_id);
    if (member != cast_member_chunks_by_id_.end()) {
        return member->second;
    }
    return nullptr;
}

std::shared_ptr<chunks::ScriptNamesChunk> LswMovieSource::scriptNames() {
    return movie_names_;
}

std::vector<std::shared_ptr<chunks::CastChunk>> LswMovieSource::castChunks() {
    std::vector<std::shared_ptr<chunks::CastChunk>> chunks;
    chunks.reserve(cast_chunks_.size());
    for (const auto& [cast_lib, chunk] : cast_chunks_) {
        (void)cast_lib;
        chunks.push_back(chunk);
    }
    return chunks;
}

std::vector<std::shared_ptr<chunks::ScriptChunk>> LswMovieSource::scriptChunksByContextId(
    int context_id) const {
    std::vector<std::shared_ptr<chunks::ScriptChunk>> chunks;
    for (const auto& script : scripts_) {
        if (script->context_id == context_id) {
            const auto found = script_chunks_by_id_.find(script->chunk_id);
            if (found != script_chunks_by_id_.end()) {
                chunks.push_back(found->second);
            }
        }
    }
    return chunks;
}

std::shared_ptr<ScriptAsset> LswMovieSource::scriptByContextId(int context_id) {
    for (const auto& script : scripts_) {
        if (script->context_id == context_id) {
            return script;
        }
    }
    return nullptr;
}

std::vector<std::shared_ptr<ScriptAsset>> LswMovieSource::scriptsByContextId(int context_id) {
    std::vector<std::shared_ptr<ScriptAsset>> result;
    for (const auto& script : scripts_) {
        if (script->context_id == context_id) {
            result.push_back(script);
        }
    }
    return result;
}

std::shared_ptr<ScriptAsset> LswMovieSource::scriptByChunkId(int chunk_id) const {
    return scriptAssetForChunkId(chunk_id);
}

std::shared_ptr<ScriptAsset> LswMovieSource::scriptAssetForChunkId(int chunk_id) const {
    const auto found = scripts_by_chunk_.find(chunk_id);
    return found != scripts_by_chunk_.end() ? found->second : nullptr;
}

std::shared_ptr<ScriptAsset> LswMovieSource::scriptForCastMember(
    const std::shared_ptr<CastMemberAsset>& member) {
    if (!member || member->script_id <= 0) {
        return nullptr;
    }
    return scriptAssetForChunkId(member->script_id);
}

std::vector<std::string> LswMovieSource::allGlobalNames() {
    std::vector<std::string> names;
    std::unordered_set<std::string> seen;
    for (const auto& script : scripts_) {
        for (const std::string& name : script->getGlobalNames()) {
            if (seen.insert(name).second) {
                names.push_back(name);
            }
        }
    }
    return names;
}

std::vector<std::string> LswMovieSource::allPropertyNames() {
    std::vector<std::string> names;
    std::unordered_set<std::string> seen;
    for (const auto& script : scripts_) {
        for (const std::string& name : script->getPropertyNames()) {
            if (seen.insert(name).second) {
                names.push_back(name);
            }
        }
    }
    return names;
}

void LswMovieSource::setJpegDecoder(JpegDecoder decoder) { jpeg_decoder_ = std::move(decoder); }

void LswMovieSource::markJpegDecodePending() { jpeg_decode_pending_ = true; }

void LswMovieSource::clearJpegDecodePending() { jpeg_decode_pending_ = false; }

bool LswMovieSource::consumeJpegDecodePending() {
    const bool pending = jpeg_decode_pending_;
    jpeg_decode_pending_ = false;
    return pending;
}

} // namespace libreshockwave::moviesource
