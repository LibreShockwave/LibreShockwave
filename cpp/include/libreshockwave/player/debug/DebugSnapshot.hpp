#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/player/debug/WatchExpression.hpp"

namespace libreshockwave::player::debug {

struct InstructionDisplay {
    int offset{0};
    int index{0};
    std::string opcode;
    int argument{0};
    std::string annotation;
    bool hasBreakpoint{false};

    friend bool operator==(const InstructionDisplay&, const InstructionDisplay&) = default;
};

struct CallFrame {
    int scriptId{0};
    std::string scriptName;
    std::string handlerName;
    std::vector<lingo::Datum> arguments;
    std::optional<lingo::Datum> receiver;

    friend bool operator==(const CallFrame&, const CallFrame&) = default;
};

struct DebugSnapshot {
    int scriptId{0};
    std::string scriptName;
    std::string handlerName;
    int instructionOffset{0};
    int instructionIndex{0};
    std::string opcode;
    int argument{0};
    std::string annotation;
    std::vector<InstructionDisplay> allInstructions;
    std::vector<lingo::Datum> stack;
    std::map<std::string, lingo::Datum> locals;
    std::map<std::string, lingo::Datum> globals;
    std::vector<lingo::Datum> arguments;
    std::optional<lingo::Datum> receiver;
    std::vector<CallFrame> callStack;
    std::vector<WatchExpression> watchResults;

    friend bool operator==(const DebugSnapshot&, const DebugSnapshot&) = default;
};

} // namespace libreshockwave::player::debug
