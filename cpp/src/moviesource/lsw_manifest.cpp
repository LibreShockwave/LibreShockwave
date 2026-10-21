#include "lsw_manifest.hpp"

#include "json_value.hpp"

namespace libreshockwave::moviesource {
namespace {

// Only format version 1 is understood. A different major version is rejected.
constexpr int kSupportedFormatVersion = 1;

ScriptType scriptTypeFromName(const std::string& name) {
    if (name == "score") {
        return ScriptType::Score;
    }
    if (name == "behavior") {
        return ScriptType::Behavior;
    }
    if (name == "movie" || name == "movieScript") {
        return ScriptType::MovieScript;
    }
    if (name == "parent") {
        return ScriptType::Parent;
    }
    return ScriptType::Unknown;
}

std::vector<std::string> stringArray(const JsonValue& value) {
    std::vector<std::string> result;
    for (const auto& item : value.asArray()) {
        if (item.isString()) {
            result.push_back(item.asString());
        }
    }
    return result;
}

bool parseScript(const JsonValue& json, LswScriptEntry& out, std::string& error) {
    out.name = json.stringField("name");
    out.path = json.stringField("path");
    out.type = scriptTypeFromName(json.stringField("type"));
    out.behavior_flags = json.intField("behaviorFlags", 0);
    out.cast_lib = json.intField("castLib", 0);
    out.member_num = json.intField("member", 0);
    out.context_id = json.intField("contextId", 0);
    out.chunk_id = json.intField("chunkId", 0);
    if (const JsonValue* names = json.find("names"); names != nullptr && names->isArray()) {
        out.names = stringArray(*names);
    }
    if (out.path.empty()) {
        error = "script '" + out.name + "' has no path";
        return false;
    }
    return true;
}

bool parseMember(const JsonValue& json, LswMemberEntry& out, std::string& error) {
    out.member_num = json.intField("num", 0);
    out.name = json.stringField("name");
    out.type = json.stringField("type");
    out.path = json.stringField("path");
    out.script_index = json.intField("script", -1);
    out.font_id = json.intField("fontId", 0);
    out.palette_id = json.intField("paletteId", 0);
    if (out.type.empty()) {
        error = "cast member '" + out.name + "' has no type";
        return false;
    }
    return true;
}

bool parseCast(const JsonValue& json, LswCastEntry& out, std::string& error) {
    out.cast_lib = json.intField("castLib", 1);
    out.name = json.stringField("name");
    if (const JsonValue* members = json.find("members");
        members != nullptr && members->isArray()) {
        for (const auto& member : members->asArray()) {
            LswMemberEntry entry;
            if (!parseMember(member, entry, error)) {
                return false;
            }
            out.members.push_back(std::move(entry));
        }
    }
    return true;
}

} // namespace

std::optional<LswManifest> parseLswManifest(const JsonValue& json, std::string& error) {
    if (!json.isObject()) {
        error = "manifest is not a JSON object";
        return std::nullopt;
    }
    LswManifest manifest;
    const JsonValue* version = json.find("formatVersion");
    if (version == nullptr || !version->isNumber()) {
        error = "manifest has no numeric formatVersion";
        return std::nullopt;
    }
    manifest.format_version = version->asInt(0);
    if (manifest.format_version != kSupportedFormatVersion) {
        error = "unsupported .lsw format version " + std::to_string(manifest.format_version);
        return std::nullopt;
    }

    if (const JsonValue* movie = json.find("movie"); movie != nullptr && movie->isObject()) {
        manifest.director_version = movie->intField("directorVersion", manifest.director_version);
        manifest.capital_x = movie->boolField("capitalX", manifest.capital_x);
        manifest.stage_width = movie->intField("stageWidth", manifest.stage_width);
        manifest.stage_height = movie->intField("stageHeight", manifest.stage_height);
        manifest.tempo = movie->intField("tempo", manifest.tempo);
    }

    if (const JsonValue* scripts = json.find("scripts");
        scripts != nullptr && scripts->isArray()) {
        for (const auto& script : scripts->asArray()) {
            LswScriptEntry entry;
            if (!parseScript(script, entry, error)) {
                return std::nullopt;
            }
            manifest.scripts.push_back(std::move(entry));
        }
    }
    if (const JsonValue* casts = json.find("casts"); casts != nullptr && casts->isArray()) {
        for (const auto& cast : casts->asArray()) {
            LswCastEntry entry;
            if (!parseCast(cast, entry, error)) {
                return std::nullopt;
            }
            manifest.casts.push_back(std::move(entry));
        }
    }
    if (const JsonValue* external = json.find("externalCasts");
        external != nullptr && external->isArray()) {
        manifest.external_casts = stringArray(*external);
    }
    manifest.score_path = json.stringField("score");
    manifest.config_path = json.stringField("config");
    manifest.font_map_path = json.stringField("fontMap");
    manifest.key_table_path = json.stringField("keyTable");
    return manifest;
}

} // namespace libreshockwave::moviesource
