#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/lingo/Datum.hpp"

namespace libreshockwave::lingo::vm {

class TraceListener {
public:
    struct HandlerInfo {
        std::string handlerName;
        int scriptId{0};
        std::string scriptDisplayName;
        std::vector<Datum> arguments;
        Datum receiver{Datum::voidValue()};
        std::unordered_map<std::string, Datum> globals;
        std::vector<chunks::ScriptChunk::LiteralEntry> literals;
        int localCount{0};
        int argCount{0};
    };

    struct InstructionInfo {
        int bytecodeIndex{0};
        int offset{0};
        std::string opcode;
        int argument{0};
        std::string annotation;
        int stackSize{0};
        std::vector<Datum> stackSnapshot;
        std::unordered_map<std::string, Datum> localsSnapshot;
        std::unordered_map<std::string, Datum> globalsSnapshot;
    };

    virtual ~TraceListener() = default;

    [[nodiscard]] virtual bool needsHandlerTrace() const { return true; }
    virtual void onHandlerEnter(const HandlerInfo&) {}
    virtual void onHandlerExit(const HandlerInfo&, const Datum&) {}
    virtual void onInstruction(const InstructionInfo&) {}
    [[nodiscard]] virtual bool needsInstructionTrace() const { return true; }
    [[nodiscard]] virtual bool needsVariableTrace() const { return true; }
    virtual void onVariableSet(std::string_view, std::string_view, const Datum&) {}
    virtual void onError(std::string_view, std::string_view) {}
    virtual void onDebugMessage(std::string_view) {}
};

} // namespace libreshockwave::lingo::vm
