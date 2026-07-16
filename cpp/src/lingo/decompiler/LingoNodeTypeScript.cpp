// TypeScript emission for the LingoNode AST.
//
// This is the "Lingo → TypeScript" transpiler: it walks the decompiler AST (built from bytecode
// by LingoDecompiler::translateHandler) and emits runnable TypeScript source for each handler.
// The emitted TS relies on the host functions and proxies exported from the runtime's
// lingo-runtime.ts (sprite, member, theProperty, callBuiltin, LingoList, etc.).
//
// The transpiler is intentionally implemented directly on LingoNode subclasses so the emitter
// can access node internals without exposing them through accessors.

#include "libreshockwave/lingo/decompiler/LingoNode.hpp"
#include "libreshockwave/lingo/vm/PropertyIdMappings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace libreshockwave::lingo::decompiler {

namespace {

constexpr std::size_t kIndentSize = 2;

std::string tsIndent(std::size_t level) {
    return std::string(level * kIndentSize, ' ');
}

std::string escapeString(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (const char ch : value) {
        switch (ch) {
            case '"': result.append("\\\""); break;
            case '\\': result.append("\\\\"); break;
            case '\b': result.append("\\b"); break;
            case '\f': result.append("\\f"); break;
            case '\n': result.append("\\n"); break;
            case '\r': result.append("\\r"); break;
            case '\t': result.append("\\t"); break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\x%02x", static_cast<unsigned char>(ch));
                    result.append(buf);
                } else {
                    result.push_back(ch);
                }
        }
    }
    result.push_back('"');
    return result;
}

std::string formatFloatTs(double value) {
    std::ostringstream stream;
    stream << std::setprecision(15) << value;
    auto text = stream.str();
    const auto exponent = text.find_first_of("eE");
    if (exponent != std::string::npos) {
        return text;
    }
    const auto decimal = text.find('.');
    if (decimal == std::string::npos) {
        return text + ".0";
    }
    while (!text.empty() && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.push_back('0');
    }
    return text;
}

std::string binaryOperatorTs(Opcode opcode) {
    switch (opcode) {
        case Opcode::ADD: return " + ";
        case Opcode::SUB: return " - ";
        case Opcode::MUL: return " * ";
        case Opcode::DIV: return " / ";
        case Opcode::MOD: return " % ";
        case Opcode::JOIN_STR: return " + ";
        case Opcode::JOIN_PAD_STR: return " + ";
        case Opcode::LT: return " < ";
        case Opcode::LT_EQ: return " <= ";
        case Opcode::GT: return " > ";
        case Opcode::GT_EQ: return " >= ";
        case Opcode::EQ: return " == ";
        case Opcode::NT_EQ: return " != ";
        case Opcode::AND: return " && ";
        case Opcode::OR: return " || ";
        case Opcode::CONTAINS_STR: return " /* contains */ ";
        case Opcode::CONTAINS_0_STR: return " /* contains0 */ ";
        default: return " /* unknownOp */ ";
    }
}

bool needsParens(const LingoNode& node, Opcode parentOp) {
    const auto* binary = dynamic_cast<const BinaryOpNode*>(&node);
    if (!binary) {
        return false;
    }
    const int nodePrec = binary->precedence();
    const int parentPrec = BinaryOpNode(parentOp, nullptr, nullptr).precedence();
    return nodePrec < parentPrec;
}

std::string maybeWrap(const LingoNode& node, std::string_view text, Opcode parentOp) {
    if (needsParens(node, parentOp)) {
        return "(" + std::string(text) + ")";
    }
    return std::string(text);
}

std::string chunkTypeString(int chunkType) {
    switch (chunkType) {
        case 1: return "char";
        case 2: return "word";
        case 3: return "item";
        case 4: return "line";
        default: return "chunk";
    }
}

std::string sanitizeTsIdentifier(std::string_view name) {
    std::string result;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char ch = name[i];
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
            result.push_back(ch);
        } else {
            result.push_back('_');
        }
    }
    if (result.empty()) {
        result = "_";
    }
    // Avoid clashing with TS reserved words or runtime helpers by prefixing.
    if (result == "var" || result == "let" || result == "const" || result == "function" ||
        result == "return" || result == "if" || result == "else" || result == "while" ||
        result == "for" || result == "break" || result == "continue" || result == "switch" ||
        result == "case" || result == "default" || result == "new" || result == "this" ||
        result == "undefined" || result == "true" || result == "false" || result == "null" ||
        result == "void" || result == "typeof" || result == "in" || result == "of" ||
        result == "do" || result == "try" || result == "catch" || result == "finally" ||
        result == "throw" || result == "with" || result == "yield" || result == "await" ||
        result == "async" || result == "class" || result == "extends" || result == "super" ||
        result == "export" || result == "import" || result == "from" || result == "as" ||
        result == "interface" || result == "type" || result == "namespace" || result == "module" ||
        result == "declare" || result == "abstract" || result == "implements" || result == "private" ||
        result == "protected" || result == "public" || result == "readonly" || result == "static" ||
        result == "get" || result == "set" || result == "constructor" || result == "debugger" ||
        result == "enum" || result == " keyof " || result == "is" || result == "infer" ||
        result == "never" || result == "unknown" || result == "any" || result == "object" ||
        result == "number" || result == "string" || result == "boolean" || result == "symbol" ||
        result == "bigint" || result == "me") {
        result = "_" + result;
    }
    return result;
}

} // namespace

std::string LingoNode::toTypeScript() const {
    throw std::runtime_error("toTypeScript not implemented for this LingoNode type");
}

std::string ErrorNode::toTypeScript() const {
    return "/* decompile error */ undefined";
}

std::string CommentNode::toTypeScript() const {
    return "// " + text_;
}

std::string LiteralNode::toTypeScript() const {
    switch (valueType_) {
        case ValueType::Int:
            return std::to_string(intValue_);
        case ValueType::Float:
            return formatFloatTs(floatValue_);
        case ValueType::String:
            return escapeString(stringValue_);
        case ValueType::Symbol:
            return "symbol(" + escapeString(stringValue_) + ")";
        case ValueType::VarRef:
            return sanitizeTsIdentifier(stringValue_);
        case ValueType::List:
        case ValueType::ArgList:
        case ValueType::ArgListNoRet: {
            std::ostringstream out;
            out << "new LingoList([";
            for (std::size_t i = 0; i < items_.size(); ++i) {
                if (i > 0) out << ", ";
                out << items_[i]->toTypeScript();
            }
            out << "])";
            return out.str();
        }
        case ValueType::PropList: {
            std::ostringstream out;
            out << "new LingoPropList(/* propList */)";
            return out.str();
        }
        case ValueType::Void:
        default:
            return "undefined";
    }
}

const HandlerNode* findHandlerAncestor(const LingoNode* node) {
    const LingoNode* current = node->parent();
    while (current) {
        const auto* handler = dynamic_cast<const HandlerNode*>(current);
        if (handler) {
            return handler;
        }
        current = current->parent();
    }
    return nullptr;
}

bool isHandlerProperty(const LingoNode* node, const std::string& name) {
    const auto* handler = findHandlerAncestor(node);
    if (!handler) {
        return false;
    }
    const auto& props = handler->propertyNames();
    return std::find(props.begin(), props.end(), name) != props.end();
}

bool isHandlerGlobal(const LingoNode* node, const std::string& name) {
    const auto* handler = findHandlerAncestor(node);
    if (!handler) {
        return false;
    }
    const auto& globals = handler->globalNames();
    return std::find(globals.begin(), globals.end(), name) != globals.end();
}

std::string VarNode::toTypeScript() const {
    if (isHandlerProperty(this, name_)) {
        return "meProp(_me, " + escapeString(name_) + ")";
    }
    if (isHandlerGlobal(this, name_)) {
        return "globalVar(" + escapeString(name_) + ")";
    }
    return sanitizeTsIdentifier(name_);
}

std::string InverseOpNode::toTypeScript() const {
    return "-" + operand_->toTypeScript();
}

std::string NotOpNode::toTypeScript() const {
    return "!" + operand_->toTypeScript();
}

std::string BinaryOpNode::toTypeScript() const {
    const std::string left = left_->toTypeScript();
    const std::string right = right_->toTypeScript();
    if (opcode_ == Opcode::JOIN_STR || opcode_ == Opcode::JOIN_PAD_STR) {
        return "(" + maybeWrap(*left_, left, opcode_) + " + " + maybeWrap(*right_, right, opcode_) + ")";
    }
    if (opcode_ == Opcode::CONTAINS_STR) {
        return "contains(" + left + ", " + right + ")";
    }
    if (opcode_ == Opcode::CONTAINS_0_STR) {
        return "starts(" + left + ", " + right + ")";
    }
    return maybeWrap(*left_, left, opcode_) + binaryOperatorTs(opcode_) + maybeWrap(*right_, right, opcode_);
}

std::string TheExprNode::toTypeScript() const {
    return "theProperty(" + escapeString(prop_) + ")";
}

std::string MemberExprNode::toTypeScript() const {
    std::ostringstream out;
    out << "member(" << memberId_->toTypeScript();
    if (castId_) {
        out << ", " << castId_->toTypeScript();
    }
    out << ")";
    return out.str();
}

std::string ObjPropExprNode::toTypeScript() const {
    const std::string obj = object_->toTypeScript();
    return obj + "." + sanitizeTsIdentifier(prop_);
}

std::string ObjBracketExprNode::toTypeScript() const {
    return object_->toTypeScript() + "[" + prop_->toTypeScript() + "]";
}

std::string ObjPropIndexExprNode::toTypeScript() const {
    std::ostringstream out;
    out << object_->toTypeScript() << "." << sanitizeTsIdentifier(prop_) << "[" << index_->toTypeScript();
    if (index2_) {
        out << ", " << index2_->toTypeScript();
    }
    out << "]";
    return out.str();
}

std::string ThePropExprNode::toTypeScript() const {
    // "the text of the member of sprite iSpr" -> thePropOf(object, "text")
    // The runtime host handles chained objects.
    return "thePropOf(" + object_->toTypeScript() + ", " + escapeString(prop_) + ")";
}

std::string ChunkExprNode::toTypeScript() const {
    std::ostringstream out;
    out << "chunkOf(" << string_->toTypeScript()
        << ", " << escapeString(chunkTypeString(chunkType_))
        << ", " << first_->toTypeScript();
    if (last_ && last_->toTypeScript() != first_->toTypeScript()) {
        out << ", " << last_->toTypeScript();
    }
    out << ")";
    return out.str();
}

std::string LastStringChunkExprNode::toTypeScript() const {
    return "lastChunk(" + string_->toTypeScript() + ", " + escapeString(chunkTypeString(chunkType_)) + ")";
}

std::string StringChunkCountExprNode::toTypeScript() const {
    return "chunkCount(" + string_->toTypeScript() + ", " + escapeString(chunkTypeString(chunkType_)) + ")";
}

std::string SpriteIntersectsExprNode::toTypeScript() const {
    return "spriteIntersects(" + first_->toTypeScript() + ", " + second_->toTypeScript() + ")";
}

std::string SpriteWithinExprNode::toTypeScript() const {
    return "spriteWithin(" + first_->toTypeScript() + ", " + second_->toTypeScript() + ")";
}

std::string MenuPropExprNode::toTypeScript() const {
    return "menuProp(" + menuId_->toTypeScript() + ", " + std::to_string(prop_) + ")";
}

std::string MenuItemPropExprNode::toTypeScript() const {
    return "menuItemProp(" + menuId_->toTypeScript() + ", " + itemId_->toTypeScript() + ", " +
           std::to_string(prop_) + ")";
}

std::string SoundPropExprNode::toTypeScript() const {
    return "soundProp(" + soundId_->toTypeScript() + ", " + std::to_string(prop_) + ")";
}

std::string SpritePropExprNode::toTypeScript() const {
    const auto name = vm::PropertyIdMappings::getSpritePropName(prop_);
    const std::string propName = name ? std::string(*name) : "prop_" + std::to_string(prop_);
    return "sprite(" + spriteId_->toTypeScript() + ")." + sanitizeTsIdentifier(propName);
}

std::string NewObjNode::toTypeScript() const {
    std::ostringstream out;
    out << "newObj(" << escapeString(objectType_) << ", " << objectArgs_->toTypeScript() << ")";
    return out.str();
}

std::string ExitStmtNode::toTypeScript() const {
    return "return;";
}

std::string ExitRepeatStmtNode::toTypeScript() const {
    return "break;";
}

std::string NextRepeatStmtNode::toTypeScript() const {
    return "continue;";
}

namespace {

// Director "put X into field/member Y" and "set the text of member Y to X" target a member
// object's property. Emit via the runtime's setThePropOf helper so the LHS is a valid expression.
std::string memberPropertyAssignmentTs(const std::string& memberExpr, const std::string& value) {
    return "setThePropOf(" + memberExpr + ", \"text\", " + value + ");";
}

bool isMemberCall(const LingoNode& node, std::string* outExpr = nullptr) {
    const auto* call = dynamic_cast<const CallNode*>(&node);
    if (!call) {
        return false;
    }
    const std::string name = sanitizeTsIdentifier(call->name());
    if (name != "member" && name != "field") {
        return false;
    }
    if (outExpr) {
        *outExpr = call->toTypeScript();
    }
    return true;
}

} // namespace

std::string AssignmentStmtNode::toTypeScript() const {
    const auto* var = dynamic_cast<const VarNode*>(variable_.get());
    if (var && isHandlerProperty(this, var->name())) {
        return "setMeProp(_me, " + escapeString(var->name()) + ", " + value_->toTypeScript() + ");";
    }
    if (var && isHandlerGlobal(this, var->name())) {
        return "setGlobal(" + escapeString(var->name()) + ", " + value_->toTypeScript() + ");";
    }
    if (dynamic_cast<const MemberExprNode*>(variable_.get())) {
        return memberPropertyAssignmentTs(variable_->toTypeScript(), value_->toTypeScript());
    }
    if (dynamic_cast<const ThePropExprNode*>(variable_.get())) {
        // "set the text of member X to Y" — ThePropExprNode emits thePropOf(..., "text");
        // reverse it into a setter call on the object.
        const auto* propExpr = static_cast<const ThePropExprNode*>(variable_.get());
        return "setThePropOf(" + propExpr->object()->toTypeScript() + ", " +
               escapeString(propExpr->prop()) + ", " + value_->toTypeScript() + ");";
    }
    if (dynamic_cast<const TheExprNode*>(variable_.get())) {
        // "set the itemDelimiter to X" — TheExprNode emits theProperty(...);
        // reverse it into the global setter.
        const auto* theExpr = static_cast<const TheExprNode*>(variable_.get());
        return "setTheProperty(" + escapeString(theExpr->prop()) + ", " + value_->toTypeScript() + ");";
    }
    if (const auto* objProp = dynamic_cast<const ObjPropExprNode*>(variable_.get())) {
        const std::string rhs = value_->toTypeScript();
        return "setThePropOf(" + objProp->object().toTypeScript() + ", " +
               escapeString(objProp->prop()) + ", " + rhs + ");";
    }
    std::string memberExpr;
    if (isMemberCall(*variable_, &memberExpr)) {
        const std::string rhs = value_->toTypeScript();
        return memberPropertyAssignmentTs(memberExpr, rhs);
    }
    if (dynamic_cast<const ChunkExprNode*>(variable_.get())) {
        return "/* chunk assignment not ported */;";
    }
    const std::string lhs = variable_->toTypeScript();
    const std::string rhs = value_->toTypeScript();
    // In strict-mode ES modules, assignments to undeclared identifiers are runtime errors.
    // Lingo locals are created on first assignment; emit `var` so the TS local is declared.
    if (var) {
        return "var " + lhs + " = " + rhs + ";";
    }
    return lhs + " = " + rhs + ";";
}

std::string PutStmtNode::toTypeScript() const {
    const auto* var = dynamic_cast<const VarNode*>(variable_.get());
    if (var && isHandlerProperty(this, var->name())) {
        if (putType_ == 0) { // into
            return "setMeProp(_me, " + escapeString(var->name()) + ", " + value_->toTypeScript() + ");";
        }
        return "setMeProp(_me, " + escapeString(var->name()) + ", meProp(_me, " +
               escapeString(var->name()) + ") + " + value_->toTypeScript() + ");";
    }
    if (dynamic_cast<const MemberExprNode*>(variable_.get()) ||
        dynamic_cast<const ThePropExprNode*>(variable_.get())) {
        const std::string memberExpr = variable_->toTypeScript();
        const std::string rhs = value_->toTypeScript();
        // "put X into field Y" is an assignment, not an append, regardless of putType_.
        return memberPropertyAssignmentTs(memberExpr, rhs);
    }
    if (const auto* theExpr = dynamic_cast<const TheExprNode*>(variable_.get())) {
        const std::string rhs = value_->toTypeScript();
        return "setTheProperty(" + escapeString(theExpr->prop()) + ", " + rhs + ");";
    }
    std::string memberExpr;
    if (isMemberCall(*variable_, &memberExpr)) {
        const std::string rhs = value_->toTypeScript();
        return memberPropertyAssignmentTs(memberExpr, rhs);
    }
    if (dynamic_cast<const ChunkExprNode*>(variable_.get())) {
        // "put X after char N of S" targets a chunk expression; chunk helpers are stubs,
        // so emit a no-op comment to keep the statement syntactically valid.
        return "/* chunk assignment not ported */;";
    }
    const std::string lhs = variable_->toTypeScript();
    const std::string rhs = value_->toTypeScript();
    if (var && isHandlerGlobal(this, var->name())) {
        if (putType_ == 0) { // into
            return "setGlobal(" + escapeString(var->name()) + ", " + rhs + ");";
        }
        return "setGlobal(" + escapeString(var->name()) + ", globalVar(" + escapeString(var->name()) + ") + " + rhs + ");";
    }
    if (var) {
        if (putType_ == 0) { // into
            return "var " + lhs + " = " + rhs + ";";
        }
        return "var " + lhs + " = " + lhs + " + " + rhs + ";";
    }
    if (putType_ == 0) { // into
        return lhs + " = " + rhs + ";";
    }
    return lhs + " = " + lhs + " + " + rhs + ";";
}

std::string ChunkHiliteStmtNode::toTypeScript() const {
    return "/* hiliteChunk(" + chunk_->toTypeScript() + ") */;";
}

std::string ChunkDeleteStmtNode::toTypeScript() const {
    return "deleteChunk(" + chunk_->toTypeScript() + ");";
}

std::string WhenStmtNode::toTypeScript() const {
    return "/* when " + std::to_string(event_) + " then " + escapeString(script_) + " */;";
}

namespace {

std::string emitArgList(const LingoNode& argList) {
    const auto& args = argList.argNodes();
    std::ostringstream out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) out << ", ";
        out << args[i]->toTypeScript();
    }
    return out.str();
}

} // namespace

std::string CallNode::toTypeScript() const {
    const std::string args = emitArgList(*argList_);
    const std::string safeName = sanitizeTsIdentifier(name_);
    // Director's VOID keyword is sometimes emitted by the decompiler as a zero-argument call.
    if ((name_ == "VOID" || name_ == "void") && argList_->argNodes().empty()) {
        return "undefined";
    }
    // Global builtins that need runtime dispatch rather than direct JS functions.
    static const std::unordered_set<std::string> kRuntimeBuiltins = {
        "random", "go", "marker", "point", "sendAllSprites", "sendFuseMsg",
        "updateStage", "preload", "play", "puppetSprite", "puppetTempo", "puppetPalette",
        "hilite", "dontPassEvent", "pass", "beep", "alert",
    };
    if (kRuntimeBuiltins.count(safeName) > 0) {
        return "callBuiltin(" + escapeString(safeName) + ", " + args + ")";
    }
    return safeName + "(" + args + ")";
}

std::string ObjCallNode::toTypeScript() const {
    const std::string args = emitArgList(*argList_);
    return "callMethod(" + escapeString(name_) + ", " + args + ")";
}

std::string ObjCallV4Node::toTypeScript() const {
    const std::string args = emitArgList(*argList_);
    return object_->toTypeScript() + "(" + args + ")";
}

std::string BlockNode::toTypeScript() const {
    std::ostringstream out;
    for (const auto& child : children_) {
        out << tsIndent(1) << child->toTypeScript() << "\n";
    }
    return out.str();
}

std::string HandlerNode::toTypeScript() const {
    std::ostringstream out;
    out << "export function " << sanitizeTsIdentifier(handlerName_) << "(";
    // Director behavior handlers receive `me` as the first argument. The Lingo source may
    // declare it explicitly ("on foo me, bar") or not ("on foo"). Emit a typed `_me` only
    // when it is not already the first declared argument, to avoid duplicate identifiers.
    const bool hasExplicitMe = !argumentNames_.empty() &&
        (argumentNames_[0] == "me" || argumentNames_[0] == "ME" || argumentNames_[0] == "Me");
    if (!hasExplicitMe) {
        out << "_me: LingoMe";
    }
    for (std::size_t i = 0; i < argumentNames_.size(); ++i) {
        if (i > 0 || !hasExplicitMe) out << ", ";
        out << sanitizeTsIdentifier(argumentNames_[i]);
        if (i == 0 && hasExplicitMe) {
            out << ": LingoMe";
        } else {
            out << ": LingoValue";
        }
    }
    out << ") {\n";

    for (const auto& global : globalNames_) {
        out << tsIndent(1) << "/* global */ var " << sanitizeTsIdentifier(global) << " = globalVar("
            << escapeString(global) << ");\n";
    }

    out << block_->toTypeScript();
    out << "}\n";
    return out.str();
}

std::string IfStmtNode::toTypeScript() const {
    std::ostringstream out;
    out << "if (" << condition_->toTypeScript() << ") {\n";
    out << block1_->toTypeScript();
    out << tsIndent(1) << "}";
    if (hasElse_) {
        out << " else {\n";
        out << block2_->toTypeScript();
        out << tsIndent(1) << "}";
    }
    out << "\n";
    return out.str();
}

std::string RepeatWhileStmtNode::toTypeScript() const {
    (void)startIndex();
    std::ostringstream out;
    out << "while (" << condition_->toTypeScript() << ") {\n";
    out << block_->toTypeScript();
    out << tsIndent(1) << "}\n";
    return out.str();
}

std::string RepeatWithInStmtNode::toTypeScript() const {
    (void)startIndex();
    std::ostringstream out;
    out << "for (const " << sanitizeTsIdentifier(variableName_) << " of "
        << list_->toTypeScript() << ".toArray()) {\n";
    out << block_->toTypeScript();
    out << tsIndent(1) << "}\n";
    return out.str();
}

std::string RepeatWithToStmtNode::toTypeScript() const {
    (void)startIndex();
    const std::string var = sanitizeTsIdentifier(variableName_);
    const std::string start = start_->toTypeScript();
    const std::string end = end_->toTypeScript();
    std::ostringstream out;
    if (up_) {
        out << "for (let " << var << " = " << start << "; " << var << " <= " << end
            << "; ++" << var << ") {\n";
    } else {
        out << "for (let " << var << " = " << start << "; " << var << " >= " << end
            << "; --" << var << ") {\n";
    }
    out << block_->toTypeScript();
    out << tsIndent(1) << "}\n";
    return out.str();
}

std::string TellStmtNode::toTypeScript() const {
    return "/* tell window " + window_->toTypeScript() + " { ... } */;";
}

std::string CaseNode::toTypeScript() const {
    (void)expect_;
    std::ostringstream out;
    out << "case " << value_->toTypeScript() << ":\n";
    if (block_) {
        out << block_->toTypeScript();
    }
    if (nextOr_) {
        out << nextOr_->toTypeScript();
    }
    if (nextCase_) {
        out << nextCase_->toTypeScript();
    }
    if (otherwise_) {
        out << tsIndent(1) << "default:\n";
        out << otherwise_->toTypeScript();
    }
    return out.str();
}

std::string CasesStmtNode::toTypeScript() const {
    std::ostringstream out;
    out << "switch (" << value_->toTypeScript() << ") {\n";
    if (firstCase_) {
        out << firstCase_->toTypeScript();
    }
    out << tsIndent(1) << "}\n";
    return out.str();
}

} // namespace libreshockwave::lingo::decompiler
