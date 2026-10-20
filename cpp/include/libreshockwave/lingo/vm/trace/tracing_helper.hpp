#pragma once

#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/lingo/vm/scope.hpp"
#include "libreshockwave/lingo/vm/trace_listener.hpp"

namespace libreshockwave::chunks {
class ScriptNamesChunk;
}

namespace libreshockwave::lingo::vm::trace {

class TracingHelper {
public:
    [[nodiscard]] TraceListener::InstructionInfo buildInstructionInfo(
        const Scope& scope,
        const chunks::ScriptChunk::Instruction& instruction,
        const RuntimeGlobals& globals,
        const chunks::ScriptNamesChunk* names = nullptr,
        bool include_snapshots = true) const;

    [[nodiscard]] std::unordered_map<std::string, Datum> captureLocals(
        const Scope& scope,
        const chunks::ScriptNamesChunk* names = nullptr) const;

    [[nodiscard]] TraceListener::HandlerInfo buildHandlerInfo(
        const chunks::ScriptChunk& script,
        const chunks::ScriptChunk::Handler& handler,
        std::span<const Datum> args,
        const Datum& receiver,
        const RuntimeGlobals& globals,
        const chunks::ScriptNamesChunk* names = nullptr,
        const std::string& script_display_name = {}) const;
    [[nodiscard]] TraceListener::HandlerInfo buildHandlerInfo(
        const chunks::ScriptChunk& script,
        const chunks::ScriptChunk::Handler& handler,
        const std::vector<Datum>& args,
        const Datum& receiver,
        const RuntimeGlobals& globals,
        const chunks::ScriptNamesChunk* names = nullptr,
        const std::string& script_display_name = {}) const;

    [[nodiscard]] std::string buildAnnotation(
        const Scope& scope,
        const chunks::ScriptChunk::Instruction& instruction,
        const chunks::ScriptNamesChunk* names = nullptr) const;
};

} // namespace libreshockwave::lingo::vm::trace
