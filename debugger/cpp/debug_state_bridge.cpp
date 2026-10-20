#include "debug_state_bridge.hpp"

#include "format/datum_format.hpp"
#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/player/debug/debug_snapshot.hpp"

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
    data.script_id = snapshot.script_id;
    data.script_name = snapshot.script_name;
    data.handler_name = snapshot.handler_name;
    data.instruction_offset = snapshot.instruction_offset;
    data.instruction_index = snapshot.instruction_index;
    data.opcode = snapshot.opcode;
    data.argument = snapshot.argument;
    data.annotation = snapshot.annotation;

    // Instructions with breakpoint flags
    data.all_instructions.reserve(snapshot.all_instructions.size());
    for (const auto& instr : snapshot.all_instructions) {
        InstructionData id;
        id.offset = instr.offset;
        id.index = instr.index;
        id.opcode = instr.opcode;
        id.argument = instr.argument;
        id.annotation = instr.annotation;
        id.has_breakpoint = instr.has_breakpoint;
        data.all_instructions.push_back(std::move(id));
    }

    // Call stack
    data.call_stack.reserve(snapshot.call_stack.size());
    for (const auto& frame : snapshot.call_stack) {
        SnapshotData::CallFrameEntry entry;
        entry.script_id = frame.script_id;
        entry.script_name = frame.script_name;
        entry.handler_name = frame.handler_name;
        data.call_stack.push_back(std::move(entry));
    }

    // Locals
    for (const auto& [name, val] : snapshot.locals) {
        SnapshotData::VariableEntry entry;
        entry.name = name;
        entry.value = datum_format::toDisplayString(val).toStdString();
        entry.type = datum_format::typeName(val).toStdString();
        entry.expandable = datum_format::isExpandable(val);
        data.locals.push_back(std::move(entry));
    }

    // Globals
    for (const auto& [name, val] : snapshot.globals) {
        SnapshotData::VariableEntry entry;
        entry.name = name;
        entry.value = datum_format::toDisplayString(val).toStdString();
        entry.type = datum_format::typeName(val).toStdString();
        entry.expandable = datum_format::isExpandable(val);
        data.globals.push_back(std::move(entry));
    }

    // Properties (from receiver)
    if (snapshot.receiver.has_value() &&
        snapshot.receiver->type() == libreshockwave::lingo::DatumType::ScriptInstanceRef) {
        const auto& props = snapshot.receiver->scriptInstanceValue().properties();
        for (const auto& [name, val] : props) {
            SnapshotData::VariableEntry entry;
            entry.name = name;
            entry.value = datum_format::toDisplayString(val).toStdString();
            entry.type = datum_format::typeName(val).toStdString();
            entry.expandable = datum_format::isExpandable(val);
            data.properties.push_back(std::move(entry));
        }
    }

    // Stack
    for (const auto& val : snapshot.stack) {
        SnapshotData::VariableEntry entry;
        entry.value = datum_format::toDisplayString(val).toStdString();
        entry.type = datum_format::typeName(val).toStdString();
        entry.expandable = datum_format::isExpandable(val);
        data.stack.push_back(std::move(entry));
    }

    // Watch results
    for (const auto& wr : snapshot.watch_results) {
        SnapshotData::WatchEntry entry;
        entry.id = wr.id;
        entry.expression = wr.expression;
        if (wr.last_value.has_value()) {
            entry.value = datum_format::toDisplayString(*wr.last_value).toStdString();
        }
        if (wr.last_error.has_value()) {
            entry.error = *wr.last_error;
        }
        data.watch_results.push_back(std::move(entry));
    }

    return data;
}

} // namespace libreshockwave::debugger
