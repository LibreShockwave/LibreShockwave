#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "libreshockwave/lingo/compiler/ast.hpp"
#include "libreshockwave/lingo/compiler/compile_error.hpp"
#include "libreshockwave/moviesource/records/script_asset.hpp"

namespace libreshockwave::lingo::compiler {

// Lowers a compiled Script AST to a moviesource::ScriptAsset (Director
// bytecode). It is the inverse of the LingoDecompiler: every construct the
// decompiler can print is lowered back to the opcodes Director emits.
//
// The name table is provided by the caller (it is carried in the .lsw
// manifest). The generator looks up each name's id in that table, so a
// round-trip .lsw reproduces the original name ids exactly. For a hand-authored
// .lsw the caller builds the table from the text.
//
// Usage:
//   CodeGenerator gen(std::move(ast), name_table, version, capital_x);
//   auto asset = gen.compile();  // throws CompileError on an unknown name
class CodeGenerator {
public:
    CodeGenerator(std::unique_ptr<Script> script,
                  const std::vector<std::string>& name_table,
                  int version,
                  bool capital_x);

    // Lower the script to a ScriptAsset. Throws CompileError.
    [[nodiscard]] moviesource::ScriptAsset compile();

private:
    std::unique_ptr<Script> script_;
    const std::vector<std::string>& names_;
    int version_;
    int mult_;

    // Per-handler emission state.
    moviesource::ScriptAsset::Handler* handler_ = nullptr;
    int offset_ = 0;
    // The current handler's local/arg index maps (name -> slot).
    std::unordered_map<std::string, int> arg_slot_;
    std::unordered_map<std::string, int> local_slot_;
    // Locals declared by the body (in first-use order) and their name ids.
    std::vector<std::string> local_names_;
    // Names declared as globals or properties (never locals).
    std::unordered_set<std::string> declared_names_;
    // Names declared as globals in this script or the current handler.
    std::unordered_set<std::string> global_names_;
    // Number of case statements whose case value is still on the stack. A
    // return inside a case pops all of them so the return arg list is clean.
    int case_depth_ = 0;
    // Nesting depth of the statement block being emitted. The handler body is
    // depth 1. Director lowers a top-level exit to RET and a nested exit to a
    // no-argument return call.
    int block_depth_ = 0;
    // Per-loop jump lists for `next repeat` and `exit repeat`. Each loop pushes
    // a context and patches the recorded JMPs once its targets are known.
    struct LoopContext {
        std::vector<int> continue_jumps;
        std::vector<int> break_jumps;
    };
    std::vector<LoopContext> loop_stack_;

    // Per-script literal table (in first-use order).
    std::vector<moviesource::ScriptAsset::LiteralEntry> literals_;
    std::unordered_map<std::string, int> literal_index_;

    // Name table lookup.
    [[nodiscard]] int nameId(const std::string& name);

    // Instruction emission (chooses the raw_opcode width from the argument).
    void emit(lingo::Opcode op, int argument);
    void emitNoArg(lingo::Opcode op);
    // Emits an integer as PUSH_ZERO / PUSH_INT8 / PUSH_INT16 / PUSH_INT32,
    // choosing the narrowest encoding that fits the value.
    void emitInt(long long value);
    void patchJump(int from_offset, int target);

    // Literal interning.
    [[nodiscard]] int internString(const std::string& value);
    [[nodiscard]] int internInt(long long value);
    [[nodiscard]] int internFloat(double value);
    [[nodiscard]] int internSymbol(const std::string& name);

    // Handler compilation.
    void compileHandler(const Handler& handler_ast, moviesource::ScriptAsset& asset);
    // First pass: collect the handler's locals (first-assignment order).
    void collectLocals(const Handler& handler_ast);
    void collectLocalsStmt(const Stmt& stmt);
    void collectLocalsExpr(const Expr& expr);
    void markLocal(const std::string& name);

    // Statement and expression lowering.
    void emitBlock(const std::vector<std::unique_ptr<Stmt>>& block);
    void emitStmt(const Stmt& stmt);
    void emitExpr(const Expr& expr);
    // Emits an expression in receiver position. A property access used as the
    // receiver of a further access is a chained read (GET_CHAINED_PROP); the
    // outermost access of the chain is a plain GET_OBJ_PROP.
    void emitReceiver(const Expr& expr);
    // Emits `base.prop[index]` as the getProp (value) or getPropRef (receiver)
    // object call. The decompiler prints both forms identically.
    void emitObjPropIndex(const Expr& expr, bool by_ref);
    void emitLvalue(const Expr& expr);
    // Emits the eight chunk bounds (first/last char, word, item, line) in the
    // order PUT_CHUNK expects. Missing bounds become PUSH_ZERO.
    void emitChunkBounds(const Expr& chunk);
    // Emits a variable reference for PUT and PUT_CHUNK and returns the var_type
    // nibble that identifies it (1 global, 3 property, 4 arg, 5 local).
    [[nodiscard]] int emitVarRefForPut(const Expr& variable);
    void emitVarGet(const std::string& name);
    void emitSetForVar(const std::string& name);
    [[nodiscard]] bool isScriptProperty(const std::string& name);
    [[nodiscard]] bool isTopLevelBuiltin(const std::string& name);
    void emitArgs(const std::vector<std::unique_ptr<Expr>>& args, bool no_ret);
    void emitCall(const std::string& name,
                  const std::vector<std::unique_ptr<Expr>>& args,
                  bool is_local,
                  bool no_ret);
    [[nodiscard]] lingo::Opcode binOpToOpcode(BinOp op);

    // Loop lowering (the exact Director patterns the decompiler recognizes).
    void emitRepeatWithIn(const Stmt& stmt);
    void emitRepeatWithTo(const Stmt& stmt);
    void emitRepeatWhile(const Stmt& stmt);
    void emitCase(const Stmt& stmt);
};

} // namespace libreshockwave::lingo::compiler
