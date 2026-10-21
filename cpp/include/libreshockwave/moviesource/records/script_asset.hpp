#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "libreshockwave/lingo/opcode.hpp"

namespace libreshockwave::moviesource {

// Script classification. Mirrors the Director script type codes.
enum class ScriptType {
    Score = 1,
    Behavior = 2,
    MovieScript = 3,
    Parent = 7,
    Unknown = -1
};

[[nodiscard]] ScriptType scriptTypeFromCode(int code);

// A self-contained script asset. The VM executes from this type. It carries the
// full execution data (handlers, instructions, literals, name table) so that no
// format-specific chunk is needed at runtime.
//
// A format parser (e.g. DirectorMovieSource) converts its parsed script into a
// ScriptAsset. The `names` table is the resolved name table for this script,
// so name resolution is self-contained.
class ScriptAsset {
public:
    using LiteralValue = std::variant<std::monostate, std::string, int, std::vector<std::uint8_t>>;

    struct Instruction {
        int offset = 0;
        lingo::Opcode opcode = lingo::Opcode::INVALID;
        int raw_opcode = 0;
        int argument = 0;

        [[nodiscard]] std::string toString() const;
    };

    struct Handler {
        int name_id = 0;
        int handler_vector_pos = 0;
        int bytecode_length = 0;
        int bytecode_offset = 0;
        int arg_count = 0;
        int local_count = 0;
        int globals_count = 0;
        int line_count = 0;
        std::vector<int> arg_name_ids;
        std::vector<int> local_name_ids;
        std::vector<Instruction> instructions;
        std::unordered_map<int, int> bytecode_index_map;
        // Name ids of the handler's own `global` declarations. Script-level
        // globals live on the ScriptAsset, not here.
        std::vector<int> global_name_ids;

        [[nodiscard]] int getInstructionIndex(int offset) const;
    };

    struct LiteralEntry {
        int type = 0;
        int offset = 0;
        LiteralValue value;
        double numeric_value = 0.0;
    };

    // Reverse-lookup key into the owning format source (e.g. the chunk id).
    int chunk_id = 0;
    // Script context id.
    int context_id = 0;
    ScriptType type = ScriptType::Unknown;
    int behavior_flags = 0;
    // Resolved script name.
    std::string name;
    // Resolved name table for this script (index = name id).
    std::vector<std::string> names;
    std::vector<Handler> handlers;
    std::vector<LiteralEntry> literals;
    std::vector<int> property_name_ids;
    std::vector<int> global_name_ids;
    std::vector<std::uint8_t> raw_bytecode;

    [[nodiscard]] bool hasProperties() const { return !property_name_ids.empty(); }
    [[nodiscard]] bool hasGlobals() const { return !global_name_ids.empty(); }

    [[nodiscard]] std::string resolveName(int name_id) const;
    [[nodiscard]] std::string getHandlerName(const Handler& handler) const;
    [[nodiscard]] std::optional<Handler> findHandlerByNameId(int name_id) const;
    [[nodiscard]] const Handler* findHandlerByNameIdPtr(int name_id) const;
    [[nodiscard]] std::optional<Handler> findHandler(std::string_view name) const;
    [[nodiscard]] const Handler* findHandlerPtr(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> getPropertyNames() const;
    [[nodiscard]] std::vector<std::string> getGlobalNames() const;
    [[nodiscard]] std::string displayName() const;
};

} // namespace libreshockwave::moviesource
