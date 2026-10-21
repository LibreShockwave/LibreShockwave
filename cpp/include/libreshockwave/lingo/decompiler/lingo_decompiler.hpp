#pragma once

#include <string>
#include <vector>

#include "libreshockwave/chunks/script_chunk.hpp"
#include "libreshockwave/lingo/decompiler/lingo_node.hpp"

namespace libreshockwave::chunks {
class ScriptNamesChunk;
}

namespace libreshockwave::lingo::decompiler {

class LingoDecompiler {
public:
    struct DecompiledLine {
        std::string text;
        int bytecode_offset = -1;
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

    [[nodiscard]] static DecompiledHandler buildLineMapping(const HandlerNode& handler, bool dot_syntax);

private:
    const chunks::ScriptChunk* script_ = nullptr;
    const chunks::ScriptNamesChunk* names_ = nullptr;
    const chunks::ScriptChunk::Handler* current_handler_ = nullptr;
    int version_ = 0x4C1;
    bool capital_x_ = false;
    bool dot_syntax_ = false;
    BlockNode* current_block_ = nullptr;
    std::vector<int> tags_;
    std::vector<int> owner_loops_;
    std::vector<NodePtr> stack_;
    std::size_t last_consumed_ = 1;

    void initFileInfo(const chunks::ScriptChunk& script);
    void tagLoops();
    [[nodiscard]] bool isRepeatWithInLoop(std::size_t start_index, int end_index) const;
    [[nodiscard]] bool isRepeatWithToLoop(std::size_t start_index, int end_index) const;
    [[nodiscard]] int instructionIndexForOffset(int offset) const;
    [[nodiscard]] std::string getVarNameFromSet(const chunks::ScriptChunk::Instruction& instruction) const;
    [[nodiscard]] std::unique_ptr<HandlerNode> translateHandler(const chunks::ScriptChunk::Handler& handler);
    void translateInstruction(const chunks::ScriptChunk::Instruction& instruction,
                              std::size_t index,
                              BlockNode& block);
    void translatePeek(const chunks::ScriptChunk::Instruction& instruction, std::size_t index, BlockNode& block);
    void translateObjCall(int bytecode_offset, int name_id, BlockNode& block);
    void enterBlock(BlockNode& block);
    void exitBlock();
    [[nodiscard]] NodePtr popNode();
    [[nodiscard]] std::vector<NodePtr> takeArgNodes(NodePtr arg_list) const;
    [[nodiscard]] NodePtr readVar(int var_type);
    [[nodiscard]] NodePtr readChunkRef(NodePtr string);
    [[nodiscard]] NodePtr readV4Property(int property_type, int property_id);
    [[nodiscard]] int variableMultiplier() const;
    [[nodiscard]] std::string resolveName(int name_id) const;
    // True when a name id resolves in the script's name table. Stale ids in
    // empty script records are not printed.
    [[nodiscard]] bool hasValidName(int name_id) const;
    [[nodiscard]] std::string getArgumentName(int raw_index) const;
    [[nodiscard]] std::string getLocalName(int raw_index) const;
    [[nodiscard]] NodePtr literalToNode(const chunks::ScriptChunk::LiteralEntry& literal) const;
    [[nodiscard]] static bool isZeroLiteral(const LingoNode& node);
};

} // namespace libreshockwave::lingo::decompiler
