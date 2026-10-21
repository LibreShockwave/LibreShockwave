#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "libreshockwave/lingo/compiler/compile_error.hpp"

namespace libreshockwave::lingo::compiler {

// A single lexical token from decompiled Lingo text. The lexer is the first
// stage of the lingo/compiler pipeline. It turns the decompiler's text output
// into a flat token stream the parser consumes.
//
// Newline tokens are significant: they separate statements within a block. Block
// structure itself is carried by keywords (end if, end repeat, ...), not by
// indentation.
enum class TokenType {
    Eof,
    Identifier,
    Keyword,
    Int,
    Float,
    String,
    Symbol,
    Symb,
    // Punctuation and operators.
    LParen,
    RParen,
    LBracket,
    RBracket,
    Colon,
    Comma,
    Semicolon,
    Dot,
    DotDot,
    Assign,
    And,
    Ampersand,
    Lt,
    Gt,
    LtEq,
    GtEq,
    Eq,
    NotEq,
    Minus,
    Plus,
    Star,
    Slash,
    Comment,
    Newline
};

struct Token {
    TokenType type = TokenType::Eof;
    std::string text;
    // 1-based line number, for diagnostics.
    int line = 0;
    // 0-based column, for diagnostics.
    int column = 0;

    [[nodiscard]] bool is(TokenType t) const { return type == t; }
    [[nodiscard]] bool isKeyword(const std::string& word) const {
        return type == TokenType::Keyword && text == word;
    }
    [[nodiscard]] std::string describe() const;
};

// Lexes decompiled Lingo text into a token stream.
//
// Usage:
//   Lexer lexer(source_text);
//   auto tokens = lexer.lex();  // throws CompileError on bad input
//
// The lexer is strict: an unbalanced string quote or a malformed number is a
// CompileError. It does not resolve keywords; it emits Identifier tokens and the
// parser decides keyword-ness. This keeps the lexer generic and lets the
// parser own the language's keyword set.
class Lexer {
public:
    explicit Lexer(std::string source);

    // Produce the token stream. Throws CompileError on a lexical error.
    [[nodiscard]] std::vector<Token> lex();

private:
    std::string source_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int column_ = 0;

    void lexInto(std::vector<Token>& out);
    [[nodiscard]] bool atEnd() const;
    [[nodiscard]] char peek() const;
    [[nodiscard]] char peekAt(std::size_t offset) const;
    char advance();
    void skipWhitespace(bool newlines);
    void pushToken(std::vector<Token>& out, TokenType type, std::string text, int line, int column);
};

} // namespace libreshockwave::lingo::compiler
