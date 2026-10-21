#pragma once

#include <memory>
#include <string>
#include <vector>

#include "libreshockwave/lingo/compiler/ast.hpp"
#include "libreshockwave/lingo/compiler/compile_error.hpp"
#include "libreshockwave/lingo/compiler/lexer.hpp"

namespace libreshockwave::lingo::compiler {

// Parses a token stream into the compiler AST. It is a recursive-descent parser
// over the decompiler's text grammar. It is the inverse of the LingoDecompiler:
// every construct the decompiler can print is parsed back here.
//
// Usage:
//   Parser parser(tokens);
//   auto script = parser.parseScript();  // throws CompileError on bad input
//
// The parser is strict: an unexpected token is a CompileError. It does not
// recover from errors; the first error aborts the parse.
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    // Parse a full script (properties, globals, handlers). Throws CompileError.
    [[nodiscard]] std::unique_ptr<Script> parseScript();

private:
    std::vector<Token> tokens_;
    std::size_t pos_ = 0;

    // Token stream access.
    [[nodiscard]] const Token& cur() const;
    [[nodiscard]] const Token& peekAt(std::size_t offset) const;
    void advance();
    [[nodiscard]] bool check(TokenType type) const;
    [[nodiscard]] bool checkKeyword(const std::string& word) const;
    void expect(TokenType type);
    void expectKeyword(const std::string& word);
    // Checks for an identifier, captures its text, advances, and returns the
    // text. (expect() advances, so cur().text after it is the next token.)
    [[nodiscard]] std::string takeIdent();
    // Builds the literal for a bare word the decompiler prints (VOID, EMPTY,
    // ENTER, BACKSPACE, TAB, RETURN, QUOTE). Returns nullptr if the word is not
    // one of those (the caller then treats it as a symbol or variable).
    [[nodiscard]] std::unique_ptr<Expr> makeBareWordLiteral(const std::string& word) const;
    // True if the current token is a case label (a literal followed by a colon).
    [[nodiscard]] bool isCaseLabelStart() const;
    [[nodiscard]] bool atEof() const;
    void skipNewlines();
    [[nodiscard]] bool checkNewline() const;

    // Grammar.
    [[nodiscard]] std::unique_ptr<Handler> parseHandler();
    [[nodiscard]] std::vector<std::unique_ptr<Stmt>> parseBlock(const std::string& terminator);
    [[nodiscard]] std::unique_ptr<Stmt> parseStatement();
    [[nodiscard]] std::unique_ptr<Stmt> parseIf();
    [[nodiscard]] std::unique_ptr<Stmt> parseRepeat();
    [[nodiscard]] std::unique_ptr<Stmt> parseTell();
    [[nodiscard]] std::unique_ptr<Stmt> parseCase();
    [[nodiscard]] std::unique_ptr<Stmt> parseReturn();
    [[nodiscard]] std::unique_ptr<Expr> parseCaseLabel();
    [[nodiscard]] std::unique_ptr<Stmt> parseWhen();
    [[nodiscard]] std::unique_ptr<Stmt> parseCallOrAssignment();
    [[nodiscard]] std::unique_ptr<Expr> parseExpression();
    [[nodiscard]] std::unique_ptr<Expr> parseJoin();
    [[nodiscard]] std::unique_ptr<Expr> parseOr();
    [[nodiscard]] std::unique_ptr<Expr> parseAnd();
    [[nodiscard]] std::unique_ptr<Expr> parseComparison();
    [[nodiscard]] std::unique_ptr<Expr> parseAdditive();
    [[nodiscard]] std::unique_ptr<Expr> parseMultiplicative();
    [[nodiscard]] std::unique_ptr<Expr> parseUnary();
    [[nodiscard]] std::unique_ptr<Expr> parsePostfix();
    [[nodiscard]] std::unique_ptr<Expr> parsePrimary();
    [[nodiscard]] std::unique_ptr<Expr> parseLvalue();
    [[nodiscard]] std::vector<std::unique_ptr<Expr>> parseArgList();
    [[nodiscard]] std::vector<std::string> parseNameList();
    [[nodiscard]] std::unique_ptr<Expr> parseTheExpr();
    [[nodiscard]] std::unique_ptr<Expr> finishTheExpr(const std::string& prop);
    [[nodiscard]] int chunkTypeFromWord(const std::string& word);
    [[nodiscard]] std::unique_ptr<Expr> parseMemberExpr();
    [[nodiscard]] std::unique_ptr<Expr> parseChunkExpr();
    [[nodiscard]] std::unique_ptr<Expr> parseListLiteral();
    [[nodiscard]] std::unique_ptr<Expr> parsePropListLiteral();
    [[nodiscard]] std::unique_ptr<Expr> makeBinary(BinOp op,
                                                std::unique_ptr<Expr> left,
                                                std::unique_ptr<Expr> right);
};

} // namespace libreshockwave::lingo::compiler
