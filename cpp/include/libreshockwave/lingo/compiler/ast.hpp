#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace libreshockwave::lingo::compiler {

// The compiler's abstract syntax tree. The parser builds this tree from the
// token stream; the code generator lowers it to ScriptAsset bytecode.
//
// The tree mirrors the constructs the LingoDecompiler emits. Every expression
// and statement type the decompiler can print has a node here, so the compiler
// is a faithful inverse of the decompiler.
//
// Ownership: child nodes are owned by their parent through std::variant of
// unique_ptr. A null child is meaningful in a few places (e.g. an optional
// `last` in a chunk range, an optional `cast_id` in a member expression).

// Binary operator, matching the lingo::Opcode set the decompiler uses.
enum class BinOp {
    Mul, Add, Sub, Div, Mod,
    JoinStr, JoinPadStr,
    Lt, LtEq, NotEq, Eq, Gt, GtEq,
    And, Or,
    ContainsStr, Contains0Str
};

// The `put` direction.
enum class PutDir { Into, After, Before };

// The `when` event.
enum class WhenEvent { MouseDown, MouseUp, KeyDown, KeyUp, TimeOut };

// A string literal, including the decompiler's bare-word forms (VOID, EMPTY,
// ENTER, BACKSPACE, TAB, RETURN, QUOTE, PI, SPACE). `bare` is the original
// token text when the value was printed as a bare word; empty otherwise.
struct StringLit {
    std::string value;
    std::string bare;
};

// Forward declarations so the literal structs can reference Expr.
struct Expr;
struct Stmt;

// A list literal: [a, b, c].
struct ListLit {
    std::vector<std::unique_ptr<Expr>> items;
};

// A property-list literal: [a: 1, b: 2] or the empty form [:].
struct PropListLit {
    enum class KeyType { Symbol, String, Int };
    std::vector<std::string> keys;
    std::vector<KeyType> key_types; // parallel to keys
    std::vector<std::unique_ptr<Expr>> values; // parallel to keys; null value = key only
    bool empty_form = false;
};

// An expression node.
struct Expr {
    enum class Kind {
        Int, Float, String, Symbol, Symb, Var,
        TheExpr, MemberExpr, ObjPropExpr, ObjBracketExpr, ObjPropIndexExpr,
        ThePropExpr, ChunkExpr, LastChunkExpr, ChunkCountExpr,
        SpriteIntersects, SpriteWithin,
        MenuProp, MenuItemProp, SoundProp, SpriteProp,
        NewObj, Call, ObjCall, ObjCallV4,
        List, PropList,
        BinaryOp, InverseOp, NotOp
    };

    Kind kind = Kind::Int;
    // Scalar payloads.
    long long int_value = 0;
    double float_value = 0.0;
    StringLit string;
    std::string name; // Symbol, Var, TheExpr prop, Call/ObjCall method, NewObj type
    int prop_id = 0; // MenuProp, MenuItemProp, SoundProp, SpriteProp
    bool up = false; // reserved
    // Child payloads.
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> index;
    std::unique_ptr<Expr> index2;
    std::unique_ptr<Expr> first;
    std::unique_ptr<Expr> last;
    std::unique_ptr<Expr> source;
    BinOp bin_op = BinOp::Add;
    int chunk_type = 0; // 1 char, 2 word, 3 item, 4 line
    std::string member_type; // "field", "member", "castLib"
    std::vector<std::unique_ptr<Expr>> args;
    std::unique_ptr<ListLit> list;
    std::unique_ptr<PropListLit> prop_list;

    static std::unique_ptr<Expr> intLit(long long value) {
        auto e = std::make_unique<Expr>();
        e->kind = Kind::Int;
        e->int_value = value;
        return e;
    }
    static std::unique_ptr<Expr> floatLit(double value) {
        auto e = std::make_unique<Expr>();
        e->kind = Kind::Float;
        e->float_value = value;
        return e;
    }
    static std::unique_ptr<Expr> stringLit(std::string value, std::string bare = "") {
        auto e = std::make_unique<Expr>();
        e->kind = Kind::String;
        e->string.value = std::move(value);
        e->string.bare = std::move(bare);
        return e;
    }
    static std::unique_ptr<Expr> symbol(std::string name) {
        auto e = std::make_unique<Expr>();
        e->kind = Kind::Symbol;
        e->name = std::move(name);
        return e;
    }
    static std::unique_ptr<Expr> symb(int id) {
        auto e = std::make_unique<Expr>();
        e->kind = Kind::Symb;
        e->int_value = id;
        return e;
    }
    static std::unique_ptr<Expr> var(std::string name) {
        auto e = std::make_unique<Expr>();
        e->kind = Kind::Var;
        e->name = std::move(name);
        return e;
    }
};

// A statement node.
struct Stmt {
    enum class Kind {
        If, RepeatWhile, RepeatWithIn, RepeatWithTo, Tell, Case,
        Exit, ExitRepeat, NextRepeat, Return,
        Assignment, Put, Hilite, Delete, Call, When
    };

    Kind kind = Kind::Exit;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Expr> variable;
    std::unique_ptr<Expr> value;
    std::unique_ptr<Expr> window; // Tell
    std::unique_ptr<Expr> case_value;
    std::unique_ptr<Expr> loop_var; // RepeatWithIn/To (a Var expr)
    std::unique_ptr<Expr> start; // RepeatWithTo
    std::unique_ptr<Expr> end; // RepeatWithTo
    std::string name; // Call
    std::vector<std::unique_ptr<Expr>> args; // Call
    std::vector<std::unique_ptr<Stmt>> then_block;
    std::vector<std::unique_ptr<Stmt>> else_block;
    std::vector<std::unique_ptr<Stmt>> block;
    bool up = false; // RepeatWithTo: true = "to", false = "down to"
    PutDir put_dir = PutDir::Into;
    WhenEvent when_event = WhenEvent::MouseDown;
    std::string when_script; // the raw when payload
    // Case branches: each entry is (label values, block). otherwise_ marks the
    // trailing `otherwise:` branch.
    struct CaseBranch {
        std::vector<std::unique_ptr<Expr>> labels;
        std::vector<std::unique_ptr<Stmt>> body;
        bool is_otherwise = false;
    };
    std::vector<CaseBranch> case_branches;
};

// A handler: on <name> [args] ... end.
struct Handler {
    std::string name;
    std::vector<std::string> arg_names;
    std::vector<std::string> global_names;
    std::vector<std::unique_ptr<Stmt>> statements;
};

// A script: the top-level compilation unit.
struct Script {
    std::vector<std::string> property_names;
    std::vector<std::string> global_names;
    std::vector<std::unique_ptr<Handler>> handlers;
};

} // namespace libreshockwave::lingo::compiler
