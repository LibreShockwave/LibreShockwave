#include "libreshockwave/moviesource/director_record_converter.hpp"

#include <utility>

#include "libreshockwave/chunks/cast_chunk.hpp"
#include "libreshockwave/chunks/cast_list_chunk.hpp"
#include "libreshockwave/chunks/cast_member_chunk.hpp"
#include "libreshockwave/chunks/config_chunk.hpp"
#include "libreshockwave/chunks/frame_labels_chunk.hpp"
#include "libreshockwave/chunks/key_table_chunk.hpp"
#include "libreshockwave/chunks/score_chunk.hpp"
#include "libreshockwave/chunks/script_chunk.hpp"
#include "libreshockwave/chunks/script_names_chunk.hpp"
#include "libreshockwave/chunks/sound_chunk.hpp"
#include "libreshockwave/chunks/text_chunk.hpp"

namespace libreshockwave::moviesource {

ScriptAsset DirectorRecordConverter::toScriptAsset(
    const chunks::ScriptChunk& chunk,
    const chunks::ScriptNamesChunk* names,
    ScriptType resolved_type,
    const std::string& resolved_name) {
    ScriptAsset asset;
    asset.chunk_id = chunk.id().value();
    asset.context_id = chunk.id().value();
    asset.type = resolved_type;
    asset.behavior_flags = chunk.behaviorFlags();
    asset.name = resolved_name;
    if (names != nullptr) {
        asset.names = names->names();
    }
    // Property and global name ids must resolve in the name table. Director
    // leaves stale ids in empty script records; they are not executable.
    const auto valid_name_id = [&asset](int id) {
        return id >= 0 && id < static_cast<int>(asset.names.size());
    };
    asset.handlers.reserve(chunk.handlers().size());
    for (const auto& src : chunk.handlers()) {
        ScriptAsset::Handler handler;
        handler.name_id = src.name_id;
        handler.handler_vector_pos = src.handler_vector_pos;
        handler.bytecode_length = src.bytecode_length;
        handler.bytecode_offset = src.bytecode_offset;
        handler.arg_count = src.arg_count;
        handler.local_count = src.local_count;
        handler.globals_count = src.globals_count;
        handler.line_count = src.line_count;
        handler.arg_name_ids = src.arg_name_ids;
        handler.local_name_ids = src.local_name_ids;
        handler.global_name_ids = src.global_name_ids;
        handler.global_name_ids.erase(
            std::remove_if(handler.global_name_ids.begin(), handler.global_name_ids.end(),
                           [&valid_name_id](int id) { return !valid_name_id(id); }),
            handler.global_name_ids.end());
        handler.instructions.reserve(src.instructions.size());
        for (const auto& instr : src.instructions) {
            ScriptAsset::Instruction out;
            out.offset = instr.offset;
            out.opcode = instr.opcode;
            out.raw_opcode = instr.raw_opcode;
            out.argument = instr.argument;
            handler.instructions.push_back(std::move(out));
        }
        handler.bytecode_index_map = src.bytecode_index_map;
        asset.handlers.push_back(std::move(handler));
    }
    asset.literals.reserve(chunk.literals().size());
    for (const auto& src : chunk.literals()) {
        ScriptAsset::LiteralEntry literal;
        literal.type = src.type;
        literal.offset = src.offset;
        literal.value = src.value;
        literal.numeric_value = src.numeric_value;
        asset.literals.push_back(std::move(literal));
    }
    for (const auto& property : chunk.properties()) {
        if (valid_name_id(property.name_id)) {
            asset.property_name_ids.push_back(property.name_id);
        }
    }
    for (const auto& global : chunk.globals()) {
        if (valid_name_id(global.name_id)) {
            asset.global_name_ids.push_back(global.name_id);
        }
    }
    asset.raw_bytecode = chunk.rawBytecode();
    return asset;
}

CastMemberAsset DirectorRecordConverter::toCastMemberAsset(
    const chunks::CastMemberChunk& chunk, int cast_lib, int member_num,
    const std::vector<LinkedAsset>& linked) {
    CastMemberAsset asset;
    asset.chunk_id = chunk.id().value();
    asset.cast_lib = cast_lib;
    asset.member_num = member_num;
    asset.name = chunk.name();
    asset.type = chunk.memberType();
    asset.script_id = chunk.scriptId();
    asset.reg_point_x = chunk.regPointX();
    asset.reg_point_y = chunk.regPointY();
    asset.info = chunk.info();
    asset.specific_data = chunk.specificData();
    asset.linked = linked;
    return asset;
}

CastAsset DirectorRecordConverter::toCastAsset(const chunks::CastChunk& chunk) {
    CastAsset asset;
    asset.cast_lib = chunk.id().value();
    asset.member_ids = chunk.memberIds();
    return asset;
}

CastListAsset DirectorRecordConverter::toCastListAsset(const chunks::CastListChunk& chunk) {
    CastListAsset asset;
    asset.entries.reserve(chunk.entries().size());
    for (const auto& entry : chunk.entries()) {
        CastListAsset::Entry out;
        out.name = entry.name;
        out.path = entry.path;
        out.preload_settings = entry.preload_settings;
        out.min_member = entry.min_member;
        out.max_member = entry.max_member;
        out.member_count = entry.member_count;
        out.id = entry.id;
        asset.entries.push_back(std::move(out));
    }
    return asset;
}

ScoreAsset DirectorRecordConverter::toScoreAsset(const chunks::ScoreChunk& chunk) {
    ScoreAsset asset;
    asset.header.total_length = chunk.header().total_length;
    asset.header.unk1 = chunk.header().unk1;
    asset.header.unk2 = chunk.header().unk2;
    asset.header.entry_count = chunk.header().entry_count;
    asset.header.unk3 = chunk.header().unk3;
    asset.header.entry_size_sum = chunk.header().entry_size_sum;
    asset.entries = chunk.entries();
    asset.frame_data.header.frame_count = chunk.frameData().header.frame_count;
    asset.frame_data.header.sprite_record_size = chunk.frameData().header.sprite_record_size;
    asset.frame_data.header.num_channels = chunk.frameData().header.num_channels;
    asset.frame_data.header.frames_version = chunk.frameData().header.frames_version;
    asset.frame_data.decompressed_data = chunk.frameData().decompressed_data;
    for (const auto& src : chunk.frameData().tempo_channel_data) {
        ScoreAsset::TempoChannelData out;
        out.frame_index = src.frame_index;
        out.tempo = src.tempo;
        asset.frame_data.tempo_channel_data.push_back(std::move(out));
    }
    for (const auto& src : chunk.frameData().palette_channel_data) {
        ScoreAsset::PaletteChannelData out;
        out.frame_index = src.frame_index;
        out.cast_lib = src.cast_lib;
        out.cast_member = src.cast_member;
        asset.frame_data.palette_channel_data.push_back(std::move(out));
    }
    for (const auto& entry : chunk.frameData().frame_channel_data) {
        ScoreAsset::FrameChannelEntry out;
        out.frame_index = entry.frame_index;
        out.channel_index = entry.channel_index;
        const auto& src = entry.data;
        out.data.sprite_type = src.sprite_type;
        out.data.ink = src.ink;
        out.data.trails = src.trails;
        out.data.stretch = src.stretch;
        out.data.fore_color = src.fore_color;
        out.data.back_color = src.back_color;
        out.data.cast_lib = src.cast_lib;
        out.data.cast_member = src.cast_member;
        out.data.unk1 = src.unk1;
        out.data.unk2 = src.unk2;
        out.data.pos_y = src.pos_y;
        out.data.pos_x = src.pos_x;
        out.data.height = src.height;
        out.data.width = src.width;
        out.data.color_flag = src.color_flag;
        out.data.blend_byte = src.blend_byte;
        out.data.thickness_flags = src.thickness_flags;
        out.data.fore_color_g = src.fore_color_g;
        out.data.back_color_g = src.back_color_g;
        out.data.fore_color_b = src.fore_color_b;
        out.data.back_color_b = src.back_color_b;
        asset.frame_data.frame_channel_data.push_back(std::move(out));
    }
    for (const auto& interval : chunk.frameIntervals()) {
        ScoreAsset::FrameInterval out;
        out.primary.start_frame = interval.primary.start_frame;
        out.primary.end_frame = interval.primary.end_frame;
        out.primary.unk0 = interval.primary.unk0;
        out.primary.unk1 = interval.primary.unk1;
        out.primary.channel_index = interval.primary.channel_index;
        out.primary.unk2 = interval.primary.unk2;
        if (interval.secondary.has_value()) {
            ScoreAsset::FrameIntervalSecondary secondary;
            secondary.cast_lib = interval.secondary->cast_lib;
            secondary.cast_member = interval.secondary->cast_member;
            secondary.unk0 = interval.secondary->unk0;
            out.secondary = std::move(secondary);
        }
        asset.frame_intervals.push_back(std::move(out));
    }
    return asset;
}

ConfigAsset DirectorRecordConverter::toConfigAsset(const chunks::ConfigChunk& chunk) {
    ConfigAsset asset;
    asset.director_version = chunk.directorVersion();
    asset.stage_top = chunk.stageTop();
    asset.stage_left = chunk.stageLeft();
    asset.stage_bottom = chunk.stageBottom();
    asset.stage_right = chunk.stageRight();
    asset.min_member = chunk.minMember();
    asset.max_member = chunk.maxMember();
    asset.tempo = chunk.tempo();
    asset.bg_color = chunk.bgColor();
    asset.stage_color = chunk.stageColor();
    asset.stage_color_rgb = chunk.stageColorRGB();
    asset.comment_font = chunk.commentFont();
    asset.comment_size = chunk.commentSize();
    asset.comment_style = chunk.commentStyle();
    asset.default_palette_cast_lib = chunk.defaultPaletteCastLib();
    asset.default_palette_member = chunk.defaultPaletteMember();
    asset.movie_version = chunk.movieVersion();
    asset.platform = chunk.platform();
    return asset;
}

KeyTableAsset DirectorRecordConverter::toKeyTableAsset(const chunks::KeyTableChunk& chunk) {
    KeyTableAsset asset;
    asset.entries.reserve(chunk.entries().size());
    for (const auto& entry : chunk.entries()) {
        KeyTableAsset::Entry out;
        out.section_id = entry.section_id;
        out.cast_id = entry.cast_id;
        out.fourcc = entry.fourcc;
        asset.entries.push_back(std::move(out));
    }
    asset.indexEntries();
    return asset;
}

FrameLabelAsset DirectorRecordConverter::toFrameLabelAsset(const chunks::FrameLabelsChunk& chunk) {
    FrameLabelAsset asset;
    asset.labels.reserve(chunk.labels().size());
    for (const auto& label : chunk.labels()) {
        FrameLabelAsset::FrameLabel out;
        out.frame_num = label.frame_num;
        out.label = label.label;
        asset.labels.push_back(std::move(out));
    }
    return asset;
}

TextAsset DirectorRecordConverter::toTextAsset(const chunks::TextChunk& chunk) {
    TextAsset asset;
    asset.text = chunk.text();
    asset.runs.reserve(chunk.runs().size());
    for (const auto& run : chunk.runs()) {
        TextAsset::TextRun out;
        out.start_offset = run.start_offset;
        out.end_offset = run.end_offset;
        out.font_id = run.font_id;
        out.font_size = run.font_size;
        out.font_style = run.font_style;
        out.color_r = run.color_r;
        out.color_g = run.color_g;
        out.color_b = run.color_b;
        asset.runs.push_back(std::move(out));
    }
    return asset;
}

SoundAsset DirectorRecordConverter::toSoundAsset(const chunks::SoundChunk& chunk) {
    SoundAsset asset;
    asset.sample_rate = chunk.sampleRate();
    asset.sample_count = chunk.sampleCount();
    asset.bits_per_sample = chunk.bitsPerSample();
    asset.channel_count = chunk.channelCount();
    asset.audio_data = chunk.audioData();
    asset.codec = chunk.codec();
    return asset;
}

} // namespace libreshockwave::moviesource
