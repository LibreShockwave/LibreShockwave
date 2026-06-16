#include "libreshockwave/lingo/vm/trace/ConsoleTracePrinter.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

namespace libreshockwave::lingo::vm::trace {

ConsoleTracePrinter::ConsoleTracePrinter(OutputHandler outputHandler)
    : outputHandler_(std::move(outputHandler)) {}

void ConsoleTracePrinter::setOutputHandler(OutputHandler outputHandler) {
    outputHandler_ = std::move(outputHandler);
}

void ConsoleTracePrinter::onHandlerEnter(const HandlerInfo& info) {
    resetForHandler(info.scriptId);
    emit(formatHandlerEnter(info));
}

void ConsoleTracePrinter::onHandlerExit(const HandlerInfo& info, const Datum& returnValue) {
    if (auto line = formatHandlerExit(info, returnValue)) {
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
    return "== Script: " + info.scriptDisplayName + " Handler: " + info.handlerName;
}

std::optional<std::string> ConsoleTracePrinter::formatHandlerExit(const HandlerInfo& info,
                                                                  const Datum& returnValue) {
    if (returnValue.isVoid()) {
        return std::nullopt;
    }
    return "== " + info.handlerName + " returned " + returnValue.stringValue();
}

void ConsoleTracePrinter::emit(std::string_view line) const {
    if (outputHandler_) {
        outputHandler_(line);
        return;
    }
    std::cout << line << '\n';
}

void ConsoleTracePrinter::resetForHandler(int handlerId) {
    if (handlerId == currentHandlerId_) {
        return;
    }
    visitedOffsets_.clear();
    loopSuppressed_ = false;
    currentHandlerId_ = handlerId;
}

bool ConsoleTracePrinter::shouldSuppressInstruction(int offset) {
    if (visitedOffsets_.contains(offset)) {
        if (!loopSuppressed_) {
            emit("    ... [loop iterations suppressed] ...");
            loopSuppressed_ = true;
        }
        return true;
    }
    visitedOffsets_.insert(offset);
    loopSuppressed_ = false;
    return false;
}

} // namespace libreshockwave::lingo::vm::trace
