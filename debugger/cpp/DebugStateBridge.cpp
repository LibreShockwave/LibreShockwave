#include "DebugStateBridge.hpp"

#include "format/DatumFormat.hpp"
#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/player/debug/DebugSnapshot.hpp"

namespace libreshockwave::debugger {

DebugStateBridge::DebugStateBridge(QObject* parent)
    : QObject(parent) {}

void DebugStateBridge::onPaused(
    const libreshockwave::player::debug::DebugSnapshot& snapshot) {
    emit paused(convertSnapshot(snapshot));
}

void DebugStateBridge::onResumed() {
    emit resumed();
}

void DebugStateBridge::onBreakpointsChanged() {
    emit breakpointsChanged();
}

void DebugStateBridge::onWatchExpressionsChanged() {
    emit watchExpressionsChanged();
}

SnapshotData DebugStateBridge::convertSnapshot(
    const libreshockwave::player::debug::DebugSnapshot& snapshot) const {
    SnapshotData data;
    data.scriptId = snapshot.scriptId;
    data.scriptName = snapshot.scriptName;
    data.handlerName = snapshot.handlerName;
    data.instructionOffset = snapshot.instructionOffset;
    data.instructionIndex = snapshot.instructionIndex;
    data.opcode = snapshot.opcode;
    data.argument = snapshot.argument;
    data.annotation = snapshot.annotation;

    // Instructions with breakpoint flags
    data.allInstructions.reserve(snapshot.allInstructions.size());
    for (const auto& instr : snapshot.allInstructions) {
        InstructionData id;
        id.offset = instr.offset;
        id.index = instr.index;
        id.opcode = instr.opcode;
        id.argument = instr.argument;
        id.annotation = instr.annotation;
        id.hasBreakpoint = instr.hasBreakpoint;
        data.allInstructions.push_back(std::move(id));
    }

    // Call stack
    data.callStack.reserve(snapshot.callStack.size());
    for (const auto& frame : snapshot.callStack) {
        SnapshotData::CallFrameEntry entry;
        entry.scriptId = frame.scriptId;
        entry.scriptName = frame.scriptName;
        entry.handlerName = frame.handlerName;
        data.callStack.push_back(std::move(entry));
    }

    // Locals
    for (const auto& [name, val] : snapshot.locals) {
        SnapshotData::VariableEntry entry;
        entry.name = name;
        entry.value = DatumFormat::toDisplayString(val).toStdString();
        entry.type = DatumFormat::typeName(val).toStdString();
        entry.expandable = DatumFormat::isExpandable(val);
        data.locals.push_back(std::move(entry));
    }

    // Globals
    for (const auto& [name, val] : snapshot.globals) {
        SnapshotData::VariableEntry entry;
        entry.name = name;
        entry.value = DatumFormat::toDisplayString(val).toStdString();
        entry.type = DatumFormat::typeName(val).toStdString();
        entry.expandable = DatumFormat::isExpandable(val);
        data.globals.push_back(std::move(entry));
    }

    // Properties (from receiver)
    if (snapshot.receiver.has_value() &&
        snapshot.receiver->type() == libreshockwave::lingo::DatumType::ScriptInstanceRef) {
        const auto& props = snapshot.receiver->scriptInstanceValue().properties();
        for (const auto& [name, val] : props) {
            SnapshotData::VariableEntry entry;
            entry.name = name;
            entry.value = DatumFormat::toDisplayString(val).toStdString();
            entry.type = DatumFormat::typeName(val).toStdString();
            entry.expandable = DatumFormat::isExpandable(val);
            data.properties.push_back(std::move(entry));
        }
    }

    // Stack
    for (const auto& val : snapshot.stack) {
        SnapshotData::VariableEntry entry;
        entry.value = DatumFormat::toDisplayString(val).toStdString();
        entry.type = DatumFormat::typeName(val).toStdString();
        entry.expandable = DatumFormat::isExpandable(val);
        data.stack.push_back(std::move(entry));
    }

    // Watch results
    for (const auto& wr : snapshot.watchResults) {
        SnapshotData::WatchEntry entry;
        entry.id = wr.id;
        entry.expression = wr.expression;
        if (wr.lastValue.has_value()) {
            entry.value = DatumFormat::toDisplayString(*wr.lastValue).toStdString();
        }
        if (wr.lastError.has_value()) {
            entry.error = *wr.lastError;
        }
        data.watchResults.push_back(std::move(entry));
    }

    return data;
}

} // namespace libreshockwave::debugger
