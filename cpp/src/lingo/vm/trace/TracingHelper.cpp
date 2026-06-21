#include "libreshockwave/lingo/vm/trace/TracingHelper.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "libreshockwave/lingo/Opcode.hpp"
#include "libreshockwave/lingo/vm/trace/InstructionAnnotator.hpp"

namespace libreshockwave::lingo::vm::trace {

TraceListener::InstructionInfo TracingHelper::buildInstructionInfo(
    const Scope& scope,
    const chunks::ScriptChunk::Instruction& instruction,
    const RuntimeGlobals& globals,
    const chunks::ScriptNamesChunk* names) const {
    std::vector<Datum> stackSnapshot;
    const int snapshotCount = std::min(10, scope.stackSize());
    stackSnapshot.reserve(static_cast<std::size_t>(snapshotCount));
    for (int index = 0; index < snapshotCount; ++index) {
        stackSnapshot.push_back(scope.peek(index).deepCopy());
    }

    return TraceListener::InstructionInfo{
        scope.bytecodeIndex(),
        instruction.offset,
        std::string(mnemonic(instruction.opcode)),
        instruction.argument,
        buildAnnotation(scope, instruction, names),
        scope.stackSize(),
        std::move(stackSnapshot),
        captureLocals(scope, names),
        globals,
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
    for (int index = 0; index < static_cast<int>(handler.argNameIds.size()); ++index) {
        locals[script->resolveName(handler.argNameIds[static_cast<std::size_t>(index)], names)] =
            scope.getParam(index).deepCopy();
    }
    for (int index = 0; index < static_cast<int>(handler.localNameIds.size()); ++index) {
        locals[script->resolveName(handler.localNameIds[static_cast<std::size_t>(index)], names)] =
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
    const std::string& scriptDisplayName) const {
    return TraceListener::HandlerInfo{
        script.getHandlerName(handler, names),
        script.id().value(),
        scriptDisplayName.empty() ? "script#" + std::to_string(script.id().value()) : scriptDisplayName,
        std::vector<Datum>(args.begin(), args.end()),
        receiver,
        globals,
        script.literals(),
        handler.localCount,
        handler.argCount,
    };
}

TraceListener::HandlerInfo TracingHelper::buildHandlerInfo(
    const chunks::ScriptChunk& script,
    const chunks::ScriptChunk::Handler& handler,
    const std::vector<Datum>& args,
    const Datum& receiver,
    const RuntimeGlobals& globals,
    const chunks::ScriptNamesChunk* names,
    const std::string& scriptDisplayName) const {
    return buildHandlerInfo(script, handler, std::span<const Datum>(args), receiver, globals, names, scriptDisplayName);
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
