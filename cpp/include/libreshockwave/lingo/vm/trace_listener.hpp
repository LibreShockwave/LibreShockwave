#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/moviesource/records/script_asset.hpp"

namespace libreshockwave::lingo::vm {

using RuntimeGlobals = std::unordered_map<std::string, Datum, TransparentStringHash, std::equal_to<>>;

class TraceListener {
public:
    struct HandlerInfo {
        std::string handler_name;
        int script_id{0};
        std::string script_display_name;
        std::vector<Datum> arguments;
        Datum receiver{Datum::voidValue()};
        RuntimeGlobals globals;
        std::vector<moviesource::ScriptAsset::LiteralEntry> literals;
        int local_count{0};
        int arg_count{0};
    };

    struct InstructionInfo {
        int bytecode_index{0};
        int offset{0};
        std::string opcode;
        int argument{0};
        std::string annotation;
        int stack_size{0};
        std::vector<Datum> stack_snapshot;
        std::unordered_map<std::string, Datum> locals_snapshot;
        RuntimeGlobals globals_snapshot;
    };

    virtual ~TraceListener() = default;

    [[nodiscard]] virtual bool needsHandlerTrace() const { return true; }
    virtual void onHandlerEnter(const HandlerInfo&) {}
    virtual void onHandlerExit(const HandlerInfo&, const Datum&) {}
    virtual void onInstruction(const InstructionInfo&) {}
    [[nodiscard]] virtual bool needsInstructionTrace() const { return true; }
    /// Whether the VM should capture stack, locals, globals, and annotations
    /// for the current instruction.  Debuggers can return false for ordinary
    /// instructions and request the expensive snapshot only at a breakpoint
    /// or step location.
    [[nodiscard]] virtual bool needsFullInstructionInfo(int /*offset*/) const { return true; }
    [[nodiscard]] virtual bool needsVariableTrace() const { return true; }
    virtual void onVariableSet(std::string_view, std::string_view, const Datum&) {}
    virtual void onError(std::string_view, std::string_view) {}
    virtual void onDebugMessage(std::string_view) {}

    /// Returns true when the listener has suspended execution (e.g. debugger
    /// paused at a breakpoint).  The VM's instruction loop checks this after
    /// every traced instruction and unwinds the handler when set.  This
    /// enables cooperative (non-blocking) pause in single-threaded
    /// environments like WASM.
    [[nodiscard]] virtual bool isExecutionSuspended() const { return false; }
};

} // namespace libreshockwave::lingo::vm
