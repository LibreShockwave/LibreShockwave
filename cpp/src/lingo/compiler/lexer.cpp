#include "libreshockwave/lingo/compiler/lexer.hpp"

#include <cctype>

namespace libreshockwave::lingo::compiler {
namespace {

bool isIdentStart(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

bool isIdentChar(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

bool isDigit(char ch) {
    return std::isdigit(static_cast<unsigned char>(ch));
}

} // namespace

std::string Token::describe() const {
    switch (type) {
        case TokenType::Eof: return "end of input";
        case TokenType::Identifier: return "identifier '" + text + "'";
        case TokenType::Keyword: return "keyword '" + text + "'";
        case TokenType::Int: return "integer '" + text + "'";
        case TokenType::Float: return "float '" + text + "'";
        case TokenType::String: return "string";
        case TokenType::Symbol: return "symbol '" + text + "'";
        case TokenType::LParen: return "'('";
        case TokenType::RParen: return "')'";
        case TokenType::LBracket: return "'['";
        case TokenType::RBracket: return "']'";
        case TokenType::Colon: return "':'";
        case TokenType::Comma: return "','";
        case TokenType::Semicolon: return "';'";
        case TokenType::Dot: return "'.'";
        case TokenType::DotDot: return "'..'";
        case TokenType::Assign: return "'='";
        case TokenType::And: return "'&&'";
        case TokenType::Ampersand: return "'&'";
        case TokenType::Lt: return "'<'";
        case TokenType::Gt: return "'>'";
        case TokenType::LtEq: return "'<='";
        case TokenType::GtEq: return "'>='";
        case TokenType::Eq: return "'='";
        case TokenType::NotEq: return "'<>'";
        case TokenType::Minus: return "'-'";
        case TokenType::Plus: return "'+'";
        case TokenType::Star: return "'*'";
        case TokenType::Slash: return "'/'";
        case TokenType::Comment: return "comment";
        case TokenType::Newline: return "newline";
    }
    return "token";
}

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

std::vector<Token> Lexer::lex() {
    std::vector<Token> out;
    lexInto(out);
    out.push_back(Token{TokenType::Eof, "", line_, column_});
    return out;
}

void Lexer::lexInto(std::vector<Token>& out) {
    while (true) {
        skipWhitespace(true);
        if (atEnd()) {
            break;
        }
        const char ch = peek();
        const int line = line_;
        const int column = column_;

        if (ch == '\n') {
            advance();
            pushToken(out, TokenType::Newline, "\\n", line, column);
            continue;
        }
        if (ch == '-' && peekAt(1) == '-') {
            // Comment to end of line.
            std::string text;
            while (!atEnd() && peek() != '\n') {
                text.push_back(advance());
            }
            pushToken(out, TokenType::Comment, text, line, column);
            continue;
        }
        if (isIdentStart(ch)) {
            std::string text;
            while (!atEnd() && isIdentChar(peek())) {
                text.push_back(advance());
            }
            pushToken(out, TokenType::Identifier, text, line, column);
            continue;
        }
        if (isDigit(ch)) {
            std::string text;
            while (!atEnd() && isDigit(peek())) {
                text.push_back(advance());
            }
            bool is_float = false;
            if (peek() == '.' && isDigit(peekAt(1))) {
                is_float = true;
                text.push_back(advance());
                while (!atEnd() && isDigit(peek())) {
                    text.push_back(advance());
                }
            }
            if (peek() == 'e' || peek() == 'E') {
                is_float = true;
                text.push_back(advance());
                if (peek() == '+' || peek() == '-') {
                    text.push_back(advance());
                }
                while (!atEnd() && isDigit(peek())) {
                    text.push_back(advance());
                }
            }
            pushToken(out, is_float ? TokenType::Float : TokenType::Int, text, line, column);
            continue;
        }
        if (ch == '"') {
            advance();
            std::string text;
            while (!atEnd() && peek() != '"') {
                text.push_back(advance());
            }
            if (atEnd()) {
                throw CompileError("unterminated string literal", line, column);
            }
            advance(); // closing quote
            pushToken(out, TokenType::String, text, line, column);
            continue;
        }
        if (ch == '#') {
            advance();
            if (peek() == '#') {
                // A symbol reference: ##NN.
                advance();
                if (atEnd() || !isIdentStart(peek())) {
                    throw CompileError("expected identifier after '##'", line, column);
                }
                std::string text;
                while (!atEnd() && isIdentChar(peek())) {
                    text.push_back(advance());
                }
                pushToken(out, TokenType::Symb, text, line, column);
                continue;
            }
            if (atEnd() || !isIdentStart(peek())) {
                throw CompileError("expected identifier after '#'", line, column);
            }
            std::string text;
            while (!atEnd() && isIdentChar(peek())) {
                text.push_back(advance());
            }
            pushToken(out, TokenType::Symbol, text, line, column);
            continue;
        }
        // Punctuation and operators.
        const char next = peekAt(1);
        if (ch == '(') { advance(); pushToken(out, TokenType::LParen, "(", line, column); continue; }
        if (ch == ')') { advance(); pushToken(out, TokenType::RParen, ")", line, column); continue; }
        if (ch == '[') { advance(); pushToken(out, TokenType::LBracket, "[", line, column); continue; }
        if (ch == ']') { advance(); pushToken(out, TokenType::RBracket, "]", line, column); continue; }
        if (ch == ':') { advance(); pushToken(out, TokenType::Colon, ":", line, column); continue; }
        if (ch == ',') { advance(); pushToken(out, TokenType::Comma, ",", line, column); continue; }
        if (ch == ';') { advance(); pushToken(out, TokenType::Semicolon, ";", line, column); continue; }
        if (ch == '.') {
            if (next == '.') { advance(); advance(); pushToken(out, TokenType::DotDot, "..", line, column); }
            else { advance(); pushToken(out, TokenType::Dot, ".", line, column); }
            continue;
        }
        if (ch == '=') { advance(); pushToken(out, TokenType::Assign, "=", line, column); continue; }
        if (ch == '&') {
            if (next == '&') { advance(); advance(); pushToken(out, TokenType::And, "&&", line, column); }
            else { advance(); pushToken(out, TokenType::Ampersand, "&", line, column); }
            continue;
        }
        if (ch == '<') {
            if (next == '=') { advance(); advance(); pushToken(out, TokenType::LtEq, "<=", line, column); }
            else if (next == '>') { advance(); advance(); pushToken(out, TokenType::NotEq, "<>", line, column); }
            else { advance(); pushToken(out, TokenType::Lt, "<", line, column); }
            continue;
        }
        if (ch == '>') {
            if (next == '=') { advance(); advance(); pushToken(out, TokenType::GtEq, ">=", line, column); }
            else { advance(); pushToken(out, TokenType::Gt, ">", line, column); }
            continue;
        }
        if (ch == '-') { advance(); pushToken(out, TokenType::Minus, "-", line, column); continue; }
        if (ch == '+') { advance(); pushToken(out, TokenType::Plus, "+", line, column); continue; }
        if (ch == '*') { advance(); pushToken(out, TokenType::Star, "*", line, column); continue; }
        if (ch == '/') { advance(); pushToken(out, TokenType::Slash, "/", line, column); continue; }
        throw CompileError(std::string("unexpected character '") + ch + "'", line, column);
    }
}

bool Lexer::atEnd() const {
    return pos_ >= source_.size();
}

char Lexer::peek() const {
    return source_[pos_];
}

char Lexer::peekAt(std::size_t offset) const {
    const std::size_t index = pos_ + offset;
    return index < source_.size() ? source_[index] : '\0';
}

char Lexer::advance() {
    const char ch = source_[pos_++];
    if (ch == '\n') {
        ++line_;
        column_ = 0;
    } else {
        ++column_;
    }
    return ch;
}

void Lexer::skipWhitespace(bool newlines) {
    while (!atEnd()) {
        const char ch = peek();
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            advance();
        } else if (ch == '\n' && !newlines) {
            advance();
        } else {
            break;
        }
    }
}

void Lexer::pushToken(std::vector<Token>& out, TokenType type, std::string text, int line, int column) {
    out.push_back(Token{type, std::move(text), line, column});
}

} // namespace libreshockwave::lingo::compiler
