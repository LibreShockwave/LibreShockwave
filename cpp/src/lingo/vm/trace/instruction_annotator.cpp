#include "libreshockwave/lingo/vm/trace/instruction_annotator.hpp"

#include <bit>
#include <cstdint>
#include <sstream>
#include <string>
#include <variant>

namespace libreshockwave::lingo::vm::trace {
namespace {

std::string literalValueToAnnotation(const chunks::ScriptChunk::LiteralValue& value) {
    if (const auto* string_value = std::get_if<std::string>(&value)) {
        return *string_value;
    }
    if (const auto* int_value = std::get_if<int>(&value)) {
        return std::to_string(*int_value);
    }
    if (const auto* bytes = std::get_if<std::vector<std::uint8_t>>(&value)) {
        return "[bytes:" + std::to_string(bytes->size()) + "]";
    }
    return "null";
}

std::string floatArgumentToString(int argument) {
    std::ostringstream out;
    out << std::bit_cast<float>(argument);
    return out.str();
}

} // namespace

std::string InstructionAnnotator::annotate(const chunks::ScriptChunk& script,
                                           const chunks::ScriptChunk::Handler* handler,
                                           const chunks::ScriptChunk::Instruction& instruction,
                                           const chunks::ScriptNamesChunk* names,
                                           bool resolve_names) {
    const int arg = instruction.argument;
    switch (instruction.opcode) {
        case Opcode::PUSH_INT8:
        case Opcode::PUSH_INT16:
        case Opcode::PUSH_INT32:
            return "<" + std::to_string(arg) + ">";
        case Opcode::PUSH_FLOAT32:
            return "<" + floatArgumentToString(arg) + ">";
        case Opcode::PUSH_CONS: {
            const auto& literals = script.literals();
            if (arg >= 0 && arg < static_cast<int>(literals.size())) {
                return "<" + literalValueToAnnotation(literals[static_cast<std::size_t>(arg)].value) + ">";
            }
            return "<literal#" + std::to_string(arg) + ">";
        }
        case Opcode::PUSH_SYMB:
            return "<#" + script.resolveName(arg, names) + ">";
        case Opcode::GET_LOCAL:
        case Opcode::SET_LOCAL:
            if (resolve_names && handler != nullptr && arg >= 0 &&
                arg < static_cast<int>(handler->local_name_ids.size())) {
                return "<" + script.resolveName(handler->local_name_ids[static_cast<std::size_t>(arg)], names) + ">";
            }
            return "<local" + std::to_string(arg) + ">";
        case Opcode::GET_PARAM:
            if (resolve_names && handler != nullptr && arg >= 0 &&
                arg < static_cast<int>(handler->arg_name_ids.size())) {
                return "<" + script.resolveName(handler->arg_name_ids[static_cast<std::size_t>(arg)], names) + ">";
            }
            return "<param" + std::to_string(arg) + ">";
        case Opcode::GET_GLOBAL:
        case Opcode::SET_GLOBAL:
        case Opcode::GET_GLOBAL2:
        case Opcode::SET_GLOBAL2:
            return "<" + script.resolveName(arg, names) + ">";
        case Opcode::GET_PROP:
        case Opcode::SET_PROP:
            return "<me." + script.resolveName(arg, names) + ">";
        case Opcode::LOCAL_CALL: {
            const auto& handlers = script.handlers();
            if (arg >= 0 && arg < static_cast<int>(handlers.size())) {
                return "<" + script.getHandlerName(handlers[static_cast<std::size_t>(arg)], names) + "()>";
            }
            return "<handler#" + std::to_string(arg) + "()>";
        }
        case Opcode::EXT_CALL:
        case Opcode::OBJ_CALL:
            return "<" + script.resolveName(arg, names) + "()>";
        case Opcode::JMP:
        case Opcode::JMP_IF_Z:
            return "<offset " + std::to_string(arg) + " -> " +
                   std::to_string(instruction.offset + arg) + ">";
        case Opcode::END_REPEAT:
            return "<back " + std::to_string(arg) + " -> " +
                   std::to_string(instruction.offset - arg) + ">";
        default:
            return "";
    }
}

std::string InstructionAnnotator::annotate(const chunks::ScriptChunk& script,
                                           const chunks::ScriptChunk::Instruction& instruction,
                                           const chunks::ScriptNamesChunk* names) {
    return annotate(script, nullptr, instruction, names, false);
}

} // namespace libreshockwave::lingo::vm::trace
