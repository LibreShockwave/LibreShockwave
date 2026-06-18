#include "libreshockwave/lingo/decompiler/LingoDecompiler.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string_view>
#include <variant>

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/chunks/ScriptNamesChunk.hpp"
#include "libreshockwave/format/ScriptFormatUtils.hpp"
#include "libreshockwave/lingo/Opcode.hpp"
#include "libreshockwave/lingo/decompiler/LingoProperties.hpp"

namespace libreshockwave::lingo::decompiler {
namespace {

constexpr int tagNone = 0;
constexpr int tagSkip = 1;
constexpr int tagRepeatWhile = 2;
constexpr int tagRepeatWithIn = 3;
constexpr int tagRepeatWithTo = 4;
constexpr int tagRepeatWithDownTo = 5;
constexpr int tagNextRepeatTarget = 6;

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
               int indentLevel,
               bool dotSyntax);

void emitCaseNode(const CaseNode& caseNode,
                  std::vector<LingoDecompiler::DecompiledLine>& lines,
                  int indentLevel,
                  bool dotSyntax) {
    const auto prefix = indent(indentLevel);
    std::string caseLine = caseNode.value().toLingo(dotSyntax);
    const auto* orCase = caseNode.nextOr();
    while (orCase != nullptr) {
        caseLine.append(", ");
        caseLine.append(orCase->value().toLingo(dotSyntax));
        orCase = orCase->nextOr();
    }
    caseLine.push_back(':');
    lines.push_back(LingoDecompiler::DecompiledLine{prefix + caseLine, caseNode.bytecodeOffset()});

    const auto* branchCase = &caseNode;
    while (branchCase->nextOr() != nullptr) {
        branchCase = branchCase->nextOr();
    }
    if (branchCase->block() != nullptr) {
        emitBlock(*branchCase->block(), lines, indentLevel + 1, dotSyntax);
    }
    if (branchCase->nextCase() != nullptr) {
        emitCaseNode(*branchCase->nextCase(), lines, indentLevel, dotSyntax);
    }
    if (branchCase->otherwise() != nullptr) {
        lines.push_back(LingoDecompiler::DecompiledLine{prefix + "otherwise:", -1});
        emitBlock(*branchCase->otherwise(), lines, indentLevel + 1, dotSyntax);
    }
}

void emitSimpleLines(const LingoNode& node,
                     std::vector<LingoDecompiler::DecompiledLine>& lines,
                     std::string_view prefix,
                     bool dotSyntax) {
    auto text = node.toLingo(dotSyntax);
    int offset = node.bytecodeOffset();
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        const auto lineEnd = end == std::string::npos ? text.size() : end;
        lines.push_back(LingoDecompiler::DecompiledLine{
            std::string(prefix) + text.substr(start, lineEnd - start),
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
              int indentLevel,
              bool dotSyntax) {
    const auto prefix = indent(indentLevel);
    const int offset = node.bytecodeOffset();

    if (const auto* ifStmt = dynamic_cast<const IfStmtNode*>(&node); ifStmt != nullptr) {
        lines.push_back(LingoDecompiler::DecompiledLine{
            prefix + "if " + ifStmt->condition().toLingo(dotSyntax) + " then",
            offset
        });
        emitBlock(ifStmt->trueBlock(), lines, indentLevel + 1, dotSyntax);
        if (ifStmt->hasElse()) {
            lines.push_back(LingoDecompiler::DecompiledLine{prefix + "else", -1});
            emitBlock(ifStmt->falseBlock(), lines, indentLevel + 1, dotSyntax);
        }
        lines.push_back(LingoDecompiler::DecompiledLine{prefix + "end if", -1});
    } else if (const auto* repeatWhile = dynamic_cast<const RepeatWhileStmtNode*>(&node);
               repeatWhile != nullptr) {
        lines.push_back(LingoDecompiler::DecompiledLine{
            prefix + "repeat while " + repeatWhile->condition().toLingo(dotSyntax),
            offset
        });
        emitBlock(repeatWhile->block(), lines, indentLevel + 1, dotSyntax);
        lines.push_back(LingoDecompiler::DecompiledLine{prefix + "end repeat", -1});
    } else if (const auto* repeatIn = dynamic_cast<const RepeatWithInStmtNode*>(&node);
               repeatIn != nullptr) {
        lines.push_back(LingoDecompiler::DecompiledLine{
            prefix + "repeat with " + repeatIn->variableName() + " in " + repeatIn->list().toLingo(dotSyntax),
            offset
        });
        emitBlock(repeatIn->block(), lines, indentLevel + 1, dotSyntax);
        lines.push_back(LingoDecompiler::DecompiledLine{prefix + "end repeat", -1});
    } else if (const auto* repeatTo = dynamic_cast<const RepeatWithToStmtNode*>(&node);
               repeatTo != nullptr) {
        const std::string direction = repeatTo->up() ? " to " : " down to ";
        lines.push_back(LingoDecompiler::DecompiledLine{
            prefix + "repeat with " + repeatTo->variableName() + " = " +
                repeatTo->start().toLingo(dotSyntax) + direction + repeatTo->end().toLingo(dotSyntax),
            offset
        });
        emitBlock(repeatTo->block(), lines, indentLevel + 1, dotSyntax);
        lines.push_back(LingoDecompiler::DecompiledLine{prefix + "end repeat", -1});
    } else if (const auto* tell = dynamic_cast<const TellStmtNode*>(&node); tell != nullptr) {
        lines.push_back(LingoDecompiler::DecompiledLine{
            prefix + "tell " + tell->window().toLingo(dotSyntax),
            offset
        });
        emitBlock(tell->block(), lines, indentLevel + 1, dotSyntax);
        lines.push_back(LingoDecompiler::DecompiledLine{prefix + "end tell", -1});
    } else if (const auto* cases = dynamic_cast<const CasesStmtNode*>(&node); cases != nullptr) {
        lines.push_back(LingoDecompiler::DecompiledLine{
            prefix + "case " + cases->value().toLingo(dotSyntax) + " of",
            offset
        });
        if (cases->firstCase() != nullptr) {
            emitCaseNode(*cases->firstCase(), lines, indentLevel + 1, dotSyntax);
        }
        lines.push_back(LingoDecompiler::DecompiledLine{prefix + "end case", -1});
    } else {
        emitSimpleLines(node, lines, prefix, dotSyntax);
    }
}

void emitBlock(const BlockNode& block,
               std::vector<LingoDecompiler::DecompiledLine>& lines,
               int indentLevel,
               bool dotSyntax) {
    for (const auto& child : block.children()) {
        if (child) {
            emitNode(*child, lines, indentLevel, dotSyntax);
        }
    }
}

bool setOpcodeToGetOpcode(Opcode setOp, Opcode& getOp) {
    switch (setOp) {
        case Opcode::SET_GLOBAL:
            getOp = Opcode::GET_GLOBAL;
            return true;
        case Opcode::SET_GLOBAL2:
            getOp = Opcode::GET_GLOBAL2;
            return true;
        case Opcode::SET_PROP:
            getOp = Opcode::GET_PROP;
            return true;
        case Opcode::SET_PARAM:
            getOp = Opcode::GET_PARAM;
            return true;
        case Opcode::SET_LOCAL:
            getOp = Opcode::GET_LOCAL;
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
        result.append(resolveName(property.nameId));
        result.push_back('\n');
    }

    for (const auto& global : script.globals()) {
        result.append("global ");
        result.append(resolveName(global.nameId));
        result.push_back('\n');
    }

    const bool hasDeclarations = !script.properties().empty() || !script.globals().empty();
    if (hasDeclarations && !script.handlers().empty()) {
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
    return translateHandler(handler)->toLingo(dotSyntax_);
}

LingoDecompiler::DecompiledHandler LingoDecompiler::decompileHandlerWithMapping(
    const chunks::ScriptChunk::Handler& handler,
    const chunks::ScriptChunk& script,
    const chunks::ScriptNamesChunk* names) {
    script_ = &script;
    names_ = names;
    initFileInfo(script);
    return buildLineMapping(*translateHandler(handler), dotSyntax_);
}

std::string LingoDecompiler::formatHandlerBytecodeOnly(const chunks::ScriptChunk::Handler& handler,
                                                       const chunks::ScriptNamesChunk* names) const {
    const auto resolve = [names](int nameId) {
        if (names != nullptr && nameId >= 0 && nameId < static_cast<int>(names->names().size())) {
            return names->getName(nameId);
        }
        return "#" + std::to_string(nameId);
    };

    std::ostringstream out;
    out << "on " << resolve(handler.nameId) << "\n";
    for (const auto& instruction : handler.instructions) {
        out << "  [" << std::setfill('0') << std::setw(4) << instruction.offset << std::setfill(' ') << "] ";
        out << std::left << std::setw(16) << std::string(lingo::mnemonic(instruction.opcode)) << std::right;
        if (instruction.rawOpcode >= 0x40) {
            out << " " << instruction.argument;
        }
        out << "\n";
    }
    out << "end\n";
    return out.str();
}

LingoDecompiler::DecompiledHandler LingoDecompiler::buildLineMapping(const HandlerNode& handler, bool dotSyntax) {
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

    emitBlock(handler.block(), result.lines, 1, dotSyntax);
    result.lines.push_back(DecompiledLine{"end", -1});
    return result;
}

std::string LingoDecompiler::resolveName(int nameId) const {
    if (names_ != nullptr && nameId >= 0 && nameId < static_cast<int>(names_->names().size())) {
        return names_->getName(nameId);
    }
    return "#" + std::to_string(nameId);
}

void LingoDecompiler::initFileInfo(const chunks::ScriptChunk& script) {
    version_ = 0x4C1;
    capitalX_ = false;
    if (script.file() != nullptr) {
        version_ = script.file()->version();
        capitalX_ = script.file()->isCapitalX();
    }
    dotSyntax_ = version_ >= 700;
}

void LingoDecompiler::tagLoops() {
    tags_.assign(currentHandler_ != nullptr ? currentHandler_->instructions.size() : 0, tagNone);
    ownerLoops_.assign(tags_.size(), -1);
    if (currentHandler_ == nullptr) {
        return;
    }

    const auto& instructions = currentHandler_->instructions;
    for (std::size_t startIndex = 0; startIndex < instructions.size(); ++startIndex) {
        const auto& jmpIfZero = instructions[startIndex];
        if (jmpIfZero.opcode != Opcode::JMP_IF_Z) {
            continue;
        }

        const int endPos = jmpIfZero.offset + jmpIfZero.argument;
        const int endIndex = instructionIndexForOffset(endPos);
        if (endIndex < 1 || endIndex > static_cast<int>(instructions.size())) {
            continue;
        }

        const auto& endRepeat = instructions[static_cast<std::size_t>(endIndex - 1)];
        if (endRepeat.opcode != Opcode::END_REPEAT) {
            continue;
        }
        if ((endRepeat.offset - endRepeat.argument) > jmpIfZero.offset) {
            continue;
        }
        if (isRepeatWithInLoop(startIndex, endIndex)) {
            tags_[startIndex] = tagRepeatWithIn;
            for (std::size_t index = startIndex - 7; index <= startIndex - 1; ++index) {
                tags_[index] = tagSkip;
            }
            for (std::size_t index = startIndex + 1; index <= startIndex + 5; ++index) {
                tags_[index] = tagSkip;
            }
            tags_[static_cast<std::size_t>(endIndex - 3)] = tagNextRepeatTarget;
            ownerLoops_[static_cast<std::size_t>(endIndex - 3)] = static_cast<int>(startIndex);
            tags_[static_cast<std::size_t>(endIndex - 2)] = tagSkip;
            tags_[static_cast<std::size_t>(endIndex - 1)] = tagSkip;
            ownerLoops_[static_cast<std::size_t>(endIndex - 1)] = static_cast<int>(startIndex);
            if (endIndex < static_cast<int>(tags_.size())) {
                tags_[static_cast<std::size_t>(endIndex)] = tagSkip;
            }
            continue;
        }
        if (isRepeatWithToLoop(startIndex, endIndex)) {
            const auto& endRepeat = instructions[static_cast<std::size_t>(endIndex - 1)];
            const int condStart = instructionIndexForOffset(endRepeat.offset - endRepeat.argument);
            if (condStart < 1) {
                continue;
            }

            tags_[startIndex] = instructions[startIndex - 1].opcode == Opcode::LT_EQ
                ? tagRepeatWithTo
                : tagRepeatWithDownTo;
            tags_[static_cast<std::size_t>(condStart - 1)] = tagSkip;
            tags_[static_cast<std::size_t>(condStart)] = tagSkip;
            tags_[startIndex - 1] = tagSkip;
            tags_[static_cast<std::size_t>(endIndex - 5)] = tagNextRepeatTarget;
            ownerLoops_[static_cast<std::size_t>(endIndex - 5)] = static_cast<int>(startIndex);
            tags_[static_cast<std::size_t>(endIndex - 4)] = tagSkip;
            tags_[static_cast<std::size_t>(endIndex - 3)] = tagSkip;
            tags_[static_cast<std::size_t>(endIndex - 2)] = tagSkip;
            tags_[static_cast<std::size_t>(endIndex - 1)] = tagSkip;
            ownerLoops_[static_cast<std::size_t>(endIndex - 1)] = static_cast<int>(startIndex);
            continue;
        }

        tags_[startIndex] = tagRepeatWhile;
        tags_[static_cast<std::size_t>(endIndex - 1)] = tagNextRepeatTarget;
        ownerLoops_[static_cast<std::size_t>(endIndex - 1)] = static_cast<int>(startIndex);
    }
}

bool LingoDecompiler::isRepeatWithInLoop(std::size_t startIndex, int endIndex) const {
    if (currentHandler_ == nullptr) {
        return false;
    }

    const auto& instructions = currentHandler_->instructions;
    if (startIndex < 7 || startIndex + 5 >= instructions.size() || endIndex < 3 ||
        endIndex >= static_cast<int>(instructions.size())) {
        return false;
    }

    const auto at = [&instructions](std::size_t index) -> const chunks::ScriptChunk::Instruction& {
        return instructions[index];
    };

    return at(startIndex - 7).opcode == Opcode::PEEK &&
           at(startIndex - 7).argument == 0 &&
           at(startIndex - 6).opcode == Opcode::PUSH_ARG_LIST &&
           at(startIndex - 6).argument == 1 &&
           at(startIndex - 5).opcode == Opcode::EXT_CALL &&
           resolveName(at(startIndex - 5).argument) == "count" &&
           at(startIndex - 4).opcode == Opcode::PUSH_INT8 &&
           at(startIndex - 4).argument == 1 &&
           at(startIndex - 3).opcode == Opcode::PEEK &&
           at(startIndex - 3).argument == 0 &&
           at(startIndex - 2).opcode == Opcode::PEEK &&
           at(startIndex - 2).argument == 2 &&
           at(startIndex - 1).opcode == Opcode::LT_EQ &&
           at(startIndex + 1).opcode == Opcode::PEEK &&
           at(startIndex + 1).argument == 2 &&
           at(startIndex + 2).opcode == Opcode::PEEK &&
           at(startIndex + 2).argument == 1 &&
           at(startIndex + 3).opcode == Opcode::PUSH_ARG_LIST &&
           at(startIndex + 3).argument == 2 &&
           at(startIndex + 4).opcode == Opcode::EXT_CALL &&
           resolveName(at(startIndex + 4).argument) == "getAt" &&
           (at(startIndex + 5).opcode == Opcode::SET_GLOBAL ||
            at(startIndex + 5).opcode == Opcode::SET_PROP ||
            at(startIndex + 5).opcode == Opcode::SET_PARAM ||
            at(startIndex + 5).opcode == Opcode::SET_LOCAL) &&
           at(static_cast<std::size_t>(endIndex - 3)).opcode == Opcode::PUSH_INT8 &&
           at(static_cast<std::size_t>(endIndex - 3)).argument == 1 &&
           at(static_cast<std::size_t>(endIndex - 2)).opcode == Opcode::ADD &&
           at(static_cast<std::size_t>(endIndex)).opcode == Opcode::POP &&
           at(static_cast<std::size_t>(endIndex)).argument == 3;
}

bool LingoDecompiler::isRepeatWithToLoop(std::size_t startIndex, int endIndex) const {
    if (currentHandler_ == nullptr || startIndex < 1 || endIndex < 5) {
        return false;
    }

    const auto& instructions = currentHandler_->instructions;
    const auto comparisonOp = instructions[startIndex - 1].opcode;
    if (comparisonOp != Opcode::LT_EQ && comparisonOp != Opcode::GT_EQ) {
        return false;
    }

    const auto& endRepeat = instructions[static_cast<std::size_t>(endIndex - 1)];
    const int condStart = instructionIndexForOffset(endRepeat.offset - endRepeat.argument);
    if (condStart < 1 || condStart >= static_cast<int>(instructions.size())) {
        return false;
    }

    const auto setOp = instructions[static_cast<std::size_t>(condStart - 1)].opcode;
    Opcode getOp = Opcode::PUSH_ZERO;
    if (!setOpcodeToGetOpcode(setOp, getOp)) {
        return false;
    }

    const int varId = instructions[static_cast<std::size_t>(condStart - 1)].argument;
    const int expectedIncrement = comparisonOp == Opcode::LT_EQ ? 1 : -1;
    return instructions[static_cast<std::size_t>(condStart)].opcode == getOp &&
           instructions[static_cast<std::size_t>(condStart)].argument == varId &&
           instructions[static_cast<std::size_t>(endIndex - 5)].opcode == Opcode::PUSH_INT8 &&
           instructions[static_cast<std::size_t>(endIndex - 5)].argument == expectedIncrement &&
           instructions[static_cast<std::size_t>(endIndex - 4)].opcode == getOp &&
           instructions[static_cast<std::size_t>(endIndex - 4)].argument == varId &&
           instructions[static_cast<std::size_t>(endIndex - 3)].opcode == Opcode::ADD &&
           instructions[static_cast<std::size_t>(endIndex - 2)].opcode == setOp &&
           instructions[static_cast<std::size_t>(endIndex - 2)].argument == varId;
}

int LingoDecompiler::instructionIndexForOffset(int offset) const {
    if (currentHandler_ == nullptr) {
        return -1;
    }
    if (const int mappedIndex = currentHandler_->getInstructionIndex(offset); mappedIndex >= 0) {
        return mappedIndex;
    }
    const auto& instructions = currentHandler_->instructions;
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
    currentHandler_ = &handler;
    tagLoops();
    stack_.clear();
    currentBlock_ = nullptr;

    std::vector<std::string> argumentNames;
    argumentNames.reserve(handler.argNameIds.size());
    for (const auto nameId : handler.argNameIds) {
        argumentNames.push_back(resolveName(nameId));
    }

    auto root = std::make_unique<HandlerNode>(resolveName(handler.nameId), std::move(argumentNames), std::vector<std::string>{});
    currentBlock_ = &root->block();
    for (std::size_t index = 0; index < handler.instructions.size();) {
        const auto& instruction = handler.instructions[index];
        while (currentBlock_ != nullptr && instruction.offset == currentBlock_->endPos) {
            auto* exitedBlock = currentBlock_;
            auto* ancestorStatement = currentBlock_->ancestorStatement();
            exitBlock();
            if (auto* ifStmt = dynamic_cast<IfStmtNode*>(ancestorStatement);
                ifStmt != nullptr && ifStmt->hasElse() && exitedBlock == &ifStmt->trueBlock()) {
                enterBlock(ifStmt->falseBlock());
            } else if (auto* casesStmt = dynamic_cast<CasesStmtNode*>(ancestorStatement);
                       casesStmt != nullptr && currentBlock_ != nullptr) {
                auto* caseNode = currentBlock_->currentCase;
                if (caseNode != nullptr) {
                    if (caseNode->expect() == CaseNode::EXPECT_OTHERWISE) {
                        if (exitedBlock == caseNode->block()) {
                            auto otherwise = std::make_unique<BlockNode>();
                            otherwise->endPos = casesStmt->endPos;
                            auto* otherwiseBlock = otherwise.get();
                            caseNode->setOtherwise(std::move(otherwise));
                            enterBlock(*otherwiseBlock);
                        } else {
                            currentBlock_->currentCase = nullptr;
                        }
                    } else if (caseNode->expect() == CaseNode::EXPECT_POP) {
                        currentBlock_->currentCase = nullptr;
                    }
                }
            }
        }
        if (currentBlock_ == nullptr) {
            currentBlock_ = &root->block();
        }
        lastConsumed_ = 1;
        translateInstruction(instruction, index, *currentBlock_);
        index += std::max<std::size_t>(lastConsumed_, 1);
    }
    currentBlock_ = nullptr;
    return root;
}

void LingoDecompiler::translateInstruction(const chunks::ScriptChunk::Instruction& instruction,
                                           std::size_t index,
                                           BlockNode& block) {
    if (index < tags_.size() && (tags_[index] == tagSkip || tags_[index] == tagNextRepeatTarget)) {
        return;
    }

    NodePtr translation;

    switch (instruction.opcode) {
        case Opcode::RET:
        case Opcode::RET_FACTORY:
            if (script_ != nullptr && currentHandler_ != nullptr && index + 1 == currentHandler_->instructions.size()) {
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
            NodePtr castId;
            if (version_ >= 500) {
                castId = popNode();
            }
            auto fieldId = popNode();
            auto field = std::make_unique<MemberExprNode>("field", std::move(fieldId), std::move(castId));
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
            NodePtr castId;
            if (version_ >= 500) {
                castId = popNode();
            }
            translation = std::make_unique<MemberExprNode>("field", popNode(), std::move(castId));
            break;
        }

        case Opcode::START_TELL: {
            auto tell = std::make_unique<TellStmtNode>(popNode());
            tell->setBytecodeOffset(instruction.offset);
            auto* nextBlock = &tell->block();
            block.addChild(std::move(tell));
            enterBlock(*nextBlock);
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
            for (int argIndex = 0; argIndex < count; ++argIndex) {
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
            const int literalId = instruction.argument / variableMultiplier();
            if (script_ != nullptr && literalId >= 0 && literalId < static_cast<int>(script_->literals().size())) {
                translation = literalToNode(script_->literals()[static_cast<std::size_t>(literalId)]);
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
            const int putType = (instruction.argument >> 4) & 0xF;
            const int varType = instruction.argument & 0xF;
            auto variable = readVar(varType);
            auto value = popNode();
            translation = std::make_unique<PutStmtNode>(putType, std::move(variable), std::move(value));
            break;
        }

        case Opcode::PUT_CHUNK: {
            const int putType = (instruction.argument >> 4) & 0xF;
            const int varType = instruction.argument & 0xF;
            auto chunk = readChunkRef(readVar(varType));
            auto value = popNode();
            translation = std::make_unique<PutStmtNode>(putType, std::move(chunk), std::move(value));
            break;
        }

        case Opcode::DELETE_CHUNK:
            translation = std::make_unique<ChunkDeleteStmtNode>(readChunkRef(readVar(instruction.argument)));
            break;

        case Opcode::GET: {
            const auto propId = popNode()->intValue();
            translation = readV4Property(instruction.argument, propId);
            break;
        }

        case Opcode::SET: {
            const auto propId = popNode()->intValue();
            auto value = popNode();
            if (instruction.argument == 0x00 &&
                propId >= 0x01 &&
                propId <= 0x05 &&
                value->valueType() == ValueType::String) {
                auto script = value->stringValue();
                if (!script.empty() && (script.front() == ' ' || script.find('\r') != std::string::npos)) {
                    translation = std::make_unique<WhenStmtNode>(propId, std::move(script));
                }
            }
            if (!translation) {
                auto property = readV4Property(instruction.argument, propId);
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
            auto argList = popNode();
            std::string callName = "handler#" + std::to_string(instruction.argument);
            if (script_ != nullptr &&
                instruction.argument >= 0 &&
                instruction.argument < static_cast<int>(script_->handlers().size())) {
                callName = resolveName(script_->handlers()[static_cast<std::size_t>(instruction.argument)].nameId);
            }
            translation = std::make_unique<CallNode>(std::move(callName), std::move(argList));
            break;
        }

        case Opcode::EXT_CALL:
        case Opcode::TELL_CALL:
            translation = std::make_unique<CallNode>(resolveName(instruction.argument), popNode());
            break;

        case Opcode::OBJ_CALL_V4: {
            auto object = readVar(instruction.argument);
            auto argList = popNode();
            const auto argListType = argList->valueType() == ValueType::ArgListNoRet
                ? ValueType::ArgListNoRet
                : ValueType::ArgList;
            auto args = takeArgNodes(std::move(argList));
            if (!args.empty() && args[0]->valueType() == ValueType::Symbol) {
                args[0] = std::make_unique<VarNode>(args[0]->stringValue());
            }
            translation = std::make_unique<ObjCallV4Node>(
                std::move(object),
                std::make_unique<LiteralNode>(argListType, std::move(args)));
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
            const int targetOffset = instruction.offset + instruction.argument;
            const int targetIndex = currentHandler_ != nullptr
                ? instructionIndexForOffset(targetOffset)
                : -1;
            if (targetIndex >= 0 && currentBlock_ != nullptr && currentHandler_ != nullptr) {
                if (auto* ancestorLoop = currentBlock_->ancestorLoop(); ancestorLoop != nullptr) {
                    if (targetIndex >= 1 &&
                        currentHandler_->instructions[static_cast<std::size_t>(targetIndex - 1)].opcode == Opcode::END_REPEAT &&
                        static_cast<std::size_t>(targetIndex - 1) < ownerLoops_.size() &&
                        ownerLoops_[static_cast<std::size_t>(targetIndex - 1)] == ancestorLoop->startIndex()) {
                        translation = std::make_unique<ExitRepeatStmtNode>();
                        break;
                    }
                    if (static_cast<std::size_t>(targetIndex) < tags_.size() &&
                        tags_[static_cast<std::size_t>(targetIndex)] == tagNextRepeatTarget &&
                        ownerLoops_[static_cast<std::size_t>(targetIndex)] == ancestorLoop->startIndex()) {
                        translation = std::make_unique<NextRepeatStmtNode>();
                        break;
                    }
                }

                if (index + 1 < currentHandler_->instructions.size() &&
                    currentHandler_->instructions[index + 1].offset == currentBlock_->endPos) {
                    auto* ancestorStatement = currentBlock_->ancestorStatement();
                    if (auto* ifStmt = dynamic_cast<IfStmtNode*>(ancestorStatement);
                        ifStmt != nullptr && currentBlock_ == &ifStmt->trueBlock()) {
                        ifStmt->setHasElse(true);
                        ifStmt->falseBlock().endPos = targetOffset;
                        return;
                    }
                    if (auto* casesStmt = dynamic_cast<CasesStmtNode*>(ancestorStatement); casesStmt != nullptr) {
                        casesStmt->endPos = targetOffset;
                        return;
                    }
                }
            }
            translation = std::make_unique<CommentNode>("ERROR: Could not identify jmp");
            break;
        }

        case Opcode::JMP_IF_Z: {
            const int endPos = instruction.offset + instruction.argument;
            if (index < tags_.size() && tags_[index] == tagRepeatWhile) {
                auto loop = std::make_unique<RepeatWhileStmtNode>(static_cast<int>(index), popNode());
                loop->setBytecodeOffset(instruction.offset);
                loop->block().endPos = endPos;
                auto* nextBlock = &loop->block();
                block.addChild(std::move(loop));
                enterBlock(*nextBlock);
                return;
            }
            if (index < tags_.size() && tags_[index] == tagRepeatWithIn) {
                auto loop = std::make_unique<RepeatWithInStmtNode>(
                    static_cast<int>(index),
                    getVarNameFromSet(currentHandler_->instructions[index + 5]),
                    popNode());
                loop->setBytecodeOffset(instruction.offset);
                loop->block().endPos = endPos;
                auto* nextBlock = &loop->block();
                block.addChild(std::move(loop));
                enterBlock(*nextBlock);
                return;
            }
            if (index < tags_.size() &&
                (tags_[index] == tagRepeatWithTo || tags_[index] == tagRepeatWithDownTo)) {
                auto end = popNode();
                auto start = popNode();
                const int endIndex = instructionIndexForOffset(endPos);
                if (endIndex >= 1 && currentHandler_ != nullptr) {
                    const auto& endRepeat = currentHandler_->instructions[static_cast<std::size_t>(endIndex - 1)];
                    const int condStart = instructionIndexForOffset(endRepeat.offset - endRepeat.argument);
                    if (condStart >= 1) {
                        auto loop = std::make_unique<RepeatWithToStmtNode>(
                            static_cast<int>(index),
                            getVarNameFromSet(currentHandler_->instructions[static_cast<std::size_t>(condStart - 1)]),
                            std::move(start),
                            tags_[index] == tagRepeatWithTo,
                            std::move(end));
                        loop->setBytecodeOffset(instruction.offset);
                        loop->block().endPos = endPos;
                        auto* nextBlock = &loop->block();
                        block.addChild(std::move(loop));
                        enterBlock(*nextBlock);
                        return;
                    }
                }
                translation = std::make_unique<CommentNode>("ERROR: Could not identify repeat with to");
                break;
            }

            auto ifStmt = std::make_unique<IfStmtNode>(popNode());
            ifStmt->setBytecodeOffset(instruction.offset);
            ifStmt->trueBlock().endPos = endPos;
            auto* nextBlock = &ifStmt->trueBlock();
            block.addChild(std::move(ifStmt));
            enterBlock(*nextBlock);
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
            if (instruction.rawOpcode >= 0x40) {
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
    const auto addError = [&](std::string text, std::size_t consumed) {
        auto error = std::make_unique<CommentNode>(std::move(text));
        error->setBytecodeOffset(instruction.offset);
        block.addChild(std::move(error));
        lastConsumed_ = std::max<std::size_t>(consumed, 1);
    };

    if (currentHandler_ == nullptr) {
        addError("ERROR: Missing handler for case translation", 1);
        return;
    }

    const auto& instructions = currentHandler_->instructions;
    auto* previousCase = block.currentCase;
    NodePtr peekedValue;
    if (previousCase == nullptr) {
        peekedValue = popNode();
    }

    const auto originalStackSize = stack_.size();
    std::size_t currentIndex = index + 1;
    while (currentIndex < instructions.size() && instructions[currentIndex].opcode == Opcode::PEEK) {
        ++currentIndex;
    }
    while (currentIndex < instructions.size()) {
        translateInstruction(instructions[currentIndex], currentIndex, block);
        ++currentIndex;
        if (currentIndex < instructions.size() &&
            stack_.size() == originalStackSize + 1 &&
            (instructions[currentIndex].opcode == Opcode::EQ ||
             instructions[currentIndex].opcode == Opcode::NT_EQ)) {
            break;
        }
    }
    if (currentIndex >= instructions.size()) {
        addError("ERROR: Expected eq or nteq", currentIndex - index + 1);
        return;
    }

    auto caseValue = popNode();

    if (currentIndex >= instructions.size() ||
        (instructions[currentIndex].opcode != Opcode::EQ && instructions[currentIndex].opcode != Opcode::NT_EQ)) {
        addError("ERROR: Expected eq or nteq", currentIndex - index + 1);
        return;
    }
    const bool notEqual = instructions[currentIndex].opcode == Opcode::NT_EQ;
    ++currentIndex;

    if (currentIndex >= instructions.size() || instructions[currentIndex].opcode != Opcode::JMP_IF_Z) {
        addError("ERROR: Expected jmpifz", currentIndex - index + 1);
        return;
    }

    const auto& jump = instructions[currentIndex];
    const int jumpPos = jump.offset + jump.argument;
    const int targetIndex = instructionIndexForOffset(jumpPos);

    int expect = CaseNode::EXPECT_OTHERWISE;
    if (notEqual) {
        expect = CaseNode::EXPECT_OR;
    } else if (targetIndex >= 0 &&
               targetIndex < static_cast<int>(instructions.size()) &&
               instructions[static_cast<std::size_t>(targetIndex)].opcode == Opcode::PEEK) {
        expect = CaseNode::EXPECT_NEXT;
    } else if (targetIndex >= 0 &&
               targetIndex < static_cast<int>(instructions.size()) &&
               instructions[static_cast<std::size_t>(targetIndex)].opcode == Opcode::POP) {
        expect = CaseNode::EXPECT_POP;
    }

    auto currentCase = std::make_unique<CaseNode>(std::move(caseValue), expect);
    currentCase->setBytecodeOffset(instruction.offset);
    auto* currentCasePtr = currentCase.get();
    bool attached = false;

    if (previousCase == nullptr) {
        auto casesStmt = std::make_unique<CasesStmtNode>(
            peekedValue ? std::move(peekedValue) : std::make_unique<ErrorNode>());
        casesStmt->setBytecodeOffset(instruction.offset);
        casesStmt->setFirstCase(std::move(currentCase));
        block.addChild(std::move(casesStmt));
        attached = true;
    } else if (previousCase->expect() == CaseNode::EXPECT_OR) {
        previousCase->setNextOr(std::move(currentCase));
        attached = true;
    } else if (previousCase->expect() == CaseNode::EXPECT_NEXT) {
        previousCase->setNextCase(std::move(currentCase));
        attached = true;
    }

    if (!attached) {
        addError("ERROR: Unexpected case branch", currentIndex - index + 1);
        return;
    }

    block.currentCase = currentCasePtr;
    if (expect != CaseNode::EXPECT_OR) {
        auto caseBlock = std::make_unique<BlockNode>();
        caseBlock->endPos = jumpPos;
        auto* nextBlock = caseBlock.get();
        currentCasePtr->setBlock(std::move(caseBlock));
        enterBlock(*nextBlock);
    }

    lastConsumed_ = currentIndex - index + 1;
}

void LingoDecompiler::translateObjCall(int bytecodeOffset, int nameId, BlockNode& block) {
    const auto method = resolveName(nameId);
    auto argList = popNode();
    const auto& rawArgs = argList->argNodes();
    const auto nargs = rawArgs.size();

    NodePtr translation;

    if (method == "getAt" && nargs == 2) {
        auto args = takeArgNodes(std::move(argList));
        translation = std::make_unique<ObjBracketExprNode>(std::move(args[0]), std::move(args[1]));
    } else if (method == "setAt" && nargs == 3) {
        auto args = takeArgNodes(std::move(argList));
        auto propExpr = std::make_unique<ObjBracketExprNode>(std::move(args[0]), std::move(args[1]));
        translation = std::make_unique<AssignmentStmtNode>(std::move(propExpr), std::move(args[2]));
    } else if ((method == "getProp" || method == "getPropRef") &&
               (nargs == 3 || nargs == 4) &&
               rawArgs[1]->valueType() == ValueType::Symbol) {
        const auto propName = rawArgs[1]->stringValue();
        auto args = takeArgNodes(std::move(argList));
        NodePtr index2;
        if (nargs == 4) {
            index2 = std::move(args[3]);
        }
        translation = std::make_unique<ObjPropIndexExprNode>(
            std::move(args[0]),
            propName,
            std::move(args[2]),
            std::move(index2));
    } else if (method == "setProp" &&
               (nargs == 4 || nargs == 5) &&
               rawArgs[1]->valueType() == ValueType::Symbol) {
        const auto propName = rawArgs[1]->stringValue();
        auto args = takeArgNodes(std::move(argList));
        NodePtr index2;
        if (nargs == 5) {
            index2 = std::move(args[3]);
        }
        auto propExpr = std::make_unique<ObjPropIndexExprNode>(
            std::move(args[0]),
            propName,
            std::move(args[2]),
            std::move(index2));
        translation = std::make_unique<AssignmentStmtNode>(std::move(propExpr), std::move(args[nargs - 1]));
    } else if (method == "count" &&
               nargs == 2 &&
               rawArgs[1]->valueType() == ValueType::Symbol) {
        const auto propName = rawArgs[1]->stringValue();
        auto args = takeArgNodes(std::move(argList));
        auto propExpr = std::make_unique<ObjPropExprNode>(std::move(args[0]), propName);
        translation = std::make_unique<ObjPropExprNode>(std::move(propExpr), "count");
    } else if ((method == "setContents" || method == "setContentsAfter" || method == "setContentsBefore") &&
               nargs == 2) {
        const int putType = method == "setContents" ? 1 : method == "setContentsAfter" ? 2 : 3;
        auto args = takeArgNodes(std::move(argList));
        translation = std::make_unique<PutStmtNode>(putType, std::move(args[0]), std::move(args[1]));
    } else if (method == "hilite" && nargs == 1) {
        auto args = takeArgNodes(std::move(argList));
        translation = std::make_unique<ChunkHiliteStmtNode>(std::move(args[0]));
    } else if (method == "delete" && nargs == 1) {
        auto args = takeArgNodes(std::move(argList));
        translation = std::make_unique<ChunkDeleteStmtNode>(std::move(args[0]));
    } else {
        translation = std::make_unique<ObjCallNode>(method, std::move(argList));
    }

    translation->setBytecodeOffset(bytecodeOffset);
    if (translation->isExpression()) {
        stack_.push_back(std::move(translation));
    } else {
        block.addChild(std::move(translation));
    }
}

void LingoDecompiler::enterBlock(BlockNode& block) {
    currentBlock_ = &block;
}

void LingoDecompiler::exitBlock() {
    if (currentBlock_ == nullptr) {
        return;
    }
    auto* ancestorStatement = currentBlock_->ancestorStatement();
    if (ancestorStatement == nullptr) {
        currentBlock_ = nullptr;
        return;
    }
    if (auto* parentBlock = dynamic_cast<BlockNode*>(ancestorStatement->parent()); parentBlock != nullptr) {
        currentBlock_ = parentBlock;
    } else {
        currentBlock_ = nullptr;
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

std::vector<NodePtr> LingoDecompiler::takeArgNodes(NodePtr argList) const {
    if (auto* literal = dynamic_cast<LiteralNode*>(argList.get()); literal != nullptr) {
        return literal->takeItems();
    }
    return {};
}

NodePtr LingoDecompiler::readVar(int varType) {
    NodePtr castId;
    if (varType == 0x6 && version_ >= 500) {
        castId = popNode();
    }
    auto id = popNode();

    switch (varType) {
        case 0x1:
        case 0x2:
        case 0x3:
            return id;
        case 0x4:
            return std::make_unique<LiteralNode>(ValueType::VarRef, getArgumentName(id->intValue()));
        case 0x5:
            return std::make_unique<LiteralNode>(ValueType::VarRef, getLocalName(id->intValue()));
        case 0x6:
            return std::make_unique<MemberExprNode>("field", std::move(id), std::move(castId));
        default:
            return std::make_unique<ErrorNode>();
    }
}

NodePtr LingoDecompiler::readChunkRef(NodePtr string) {
    auto lastLine = popNode();
    auto firstLine = popNode();
    auto lastItem = popNode();
    auto firstItem = popNode();
    auto lastWord = popNode();
    auto firstWord = popNode();
    auto lastChar = popNode();
    auto firstChar = popNode();

    if (!isZeroLiteral(*firstLine)) {
        string = std::make_unique<ChunkExprNode>(
            4,
            std::move(firstLine),
            std::move(lastLine),
            std::move(string));
    }
    if (!isZeroLiteral(*firstItem)) {
        string = std::make_unique<ChunkExprNode>(
            3,
            std::move(firstItem),
            std::move(lastItem),
            std::move(string));
    }
    if (!isZeroLiteral(*firstWord)) {
        string = std::make_unique<ChunkExprNode>(
            2,
            std::move(firstWord),
            std::move(lastWord),
            std::move(string));
    }
    if (!isZeroLiteral(*firstChar)) {
        string = std::make_unique<ChunkExprNode>(
            1,
            std::move(firstChar),
            std::move(lastChar),
            std::move(string));
    }

    return string;
}

NodePtr LingoDecompiler::readV4Property(int propertyType, int propertyId) {
    switch (propertyType) {
        case 0x00:
            if (propertyId <= 0x0b) {
                return std::make_unique<TheExprNode>(std::string(moviePropertyName(propertyId)));
            }
            return std::make_unique<LastStringChunkExprNode>(propertyId - 0x0b, popNode());

        case 0x01:
            return std::make_unique<StringChunkCountExprNode>(propertyId, popNode());

        case 0x02:
            return std::make_unique<MenuPropExprNode>(popNode(), propertyId);

        case 0x03: {
            auto menuId = popNode();
            return std::make_unique<MenuItemPropExprNode>(std::move(menuId), popNode(), propertyId);
        }

        case 0x04:
            return std::make_unique<SoundPropExprNode>(popNode(), propertyId);

        case 0x05:
            return std::make_unique<CommentNode>("ERROR: Resource property");

        case 0x06:
            return std::make_unique<SpritePropExprNode>(popNode(), propertyId);

        case 0x07:
            return std::make_unique<TheExprNode>(std::string(animationPropertyName(propertyId)));

        case 0x08:
            if (propertyId == 0x02 && version_ >= 500) {
                auto castLib = popNode();
                if (!isZeroLiteral(*castLib)) {
                    return std::make_unique<ThePropExprNode>(
                        std::make_unique<MemberExprNode>("castLib", std::move(castLib)),
                        std::string(animation2PropertyName(propertyId)));
                }
            }
            return std::make_unique<TheExprNode>(std::string(animation2PropertyName(propertyId)));

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
            NodePtr castId;
            if (version_ >= 500) {
                castId = popNode();
            }
            auto memberId = popNode();
            std::string prefix;
            if (propertyType == 0x0b || propertyType == 0x0c) {
                prefix = "field";
            } else if (propertyType == 0x14 || propertyType == 0x15) {
                prefix = "script";
            } else {
                prefix = version_ >= 500 ? "member" : "cast";
            }

            NodePtr entity = std::make_unique<MemberExprNode>(std::move(prefix), std::move(memberId), std::move(castId));
            if (propertyType == 0x0a || propertyType == 0x0c || propertyType == 0x15) {
                entity = readChunkRef(std::move(entity));
            }
            return std::make_unique<ThePropExprNode>(std::move(entity), std::string(memberPropertyName(propertyId)));
        }

        default:
            return std::make_unique<CommentNode>("ERROR: Unknown property type " + std::to_string(propertyType));
    }
}

int LingoDecompiler::variableMultiplier() const {
    if (capitalX_) {
        return 1;
    }
    if (version_ >= 500) {
        return 8;
    }
    return 6;
}

std::string LingoDecompiler::getArgumentName(int rawIndex) const {
    const int index = rawIndex / variableMultiplier();
    if (currentHandler_ != nullptr &&
        index >= 0 &&
        index < static_cast<int>(currentHandler_->argNameIds.size())) {
        return resolveName(currentHandler_->argNameIds[static_cast<std::size_t>(index)]);
    }
    return "UNKNOWN_ARG_" + std::to_string(index);
}

std::string LingoDecompiler::getLocalName(int rawIndex) const {
    const int index = rawIndex / variableMultiplier();
    if (currentHandler_ != nullptr &&
        index >= 0 &&
        index < static_cast<int>(currentHandler_->localNameIds.size())) {
        return resolveName(currentHandler_->localNameIds[static_cast<std::size_t>(index)]);
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
            return std::make_unique<LiteralNode>(literal.numericValue);
        default:
            return std::make_unique<LiteralNode>(ValueType::String, rawLiteralString(literal.value));
    }
}

bool LingoDecompiler::isZeroLiteral(const LingoNode& node) {
    return node.valueType() == ValueType::Int && node.intValue() == 0;
}

} // namespace libreshockwave::lingo::decompiler
