#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/player/debug/watch_expression.hpp"

namespace libreshockwave::player::debug {

struct InstructionDisplay {
    int offset{0};
    int index{0};
    std::string opcode;
    int argument{0};
    std::string annotation;
    bool has_breakpoint{false};

    friend bool operator==(const InstructionDisplay&, const InstructionDisplay&) = default;
};

struct CallFrame {
    int script_id{0};
    std::string script_name;
    std::string handler_name;
    std::vector<lingo::Datum> arguments;
    std::optional<lingo::Datum> receiver;

    friend bool operator==(const CallFrame&, const CallFrame&) = default;
};

struct DebugSnapshot {
    int script_id{0};
    std::string script_name;
    std::string handler_name;
    int instruction_offset{0};
    int instruction_index{0};
    std::string opcode;
    int argument{0};
    std::string annotation;
    std::vector<InstructionDisplay> all_instructions;
    std::vector<lingo::Datum> stack;
    std::map<std::string, lingo::Datum> locals;
    std::map<std::string, lingo::Datum> globals;
    std::vector<lingo::Datum> arguments;
    std::optional<lingo::Datum> receiver;
    std::vector<CallFrame> call_stack;
    std::vector<WatchExpression> watch_results;

    friend bool operator==(const DebugSnapshot&, const DebugSnapshot&) = default;
};

} // namespace libreshockwave::player::debug
