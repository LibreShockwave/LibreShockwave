#include "libreshockwave/moviesource/lsw_movie_source.hpp"

#include "libreshockwave/chunks/score_chunk.hpp"

#include "json_value.hpp"
#include "lsw_chunk_builder.hpp"
#include "lsw_manifest.hpp"

namespace libreshockwave::moviesource {
namespace {

ScoreAsset::ChannelData parseChannel(const JsonValue& json) {
    ScoreAsset::ChannelData data = ScoreAsset::ChannelData::empty();
    data.sprite_type = json.intField("spriteType", 0);
    data.ink = json.intField("ink", 0);
    data.trails = json.intField("trails", 0);
    data.stretch = json.intField("stretch", 0);
    data.fore_color = json.intField("foreColor", 0);
    data.back_color = json.intField("backColor", 0);
    data.cast_lib = json.intField("castLib", 0);
    data.cast_member = json.intField("member", 0);
    data.pos_x = json.intField("locX", 0);
    data.pos_y = json.intField("locY", 0);
    data.width = json.intField("width", 0);
    data.height = json.intField("height", 0);
    data.blend_byte = json.intField("blend", 0);
    data.fore_color_g = json.intField("foreColorG", 0);
    data.back_color_g = json.intField("backColorG", 0);
    data.fore_color_b = json.intField("foreColorB", 0);
    data.back_color_b = json.intField("backColorB", 0);
    data.color_flag = json.intField("colorFlag", 0);
    return data;
}

chunks::ScoreChunk::ChannelData toChunkChannel(const ScoreAsset::ChannelData& data) {
    chunks::ScoreChunk::ChannelData out{};
    out.sprite_type = data.sprite_type;
    out.ink = data.ink;
    out.trails = data.trails;
    out.stretch = data.stretch;
    out.fore_color = data.fore_color;
    out.back_color = data.back_color;
    out.cast_lib = data.cast_lib;
    out.cast_member = data.cast_member;
    out.unk1 = data.unk1;
    out.unk2 = data.unk2;
    out.pos_y = data.pos_y;
    out.pos_x = data.pos_x;
    out.height = data.height;
    out.width = data.width;
    out.color_flag = data.color_flag;
    out.blend_byte = data.blend_byte;
    out.thickness_flags = data.thickness_flags;
    out.fore_color_g = data.fore_color_g;
    out.back_color_g = data.back_color_g;
    out.fore_color_b = data.fore_color_b;
    out.back_color_b = data.back_color_b;
    return out;
}

} // namespace

void LswMovieSource::loadConfig() {
    auto asset = std::make_shared<ConfigAsset>();
    asset->director_version = manifest_->director_version;
    asset->stage_top = 0;
    asset->stage_left = 0;
    asset->stage_right = manifest_->stage_width;
    asset->stage_bottom = manifest_->stage_height;
    asset->tempo = manifest_->tempo;
    asset->movie_version = manifest_->director_version;
    if (!manifest_->config_path.empty()) {
        const auto bytes = archive_.extract(manifest_->config_path);
        if (!bytes) {
            throw MovieSourceLoadError("config is missing entry " + manifest_->config_path);
        }
        std::string error;
        const std::string text(bytes->begin(), bytes->end());
        const auto json = JsonValue::parse(text, error);
        if (!json) {
            throw MovieSourceLoadError("config.json: " + error);
        }
        asset->stage_top = json->intField("stageTop", asset->stage_top);
        asset->stage_left = json->intField("stageLeft", asset->stage_left);
        asset->stage_right = json->intField("stageRight", asset->stage_right);
        asset->stage_bottom = json->intField("stageBottom", asset->stage_bottom);
        asset->tempo = json->intField("tempo", asset->tempo);
        asset->bg_color = json->intField("bgColor", asset->bg_color);
        asset->stage_color = json->intField("stageColor", asset->stage_color);
        asset->stage_color_rgb = json->intField("stageColorRgb", asset->stage_color_rgb);
        asset->default_palette_cast_lib = json->intField("defaultPaletteCastLib", 0);
        asset->default_palette_member = json->intField("defaultPaletteMember", 0);
    }
    config_asset_ = std::move(asset);
}

void LswMovieSource::loadScore() {
    auto asset = std::make_shared<ScoreAsset>();
    asset->frame_data.header.frame_count = 1;
    asset->frame_data.header.num_channels = 0;
    if (!manifest_->score_path.empty()) {
        const auto bytes = archive_.extract(manifest_->score_path);
        if (!bytes) {
            throw MovieSourceLoadError("score is missing entry " + manifest_->score_path);
        }
        std::string error;
        const std::string text(bytes->begin(), bytes->end());
        const auto json = JsonValue::parse(text, error);
        if (!json) {
            throw MovieSourceLoadError("score: " + error);
        }
        asset->frame_data.header.frame_count = json->intField("frameCount", 1);
        asset->frame_data.header.num_channels = json->intField("channelCount", 0);
        asset->frame_data.header.sprite_record_size = json->intField("spriteRecordSize", 0);
        asset->header.total_length = json->intField("totalLength", 0);
        asset->header.entry_count = json->intField("entryCount", 0);
        if (const JsonValue* tempo = json->find("tempoChannel");
            tempo != nullptr && tempo->isArray()) {
            for (const auto& entry : tempo->asArray()) {
                asset->frame_data.tempo_channel_data.push_back(
                    ScoreAsset::TempoChannelData{entry.intField("frame", 1),
                                                 entry.intField("tempo", 0)});
            }
        }
        if (const JsonValue* palette = json->find("paletteChannel");
            palette != nullptr && palette->isArray()) {
            for (const auto& entry : palette->asArray()) {
                asset->frame_data.palette_channel_data.push_back(
                    ScoreAsset::PaletteChannelData{entry.intField("frame", 1),
                                                   entry.intField("castLib", 0),
                                                   entry.intField("member", 0)});
            }
        }
        if (const JsonValue* frames = json->find("frames");
            frames != nullptr && frames->isArray()) {
            for (const auto& frame : frames->asArray()) {
                const int frame_index = frame.intField("frame", 1);
                if (const JsonValue* channels = frame.find("channels");
                    channels != nullptr && channels->isArray()) {
                    for (const auto& channel : channels->asArray()) {
                        ScoreAsset::FrameChannelEntry entry;
                        entry.frame_index = id::FrameIndex(frame_index);
                        entry.channel_index = id::ChannelId(channel.intField("channel", 1));
                        entry.data = parseChannel(channel);
                        asset->frame_data.frame_channel_data.push_back(std::move(entry));
                    }
                }
            }
        }
    }
    score_ = std::move(asset);
}

void LswMovieSource::loadFontMap() {
    if (manifest_->font_map_path.empty()) {
        return;
    }
    const auto bytes = archive_.extract(manifest_->font_map_path);
    if (!bytes) {
        throw MovieSourceLoadError("font map is missing entry " + manifest_->font_map_path);
    }
    std::string error;
    const std::string text(bytes->begin(), bytes->end());
    const auto json = JsonValue::parse(text, error);
    if (!json) {
        throw MovieSourceLoadError("font map: " + error);
    }
    const JsonValue* fonts = json->find("fonts");
    if (fonts == nullptr || !fonts->isArray()) {
        return;
    }
    for (const auto& entry : fonts->asArray()) {
        const int font_id = entry.intField("id", 0);
        if (font_id > 0) {
            font_names_[font_id] = entry.stringField("name");
        }
    }
}

std::shared_ptr<ScoreAsset> LswMovieSource::score() { return score_; }

int LswMovieSource::stageWidth() const {
    return config_asset_ ? config_asset_->stageWidth() : manifest_->stage_width;
}

int LswMovieSource::stageHeight() const {
    return config_asset_ ? config_asset_->stageHeight() : manifest_->stage_height;
}

int LswMovieSource::channelCount() const {
    return score_ ? score_->getChannelCount() : 0;
}

int LswMovieSource::tempo() const {
    if (config_asset_ && config_asset_->tempo > 0) {
        return config_asset_->tempo;
    }
    return manifest_->tempo;
}

int LswMovieSource::scoreTempo(int frame) const {
    const int frame_tempo = score_ ? score_->getFrameTempo(frame) : -1;
    return frame_tempo > 0 ? frame_tempo : tempo();
}

std::optional<ScoreAsset::PaletteChannelData> LswMovieSource::scorePalette(int frame) const {
    return score_ ? score_->getFramePalette(frame) : std::nullopt;
}

std::shared_ptr<ConfigAsset> LswMovieSource::config() const { return config_asset_; }

std::shared_ptr<KeyTableAsset> LswMovieSource::keyTable() { return key_table_; }

std::shared_ptr<FrameLabelAsset> LswMovieSource::frameLabels() { return frame_label_asset_; }

std::shared_ptr<chunks::ScoreChunk> LswMovieSource::scoreChunk() const {
    if (!score_) {
        return nullptr;
    }
    chunks::ScoreChunk::Header header{};
    header.total_length = score_->header.total_length;
    header.entry_count = score_->header.entry_count;
    chunks::ScoreChunk::ScoreFrameData frame_data{};
    frame_data.header.frame_count = score_->frame_data.header.frame_count;
    frame_data.header.sprite_record_size = score_->frame_data.header.sprite_record_size;
    frame_data.header.num_channels = score_->frame_data.header.num_channels;
    frame_data.header.frames_version = score_->frame_data.header.frames_version;
    frame_data.decompressed_data = score_->frame_data.decompressed_data;
    for (const auto& entry : score_->frame_data.frame_channel_data) {
        frame_data.frame_channel_data.push_back(chunks::ScoreChunk::FrameChannelEntry{
            entry.frame_index, entry.channel_index, toChunkChannel(entry.data)});
    }
    for (const auto& entry : score_->frame_data.tempo_channel_data) {
        frame_data.tempo_channel_data.push_back(
            chunks::ScoreChunk::TempoChannelData{entry.frame_index, entry.tempo});
    }
    for (const auto& entry : score_->frame_data.palette_channel_data) {
        frame_data.palette_channel_data.push_back(chunks::ScoreChunk::PaletteChannelData{
            entry.frame_index, entry.cast_lib, entry.cast_member});
    }
    return std::make_shared<chunks::ScoreChunk>(this, id::ChunkId(0), header,
                                                score_->entries, std::move(frame_data),
                                                std::vector<chunks::ScoreChunk::FrameInterval>());
}

std::shared_ptr<chunks::FrameLabelsChunk> LswMovieSource::frameLabelsChunk() const {
    if (!frame_label_asset_) {
        return nullptr;
    }
    return buildFrameLabelsChunk(this, 0, *frame_label_asset_);
}

int LswMovieSource::chunkCount() const {
    return static_cast<int>(script_chunks_.size() + cast_member_chunks_.size());
}

int LswMovieSource::paletteCount() const {
    return static_cast<int>(palettes_.size());
}

std::vector<std::shared_ptr<chunks::PaletteChunk>> LswMovieSource::paletteChunks() {
    std::vector<std::shared_ptr<chunks::PaletteChunk>> chunks;
    chunks.reserve(palettes_.size());
    for (const auto& [chunk_id, palette] : palettes_) {
        std::vector<std::uint32_t> colors = palette->colors();
        chunks.push_back(buildPaletteChunk(this, chunk_id, colors));
    }
    return chunks;
}

} // namespace libreshockwave::moviesource
