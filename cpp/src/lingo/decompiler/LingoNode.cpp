#include "libreshockwave/lingo/decompiler/LingoNode.hpp"

#include "libreshockwave/lingo/decompiler/LingoProperties.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>

namespace libreshockwave::lingo::decompiler {
namespace {

const std::vector<NodePtr>& emptyNodes() {
    static const std::vector<NodePtr> empty;
    return empty;
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

bool isIntLiteralZero(const LingoNode* node) {
    return node != nullptr &&
           node->valueType() == ValueType::Int &&
           node->intValue() == 0;
}

std::string formatFloat(double value) {
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

std::string toString(std::string_view value) {
    return std::string(value);
}

} // namespace

ValueType LingoNode::valueType() const {
    return ValueType::Void;
}

int LingoNode::intValue() const {
    return 0;
}

std::string LingoNode::stringValue() const {
    return "";
}

const std::vector<NodePtr>& LingoNode::argNodes() const {
    return emptyNodes();
}

void LingoNode::setValueType(ValueType /*type*/) {}

LingoNode* LingoNode::ancestorStatement() const {
    auto* ancestor = parent_;
    while (ancestor != nullptr && !ancestor->isStatement()) {
        ancestor = ancestor->parent();
    }
    return ancestor;
}

LoopNodeBase* LingoNode::ancestorLoop() const {
    auto* ancestor = parent_;
    while (ancestor != nullptr && !ancestor->isLoop()) {
        ancestor = ancestor->parent();
    }
    return dynamic_cast<LoopNodeBase*>(ancestor);
}

std::string LingoNode::indent(std::string_view text) {
    std::string result;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        const auto lineEnd = end == std::string_view::npos ? text.size() : end;
        const auto line = text.substr(start, lineEnd - start);
        if (!line.empty()) {
            result.append("  ");
            result.append(line);
        }
        result.push_back('\n');
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    if (!result.empty() && result.back() == '\n' && !text.empty() && text.back() != '\n') {
        result.pop_back();
    }
    return result;
}

void LingoNode::adopt(const NodePtr& child) {
    if (child) {
        child->setParent(this);
    }
}

ErrorNode::ErrorNode() {
    markExpression();
}

std::string ErrorNode::toLingo(bool /*dot*/) const {
    return "ERROR";
}

CommentNode::CommentNode(std::string text) : text_(std::move(text)) {}

std::string CommentNode::toLingo(bool /*dot*/) const {
    return "-- " + text_;
}

LiteralNode::LiteralNode(int value) : valueType_(ValueType::Int), intValue_(value) {
    markExpression();
}

LiteralNode::LiteralNode(double value) : valueType_(ValueType::Float), floatValue_(value) {
    markExpression();
}

LiteralNode::LiteralNode(ValueType type, std::string value)
    : valueType_(type), stringValue_(std::move(value)) {
    markExpression();
}

LiteralNode::LiteralNode(ValueType type, std::vector<NodePtr> items)
    : valueType_(type), items_(std::move(items)) {
    markExpression();
    for (const auto& item : items_) {
        adopt(item);
    }
}

ValueType LiteralNode::valueType() const {
    return valueType_;
}

int LiteralNode::intValue() const {
    return intValue_;
}

double LiteralNode::floatValue() const {
    return floatValue_;
}

std::string LiteralNode::stringValue() const {
    return stringValue_;
}

const std::vector<NodePtr>& LiteralNode::argNodes() const {
    return items_;
}

std::vector<NodePtr> LiteralNode::takeItems() {
    return std::move(items_);
}

void LiteralNode::setValueType(ValueType type) {
    valueType_ = type;
}

std::string LiteralNode::toLingo(bool dot) const {
    switch (valueType_) {
        case ValueType::Void:
            return "VOID";
        case ValueType::Symbol:
            return "#" + stringValue_;
        case ValueType::VarRef:
            return stringValue_;
        case ValueType::String:
            if (stringValue_.empty()) {
                return "EMPTY";
            }
            if (stringValue_.size() == 1) {
                switch (stringValue_[0]) {
                    case '\x03': return "ENTER";
                    case '\b': return "BACKSPACE";
                    case '\t': return "TAB";
                    case '\r': return "RETURN";
                    case '"': return "QUOTE";
                    default: break;
                }
            }
            return "\"" + stringValue_ + "\"";
        case ValueType::Int:
            return std::to_string(intValue_);
        case ValueType::Float:
            return formatFloat(floatValue_);
        case ValueType::List: {
            std::string result = "[";
            for (std::size_t index = 0; index < items_.size(); ++index) {
                if (index > 0) {
                    result.append(", ");
                }
                result.append(items_[index]->toLingo(dot));
            }
            result.push_back(']');
            return result;
        }
        case ValueType::ArgList:
        case ValueType::ArgListNoRet: {
            std::string result;
            for (std::size_t index = 0; index < items_.size(); ++index) {
                if (index > 0) {
                    result.append(", ");
                }
                result.append(items_[index]->toLingo(dot));
            }
            return result;
        }
        case ValueType::PropList: {
            std::string result = "[";
            if (items_.empty()) {
                result.push_back(':');
            } else {
                for (std::size_t index = 0; index < items_.size(); index += 2) {
                    if (index > 0) {
                        result.append(", ");
                    }
                    result.append(items_[index]->toLingo(dot));
                    result.append(": ");
                    if (index + 1 < items_.size()) {
                        result.append(items_[index + 1]->toLingo(dot));
                    }
                }
            }
            result.push_back(']');
            return result;
        }
    }
    return "VOID";
}

VarNode::VarNode(std::string name) : name_(std::move(name)) {
    markExpression();
}

std::string VarNode::toLingo(bool /*dot*/) const {
    return name_;
}

InverseOpNode::InverseOpNode(NodePtr operand) : operand_(std::move(operand)) {
    markExpression();
    adopt(operand_);
}

std::string InverseOpNode::toLingo(bool dot) const {
    if (dynamic_cast<const BinaryOpNode*>(operand_.get()) != nullptr) {
        return "-(" + operand_->toLingo(dot) + ")";
    }
    return "-" + operand_->toLingo(dot);
}

NotOpNode::NotOpNode(NodePtr operand) : operand_(std::move(operand)) {
    markExpression();
    adopt(operand_);
}

std::string NotOpNode::toLingo(bool dot) const {
    if (dynamic_cast<const BinaryOpNode*>(operand_.get()) != nullptr) {
        return "not (" + operand_->toLingo(dot) + ")";
    }
    return "not " + operand_->toLingo(dot);
}

BinaryOpNode::BinaryOpNode(Opcode opcode, NodePtr left, NodePtr right)
    : opcode_(opcode), left_(std::move(left)), right_(std::move(right)) {
    markExpression();
    adopt(left_);
    adopt(right_);
}

int BinaryOpNode::precedence() const {
    switch (opcode_) {
        case Opcode::MUL:
        case Opcode::DIV:
        case Opcode::MOD:
            return 1;
        case Opcode::ADD:
        case Opcode::SUB:
            return 2;
        case Opcode::LT:
        case Opcode::LT_EQ:
        case Opcode::NT_EQ:
        case Opcode::EQ:
        case Opcode::GT:
        case Opcode::GT_EQ:
            return 3;
        case Opcode::AND:
            return 4;
        case Opcode::OR:
            return 5;
        default:
            return 0;
    }
}

std::string BinaryOpNode::toLingo(bool dot) const {
    auto left = left_->toLingo(dot);
    auto right = right_->toLingo(dot);
    const auto prec = precedence();
    if (prec > 0) {
        if (const auto* leftBinary = dynamic_cast<const BinaryOpNode*>(left_.get());
            leftBinary != nullptr && leftBinary->precedence() > prec) {
            left = "(" + left + ")";
        }
        if (const auto* rightBinary = dynamic_cast<const BinaryOpNode*>(right_.get());
            rightBinary != nullptr && rightBinary->precedence() >= prec) {
            right = "(" + right + ")";
        }
    }
    return left + " " + toString(binaryOpName(opcode_)) + " " + right;
}

TheExprNode::TheExprNode(std::string prop) : prop_(std::move(prop)) {
    markExpression();
}

std::string TheExprNode::toLingo(bool /*dot*/) const {
    return "the " + prop_;
}

MemberExprNode::MemberExprNode(std::string memberType, NodePtr memberId, NodePtr castId)
    : memberType_(std::move(memberType)), memberId_(std::move(memberId)), castId_(std::move(castId)) {
    markExpression();
    adopt(memberId_);
    adopt(castId_);
}

std::string MemberExprNode::toLingo(bool dot) const {
    const bool noCast = castId_ == nullptr || isIntLiteralZero(castId_.get());
    if (noCast) {
        if (dot) {
            return memberType_ + "(" + memberId_->toLingo(dot) + ")";
        }
        if (dynamic_cast<const BinaryOpNode*>(memberId_.get()) != nullptr) {
            return memberType_ + " (" + memberId_->toLingo(dot) + ")";
        }
        return memberType_ + " " + memberId_->toLingo(dot);
    }
    return memberType_ + "(" + memberId_->toLingo(dot) + ", " + castId_->toLingo(dot) + ")";
}

ObjPropExprNode::ObjPropExprNode(NodePtr object, std::string prop)
    : object_(std::move(object)), prop_(std::move(prop)) {
    markExpression();
    adopt(object_);
}

std::string ObjPropExprNode::toLingo(bool dot) const {
    if (dot) {
        return maybeParenDot(*object_, dot) + "." + prop_;
    }
    return "the " + prop_ + " of " + object_->toLingo(dot);
}

ObjBracketExprNode::ObjBracketExprNode(NodePtr object, NodePtr prop)
    : object_(std::move(object)), prop_(std::move(prop)) {
    markExpression();
    adopt(object_);
    adopt(prop_);
}

std::string ObjBracketExprNode::toLingo(bool dot) const {
    return object_->toLingo(dot) + "[" + prop_->toLingo(dot) + "]";
}

ObjPropIndexExprNode::ObjPropIndexExprNode(NodePtr object, std::string prop, NodePtr index, NodePtr index2)
    : object_(std::move(object)),
      prop_(std::move(prop)),
      index_(std::move(index)),
      index2_(std::move(index2)) {
    markExpression();
    adopt(object_);
    adopt(index_);
    adopt(index2_);
}

std::string ObjPropIndexExprNode::toLingo(bool dot) const {
    auto result = maybeParenDot(*object_, dot) + "." + prop_ + "[" + index_->toLingo(dot);
    if (index2_) {
        result.append("..");
        result.append(index2_->toLingo(dot));
    }
    result.push_back(']');
    return result;
}

ThePropExprNode::ThePropExprNode(NodePtr object, std::string prop)
    : object_(std::move(object)), prop_(std::move(prop)) {
    markExpression();
    adopt(object_);
}

std::string ThePropExprNode::toLingo(bool /*dot*/) const {
    return "the " + prop_ + " of " + object_->toLingo(false);
}

ChunkExprNode::ChunkExprNode(int chunkType, NodePtr first, NodePtr last, NodePtr string)
    : chunkType_(chunkType), first_(std::move(first)), last_(std::move(last)), string_(std::move(string)) {
    markExpression();
    adopt(first_);
    adopt(last_);
    adopt(string_);
}

std::string ChunkExprNode::toLingo(bool dot) const {
    std::string result = toString(chunkTypeName(chunkType_)) + " " + first_->toLingo(dot);
    if (!isIntLiteralZero(last_.get())) {
        result.append(" to ");
        result.append(last_->toLingo(dot));
    }
    result.append(" of ");
    result.append(string_->toLingo(false));
    return result;
}

LastStringChunkExprNode::LastStringChunkExprNode(int chunkType, NodePtr string)
    : chunkType_(chunkType), string_(std::move(string)) {
    markExpression();
    adopt(string_);
}

std::string LastStringChunkExprNode::toLingo(bool /*dot*/) const {
    return "the last " + toString(chunkTypeName(chunkType_)) + " in " + string_->toLingo(false);
}

StringChunkCountExprNode::StringChunkCountExprNode(int chunkType, NodePtr string)
    : chunkType_(chunkType), string_(std::move(string)) {
    markExpression();
    adopt(string_);
}

std::string StringChunkCountExprNode::toLingo(bool /*dot*/) const {
    return "the number of " + toString(chunkTypeName(chunkType_)) + "s in " + string_->toLingo(false);
}

SpriteIntersectsExprNode::SpriteIntersectsExprNode(NodePtr first, NodePtr second)
    : first_(std::move(first)), second_(std::move(second)) {
    markExpression();
    adopt(first_);
    adopt(second_);
}

std::string SpriteIntersectsExprNode::toLingo(bool dot) const {
    return "sprite " + first_->toLingo(dot) + " intersects " + second_->toLingo(dot);
}

SpriteWithinExprNode::SpriteWithinExprNode(NodePtr first, NodePtr second)
    : first_(std::move(first)), second_(std::move(second)) {
    markExpression();
    adopt(first_);
    adopt(second_);
}

std::string SpriteWithinExprNode::toLingo(bool dot) const {
    return "sprite " + first_->toLingo(dot) + " within " + second_->toLingo(dot);
}

MenuPropExprNode::MenuPropExprNode(NodePtr menuId, int prop)
    : menuId_(std::move(menuId)), prop_(prop) {
    markExpression();
    adopt(menuId_);
}

std::string MenuPropExprNode::toLingo(bool dot) const {
    return "the " + toString(menuPropertyName(prop_)) + " of menu " + menuId_->toLingo(dot);
}

MenuItemPropExprNode::MenuItemPropExprNode(NodePtr menuId, NodePtr itemId, int prop)
    : menuId_(std::move(menuId)), itemId_(std::move(itemId)), prop_(prop) {
    markExpression();
    adopt(menuId_);
    adopt(itemId_);
}

std::string MenuItemPropExprNode::toLingo(bool dot) const {
    return "the " + toString(menuItemPropertyName(prop_)) + " of menuItem " +
           itemId_->toLingo(dot) + " of menu " + menuId_->toLingo(dot);
}

SoundPropExprNode::SoundPropExprNode(NodePtr soundId, int prop)
    : soundId_(std::move(soundId)), prop_(prop) {
    markExpression();
    adopt(soundId_);
}

std::string SoundPropExprNode::toLingo(bool dot) const {
    return "the " + toString(soundPropertyName(prop_)) + " of sound " + soundId_->toLingo(dot);
}

SpritePropExprNode::SpritePropExprNode(NodePtr spriteId, int prop)
    : spriteId_(std::move(spriteId)), prop_(prop) {
    markExpression();
    adopt(spriteId_);
}

std::string SpritePropExprNode::toLingo(bool dot) const {
    return "the " + toString(spritePropertyName(prop_)) + " of sprite " + spriteId_->toLingo(dot);
}

NewObjNode::NewObjNode(std::string objectType, NodePtr objectArgs)
    : objectType_(std::move(objectType)), objectArgs_(std::move(objectArgs)) {
    markExpression();
    adopt(objectArgs_);
}

std::string NewObjNode::toLingo(bool dot) const {
    return "new " + objectType_ + "(" + objectArgs_->toLingo(dot) + ")";
}

ExitStmtNode::ExitStmtNode() {
    markStatement();
}

std::string ExitStmtNode::toLingo(bool /*dot*/) const {
    return "exit";
}

ExitRepeatStmtNode::ExitRepeatStmtNode() {
    markStatement();
}

std::string ExitRepeatStmtNode::toLingo(bool /*dot*/) const {
    return "exit repeat";
}

NextRepeatStmtNode::NextRepeatStmtNode() {
    markStatement();
}

std::string NextRepeatStmtNode::toLingo(bool /*dot*/) const {
    return "next repeat";
}

AssignmentStmtNode::AssignmentStmtNode(NodePtr variable, NodePtr value, bool forceVerbose)
    : variable_(std::move(variable)), value_(std::move(value)), forceVerbose_(forceVerbose) {
    markStatement();
    adopt(variable_);
    adopt(value_);
}

std::string AssignmentStmtNode::toLingo(bool dot) const {
    if (!dot || forceVerbose_) {
        return "set " + variable_->toLingo(false) + " to " + value_->toLingo(dot);
    }
    return variable_->toLingo(dot) + " = " + value_->toLingo(dot);
}

PutStmtNode::PutStmtNode(int putType, NodePtr variable, NodePtr value)
    : putType_(putType), variable_(std::move(variable)), value_(std::move(value)) {
    markStatement();
    adopt(variable_);
    adopt(value_);
}

std::string PutStmtNode::toLingo(bool dot) const {
    return "put " + value_->toLingo(dot) + " " + toString(putTypeName(putType_)) + " " +
           variable_->toLingo(false);
}

ChunkHiliteStmtNode::ChunkHiliteStmtNode(NodePtr chunk) : chunk_(std::move(chunk)) {
    markStatement();
    adopt(chunk_);
}

std::string ChunkHiliteStmtNode::toLingo(bool dot) const {
    return "hilite " + chunk_->toLingo(dot);
}

ChunkDeleteStmtNode::ChunkDeleteStmtNode(NodePtr chunk) : chunk_(std::move(chunk)) {
    markStatement();
    adopt(chunk_);
}

std::string ChunkDeleteStmtNode::toLingo(bool dot) const {
    return "delete " + chunk_->toLingo(dot);
}

WhenStmtNode::WhenStmtNode(int event, std::string script)
    : event_(event), script_(std::move(script)) {
    markStatement();
}

std::string WhenStmtNode::toLingo(bool /*dot*/) const {
    std::string result = "when " + toString(whenEventName(event_)) + " then ";
    std::size_t index = 0;
    while (index < script_.size()) {
        while (index < script_.size() &&
               std::isspace(static_cast<unsigned char>(script_[index])) &&
               script_[index] != '\r') {
            ++index;
        }
        if (index >= script_.size()) {
            break;
        }
        while (index < script_.size() && script_[index] != '\r') {
            result.push_back(script_[index]);
            ++index;
        }
        if (index >= script_.size()) {
            break;
        }
        if (index < script_.size() - 1) {
            result.append("\n  ");
        }
        ++index;
    }
    return result;
}

CallNode::CallNode(std::string name, NodePtr argList)
    : name_(std::move(name)), argList_(std::move(argList)) {
    adopt(argList_);
    if (argList_->valueType() == ValueType::ArgListNoRet) {
        markStatement();
    } else {
        markExpression();
    }
}

bool CallNode::noParens() const {
    if (isStatement()) {
        return name_ == "put" || name_ == "return";
    }
    return false;
}

std::string CallNode::toLingo(bool dot) const {
    if (isExpression() && argList_->argNodes().empty()) {
        if (name_ == "pi") {
            return "PI";
        }
        if (name_ == "space") {
            return "SPACE";
        }
        if (name_ == "void") {
            return "VOID";
        }
    }
    if (noParens()) {
        return name_ + " " + argList_->toLingo(dot);
    }
    return name_ + "(" + argList_->toLingo(dot) + ")";
}

ObjCallNode::ObjCallNode(std::string name, NodePtr argList)
    : name_(std::move(name)), argList_(std::move(argList)) {
    adopt(argList_);
    if (argList_->valueType() == ValueType::ArgListNoRet) {
        markStatement();
    } else {
        markExpression();
    }
}

std::string ObjCallNode::toLingo(bool dot) const {
    const auto& rawArgs = argList_->argNodes();
    if (rawArgs.empty()) {
        return "???." + name_ + "()";
    }
    std::string result = maybeParenDot(*rawArgs[0], dot) + "." + name_ + "(";
    for (std::size_t index = 1; index < rawArgs.size(); ++index) {
        if (index > 1) {
            result.append(", ");
        }
        result.append(rawArgs[index]->toLingo(dot));
    }
    result.push_back(')');
    return result;
}

ObjCallV4Node::ObjCallV4Node(NodePtr object, NodePtr argList)
    : object_(std::move(object)), argList_(std::move(argList)) {
    adopt(object_);
    adopt(argList_);
    if (argList_->valueType() == ValueType::ArgListNoRet) {
        markStatement();
    } else {
        markExpression();
    }
}

std::string ObjCallV4Node::toLingo(bool dot) const {
    return object_->toLingo(dot) + "(" + argList_->toLingo(dot) + ")";
}

BlockNode::BlockNode() {
    markExpression();
}

void BlockNode::addChild(NodePtr child) {
    adopt(child);
    children_.push_back(std::move(child));
}

std::string BlockNode::toLingo(bool dot) const {
    std::string result;
    for (const auto& child : children_) {
        result.append(indent(child->toLingo(dot)));
        result.push_back('\n');
    }
    return result;
}

HandlerNode::HandlerNode(std::string handlerName, std::vector<std::string> argumentNames, std::vector<std::string> globalNames)
    : handlerName_(std::move(handlerName)),
      argumentNames_(std::move(argumentNames)),
      globalNames_(std::move(globalNames)),
      block_(std::make_unique<BlockNode>()) {
    block_->setParent(this);
}

std::string HandlerNode::toLingo(bool dot) const {
    std::string result = "on " + handlerName_;
    if (!argumentNames_.empty()) {
        result.push_back(' ');
        result.append(joinStrings(argumentNames_, ", "));
    }
    result.push_back('\n');
    if (!globalNames_.empty()) {
        result.append("  global ");
        result.append(joinStrings(globalNames_, ", "));
        result.push_back('\n');
    }
    result.append(block_->toLingo(dot));
    result.append("end");
    return result;
}

IfStmtNode::IfStmtNode(NodePtr condition)
    : condition_(std::move(condition)),
      block1_(std::make_unique<BlockNode>()),
      block2_(std::make_unique<BlockNode>()) {
    markStatement();
    adopt(condition_);
    block1_->setParent(this);
    block2_->setParent(this);
}

std::string IfStmtNode::toLingo(bool dot) const {
    std::string result = "if " + condition_->toLingo(dot) + " then\n";
    result.append(block1_->toLingo(dot));
    if (hasElse_) {
        result.append("else\n");
        result.append(block2_->toLingo(dot));
    }
    result.append("end if");
    return result;
}

LoopNodeBase::LoopNodeBase(int startIndex) : startIndex_(startIndex) {
    markStatement();
    markLoop();
}

RepeatWhileStmtNode::RepeatWhileStmtNode(int startIndex, NodePtr condition)
    : LoopNodeBase(startIndex),
      condition_(std::move(condition)),
      block_(std::make_unique<BlockNode>()) {
    adopt(condition_);
    block_->setParent(this);
}

std::string RepeatWhileStmtNode::toLingo(bool dot) const {
    return "repeat while " + condition_->toLingo(dot) + "\n" +
           block_->toLingo(dot) + "end repeat";
}

RepeatWithInStmtNode::RepeatWithInStmtNode(int startIndex, std::string variableName, NodePtr list)
    : LoopNodeBase(startIndex),
      variableName_(std::move(variableName)),
      list_(std::move(list)),
      block_(std::make_unique<BlockNode>()) {
    adopt(list_);
    block_->setParent(this);
}

std::string RepeatWithInStmtNode::toLingo(bool dot) const {
    return "repeat with " + variableName_ + " in " + list_->toLingo(dot) + "\n" +
           block_->toLingo(dot) + "end repeat";
}

RepeatWithToStmtNode::RepeatWithToStmtNode(int startIndex, std::string variableName, NodePtr start, bool up, NodePtr end)
    : LoopNodeBase(startIndex),
      variableName_(std::move(variableName)),
      start_(std::move(start)),
      up_(up),
      end_(std::move(end)),
      block_(std::make_unique<BlockNode>()) {
    adopt(start_);
    adopt(end_);
    block_->setParent(this);
}

std::string RepeatWithToStmtNode::toLingo(bool dot) const {
    const std::string direction = up_ ? " to " : " down to ";
    return "repeat with " + variableName_ + " = " + start_->toLingo(dot) + direction +
           end_->toLingo(dot) + "\n" + block_->toLingo(dot) + "end repeat";
}

TellStmtNode::TellStmtNode(NodePtr window)
    : window_(std::move(window)), block_(std::make_unique<BlockNode>()) {
    markStatement();
    adopt(window_);
    block_->setParent(this);
}

std::string TellStmtNode::toLingo(bool dot) const {
    return "tell " + window_->toLingo(dot) + "\n" + block_->toLingo(dot) + "end tell";
}

CaseNode::CaseNode(NodePtr value, int expect) : value_(std::move(value)), expect_(expect) {
    adopt(value_);
}

void CaseNode::setNextOr(std::unique_ptr<CaseNode> nextOr) {
    nextOr_ = std::move(nextOr);
    if (nextOr_) {
        nextOr_->setParent(this);
    }
}

void CaseNode::setNextCase(std::unique_ptr<CaseNode> nextCase) {
    nextCase_ = std::move(nextCase);
    if (nextCase_) {
        nextCase_->setParent(this);
    }
}

void CaseNode::setBlock(std::unique_ptr<BlockNode> block) {
    block_ = std::move(block);
    if (block_) {
        block_->setParent(this);
    }
}

void CaseNode::setOtherwise(std::unique_ptr<BlockNode> otherwise) {
    otherwise_ = std::move(otherwise);
    if (otherwise_) {
        otherwise_->setParent(this);
    }
}

std::string CaseNode::toLingo(bool dot) const {
    std::string result = value_->toLingo(dot);
    if (nextOr_) {
        result.append(", ");
        result.append(nextOr_->toLingo(dot));
    } else {
        result.append(":\n");
        if (block_) {
            result.append(block_->toLingo(dot));
        }
    }
    if (nextCase_) {
        result.append(nextCase_->toLingo(dot));
    } else if (otherwise_) {
        result.append("otherwise:\n");
        result.append(otherwise_->toLingo(dot));
    }
    return result;
}

CasesStmtNode::CasesStmtNode(NodePtr value) : value_(std::move(value)) {
    markStatement();
    adopt(value_);
}

void CasesStmtNode::setFirstCase(std::unique_ptr<CaseNode> firstCase) {
    firstCase_ = std::move(firstCase);
    if (firstCase_) {
        firstCase_->setParent(this);
    }
}

std::string CasesStmtNode::toLingo(bool dot) const {
    std::string result = "case " + value_->toLingo(dot) + " of\n";
    if (firstCase_) {
        result.append(indent(firstCase_->toLingo(dot)));
    }
    result.append("end case");
    return result;
}

std::string maybeParenDot(const LingoNode& node, bool dot) {
    const auto text = node.toLingo(dot);
    if (dynamic_cast<const VarNode*>(&node) != nullptr ||
        dynamic_cast<const ObjCallNode*>(&node) != nullptr ||
        dynamic_cast<const ObjCallV4Node*>(&node) != nullptr ||
        dynamic_cast<const CallNode*>(&node) != nullptr ||
        dynamic_cast<const ObjPropExprNode*>(&node) != nullptr ||
        dynamic_cast<const ObjBracketExprNode*>(&node) != nullptr ||
        dynamic_cast<const ObjPropIndexExprNode*>(&node) != nullptr) {
        return text;
    }
    return "(" + text + ")";
}

} // namespace libreshockwave::lingo::decompiler
