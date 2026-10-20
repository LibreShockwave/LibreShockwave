#include "libreshockwave/lingo/vm/trace/tracing_helper.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "libreshockwave/lingo/opcode.hpp"
#include "libreshockwave/lingo/vm/trace/instruction_annotator.hpp"

namespace libreshockwave::lingo::vm::trace {

TraceListener::InstructionInfo TracingHelper::buildInstructionInfo(
    const Scope& scope,
    const chunks::ScriptChunk::Instruction& instruction,
    const RuntimeGlobals& globals,
    const chunks::ScriptNamesChunk* names,
    bool include_snapshots) const {
    std::vector<Datum> stack_snapshot;
    if (include_snapshots) {
        const int snapshot_count = std::min(10, scope.stackSize());
        stack_snapshot.reserve(static_cast<std::size_t>(snapshot_count));
        for (int index = 0; index < snapshot_count; ++index) {
            stack_snapshot.push_back(scope.peek(index).deepCopy());
        }
    }

    return TraceListener::InstructionInfo{
        scope.bytecodeIndex(),
        instruction.offset,
        std::string(mnemonic(instruction.opcode)),
        instruction.argument,
        include_snapshots ? buildAnnotation(scope, instruction, names) : std::string(),
        scope.stackSize(),
        std::move(stack_snapshot),
        include_snapshots ? captureLocals(scope, names) : std::unordered_map<std::string, Datum>{},
        include_snapshots ? globals : RuntimeGlobals{},
    };
}

std::unordered_map<std::string, Datum> TracingHelper::captureLocals(
    const Scope& scope,
    const chunks::ScriptNamesChunk* names) const {
    std::unordered_map<std::string, Datum> locals;
    const auto* script = scope.script();
    if (script == nullptr) {
        return locals;
    }

    const auto& handler = scope.handler();
    for (int index = 0; index < static_cast<int>(handler.arg_name_ids.size()); ++index) {
        locals[script->resolveName(handler.arg_name_ids[static_cast<std::size_t>(index)], names)] =
            scope.getParam(index).deepCopy();
    }
    for (int index = 0; index < static_cast<int>(handler.local_name_ids.size()); ++index) {
        locals[script->resolveName(handler.local_name_ids[static_cast<std::size_t>(index)], names)] =
            scope.getLocal(index).deepCopy();
    }
    return locals;
}

TraceListener::HandlerInfo TracingHelper::buildHandlerInfo(
    const chunks::ScriptChunk& script,
    const chunks::ScriptChunk::Handler& handler,
    std::span<const Datum> args,
    const Datum& receiver,
    const RuntimeGlobals& globals,
    const chunks::ScriptNamesChunk* names,
    const std::string& script_display_name) const {
    return TraceListener::HandlerInfo{
        script.getHandlerName(handler, names),
        script.id().value(),
        script_display_name.empty() ? "script#" + std::to_string(script.id().value()) : script_display_name,
        std::vector<Datum>(args.begin(), args.end()),
        receiver,
        globals,
        script.literals(),
        handler.local_count,
        handler.arg_count,
    };
}

TraceListener::HandlerInfo TracingHelper::buildHandlerInfo(
    const chunks::ScriptChunk& script,
    const chunks::ScriptChunk::Handler& handler,
    const std::vector<Datum>& args,
    const Datum& receiver,
    const RuntimeGlobals& globals,
    const chunks::ScriptNamesChunk* names,
    const std::string& script_display_name) const {
    return buildHandlerInfo(script, handler, std::span<const Datum>(args), receiver, globals, names, script_display_name);
}

std::string TracingHelper::buildAnnotation(
    const Scope& scope,
    const chunks::ScriptChunk::Instruction& instruction,
    const chunks::ScriptNamesChunk* names) const {
    const auto* script = scope.script();
    if (script == nullptr) {
        return instruction.toString();
    }
    return InstructionAnnotator::annotate(*script, instruction, names);
}

} // namespace libreshockwave::lingo::vm::trace
