#pragma once

#include <string>
#include <vector>

#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/lingo/decompiler/LingoNode.hpp"

namespace libreshockwave::chunks {
class ScriptNamesChunk;
}

namespace libreshockwave::lingo::decompiler {

class LingoDecompiler {
public:
    struct DecompiledLine {
        std::string text;
        int bytecodeOffset = -1;
    };

    struct DecompiledHandler {
        std::vector<DecompiledLine> lines;

        [[nodiscard]] std::string toText() const;
    };

    [[nodiscard]] std::string decompile(const chunks::ScriptChunk& script,
                                        const chunks::ScriptNamesChunk* names = nullptr);
    [[nodiscard]] std::string decompileHandler(const chunks::ScriptChunk::Handler& handler,
                                               const chunks::ScriptChunk& script,
                                               const chunks::ScriptNamesChunk* names = nullptr);
    [[nodiscard]] DecompiledHandler decompileHandlerWithMapping(const chunks::ScriptChunk::Handler& handler,
                                                                const chunks::ScriptChunk& script,
                                                                const chunks::ScriptNamesChunk* names = nullptr);
    [[nodiscard]] std::string formatHandlerBytecodeOnly(const chunks::ScriptChunk::Handler& handler,
                                                        const chunks::ScriptNamesChunk* names = nullptr) const;

    /** Translate a single handler to runnable TypeScript via the LingoNode AST. */
    [[nodiscard]] std::string emitTypeScriptHandler(const chunks::ScriptChunk::Handler& handler,
                                                    const chunks::ScriptChunk& script,
                                                    const chunks::ScriptNamesChunk* names = nullptr);

    [[nodiscard]] static DecompiledHandler buildLineMapping(const HandlerNode& handler, bool dotSyntax);

private:
    const chunks::ScriptChunk* script_ = nullptr;
    const chunks::ScriptNamesChunk* names_ = nullptr;
    const chunks::ScriptChunk::Handler* currentHandler_ = nullptr;
    int version_ = 0x4C1;
    bool capitalX_ = false;
    bool dotSyntax_ = false;
    BlockNode* currentBlock_ = nullptr;
    std::vector<int> tags_;
    std::vector<int> ownerLoops_;
    std::vector<NodePtr> stack_;
    std::size_t lastConsumed_ = 1;

    void initFileInfo(const chunks::ScriptChunk& script);
    void tagLoops();
    [[nodiscard]] bool isRepeatWithInLoop(std::size_t startIndex, int endIndex) const;
    [[nodiscard]] bool isRepeatWithToLoop(std::size_t startIndex, int endIndex) const;
    [[nodiscard]] int instructionIndexForOffset(int offset) const;
    [[nodiscard]] std::string getVarNameFromSet(const chunks::ScriptChunk::Instruction& instruction) const;
    [[nodiscard]] std::unique_ptr<HandlerNode> translateHandler(const chunks::ScriptChunk::Handler& handler);
    void translateInstruction(const chunks::ScriptChunk::Instruction& instruction,
                              std::size_t index,
                              BlockNode& block);
    void translatePeek(const chunks::ScriptChunk::Instruction& instruction, std::size_t index, BlockNode& block);
    void translateObjCall(int bytecodeOffset, int nameId, BlockNode& block);
    void enterBlock(BlockNode& block);
    void exitBlock();
    [[nodiscard]] NodePtr popNode();
    [[nodiscard]] std::vector<NodePtr> takeArgNodes(NodePtr argList) const;
    [[nodiscard]] NodePtr readVar(int varType);
    [[nodiscard]] NodePtr readChunkRef(NodePtr string);
    [[nodiscard]] NodePtr readV4Property(int propertyType, int propertyId);
    [[nodiscard]] int variableMultiplier() const;
    [[nodiscard]] std::string resolveName(int nameId) const;
    [[nodiscard]] std::string getArgumentName(int rawIndex) const;
    [[nodiscard]] std::string getLocalName(int rawIndex) const;
    [[nodiscard]] NodePtr literalToNode(const chunks::ScriptChunk::LiteralEntry& literal) const;
    [[nodiscard]] static bool isZeroLiteral(const LingoNode& node);
};

} // namespace libreshockwave::lingo::decompiler
