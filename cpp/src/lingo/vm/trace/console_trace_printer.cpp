#include "libreshockwave/lingo/vm/trace/console_trace_printer.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

namespace libreshockwave::lingo::vm::trace {

ConsoleTracePrinter::ConsoleTracePrinter(OutputHandler output_handler)
    : output_handler_(std::move(output_handler)) {}

void ConsoleTracePrinter::setOutputHandler(OutputHandler output_handler) {
    output_handler_ = std::move(output_handler);
}

void ConsoleTracePrinter::onHandlerEnter(const HandlerInfo& info) {
    resetForHandler(info.script_id);
    emit(formatHandlerEnter(info));
}

void ConsoleTracePrinter::onHandlerExit(const HandlerInfo& info, const Datum& return_value) {
    if (auto line = formatHandlerExit(info, return_value)) {
        emit(*line);
    }
}

void ConsoleTracePrinter::onInstruction(const InstructionInfo& info) {
    if (shouldSuppressInstruction(info.offset)) {
        return;
    }
    emit(formatInstruction(info));
}

std::string ConsoleTracePrinter::formatInstruction(const InstructionInfo& info) {
    std::ostringstream out;
    out << "--> [" << std::setw(3) << info.offset << "] "
        << std::left << std::setw(16) << info.opcode;
    if (info.argument != 0) {
        out << ' ' << info.argument;
    }

    std::string line = out.str();
    while (line.size() < 38) {
        line.push_back('.');
    }
    if (!info.annotation.empty()) {
        line.push_back(' ');
        line += info.annotation;
    }
    return line;
}

std::string ConsoleTracePrinter::formatHandlerEnter(const HandlerInfo& info) {
    return "== Script: " + info.script_display_name + " Handler: " + info.handler_name;
}

std::optional<std::string> ConsoleTracePrinter::formatHandlerExit(const HandlerInfo& info,
                                                                  const Datum& return_value) {
    if (return_value.isVoid()) {
        return std::nullopt;
    }
    return "== " + info.handler_name + " returned " + return_value.stringValue();
}

void ConsoleTracePrinter::emit(std::string_view line) const {
    if (output_handler_) {
        output_handler_(line);
        return;
    }
    std::cout << line << '\n';
}

void ConsoleTracePrinter::resetForHandler(int handler_id) {
    if (handler_id == current_handler_id_) {
        return;
    }
    visited_offsets_.clear();
    loop_suppressed_ = false;
    current_handler_id_ = handler_id;
}

bool ConsoleTracePrinter::shouldSuppressInstruction(int offset) {
    if (visited_offsets_.contains(offset)) {
        if (!loop_suppressed_) {
            emit("    ... [loop iterations suppressed] ...");
            loop_suppressed_ = true;
        }
        return true;
    }
    visited_offsets_.insert(offset);
    loop_suppressed_ = false;
    return false;
}

} // namespace libreshockwave::lingo::vm::trace
