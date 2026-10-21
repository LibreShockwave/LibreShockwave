#pragma once

#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/lingo/vm/scope.hpp"
#include "libreshockwave/lingo/vm/trace_listener.hpp"
#include "libreshockwave/moviesource/records/script_asset.hpp"

namespace libreshockwave::lingo::vm::trace {

class TracingHelper {
public:
    [[nodiscard]] TraceListener::InstructionInfo buildInstructionInfo(
        const Scope& scope,
        const moviesource::ScriptAsset::Instruction& instruction,
        const RuntimeGlobals& globals,
        bool include_snapshots = true) const;

    [[nodiscard]] std::unordered_map<std::string, Datum> captureLocals(
        const Scope& scope) const;

    [[nodiscard]] TraceListener::HandlerInfo buildHandlerInfo(
        const moviesource::ScriptAsset& script,
        const moviesource::ScriptAsset::Handler& handler,
        std::span<const Datum> args,
        const Datum& receiver,
        const RuntimeGlobals& globals,
        const std::string& script_display_name = {}) const;
    [[nodiscard]] TraceListener::HandlerInfo buildHandlerInfo(
        const moviesource::ScriptAsset& script,
        const moviesource::ScriptAsset::Handler& handler,
        const std::vector<Datum>& args,
        const Datum& receiver,
        const RuntimeGlobals& globals,
        const std::string& script_display_name = {}) const;

    [[nodiscard]] std::string buildAnnotation(
        const Scope& scope,
        const moviesource::ScriptAsset::Instruction& instruction) const;
};

} // namespace libreshockwave::lingo::vm::trace
