#pragma once

#include <optional>
#include <string>
#include <vector>

#include "libreshockwave/moviesource/records/script_asset.hpp"

namespace libreshockwave::moviesource {

class JsonValue;

// One script entry from the .lsw manifest. The text lives at `path` inside the
// archive. `names` is the optional name table; when it is empty the loader
// builds one from the script text.
struct LswScriptEntry {
    std::string name;
    std::string path;
    ScriptType type = ScriptType::Unknown;
    int behavior_flags = 0;
    int cast_lib = 0;
    int member_num = 0;
    int context_id = 0;
    int chunk_id = 0;
    std::vector<std::string> names;
};

// One cast member entry. `type` selects the decoder: bitmap, text, sound,
// script, font, or palette.
struct LswMemberEntry {
    int member_num = 0;
    std::string name;
    std::string type;
    std::string path;
    int script_index = -1;
    int font_id = 0;
    int palette_id = 0;
};

// One cast library entry with its members.
struct LswCastEntry {
    int cast_lib = 0;
    std::string name;
    std::vector<LswMemberEntry> members;
};

// The parsed manifest of a .lsw archive. The loader rejects unknown major
// format versions before using any other field.
struct LswManifest {
    int format_version = 1;
    int director_version = 600;
    bool capital_x = true;
    int stage_width = 0;
    int stage_height = 0;
    int tempo = 0;
    std::vector<LswScriptEntry> scripts;
    std::vector<LswCastEntry> casts;
    std::vector<std::string> external_casts;
    std::string score_path;
    std::string config_path;
    std::string font_map_path;
    std::string key_table_path;
};

// Parse a manifest object. Returns std::nullopt and fills `error` on a bad
// format version, a missing field, or a malformed entry.
[[nodiscard]] std::optional<LswManifest> parseLswManifest(const JsonValue& json,
                                                         std::string& error);

} // namespace libreshockwave::moviesource
