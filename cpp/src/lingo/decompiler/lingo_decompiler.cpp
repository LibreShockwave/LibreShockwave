#include "libreshockwave/lingo/decompiler/lingo_decompiler.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string_view>
#include <variant>

#include "libreshockwave/moviesource/director_movie_source.hpp"
#include "libreshockwave/chunks/script_names_chunk.hpp"
#include "libreshockwave/format/script_format_utils.hpp"
#include "libreshockwave/lingo/opcode.hpp"
#include "libreshockwave/lingo/decompiler/lingo_properties.hpp"

namespace libreshockwave::lingo::decompiler {
namespace {

constexpr int tag_none = 0;
constexpr int tag_skip = 1;
constexpr int tag_repeat_while = 2;
constexpr int tag_repeat_with_in = 3;
constexpr int tag_repeat_with_to = 4;
constexpr int tag_repeat_with_down_to = 5;
constexpr int tag_next_repeat_target = 6;

std::string indent(int level) {
    return std::string(static_cast<std::size_t>(level) * 2U, ' ');
}

std::string joinStrings(const std::vector<std::string>& values, std::string_view separator) {
    std::string result;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            result.append(separator);
        }
        result.append(values[index]);
    }
    return result;
}

std::string rawLiteralString(const chunks::ScriptChunk::LiteralValue& value) {
    if (const auto* text = std::get_if<std::string>(&value); text != nullptr) {
        return *text;
    }
    if (const auto* integer = std::get_if<int>(&value); integer != nullptr) {
        return std::to_string(*integer);
    }
    if (const auto* bytes = std::get_if<std::vector<std::uint8_t>>(&value); bytes != nullptr) {
        return "[bytes:" + std::to_string(bytes->size()) + "]";
    }
    return "null";
}

void emitBlock(const BlockNode& block,
               std::vector<LingoDecompiler::DecompiledLine>& lines,
               int indent_level,
               bool dot_syntax);

void emitCaseNode(const CaseNode& case_node,
                  std::vector<LingoDecompiler::DecompiledLine>& lines,
                  int indent_level,
                  bool dot_syntax) {
    const auto prefix = indent(indent_level);
    std::string case_line = case_node.value().toLingo(dot_syntax);
    const auto* or_case = case_node.nextOr();
    while (or_case != nullptr) {
        case_line.append(", ");
        case_line.append(or_case->value().toLingo(dot_syntax));
        or_case = or_case->nextOr();
    }
    case_line.push_back(':');
    lines.push_back(LingoDecompiler::DecompiledLine{prefix + case_line, case_node.bytecodeOffset()});

    const auto* branch_case = &case_node;
    while (branch_case->nextOr() != nullptr) {
        branch_case = branch_case->nextOr();
    }
    if (branch_case->block() != nullptr) {
        emitBlock(*branch_case->block(), lines, indent_level + 1, dot_syntax);
    }
    if (branch_case->nextCase() != nullptr) {
        emitCaseNode(*branch_case->nextCase(), lines, indent_level, dot_syntax);
    }
    if (branch_case->otherwise() != nullptr) {
        lines.push_back(LingoDecompiler::DecompiledLine{prefix + "otherwise:", -1});
        emitBlock(*branch_case->otherwise(), lines, indent_level + 1, dot_syntax);
    }
}

void emitSimpleLines(const LingoNode& node,
                     std::vector<LingoDecompiler::DecompiledLine>& lines,
                     std::string_view prefix,
                     bool dot_syntax) {
    auto text = node.toLingo(dot_syntax);
    int offset = node.bytecodeOffset();
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        const auto line_end = end == std::string::npos ? text.size() : end;
        lines.push_back(LingoDecompiler::DecompiledLine{
            std::string(prefix) + text.substr(start, line_end - start),
            offset
        });
        offset = -1;
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
}

void emitNode(const LingoNode& node,
              std::vector<LingoDecompiler::DecompiledLine>& lines,
              int indent_level,
              bool dot_syntax) {
    const auto prefix = indent(indent_level);
    const int offset = node.bytecodeOffset();

    if (const auto* if_stmt = dynamic_cast<const IfStmtNode*>(&node); if_stmt != nullptr) {
        lines.push_back(LingoDecompiler::DecompiledLine{
            prefix + "if " + if_stmt->condition().toLingo(dot_syntax) + " then",
            offset
        });
        emitBlock(if_stmt->trueBlock(), lines, indent_level + 1, dot_syntax);
        if (if_stmt->hasElse()) {
            lines.push_back(LingoDecompiler::DecompiledLine{prefix + "else", -1});
            emitBlock(if_stmt->falseBlock(), lines, indent_level + 1, dot_syntax);
        }
        lines.push_back(LingoDecompiler::DecompiledLine{prefix + "end if", -1});
    } else if (const auto* repeat_while = dynamic_cast<const RepeatWhileStmtNode*>(&node);
               repeat_while != nullptr) {
        lines.push_back(LingoDecompiler::DecompiledLine{
            prefix + "repeat while " + repeat_while->condition().toLingo(dot_syntax),
            offset
        });
        emitBlock(repeat_while->block(), lines, indent_level + 1, dot_syntax);
        lines.push_back(LingoDecompiler::DecompiledLine{prefix + "end repeat", -1});
    } else if (const auto* repeat_in = dynamic_cast<const RepeatWithInStmtNode*>(&node);
               repeat_in != nullptr) {
        lines.push_back(LingoDecompiler::DecompiledLine{
            prefix + "repeat with " + repeat_in->variableName() + " in " + repeat_in->list().toLingo(dot_syntax),
            offset
        });
        emitBlock(repeat_in->block(), lines, indent_level + 1, dot_syntax);
        lines.push_back(LingoDecompiler::DecompiledLine{prefix + "end repeat", -1});
    } else if (const auto* repeat_to = dynamic_cast<const RepeatWithToStmtNode*>(&node);
               repeat_to != nullptr) {
        const std::string direction = repeat_to->up() ? " to " : " down to ";
        lines.push_back(LingoDecompiler::DecompiledLine{
            prefix + "repeat with " + repeat_to->variableName() + " = " +
                repeat_to->start().toLingo(dot_syntax) + direction + repeat_to->end().toLingo(dot_syntax),
            offset
        });
        emitBlock(repeat_to->block(), lines, indent_level + 1, dot_syntax);
        lines.push_back(LingoDecompiler::DecompiledLine{prefix + "end repeat", -1});
    } else if (const auto* tell = dynamic_cast<const TellStmtNode*>(&node); tell != nullptr) {
        lines.push_back(LingoDecompiler::DecompiledLine{
            prefix + "tell " + tell->window().toLingo(dot_syntax),
            offset
        });
        emitBlock(tell->block(), lines, indent_level + 1, dot_syntax);
        lines.push_back(LingoDecompiler::DecompiledLine{prefix + "end tell", -1});
    } else if (const auto* cases = dynamic_cast<const CasesStmtNode*>(&node); cases != nullptr) {
        lines.push_back(LingoDecompiler::DecompiledLine{
            prefix + "case " + cases->value().toLingo(dot_syntax) + " of",
            offset
        });
        if (cases->firstCase() != nullptr) {
            emitCaseNode(*cases->firstCase(), lines, indent_level + 1, dot_syntax);
        }
        lines.push_back(LingoDecompiler::DecompiledLine{prefix + "end case", -1});
    } else {
        emitSimpleLines(node, lines, prefix, dot_syntax);
    }
}

void emitBlock(const BlockNode& block,
               std::vector<LingoDecompiler::DecompiledLine>& lines,
               int indent_level,
               bool dot_syntax) {
    for (const auto& child : block.children()) {
        if (child) {
            emitNode(*child, lines, indent_level, dot_syntax);
        }
    }
}

bool setOpcodeToGetOpcode(Opcode set_op, Opcode& get_op) {
    switch (set_op) {
        case Opcode::SET_GLOBAL:
            get_op = Opcode::GET_GLOBAL;
            return true;
        case Opcode::SET_GLOBAL2:
            get_op = Opcode::GET_GLOBAL2;
            return true;
        case Opcode::SET_PROP:
            get_op = Opcode::GET_PROP;
            return true;
        case Opcode::SET_PARAM:
            get_op = Opcode::GET_PARAM;
            return true;
        case Opcode::SET_LOCAL:
            get_op = Opcode::GET_LOCAL;
            return true;
        default:
            return false;
    }
}

} // namespace

std::string LingoDecompiler::DecompiledHandler::toText() const {
    std::string result;
    for (const auto& line : lines) {
        result.append(line.text);
        result.push_back('\n');
    }
    return result;
}

std::string LingoDecompiler::decompile(const chunks::ScriptChunk& script,
                                       const chunks::ScriptNamesChunk* names) {
    script_ = &script;
    names_ = names;
    initFileInfo(script);

    std::string result = "-- " + format::getScriptTypeName(script.scriptType()) + "\n";

    for (const auto& property : script.properties()) {
        result.append("property ");
        result.append(resolveName(property.name_id));
        result.push_back('\n');
    }

    for (const auto& global : script.globals()) {
        result.append("global ");
        result.append(resolveName(global.name_id));
        result.push_back('\n');
    }

    const bool has_declarations = !script.properties().empty() || !script.globals().empty();
    if (has_declarations && !script.handlers().empty()) {
        result.push_back('\n');
    }

    for (std::size_t index = 0; index < script.handlers().size(); ++index) {
        if (index > 0) {
            result.push_back('\n');
        }
        const auto& handler = script.handlers()[index];
        result.append(decompileHandler(handler, script, names));
    }

    return result;
}

std::string LingoDecompiler::decompileHandler(const chunks::ScriptChunk::Handler& handler,
                                              const chunks::ScriptChunk& script,
                                              const chunks::ScriptNamesChunk* names) {
    script_ = &script;
    names_ = names;
    initFileInfo(script);
    return translateHandler(handler)->toLingo(dot_syntax_);
}

LingoDecompiler::DecompiledHandler LingoDecompiler::decompileHandlerWithMapping(
    const chunks::ScriptChunk::Handler& handler,
    const chunks::ScriptChunk& script,
    const chunks::ScriptNamesChunk* names) {
    script_ = &script;
    names_ = names;
    initFileInfo(script);
    return buildLineMapping(*translateHandler(handler), dot_syntax_);
}

std::string LingoDecompiler::emitTypeScriptHandler(const chunks::ScriptChunk::Handler& handler,
                                                   const chunks::ScriptChunk& script,
                                                   const chunks::ScriptNamesChunk* names) {
    script_ = &script;
    names_ = names;
    initFileInfo(script);
    auto root = translateHandler(handler);
    // Per-handler globals override script-wide globals; if the handler has its own global
    // declaration list (e.g. "global gFoo" inside a handler), use it. Otherwise fall back to
    // the script's global list so all globals used in the handler get runtime backing.
    if (root->globalNames().empty() && script.hasGlobals()) {
        root->setGlobalNames(script.getGlobalNames(names));
    }
    return root->toTypeScript();
}

std::string LingoDecompiler::formatHandlerBytecodeOnly(const chunks::ScriptChunk::Handler& handler,
                                                       const chunks::ScriptNamesChunk* names) const {
    const auto resolve = [names](int name_id) {
        if (names != nullptr && name_id >= 0 && name_id < static_cast<int>(names->names().size())) {
            return names->getName(name_id);
        }
        return "#" + std::to_string(name_id);
    };

    std::ostringstream out;
    out << "on " << resolve(handler.name_id) << "\n";
    for (const auto& instruction : handler.instructions) {
        out << "  [" << std::setfill('0') << std::setw(4) << instruction.offset << std::setfill(' ') << "] ";
        out << std::left << std::setw(16) << std::string(lingo::mnemonic(instruction.opcode)) << std::right;
        if (instruction.raw_opcode >= 0x40) {
            out << " " << instruction.argument;
        }
        out << "\n";
    }
    out << "end\n";
    return out.str();
}

LingoDecompiler::DecompiledHandler LingoDecompiler::buildLineMapping(const HandlerNode& handler, bool dot_syntax) {
    DecompiledHandler result;
    std::string signature = "on " + handler.handlerName();
    if (!handler.argumentNames().empty()) {
        signature.push_back(' ');
        signature.append(joinStrings(handler.argumentNames(), ", "));
    }
    result.lines.push_back(DecompiledLine{signature, -1});

    if (!handler.globalNames().empty()) {
        result.lines.push_back(DecompiledLine{"  global " + joinStrings(handler.globalNames(), ", "), -1});
    }

    emitBlock(handler.block(), result.lines, 1, dot_syntax);
    result.lines.push_back(DecompiledLine{"end", -1});
    return result;
}

std::string LingoDecompiler::resolveName(int name_id) const {
    if (names_ != nullptr && name_id >= 0 && name_id < static_cast<int>(names_->names().size())) {
        return names_->getName(name_id);
    }
    return "#" + std::to_string(name_id);
}

void LingoDecompiler::initFileInfo(const chunks::ScriptChunk& script) {
    version_ = 0x4C1;
    capital_x_ = false;
    if (script.file() != nullptr) {
        version_ = script.file()->version();
        capital_x_ = script.file()->isCapitalX();
    }
    dot_syntax_ = version_ >= 700;
}

void LingoDecompiler::tagLoops() {
    tags_.assign(current_handler_ != nullptr ? current_handler_->instructions.size() : 0, tag_none);
    owner_loops_.assign(tags_.size(), -1);
    if (current_handler_ == nullptr) {
        return;
    }

    const auto& instructions = current_handler_->instructions;
    for (std::size_t start_index = 0; start_index < instructions.size(); ++start_index) {
        const auto& jmp_if_zero = instructions[start_index];
        if (jmp_if_zero.opcode != Opcode::JMP_IF_Z) {
            continue;
        }

        const int end_pos = jmp_if_zero.offset + jmp_if_zero.argument;
        const int end_index = instructionIndexForOffset(end_pos);
        if (end_index < 1 || end_index > static_cast<int>(instructions.size())) {
            continue;
        }

        const auto& end_repeat = instructions[static_cast<std::size_t>(end_index - 1)];
        if (end_repeat.opcode != Opcode::END_REPEAT) {
            continue;
        }
        if ((end_repeat.offset - end_repeat.argument) > jmp_if_zero.offset) {
            continue;
        }
        if (isRepeatWithInLoop(start_index, end_index)) {
            tags_[start_index] = tag_repeat_with_in;
            for (std::size_t index = start_index - 7; index <= start_index - 1; ++index) {
                tags_[index] = tag_skip;
            }
            for (std::size_t index = start_index + 1; index <= start_index + 5; ++index) {
                tags_[index] = tag_skip;
            }
            tags_[static_cast<std::size_t>(end_index - 3)] = tag_next_repeat_target;
            owner_loops_[static_cast<std::size_t>(end_index - 3)] = static_cast<int>(start_index);
            tags_[static_cast<std::size_t>(end_index - 2)] = tag_skip;
            tags_[static_cast<std::size_t>(end_index - 1)] = tag_skip;
            owner_loops_[static_cast<std::size_t>(end_index - 1)] = static_cast<int>(start_index);
            if (end_index < static_cast<int>(tags_.size())) {
                tags_[static_cast<std::size_t>(end_index)] = tag_skip;
            }
            continue;
        }
        if (isRepeatWithToLoop(start_index, end_index)) {
            const auto& end_repeat = instructions[static_cast<std::size_t>(end_index - 1)];
            const int cond_start = instructionIndexForOffset(end_repeat.offset - end_repeat.argument);
            if (cond_start < 1) {
                continue;
            }

            tags_[start_index] = instructions[start_index - 1].opcode == Opcode::LT_EQ
                ? tag_repeat_with_to
                : tag_repeat_with_down_to;
            tags_[static_cast<std::size_t>(cond_start - 1)] = tag_skip;
            tags_[static_cast<std::size_t>(cond_start)] = tag_skip;
            tags_[start_index - 1] = tag_skip;
            tags_[static_cast<std::size_t>(end_index - 5)] = tag_next_repeat_target;
            owner_loops_[static_cast<std::size_t>(end_index - 5)] = static_cast<int>(start_index);
            tags_[static_cast<std::size_t>(end_index - 4)] = tag_skip;
            tags_[static_cast<std::size_t>(end_index - 3)] = tag_skip;
            tags_[static_cast<std::size_t>(end_index - 2)] = tag_skip;
            tags_[static_cast<std::size_t>(end_index - 1)] = tag_skip;
            owner_loops_[static_cast<std::size_t>(end_index - 1)] = static_cast<int>(start_index);
            continue;
        }

        tags_[start_index] = tag_repeat_while;
        tags_[static_cast<std::size_t>(end_index - 1)] = tag_next_repeat_target;
        owner_loops_[static_cast<std::size_t>(end_index - 1)] = static_cast<int>(start_index);
    }
}

bool LingoDecompiler::isRepeatWithInLoop(std::size_t start_index, int end_index) const {
    if (current_handler_ == nullptr) {
        return false;
    }

    const auto& instructions = current_handler_->instructions;
    if (start_index < 7 || start_index + 5 >= instructions.size() || end_index < 3 ||
        end_index >= static_cast<int>(instructions.size())) {
        return false;
    }

    const auto at = [&instructions](std::size_t index) -> const chunks::ScriptChunk::Instruction& {
        return instructions[index];
    };

    return at(start_index - 7).opcode == Opcode::PEEK &&
           at(start_index - 7).argument == 0 &&
           at(start_index - 6).opcode == Opcode::PUSH_ARG_LIST &&
           at(start_index - 6).argument == 1 &&
           at(start_index - 5).opcode == Opcode::EXT_CALL &&
           resolveName(at(start_index - 5).argument) == "count" &&
           at(start_index - 4).opcode == Opcode::PUSH_INT8 &&
           at(start_index - 4).argument == 1 &&
           at(start_index - 3).opcode == Opcode::PEEK &&
           at(start_index - 3).argument == 0 &&
           at(start_index - 2).opcode == Opcode::PEEK &&
           at(start_index - 2).argument == 2 &&
           at(start_index - 1).opcode == Opcode::LT_EQ &&
           at(start_index + 1).opcode == Opcode::PEEK &&
           at(start_index + 1).argument == 2 &&
           at(start_index + 2).opcode == Opcode::PEEK &&
           at(start_index + 2).argument == 1 &&
           at(start_index + 3).opcode == Opcode::PUSH_ARG_LIST &&
           at(start_index + 3).argument == 2 &&
           at(start_index + 4).opcode == Opcode::EXT_CALL &&
           resolveName(at(start_index + 4).argument) == "getAt" &&
           (at(start_index + 5).opcode == Opcode::SET_GLOBAL ||
            at(start_index + 5).opcode == Opcode::SET_PROP ||
            at(start_index + 5).opcode == Opcode::SET_PARAM ||
            at(start_index + 5).opcode == Opcode::SET_LOCAL) &&
           at(static_cast<std::size_t>(end_index - 3)).opcode == Opcode::PUSH_INT8 &&
           at(static_cast<std::size_t>(end_index - 3)).argument == 1 &&
           at(static_cast<std::size_t>(end_index - 2)).opcode == Opcode::ADD &&
           at(static_cast<std::size_t>(end_index)).opcode == Opcode::POP &&
           at(static_cast<std::size_t>(end_index)).argument == 3;
}

bool LingoDecompiler::isRepeatWithToLoop(std::size_t start_index, int end_index) const {
    if (current_handler_ == nullptr || start_index < 1 || end_index < 5) {
        return false;
    }

    const auto& instructions = current_handler_->instructions;
    const auto comparison_op = instructions[start_index - 1].opcode;
    if (comparison_op != Opcode::LT_EQ && comparison_op != Opcode::GT_EQ) {
        return false;
    }

    const auto& end_repeat = instructions[static_cast<std::size_t>(end_index - 1)];
    const int cond_start = instructionIndexForOffset(end_repeat.offset - end_repeat.argument);
    if (cond_start < 1 || cond_start >= static_cast<int>(instructions.size())) {
        return false;
    }

    const auto set_op = instructions[static_cast<std::size_t>(cond_start - 1)].opcode;
    Opcode get_op = Opcode::PUSH_ZERO;
    if (!setOpcodeToGetOpcode(set_op, get_op)) {
        return false;
    }

    const int var_id = instructions[static_cast<std::size_t>(cond_start - 1)].argument;
    const int expected_increment = comparison_op == Opcode::LT_EQ ? 1 : -1;
    return instructions[static_cast<std::size_t>(cond_start)].opcode == get_op &&
           instructions[static_cast<std::size_t>(cond_start)].argument == var_id &&
           instructions[static_cast<std::size_t>(end_index - 5)].opcode == Opcode::PUSH_INT8 &&
           instructions[static_cast<std::size_t>(end_index - 5)].argument == expected_increment &&
           instructions[static_cast<std::size_t>(end_index - 4)].opcode == get_op &&
           instructions[static_cast<std::size_t>(end_index - 4)].argument == var_id &&
           instructions[static_cast<std::size_t>(end_index - 3)].opcode == Opcode::ADD &&
           instructions[static_cast<std::size_t>(end_index - 2)].opcode == set_op &&
           instructions[static_cast<std::size_t>(end_index - 2)].argument == var_id;
}

int LingoDecompiler::instructionIndexForOffset(int offset) const {
    if (current_handler_ == nullptr) {
        return -1;
    }
    if (const int mapped_index = current_handler_->getInstructionIndex(offset); mapped_index >= 0) {
        return mapped_index;
    }
    const auto& instructions = current_handler_->instructions;
    const auto found = std::find_if(instructions.begin(), instructions.end(), [offset](const auto& instruction) {
        return instruction.offset == offset;
    });
    if (found == instructions.end()) {
        return -1;
    }
    return static_cast<int>(std::distance(instructions.begin(), found));
}

std::string LingoDecompiler::getVarNameFromSet(const chunks::ScriptChunk::Instruction& instruction) const {
    switch (instruction.opcode) {
        case Opcode::SET_GLOBAL:
        case Opcode::SET_GLOBAL2:
        case Opcode::SET_PROP:
            return resolveName(instruction.argument);
        case Opcode::SET_PARAM:
            return getArgumentName(instruction.argument);
        case Opcode::SET_LOCAL:
            return getLocalName(instruction.argument);
        default:
            return "ERROR";
    }
}

std::unique_ptr<HandlerNode> LingoDecompiler::translateHandler(const chunks::ScriptChunk::Handler& handler) {
    current_handler_ = &handler;
    tagLoops();
    stack_.clear();
    current_block_ = nullptr;

    std::vector<std::string> argument_names;
    argument_names.reserve(handler.arg_name_ids.size());
    for (const auto name_id : handler.arg_name_ids) {
        argument_names.push_back(resolveName(name_id));
    }

    auto root = std::make_unique<HandlerNode>(resolveName(handler.name_id), std::move(argument_names), std::vector<std::string>{});
    // Handler-level globals are part of the handler record, not the script
    // record. Emit them so the decompiled text round-trips the global list.
    std::vector<std::string> handler_globals;
    handler_globals.reserve(handler.global_name_ids.size());
    for (const auto name_id : handler.global_name_ids) {
        handler_globals.push_back(resolveName(name_id));
    }
    root->setGlobalNames(std::move(handler_globals));
    if (script_ != nullptr) {
        std::vector<std::string> property_names;
        for (const auto& prop : script_->properties()) {
            const std::string name = resolveName(prop.name_id);
            if (!name.empty()) {
                property_names.push_back(name);
            }
        }
        root->setPropertyNames(std::move(property_names));
    }
    current_block_ = &root->block();
    for (std::size_t index = 0; index < handler.instructions.size();) {
        const auto& instruction = handler.instructions[index];
        while (current_block_ != nullptr && instruction.offset == current_block_->end_pos) {
            auto* exited_block = current_block_;
            auto* ancestor_statement = current_block_->ancestorStatement();
            exitBlock();
            if (auto* if_stmt = dynamic_cast<IfStmtNode*>(ancestor_statement);
                if_stmt != nullptr && if_stmt->hasElse() && exited_block == &if_stmt->trueBlock()) {
                enterBlock(if_stmt->falseBlock());
            } else if (auto* cases_stmt = dynamic_cast<CasesStmtNode*>(ancestor_statement);
                       cases_stmt != nullptr && current_block_ != nullptr) {
                auto* case_node = current_block_->current_case;
                if (case_node != nullptr) {
                    if (case_node->expect() == CaseNode::EXPECT_OTHERWISE) {
                        if (exited_block == case_node->block()) {
                            auto otherwise = std::make_unique<BlockNode>();
                            otherwise->end_pos = cases_stmt->end_pos;
                            auto* otherwise_block = otherwise.get();
                            case_node->setOtherwise(std::move(otherwise));
                            enterBlock(*otherwise_block);
                        } else {
                            current_block_->current_case = nullptr;
                        }
                    } else if (case_node->expect() == CaseNode::EXPECT_POP) {
                        current_block_->current_case = nullptr;
                    }
                }
            }
        }
        if (current_block_ == nullptr) {
            current_block_ = &root->block();
        }
        last_consumed_ = 1;
        translateInstruction(instruction, index, *current_block_);
        index += std::max<std::size_t>(last_consumed_, 1);
    }
    current_block_ = nullptr;
    return root;
}

void LingoDecompiler::translateInstruction(const chunks::ScriptChunk::Instruction& instruction,
                                           std::size_t index,
                                           BlockNode& block) {
    if (index < tags_.size() && (tags_[index] == tag_skip || tags_[index] == tag_next_repeat_target)) {
        return;
    }

    NodePtr translation;

    switch (instruction.opcode) {
        case Opcode::RET:
        case Opcode::RET_FACTORY:
            if (script_ != nullptr && current_handler_ != nullptr && index + 1 == current_handler_->instructions.size()) {
                return;
            }
            translation = std::make_unique<ExitStmtNode>();
            break;

        case Opcode::PUSH_ZERO:
            translation = std::make_unique<LiteralNode>(0);
            break;

        case Opcode::MUL:
        case Opcode::ADD:
        case Opcode::SUB:
        case Opcode::DIV:
        case Opcode::MOD:
        case Opcode::JOIN_STR:
        case Opcode::JOIN_PAD_STR:
        case Opcode::LT:
        case Opcode::LT_EQ:
        case Opcode::NT_EQ:
        case Opcode::EQ:
        case Opcode::GT:
        case Opcode::GT_EQ:
        case Opcode::AND:
        case Opcode::OR:
        case Opcode::CONTAINS_STR:
        case Opcode::CONTAINS_0_STR: {
            auto right = popNode();
            auto left = popNode();
            translation = std::make_unique<BinaryOpNode>(instruction.opcode, std::move(left), std::move(right));
            break;
        }

        case Opcode::INV:
            translation = std::make_unique<InverseOpNode>(popNode());
            break;

        case Opcode::NOT:
            translation = std::make_unique<NotOpNode>(popNode());
            break;

        case Opcode::GET_CHUNK:
            translation = readChunkRef(popNode());
            break;

        case Opcode::HILITE_CHUNK: {
            NodePtr cast_id;
            if (version_ >= 500) {
                cast_id = popNode();
            }
            auto field_id = popNode();
            auto field = std::make_unique<MemberExprNode>("field", std::move(field_id), std::move(cast_id));
            translation = std::make_unique<ChunkHiliteStmtNode>(readChunkRef(std::move(field)));
            break;
        }

        case Opcode::ONTO_SPR: {
            auto right = popNode();
            auto left = popNode();
            translation = std::make_unique<SpriteIntersectsExprNode>(std::move(left), std::move(right));
            break;
        }

        case Opcode::INTO_SPR: {
            auto right = popNode();
            auto left = popNode();
            translation = std::make_unique<SpriteWithinExprNode>(std::move(left), std::move(right));
            break;
        }

        case Opcode::GET_FIELD: {
            NodePtr cast_id;
            if (version_ >= 500) {
                cast_id = popNode();
            }
            translation = std::make_unique<MemberExprNode>("field", popNode(), std::move(cast_id));
            break;
        }

        case Opcode::START_TELL: {
            auto tell = std::make_unique<TellStmtNode>(popNode());
            tell->setBytecodeOffset(instruction.offset);
            auto* next_block = &tell->block();
            block.addChild(std::move(tell));
            enterBlock(*next_block);
            return;
        }

        case Opcode::END_TELL:
            exitBlock();
            return;

        case Opcode::PUSH_INT8:
        case Opcode::PUSH_INT16:
        case Opcode::PUSH_INT32:
            translation = std::make_unique<LiteralNode>(instruction.argument);
            break;

        case Opcode::PUSH_FLOAT32: {
            const auto bits = static_cast<std::uint32_t>(instruction.argument);
            translation = std::make_unique<LiteralNode>(static_cast<double>(std::bit_cast<float>(bits)));
            break;
        }

        case Opcode::PUSH_ARG_LIST:
        case Opcode::PUSH_ARG_LIST_NO_RET: {
            std::vector<NodePtr> args;
            const int count = std::max(0, instruction.argument);
            args.reserve(static_cast<std::size_t>(count));
            for (int arg_index = 0; arg_index < count; ++arg_index) {
                args.insert(args.begin(), popNode());
            }
            translation = std::make_unique<LiteralNode>(
                instruction.opcode == Opcode::PUSH_ARG_LIST ? ValueType::ArgList : ValueType::ArgListNoRet,
                std::move(args));
            break;
        }

        case Opcode::PUSH_LIST:
        case Opcode::PUSH_PROP_LIST:
            translation = popNode();
            translation->setValueType(instruction.opcode == Opcode::PUSH_LIST ? ValueType::List : ValueType::PropList);
            break;

        case Opcode::PUSH_CONS: {
            const int literal_id = instruction.argument / variableMultiplier();
            if (script_ != nullptr && literal_id >= 0 && literal_id < static_cast<int>(script_->literals().size())) {
                translation = literalToNode(script_->literals()[static_cast<std::size_t>(literal_id)]);
            } else {
                translation = std::make_unique<ErrorNode>();
            }
            break;
        }

        case Opcode::PUSH_SYMB:
            translation = std::make_unique<LiteralNode>(ValueType::Symbol, resolveName(instruction.argument));
            break;

        case Opcode::PUSH_VAR_REF:
            translation = std::make_unique<LiteralNode>(ValueType::VarRef, resolveName(instruction.argument));
            break;

        case Opcode::GET_GLOBAL:
        case Opcode::GET_GLOBAL2:
        case Opcode::GET_PROP:
        case Opcode::GET_TOP_LEVEL_PROP:
            translation = std::make_unique<VarNode>(resolveName(instruction.argument));
            break;

        case Opcode::GET_PARAM:
            translation = std::make_unique<VarNode>(getArgumentName(instruction.argument));
            break;

        case Opcode::GET_LOCAL:
            translation = std::make_unique<VarNode>(getLocalName(instruction.argument));
            break;

        case Opcode::SET_GLOBAL:
        case Opcode::SET_GLOBAL2:
        case Opcode::SET_PROP:
            translation = std::make_unique<AssignmentStmtNode>(
                std::make_unique<VarNode>(resolveName(instruction.argument)),
                popNode());
            break;

        case Opcode::SET_PARAM:
            translation = std::make_unique<AssignmentStmtNode>(
                std::make_unique<VarNode>(getArgumentName(instruction.argument)),
                popNode());
            break;

        case Opcode::SET_LOCAL:
            translation = std::make_unique<AssignmentStmtNode>(
                std::make_unique<VarNode>(getLocalName(instruction.argument)),
                popNode());
            break;

        case Opcode::PUT: {
            const int put_type = (instruction.argument >> 4) & 0xF;
            const int var_type = instruction.argument & 0xF;
            auto variable = readVar(var_type);
            auto value = popNode();
            translation = std::make_unique<PutStmtNode>(put_type, std::move(variable), std::move(value));
            break;
        }

        case Opcode::PUT_CHUNK: {
            const int put_type = (instruction.argument >> 4) & 0xF;
            const int var_type = instruction.argument & 0xF;
            auto chunk = readChunkRef(readVar(var_type));
            auto value = popNode();
            translation = std::make_unique<PutStmtNode>(put_type, std::move(chunk), std::move(value));
            break;
        }

        case Opcode::DELETE_CHUNK:
            translation = std::make_unique<ChunkDeleteStmtNode>(readChunkRef(readVar(instruction.argument)));
            break;

        case Opcode::GET: {
            const auto prop_id = popNode()->intValue();
            translation = readV4Property(instruction.argument, prop_id);
            break;
        }

        case Opcode::SET: {
            const auto prop_id = popNode()->intValue();
            auto value = popNode();
            if (instruction.argument == 0x00 &&
                prop_id >= 0x01 &&
                prop_id <= 0x05 &&
                value->valueType() == ValueType::String) {
                auto script = value->stringValue();
                if (!script.empty() && (script.front() == ' ' || script.find('\r') != std::string::npos)) {
                    translation = std::make_unique<WhenStmtNode>(prop_id, std::move(script));
                }
            }
            if (!translation) {
                auto property = readV4Property(instruction.argument, prop_id);
                if (dynamic_cast<CommentNode*>(property.get()) != nullptr) {
                    translation = std::move(property);
                } else {
                    translation = std::make_unique<AssignmentStmtNode>(
                        std::move(property),
                        std::move(value),
                        true);
                }
            }
            break;
        }

        case Opcode::LOCAL_CALL: {
            auto arg_list = popNode();
            std::string call_name = "handler#" + std::to_string(instruction.argument);
            if (script_ != nullptr &&
                instruction.argument >= 0 &&
                instruction.argument < static_cast<int>(script_->handlers().size())) {
                call_name = resolveName(script_->handlers()[static_cast<std::size_t>(instruction.argument)].name_id);
            }
            translation = std::make_unique<CallNode>(std::move(call_name), std::move(arg_list));
            break;
        }

        case Opcode::EXT_CALL:
        case Opcode::TELL_CALL: {
            auto call_name = resolveName(instruction.argument);
            auto arg_list = popNode();
            // Lingo's `return <value>` lowers to EXT_CALL "return" with the value
            // wrapped in an ArgList (PUSH_ARG_LIST_NO_RET or PUSH_ARG_LIST with
            // arity 1). Materialize it as a ReturnStmtNode so the TypeScript
            // emitter produces `return <value>;` (an actual `return` statement)
            // instead of `_return(<value>)` (a sanitized function call whose
            // return value is discarded and the handler exits with `undefined`).
            // Bare `return` (no args) becomes an ExitStmtNode (`return;`),
            // not `_return()`.
            if (call_name == "return" &&
                (arg_list->valueType() == ValueType::ArgList ||
                 arg_list->valueType() == ValueType::ArgListNoRet)) {
                if (arg_list->argNodes().empty()) {
                    translation = std::make_unique<ExitStmtNode>();
                    break;
                }
                if (arg_list->argNodes().size() == 1) {
                    auto args = takeArgNodes(std::move(arg_list));
                    translation = std::make_unique<ReturnStmtNode>(std::move(args.front()));
                    break;
                }
            }
            translation = std::make_unique<CallNode>(std::move(call_name), std::move(arg_list));
            break;
        }

        case Opcode::OBJ_CALL_V4: {
            auto object = readVar(instruction.argument);
            auto arg_list = popNode();
            const auto arg_list_type = arg_list->valueType() == ValueType::ArgListNoRet
                ? ValueType::ArgListNoRet
                : ValueType::ArgList;
            auto args = takeArgNodes(std::move(arg_list));
            if (!args.empty() && args[0]->valueType() == ValueType::Symbol) {
                args[0] = std::make_unique<VarNode>(args[0]->stringValue());
            }
            translation = std::make_unique<ObjCallV4Node>(
                std::move(object),
                std::make_unique<LiteralNode>(arg_list_type, std::move(args)));
            break;
        }

        case Opcode::GET_MOVIE_PROP:
            translation = std::make_unique<TheExprNode>(resolveName(instruction.argument));
            break;

        case Opcode::SET_MOVIE_PROP:
            translation = std::make_unique<AssignmentStmtNode>(
                std::make_unique<TheExprNode>(resolveName(instruction.argument)),
                popNode());
            break;

        case Opcode::GET_OBJ_PROP:
        case Opcode::GET_CHAINED_PROP:
            translation = std::make_unique<ObjPropExprNode>(popNode(), resolveName(instruction.argument));
            break;

        case Opcode::SET_OBJ_PROP: {
            auto value = popNode();
            auto object = popNode();
            translation = std::make_unique<AssignmentStmtNode>(
                std::make_unique<ObjPropExprNode>(std::move(object), resolveName(instruction.argument)),
                std::move(value));
            break;
        }

        case Opcode::PEEK:
            translatePeek(instruction, index, block);
            return;

        case Opcode::THE_BUILTIN:
            (void)popNode();
            translation = std::make_unique<TheExprNode>(resolveName(instruction.argument));
            break;

        case Opcode::OBJ_CALL:
            translateObjCall(instruction.offset, instruction.argument, block);
            return;

        case Opcode::JMP: {
            const int target_offset = instruction.offset + instruction.argument;
            const int target_index = current_handler_ != nullptr
                ? instructionIndexForOffset(target_offset)
                : -1;
            if (target_index >= 0 && current_block_ != nullptr && current_handler_ != nullptr) {
                if (auto* ancestorLoop = current_block_->ancestorLoop(); ancestorLoop != nullptr) {
                    if (target_index >= 1 &&
                        current_handler_->instructions[static_cast<std::size_t>(target_index - 1)].opcode == Opcode::END_REPEAT &&
                        static_cast<std::size_t>(target_index - 1) < owner_loops_.size() &&
                        owner_loops_[static_cast<std::size_t>(target_index - 1)] == ancestorLoop->startIndex()) {
                        translation = std::make_unique<ExitRepeatStmtNode>();
                        break;
                    }
                    if (static_cast<std::size_t>(target_index) < tags_.size() &&
                        tags_[static_cast<std::size_t>(target_index)] == tag_next_repeat_target &&
                        owner_loops_[static_cast<std::size_t>(target_index)] == ancestorLoop->startIndex()) {
                        translation = std::make_unique<NextRepeatStmtNode>();
                        break;
                    }
                }

                if (index + 1 < current_handler_->instructions.size() &&
                    current_handler_->instructions[index + 1].offset == current_block_->end_pos) {
                    auto* ancestor_statement = current_block_->ancestorStatement();
                    if (auto* if_stmt = dynamic_cast<IfStmtNode*>(ancestor_statement);
                        if_stmt != nullptr && current_block_ == &if_stmt->trueBlock()) {
                        if_stmt->setHasElse(true);
                        if_stmt->falseBlock().end_pos = target_offset;
                        return;
                    }
                    if (auto* cases_stmt = dynamic_cast<CasesStmtNode*>(ancestor_statement); cases_stmt != nullptr) {
                        cases_stmt->end_pos = target_offset;
                        return;
                    }
                }
            }
            translation = std::make_unique<CommentNode>("ERROR: Could not identify jmp");
            break;
        }

        case Opcode::JMP_IF_Z: {
            const int end_pos = instruction.offset + instruction.argument;
            if (index < tags_.size() && tags_[index] == tag_repeat_while) {
                auto loop = std::make_unique<RepeatWhileStmtNode>(static_cast<int>(index), popNode());
                loop->setBytecodeOffset(instruction.offset);
                loop->block().end_pos = end_pos;
                auto* next_block = &loop->block();
                block.addChild(std::move(loop));
                enterBlock(*next_block);
                return;
            }
            if (index < tags_.size() && tags_[index] == tag_repeat_with_in) {
                auto loop = std::make_unique<RepeatWithInStmtNode>(
                    static_cast<int>(index),
                    getVarNameFromSet(current_handler_->instructions[index + 5]),
                    popNode());
                loop->setBytecodeOffset(instruction.offset);
                loop->block().end_pos = end_pos;
                auto* next_block = &loop->block();
                block.addChild(std::move(loop));
                enterBlock(*next_block);
                return;
            }
            if (index < tags_.size() &&
                (tags_[index] == tag_repeat_with_to || tags_[index] == tag_repeat_with_down_to)) {
                auto end = popNode();
                auto start = popNode();
                const int end_index = instructionIndexForOffset(end_pos);
                if (end_index >= 1 && current_handler_ != nullptr) {
                    const auto& end_repeat = current_handler_->instructions[static_cast<std::size_t>(end_index - 1)];
                    const int cond_start = instructionIndexForOffset(end_repeat.offset - end_repeat.argument);
                    if (cond_start >= 1) {
                        auto loop = std::make_unique<RepeatWithToStmtNode>(
                            static_cast<int>(index),
                            getVarNameFromSet(current_handler_->instructions[static_cast<std::size_t>(cond_start - 1)]),
                            std::move(start),
                            tags_[index] == tag_repeat_with_to,
                            std::move(end));
                        loop->setBytecodeOffset(instruction.offset);
                        loop->block().end_pos = end_pos;
                        auto* next_block = &loop->block();
                        block.addChild(std::move(loop));
                        enterBlock(*next_block);
                        return;
                    }
                }
                translation = std::make_unique<CommentNode>("ERROR: Could not identify repeat with to");
                break;
            }

            auto if_stmt = std::make_unique<IfStmtNode>(popNode());
            if_stmt->setBytecodeOffset(instruction.offset);
            if_stmt->trueBlock().end_pos = end_pos;
            auto* next_block = &if_stmt->trueBlock();
            block.addChild(std::move(if_stmt));
            enterBlock(*next_block);
            return;
        }

        case Opcode::END_REPEAT:
            translation = std::make_unique<CommentNode>("ERROR: Stray endrepeat");
            break;

        case Opcode::PUSH_CHUNK_VAR_REF:
            translation = readVar(instruction.argument);
            break;

        case Opcode::NEW_OBJ:
            translation = std::make_unique<NewObjNode>(resolveName(instruction.argument), popNode());
            break;

        case Opcode::SWAP:
            if (stack_.size() >= 2) {
                std::swap(stack_[stack_.size() - 1], stack_[stack_.size() - 2]);
            }
            return;

        case Opcode::POP:
            for (int count = 0; count < instruction.argument; ++count) {
                (void)popNode();
            }
            return;

        case Opcode::CALL_JAVASCRIPT: {
            stack_.clear();
            std::string script;
            if (script_ != nullptr && !script_->literals().empty()) {
                if (const auto* text = std::get_if<std::string>(&script_->literals().front().value); text != nullptr) {
                    script = *text;
                }
            }
            translation = std::make_unique<CommentNode>("@js\n" + script);
            break;
        }

        default: {
            std::string text(lingo::mnemonic(instruction.opcode));
            if (instruction.raw_opcode >= 0x40) {
                text.push_back(' ');
                text.append(std::to_string(instruction.argument));
            }
            translation = std::make_unique<CommentNode>(std::move(text));
            stack_.clear();
            break;
        }
    }

    if (!translation) {
        translation = std::make_unique<ErrorNode>();
    }
    translation->setBytecodeOffset(instruction.offset);

    if (translation->isExpression()) {
        stack_.push_back(std::move(translation));
    } else {
        block.addChild(std::move(translation));
    }
}

void LingoDecompiler::translatePeek(const chunks::ScriptChunk::Instruction& instruction,
                                    std::size_t index,
                                    BlockNode& block) {
    const auto add_error = [&](std::string text, std::size_t consumed) {
        auto error = std::make_unique<CommentNode>(std::move(text));
        error->setBytecodeOffset(instruction.offset);
        block.addChild(std::move(error));
        last_consumed_ = std::max<std::size_t>(consumed, 1);
    };

    if (current_handler_ == nullptr) {
        add_error("ERROR: Missing handler for case translation", 1);
        return;
    }

    const auto& instructions = current_handler_->instructions;
    auto* previous_case = block.current_case;
    NodePtr peeked_value;
    if (previous_case == nullptr) {
        peeked_value = popNode();
    }

    const auto original_stack_size = stack_.size();
    std::size_t current_index = index + 1;
    while (current_index < instructions.size() && instructions[current_index].opcode == Opcode::PEEK) {
        ++current_index;
    }
    while (current_index < instructions.size()) {
        translateInstruction(instructions[current_index], current_index, block);
        ++current_index;
        if (current_index < instructions.size() &&
            stack_.size() == original_stack_size + 1 &&
            (instructions[current_index].opcode == Opcode::EQ ||
             instructions[current_index].opcode == Opcode::NT_EQ)) {
            break;
        }
    }
    if (current_index >= instructions.size()) {
        add_error("ERROR: Expected eq or nteq", current_index - index + 1);
        return;
    }

    auto case_value = popNode();

    if (current_index >= instructions.size() ||
        (instructions[current_index].opcode != Opcode::EQ && instructions[current_index].opcode != Opcode::NT_EQ)) {
        add_error("ERROR: Expected eq or nteq", current_index - index + 1);
        return;
    }
    const bool not_equal = instructions[current_index].opcode == Opcode::NT_EQ;
    ++current_index;

    if (current_index >= instructions.size() || instructions[current_index].opcode != Opcode::JMP_IF_Z) {
        add_error("ERROR: Expected jmpifz", current_index - index + 1);
        return;
    }

    const auto& jump = instructions[current_index];
    const int jump_pos = jump.offset + jump.argument;
    const int target_index = instructionIndexForOffset(jump_pos);

    int expect = CaseNode::EXPECT_OTHERWISE;
    if (not_equal) {
        expect = CaseNode::EXPECT_OR;
    } else if (target_index >= 0 &&
               target_index < static_cast<int>(instructions.size()) &&
               instructions[static_cast<std::size_t>(target_index)].opcode == Opcode::PEEK) {
        expect = CaseNode::EXPECT_NEXT;
    } else if (target_index >= 0 &&
               target_index < static_cast<int>(instructions.size()) &&
               instructions[static_cast<std::size_t>(target_index)].opcode == Opcode::POP) {
        expect = CaseNode::EXPECT_POP;
    }

    auto current_case = std::make_unique<CaseNode>(std::move(case_value), expect);
    current_case->setBytecodeOffset(instruction.offset);
    auto* current_case_ptr = current_case.get();
    bool attached = false;

    if (previous_case == nullptr) {
        auto cases_stmt = std::make_unique<CasesStmtNode>(
            peeked_value ? std::move(peeked_value) : std::make_unique<ErrorNode>());
        cases_stmt->setBytecodeOffset(instruction.offset);
        cases_stmt->setFirstCase(std::move(current_case));
        block.addChild(std::move(cases_stmt));
        attached = true;
    } else if (previous_case->expect() == CaseNode::EXPECT_OR) {
        previous_case->setNextOr(std::move(current_case));
        attached = true;
    } else if (previous_case->expect() == CaseNode::EXPECT_NEXT) {
        previous_case->setNextCase(std::move(current_case));
        attached = true;
    }

    if (!attached) {
        add_error("ERROR: Unexpected case branch", current_index - index + 1);
        return;
    }

    block.current_case = current_case_ptr;
    if (expect != CaseNode::EXPECT_OR) {
        auto case_block = std::make_unique<BlockNode>();
        case_block->end_pos = jump_pos;
        auto* next_block = case_block.get();
        current_case_ptr->setBlock(std::move(case_block));
        enterBlock(*next_block);
    }

    last_consumed_ = current_index - index + 1;
}

void LingoDecompiler::translateObjCall(int bytecode_offset, int name_id, BlockNode& block) {
    const auto method = resolveName(name_id);
    auto arg_list = popNode();
    const auto& raw_args = arg_list->argNodes();
    const auto nargs = raw_args.size();

    NodePtr translation;

    if (method == "getAt" && nargs == 2) {
        auto args = takeArgNodes(std::move(arg_list));
        translation = std::make_unique<ObjBracketExprNode>(std::move(args[0]), std::move(args[1]));
    } else if (method == "setAt" && nargs == 3) {
        auto args = takeArgNodes(std::move(arg_list));
        auto prop_expr = std::make_unique<ObjBracketExprNode>(std::move(args[0]), std::move(args[1]));
        translation = std::make_unique<AssignmentStmtNode>(std::move(prop_expr), std::move(args[2]));
    } else if ((method == "getProp" || method == "getPropRef") &&
               (nargs == 3 || nargs == 4) &&
               raw_args[1]->valueType() == ValueType::Symbol) {
        const auto prop_name = raw_args[1]->stringValue();
        auto args = takeArgNodes(std::move(arg_list));
        NodePtr index2;
        if (nargs == 4) {
            index2 = std::move(args[3]);
        }
        translation = std::make_unique<ObjPropIndexExprNode>(
            std::move(args[0]),
            prop_name,
            std::move(args[2]),
            std::move(index2));
    } else if (method == "setProp" &&
               (nargs == 4 || nargs == 5) &&
               raw_args[1]->valueType() == ValueType::Symbol) {
        const auto prop_name = raw_args[1]->stringValue();
        auto args = takeArgNodes(std::move(arg_list));
        NodePtr index2;
        if (nargs == 5) {
            index2 = std::move(args[3]);
        }
        auto prop_expr = std::make_unique<ObjPropIndexExprNode>(
            std::move(args[0]),
            prop_name,
            std::move(args[2]),
            std::move(index2));
        translation = std::make_unique<AssignmentStmtNode>(std::move(prop_expr), std::move(args[nargs - 1]));
    } else if (method == "count" &&
               nargs == 2 &&
               raw_args[1]->valueType() == ValueType::Symbol) {
        const auto prop_name = raw_args[1]->stringValue();
        auto args = takeArgNodes(std::move(arg_list));
        auto prop_expr = std::make_unique<ObjPropExprNode>(std::move(args[0]), prop_name);
        translation = std::make_unique<ObjPropExprNode>(std::move(prop_expr), "count");
    } else if ((method == "setContents" || method == "setContentsAfter" || method == "setContentsBefore") &&
               nargs == 2) {
        const int put_type = method == "setContents" ? 1 : method == "setContentsAfter" ? 2 : 3;
        auto args = takeArgNodes(std::move(arg_list));
        translation = std::make_unique<PutStmtNode>(put_type, std::move(args[0]), std::move(args[1]));
    } else if (method == "hilite" && nargs == 1) {
        auto args = takeArgNodes(std::move(arg_list));
        translation = std::make_unique<ChunkHiliteStmtNode>(std::move(args[0]));
    } else if (method == "delete" && nargs == 1) {
        auto args = takeArgNodes(std::move(arg_list));
        translation = std::make_unique<ChunkDeleteStmtNode>(std::move(args[0]));
    } else {
        translation = std::make_unique<ObjCallNode>(method, std::move(arg_list));
    }

    translation->setBytecodeOffset(bytecode_offset);
    if (translation->isExpression()) {
        stack_.push_back(std::move(translation));
    } else {
        block.addChild(std::move(translation));
    }
}

void LingoDecompiler::enterBlock(BlockNode& block) {
    current_block_ = &block;
}

void LingoDecompiler::exitBlock() {
    if (current_block_ == nullptr) {
        return;
    }
    auto* ancestor_statement = current_block_->ancestorStatement();
    if (ancestor_statement == nullptr) {
        current_block_ = nullptr;
        return;
    }
    if (auto* parent_block = dynamic_cast<BlockNode*>(ancestor_statement->parent()); parent_block != nullptr) {
        current_block_ = parent_block;
    } else {
        current_block_ = nullptr;
    }
}

NodePtr LingoDecompiler::popNode() {
    if (stack_.empty()) {
        return std::make_unique<ErrorNode>();
    }
    auto node = std::move(stack_.back());
    stack_.pop_back();
    return node;
}

std::vector<NodePtr> LingoDecompiler::takeArgNodes(NodePtr arg_list) const {
    if (auto* literal = dynamic_cast<LiteralNode*>(arg_list.get()); literal != nullptr) {
        return literal->takeItems();
    }
    return {};
}

NodePtr LingoDecompiler::readVar(int var_type) {
    NodePtr cast_id;
    if (var_type == 0x6 && version_ >= 500) {
        cast_id = popNode();
    }
    auto id = popNode();

    switch (var_type) {
        case 0x1:
        case 0x2:
        case 0x3:
            return id;
        case 0x4:
            return std::make_unique<LiteralNode>(ValueType::VarRef, getArgumentName(id->intValue()));
        case 0x5:
            return std::make_unique<LiteralNode>(ValueType::VarRef, getLocalName(id->intValue()));
        case 0x6:
            return std::make_unique<MemberExprNode>("field", std::move(id), std::move(cast_id));
        default:
            return std::make_unique<ErrorNode>();
    }
}

NodePtr LingoDecompiler::readChunkRef(NodePtr string) {
    auto last_line = popNode();
    auto first_line = popNode();
    auto last_item = popNode();
    auto first_item = popNode();
    auto last_word = popNode();
    auto first_word = popNode();
    auto last_char = popNode();
    auto first_char = popNode();

    if (!isZeroLiteral(*first_line)) {
        string = std::make_unique<ChunkExprNode>(
            4,
            std::move(first_line),
            std::move(last_line),
            std::move(string));
    }
    if (!isZeroLiteral(*first_item)) {
        string = std::make_unique<ChunkExprNode>(
            3,
            std::move(first_item),
            std::move(last_item),
            std::move(string));
    }
    if (!isZeroLiteral(*first_word)) {
        string = std::make_unique<ChunkExprNode>(
            2,
            std::move(first_word),
            std::move(last_word),
            std::move(string));
    }
    if (!isZeroLiteral(*first_char)) {
        string = std::make_unique<ChunkExprNode>(
            1,
            std::move(first_char),
            std::move(last_char),
            std::move(string));
    }

    return string;
}

NodePtr LingoDecompiler::readV4Property(int property_type, int property_id) {
    switch (property_type) {
        case 0x00:
            if (property_id <= 0x0b) {
                return std::make_unique<TheExprNode>(std::string(moviePropertyName(property_id)));
            }
            return std::make_unique<LastStringChunkExprNode>(property_id - 0x0b, popNode());

        case 0x01:
            return std::make_unique<StringChunkCountExprNode>(property_id, popNode());

        case 0x02:
            return std::make_unique<MenuPropExprNode>(popNode(), property_id);

        case 0x03: {
            auto menu_id = popNode();
            return std::make_unique<MenuItemPropExprNode>(std::move(menu_id), popNode(), property_id);
        }

        case 0x04:
            return std::make_unique<SoundPropExprNode>(popNode(), property_id);

        case 0x05:
            return std::make_unique<CommentNode>("ERROR: Resource property");

        case 0x06:
            return std::make_unique<SpritePropExprNode>(popNode(), property_id);

        case 0x07:
            return std::make_unique<TheExprNode>(std::string(animationPropertyName(property_id)));

        case 0x08:
            if (property_id == 0x02 && version_ >= 500) {
                auto cast_lib = popNode();
                if (!isZeroLiteral(*cast_lib)) {
                    return std::make_unique<ThePropExprNode>(
                        std::make_unique<MemberExprNode>("castLib", std::move(cast_lib)),
                        std::string(animation2PropertyName(property_id)));
                }
            }
            return std::make_unique<TheExprNode>(std::string(animation2PropertyName(property_id)));

        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15: {
            NodePtr cast_id;
            if (version_ >= 500) {
                cast_id = popNode();
            }
            auto member_id = popNode();
            std::string prefix;
            if (property_type == 0x0b || property_type == 0x0c) {
                prefix = "field";
            } else if (property_type == 0x14 || property_type == 0x15) {
                prefix = "script";
            } else {
                prefix = version_ >= 500 ? "member" : "cast";
            }

            NodePtr entity = std::make_unique<MemberExprNode>(std::move(prefix), std::move(member_id), std::move(cast_id));
            if (property_type == 0x0a || property_type == 0x0c || property_type == 0x15) {
                entity = readChunkRef(std::move(entity));
            }
            return std::make_unique<ThePropExprNode>(std::move(entity), std::string(memberPropertyName(property_id)));
        }

        default:
            return std::make_unique<CommentNode>("ERROR: Unknown property type " + std::to_string(property_type));
    }
}

int LingoDecompiler::variableMultiplier() const {
    if (capital_x_) {
        return 1;
    }
    if (version_ >= 500) {
        return 8;
    }
    return 6;
}

std::string LingoDecompiler::getArgumentName(int raw_index) const {
    const int index = raw_index / variableMultiplier();
    if (current_handler_ != nullptr &&
        index >= 0 &&
        index < static_cast<int>(current_handler_->arg_name_ids.size())) {
        return resolveName(current_handler_->arg_name_ids[static_cast<std::size_t>(index)]);
    }
    return "UNKNOWN_ARG_" + std::to_string(index);
}

std::string LingoDecompiler::getLocalName(int raw_index) const {
    const int index = raw_index / variableMultiplier();
    if (current_handler_ != nullptr &&
        index >= 0 &&
        index < static_cast<int>(current_handler_->local_name_ids.size())) {
        return resolveName(current_handler_->local_name_ids[static_cast<std::size_t>(index)]);
    }
    return "UNKNOWN_LOCAL_" + std::to_string(index);
}

NodePtr LingoDecompiler::literalToNode(const chunks::ScriptChunk::LiteralEntry& literal) const {
    switch (literal.type) {
        case 1:
            return std::make_unique<LiteralNode>(ValueType::String, rawLiteralString(literal.value));
        case 4:
            if (std::holds_alternative<int>(literal.value)) {
                return std::make_unique<LiteralNode>(std::get<int>(literal.value));
            }
            return std::make_unique<LiteralNode>(0);
        case 9:
            return std::make_unique<LiteralNode>(literal.numeric_value);
        default:
            return std::make_unique<LiteralNode>(ValueType::String, rawLiteralString(literal.value));
    }
}

bool LingoDecompiler::isZeroLiteral(const LingoNode& node) {
    return node.valueType() == ValueType::Int && node.intValue() == 0;
}

} // namespace libreshockwave::lingo::decompiler
