#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "libreshockwave/lingo/Opcode.hpp"

namespace libreshockwave::lingo::decompiler {

class LingoNode;
class LoopNodeBase;

using NodePtr = std::unique_ptr<LingoNode>;

template <typename T, typename... Args>
std::unique_ptr<T> makeNode(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}

enum class ValueType {
    Void,
    Symbol,
    VarRef,
    String,
    Int,
    Float,
    List,
    ArgList,
    ArgListNoRet,
    PropList
};

class LingoNode {
public:
    virtual ~LingoNode() = default;

    [[nodiscard]] virtual std::string toLingo(bool dot) const = 0;

    [[nodiscard]] virtual ValueType valueType() const;
    [[nodiscard]] virtual int intValue() const;
    [[nodiscard]] virtual std::string stringValue() const;
    [[nodiscard]] virtual const std::vector<NodePtr>& argNodes() const;
    virtual void setValueType(ValueType type);

    [[nodiscard]] LingoNode* parent() const { return parent_; }
    void setParent(LingoNode* parent) { parent_ = parent; }
    [[nodiscard]] bool isExpression() const { return isExpression_; }
    [[nodiscard]] bool isStatement() const { return isStatement_; }
    [[nodiscard]] bool isLoop() const { return isLoop_; }
    [[nodiscard]] int bytecodeOffset() const { return bytecodeOffset_; }
    void setBytecodeOffset(int offset) { bytecodeOffset_ = offset; }

    [[nodiscard]] LingoNode* ancestorStatement() const;
    [[nodiscard]] LoopNodeBase* ancestorLoop() const;

    [[nodiscard]] static std::string indent(std::string_view text);

protected:
    void markExpression() { isExpression_ = true; }
    void markStatement() { isStatement_ = true; }
    void markLoop() { isLoop_ = true; }
    void adopt(const NodePtr& child);

private:
    LingoNode* parent_ = nullptr;
    bool isExpression_ = false;
    bool isStatement_ = false;
    bool isLoop_ = false;
    int bytecodeOffset_ = -1;
};

class ErrorNode final : public LingoNode {
public:
    ErrorNode();
    [[nodiscard]] std::string toLingo(bool dot) const override;
};

class CommentNode final : public LingoNode {
public:
    explicit CommentNode(std::string text);
    [[nodiscard]] const std::string& text() const { return text_; }
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    std::string text_;
};

class LiteralNode final : public LingoNode {
public:
    explicit LiteralNode(int value);
    explicit LiteralNode(double value);
    LiteralNode(ValueType type, std::string value);
    LiteralNode(ValueType type, std::vector<NodePtr> items);

    [[nodiscard]] ValueType valueType() const override;
    [[nodiscard]] int intValue() const override;
    [[nodiscard]] double floatValue() const;
    [[nodiscard]] std::string stringValue() const override;
    [[nodiscard]] const std::vector<NodePtr>& argNodes() const override;
    [[nodiscard]] std::vector<NodePtr> takeItems();
    void setValueType(ValueType type) override;
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    ValueType valueType_ = ValueType::Void;
    int intValue_ = 0;
    double floatValue_ = 0.0;
    std::string stringValue_;
    std::vector<NodePtr> items_;
};

class VarNode final : public LingoNode {
public:
    explicit VarNode(std::string name);
    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    std::string name_;
};

class InverseOpNode final : public LingoNode {
public:
    explicit InverseOpNode(NodePtr operand);
    [[nodiscard]] const LingoNode& operand() const { return *operand_; }
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr operand_;
};

class NotOpNode final : public LingoNode {
public:
    explicit NotOpNode(NodePtr operand);
    [[nodiscard]] const LingoNode& operand() const { return *operand_; }
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr operand_;
};

class BinaryOpNode final : public LingoNode {
public:
    BinaryOpNode(Opcode opcode, NodePtr left, NodePtr right);
    [[nodiscard]] Opcode opcode() const { return opcode_; }
    [[nodiscard]] const LingoNode& left() const { return *left_; }
    [[nodiscard]] const LingoNode& right() const { return *right_; }
    [[nodiscard]] int precedence() const;
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    Opcode opcode_;
    NodePtr left_;
    NodePtr right_;
};

class TheExprNode final : public LingoNode {
public:
    explicit TheExprNode(std::string prop);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    std::string prop_;
};

class MemberExprNode final : public LingoNode {
public:
    MemberExprNode(std::string memberType, NodePtr memberId, NodePtr castId = nullptr);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    std::string memberType_;
    NodePtr memberId_;
    NodePtr castId_;
};

class ObjPropExprNode final : public LingoNode {
public:
    ObjPropExprNode(NodePtr object, std::string prop);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr object_;
    std::string prop_;
};

class ObjBracketExprNode final : public LingoNode {
public:
    ObjBracketExprNode(NodePtr object, NodePtr prop);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr object_;
    NodePtr prop_;
};

class ObjPropIndexExprNode final : public LingoNode {
public:
    ObjPropIndexExprNode(NodePtr object, std::string prop, NodePtr index, NodePtr index2 = nullptr);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr object_;
    std::string prop_;
    NodePtr index_;
    NodePtr index2_;
};

class ThePropExprNode final : public LingoNode {
public:
    ThePropExprNode(NodePtr object, std::string prop);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr object_;
    std::string prop_;
};

class ChunkExprNode final : public LingoNode {
public:
    ChunkExprNode(int chunkType, NodePtr first, NodePtr last, NodePtr string);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    int chunkType_;
    NodePtr first_;
    NodePtr last_;
    NodePtr string_;
};

class LastStringChunkExprNode final : public LingoNode {
public:
    LastStringChunkExprNode(int chunkType, NodePtr string);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    int chunkType_;
    NodePtr string_;
};

class StringChunkCountExprNode final : public LingoNode {
public:
    StringChunkCountExprNode(int chunkType, NodePtr string);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    int chunkType_;
    NodePtr string_;
};

class SpriteIntersectsExprNode final : public LingoNode {
public:
    SpriteIntersectsExprNode(NodePtr first, NodePtr second);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr first_;
    NodePtr second_;
};

class SpriteWithinExprNode final : public LingoNode {
public:
    SpriteWithinExprNode(NodePtr first, NodePtr second);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr first_;
    NodePtr second_;
};

class MenuPropExprNode final : public LingoNode {
public:
    MenuPropExprNode(NodePtr menuId, int prop);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr menuId_;
    int prop_;
};

class MenuItemPropExprNode final : public LingoNode {
public:
    MenuItemPropExprNode(NodePtr menuId, NodePtr itemId, int prop);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr menuId_;
    NodePtr itemId_;
    int prop_;
};

class SoundPropExprNode final : public LingoNode {
public:
    SoundPropExprNode(NodePtr soundId, int prop);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr soundId_;
    int prop_;
};

class SpritePropExprNode final : public LingoNode {
public:
    SpritePropExprNode(NodePtr spriteId, int prop);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr spriteId_;
    int prop_;
};

class NewObjNode final : public LingoNode {
public:
    NewObjNode(std::string objectType, NodePtr objectArgs);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    std::string objectType_;
    NodePtr objectArgs_;
};

class ExitStmtNode final : public LingoNode {
public:
    ExitStmtNode();
    [[nodiscard]] std::string toLingo(bool dot) const override;
};

class ExitRepeatStmtNode final : public LingoNode {
public:
    ExitRepeatStmtNode();
    [[nodiscard]] std::string toLingo(bool dot) const override;
};

class NextRepeatStmtNode final : public LingoNode {
public:
    NextRepeatStmtNode();
    [[nodiscard]] std::string toLingo(bool dot) const override;
};

class AssignmentStmtNode final : public LingoNode {
public:
    AssignmentStmtNode(NodePtr variable, NodePtr value, bool forceVerbose = false);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr variable_;
    NodePtr value_;
    bool forceVerbose_ = false;
};

class PutStmtNode final : public LingoNode {
public:
    PutStmtNode(int putType, NodePtr variable, NodePtr value);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    int putType_;
    NodePtr variable_;
    NodePtr value_;
};

class ChunkHiliteStmtNode final : public LingoNode {
public:
    explicit ChunkHiliteStmtNode(NodePtr chunk);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr chunk_;
};

class ChunkDeleteStmtNode final : public LingoNode {
public:
    explicit ChunkDeleteStmtNode(NodePtr chunk);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr chunk_;
};

class WhenStmtNode final : public LingoNode {
public:
    WhenStmtNode(int event, std::string script);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    int event_;
    std::string script_;
};

class CallNode final : public LingoNode {
public:
    CallNode(std::string name, NodePtr argList);
    [[nodiscard]] bool noParens() const;
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    std::string name_;
    NodePtr argList_;
};

class ObjCallNode final : public LingoNode {
public:
    ObjCallNode(std::string name, NodePtr argList);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    std::string name_;
    NodePtr argList_;
};

class ObjCallV4Node final : public LingoNode {
public:
    ObjCallV4Node(NodePtr object, NodePtr argList);
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr object_;
    NodePtr argList_;
};

class CaseNode;

class BlockNode final : public LingoNode {
public:
    BlockNode();
    void addChild(NodePtr child);
    [[nodiscard]] const std::vector<NodePtr>& children() const { return children_; }
    [[nodiscard]] std::string toLingo(bool dot) const override;

    int endPos = -1;
    CaseNode* currentCase = nullptr;

private:
    std::vector<NodePtr> children_;
};

class HandlerNode final : public LingoNode {
public:
    HandlerNode(std::string handlerName, std::vector<std::string> argumentNames, std::vector<std::string> globalNames);
    [[nodiscard]] const std::string& handlerName() const { return handlerName_; }
    [[nodiscard]] const std::vector<std::string>& argumentNames() const { return argumentNames_; }
    [[nodiscard]] const std::vector<std::string>& globalNames() const { return globalNames_; }
    [[nodiscard]] BlockNode& block() { return *block_; }
    [[nodiscard]] const BlockNode& block() const { return *block_; }
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    std::string handlerName_;
    std::vector<std::string> argumentNames_;
    std::vector<std::string> globalNames_;
    std::unique_ptr<BlockNode> block_;
};

class IfStmtNode final : public LingoNode {
public:
    explicit IfStmtNode(NodePtr condition);
    [[nodiscard]] const LingoNode& condition() const { return *condition_; }
    [[nodiscard]] BlockNode& trueBlock() { return *block1_; }
    [[nodiscard]] const BlockNode& trueBlock() const { return *block1_; }
    [[nodiscard]] BlockNode& falseBlock() { return *block2_; }
    [[nodiscard]] const BlockNode& falseBlock() const { return *block2_; }
    void setHasElse(bool hasElse) { hasElse_ = hasElse; }
    [[nodiscard]] bool hasElse() const { return hasElse_; }
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr condition_;
    std::unique_ptr<BlockNode> block1_;
    std::unique_ptr<BlockNode> block2_;
    bool hasElse_ = false;
};

class LoopNodeBase : public LingoNode {
public:
    explicit LoopNodeBase(int startIndex);
    [[nodiscard]] int startIndex() const { return startIndex_; }

private:
    int startIndex_;
};

class RepeatWhileStmtNode final : public LoopNodeBase {
public:
    RepeatWhileStmtNode(int startIndex, NodePtr condition);
    [[nodiscard]] const LingoNode& condition() const { return *condition_; }
    [[nodiscard]] BlockNode& block() { return *block_; }
    [[nodiscard]] const BlockNode& block() const { return *block_; }
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr condition_;
    std::unique_ptr<BlockNode> block_;
};

class RepeatWithInStmtNode final : public LoopNodeBase {
public:
    RepeatWithInStmtNode(int startIndex, std::string variableName, NodePtr list);
    [[nodiscard]] const std::string& variableName() const { return variableName_; }
    [[nodiscard]] const LingoNode& list() const { return *list_; }
    [[nodiscard]] BlockNode& block() { return *block_; }
    [[nodiscard]] const BlockNode& block() const { return *block_; }
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    std::string variableName_;
    NodePtr list_;
    std::unique_ptr<BlockNode> block_;
};

class RepeatWithToStmtNode final : public LoopNodeBase {
public:
    RepeatWithToStmtNode(int startIndex, std::string variableName, NodePtr start, bool up, NodePtr end);
    [[nodiscard]] const std::string& variableName() const { return variableName_; }
    [[nodiscard]] const LingoNode& start() const { return *start_; }
    [[nodiscard]] bool up() const { return up_; }
    [[nodiscard]] const LingoNode& end() const { return *end_; }
    [[nodiscard]] BlockNode& block() { return *block_; }
    [[nodiscard]] const BlockNode& block() const { return *block_; }
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    std::string variableName_;
    NodePtr start_;
    bool up_;
    NodePtr end_;
    std::unique_ptr<BlockNode> block_;
};

class TellStmtNode final : public LingoNode {
public:
    explicit TellStmtNode(NodePtr window);
    [[nodiscard]] const LingoNode& window() const { return *window_; }
    [[nodiscard]] BlockNode& block() { return *block_; }
    [[nodiscard]] const BlockNode& block() const { return *block_; }
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr window_;
    std::unique_ptr<BlockNode> block_;
};

class CaseNode final : public LingoNode {
public:
    static constexpr int EXPECT_POP = 0;
    static constexpr int EXPECT_OR = 1;
    static constexpr int EXPECT_NEXT = 2;
    static constexpr int EXPECT_OTHERWISE = 3;

    CaseNode(NodePtr value, int expect);
    void setNextOr(std::unique_ptr<CaseNode> nextOr);
    void setNextCase(std::unique_ptr<CaseNode> nextCase);
    void setBlock(std::unique_ptr<BlockNode> block);
    void setOtherwise(std::unique_ptr<BlockNode> otherwise);
    [[nodiscard]] const LingoNode& value() const { return *value_; }
    [[nodiscard]] int expect() const { return expect_; }
    [[nodiscard]] CaseNode* nextOr() { return nextOr_.get(); }
    [[nodiscard]] const CaseNode* nextOr() const { return nextOr_.get(); }
    [[nodiscard]] CaseNode* nextCase() { return nextCase_.get(); }
    [[nodiscard]] const CaseNode* nextCase() const { return nextCase_.get(); }
    [[nodiscard]] BlockNode* block() { return block_.get(); }
    [[nodiscard]] const BlockNode* block() const { return block_.get(); }
    [[nodiscard]] BlockNode* otherwise() { return otherwise_.get(); }
    [[nodiscard]] const BlockNode* otherwise() const { return otherwise_.get(); }
    [[nodiscard]] std::string toLingo(bool dot) const override;

private:
    NodePtr value_;
    int expect_;
    std::unique_ptr<CaseNode> nextOr_;
    std::unique_ptr<CaseNode> nextCase_;
    std::unique_ptr<BlockNode> block_;
    std::unique_ptr<BlockNode> otherwise_;
};

class CasesStmtNode final : public LingoNode {
public:
    explicit CasesStmtNode(NodePtr value);
    void setFirstCase(std::unique_ptr<CaseNode> firstCase);
    [[nodiscard]] const LingoNode& value() const { return *value_; }
    [[nodiscard]] CaseNode* firstCase() { return firstCase_.get(); }
    [[nodiscard]] const CaseNode* firstCase() const { return firstCase_.get(); }
    [[nodiscard]] std::string toLingo(bool dot) const override;

    int endPos = -1;

private:
    NodePtr value_;
    std::unique_ptr<CaseNode> firstCase_;
};

[[nodiscard]] std::string maybeParenDot(const LingoNode& node, bool dot);

} // namespace libreshockwave::lingo::decompiler
