#include "libreshockwave/lingo/compiler/parser.hpp"

#include <algorithm>
#include <iostream>

namespace libreshockwave::lingo::compiler {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

std::unique_ptr<Expr> Parser::makeBareWordLiteral(const std::string& word) const {
    if (word == "VOID") {
        return Expr::stringLit("", "VOID");
    }
    if (word == "EMPTY") {
        return Expr::stringLit("", "EMPTY");
    }
    if (word == "ENTER") {
        return Expr::stringLit(std::string(1, '\x03'), "ENTER");
    }
    if (word == "BACKSPACE") {
        return Expr::stringLit(std::string(1, '\b'), "BACKSPACE");
    }
    if (word == "TAB") {
        return Expr::stringLit(std::string(1, '\t'), "TAB");
    }
    if (word == "RETURN") {
        return Expr::stringLit(std::string(1, '\r'), "RETURN");
    }
    if (word == "QUOTE") {
        return Expr::stringLit("\"", "QUOTE");
    }
    return nullptr;
}

namespace {

const char* typeLabel(TokenType type) {
    switch (type) {
        case TokenType::Eof: return "end of input";
        case TokenType::Identifier: return "identifier";
        case TokenType::Keyword: return "keyword";
        case TokenType::Int: return "integer";
        case TokenType::Float: return "float";
        case TokenType::String: return "string";
        case TokenType::Symbol: return "symbol";
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

} // namespace

// ---------------------------------------------------------------------------
// Token stream access.
// ---------------------------------------------------------------------------

const Token& Parser::cur() const {
    return pos_ < tokens_.size() ? tokens_[pos_] : tokens_.back();
}

const Token& Parser::peekAt(std::size_t offset) const {
    const std::size_t index = pos_ + offset;
    return index < tokens_.size() ? tokens_[index] : tokens_.back();
}

void Parser::advance() {
    if (pos_ < tokens_.size()) {
        ++pos_;
    }
}

bool Parser::check(TokenType type) const {
    return cur().is(type);
}

// The lexer emits Identifier for every word (it does not resolve keywords).
// Keyword-ness is owned by the parser, so a "keyword" is an Identifier whose
// text matches the given word.
bool Parser::checkKeyword(const std::string& word) const {
    return check(TokenType::Identifier) && cur().text == word;
}

void Parser::expect(TokenType type) {
    if (!check(type)) {
        throw CompileError("expected " + std::string(typeLabel(type)) + " but found " +
                          cur().describe(), cur().line, cur().column);
    }
    advance();
}

void Parser::expectKeyword(const std::string& word) {
    if (!checkKeyword(word)) {
        throw CompileError("expected '" + word + "' but found " + cur().describe(),
                          cur().line, cur().column);
    }
    advance();
}

std::string Parser::takeIdent() {
    if (!check(TokenType::Identifier)) {
        throw CompileError("expected an identifier but found " + cur().describe(),
                          cur().line, cur().column);
    }
    std::string text = cur().text;
    advance();
    return text;
}

bool Parser::isCaseLabelStart() const {
    const bool is_literal = check(TokenType::Symbol) || check(TokenType::Int) ||
                            check(TokenType::Float) || check(TokenType::String) ||
                            check(TokenType::Identifier);
    // A negative-int label is `-` followed by an int.
    const bool is_neg_int = check(TokenType::Minus) &&
                            peekAt(1).is(TokenType::Int);
    // A case label list is `<label>[, <label>...]:`. The first label is
    // followed by `:` (single label) or `,` (more labels). Case labels are
    // always at line-start, and parseBlock skips newlines before calling this.
    const bool followed_by_delim = peekAt(is_neg_int ? 2 : 1).is(TokenType::Colon) ||
                                  peekAt(is_neg_int ? 2 : 1).is(TokenType::Comma);
    return (is_literal || is_neg_int) && followed_by_delim;
}

bool Parser::atEof() const {
    return check(TokenType::Eof);
}

void Parser::skipNewlines() {
    while (check(TokenType::Newline) || check(TokenType::Comment)) {
        advance();
    }
}

bool Parser::checkNewline() const {
    return check(TokenType::Newline);
}

// ---------------------------------------------------------------------------
// Top-level script.
// ---------------------------------------------------------------------------

std::unique_ptr<Script> Parser::parseScript() {
    auto script = std::make_unique<Script>();
    skipNewlines();
    // Optional leading comment header (`-- Script`, `-- Behavior`, ...). It is
    // not part of the executable grammar; skip it.
    if (check(TokenType::Comment)) {
        advance();
        skipNewlines();
    }
    while (!atEof()) {
        if (checkKeyword("property")) {
            advance();
            script->property_names.push_back(takeIdent());
            skipNewlines();
            continue;
        }
        if (checkKeyword("global")) {
            advance();
            // A top-level global list is comma-separated.
            do {
                script->global_names.push_back(takeIdent());
            } while (check(TokenType::Comma) && (advance(), true));
            skipNewlines();
            continue;
        }
        if (checkKeyword("on")) {
            script->handlers.push_back(parseHandler());
            skipNewlines();
            continue;
        }
        throw CompileError("unexpected token " + cur().describe() + " at script level",
                          cur().line, cur().column);
    }
    return script;
}

// ---------------------------------------------------------------------------
// Handlers.
// ---------------------------------------------------------------------------

std::unique_ptr<Handler> Parser::parseHandler() {
    expectKeyword("on");
    auto handler = std::make_unique<Handler>();
    const int name_line = cur().line;
    handler->name = takeIdent();
    // Args are on the same line as the name; a following-line identifier is the
    // block's first statement, not an arg.
    if (check(TokenType::Identifier) && cur().line == name_line) {
        handler->arg_names.push_back(takeIdent());
        while (check(TokenType::Comma)) {
            advance();
            handler->arg_names.push_back(takeIdent());
        }
    }
    skipNewlines();
    // Optional handler-level global declarations.
    if (checkKeyword("global")) {
        advance();
        do {
            handler->global_names.push_back(takeIdent());
        } while (check(TokenType::Comma) && (advance(), true));
        skipNewlines();
    }
    handler->statements = parseBlock("end");
    expectKeyword("end");
    return handler;
}

// ---------------------------------------------------------------------------
// Blocks and statements.
// ---------------------------------------------------------------------------

std::vector<std::unique_ptr<Stmt>> Parser::parseBlock(const std::string& terminator) {
    std::vector<std::unique_ptr<Stmt>> block;
    while (!atEof()) {
        skipNewlines();
        if (atEof()) {
            break;
        }
        // A block ends at its terminator keyword (`end`, `else`, `end if`, ...).
        if (checkKeyword(terminator)) {
            break;
        }
        // `end` always terminates a block (it starts `end if`, `end repeat`, ...).
        if (checkKeyword("end")) {
            break;
        }
        // A case label (a literal followed by a colon) ends the block.
        if (isCaseLabelStart()) {
            break;
        }
        if (terminator == "end" && checkKeyword("else")) {
            break;
        }
        block.push_back(parseStatement());
    }
    return block;
}

std::unique_ptr<Stmt> Parser::parseStatement() {
    if (checkKeyword("if")) {
        return parseIf();
    }
    if (checkKeyword("repeat")) {
        return parseRepeat();
    }
    if (checkKeyword("tell")) {
        return parseTell();
    }
    if (checkKeyword("case")) {
        return parseCase();
    }
    if (checkKeyword("exit")) {
        advance();
        if (checkKeyword("repeat")) {
            advance();
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = Stmt::Kind::ExitRepeat;
            return stmt;
        }
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Exit;
        return stmt;
    }
    if (checkKeyword("next")) {
        advance();
        expectKeyword("repeat");
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::NextRepeat;
        return stmt;
    }
    if (checkKeyword("return")) {
        advance();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Return;
        // A bare `return` has no value; `return <value>` does. The value is an
        // expression, but a trailing keyword (none expected here) would signal
        // the bare form. We treat any following expression as the value.
        if (!checkNewline() && !atEof() && !checkKeyword("end")) {
            stmt->value = parseExpression();
        }
        return stmt;
    }
    if (checkKeyword("set")) {
        advance();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Assignment;
        stmt->variable = parseLvalue();
        expectKeyword("to");
        stmt->value = parseExpression();
        return stmt;
    }
    if (checkKeyword("put")) {
        advance();
        auto stmt = std::make_unique<Stmt>();
        stmt->value = parseExpression();
        if (checkKeyword("into")) {
            advance();
            stmt->kind = Stmt::Kind::Put;
            stmt->put_dir = PutDir::Into;
            stmt->variable = parseLvalue();
            return stmt;
        }
        if (checkKeyword("after")) {
            advance();
            stmt->kind = Stmt::Kind::Put;
            stmt->put_dir = PutDir::After;
            stmt->variable = parseLvalue();
            return stmt;
        }
        if (checkKeyword("before")) {
            advance();
            stmt->kind = Stmt::Kind::Put;
            stmt->put_dir = PutDir::Before;
            stmt->variable = parseLvalue();
            return stmt;
        }
        // `put <value>` with no target: a call to a function named "put"
        // (the decompiler prints statement calls named "put" without parens).
        stmt->kind = Stmt::Kind::Call;
        stmt->name = "put";
        stmt->args.push_back(std::move(stmt->value));
        return stmt;
    }
    if (checkKeyword("hilite")) {
        advance();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Hilite;
        stmt->value = parseExpression();
        return stmt;
    }
    if (checkKeyword("delete")) {
        advance();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Delete;
        stmt->value = parseExpression();
        return stmt;
    }
    if (checkKeyword("when")) {
        return parseWhen();
    }
    // A statement that begins with an identifier is either a call
    // (`name(args)`, `expr.method(args)`) or an assignment (`lvalue = value`).
    return parseCallOrAssignment();
}

std::unique_ptr<Stmt> Parser::parseIf() {
    expectKeyword("if");
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::If;
    stmt->condition = parseExpression();
    expectKeyword("then");
    stmt->then_block = parseBlock("else");
    if (checkKeyword("else")) {
        advance();
        // `else if` chains: the decompiler prints a nested `if` in the else block.
        if (checkKeyword("if")) {
            auto nested = parseIf();
            stmt->else_block.push_back(std::move(nested));
        } else {
            stmt->else_block = parseBlock("end");
        }
    }
    expectKeyword("end");
    expectKeyword("if");
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseRepeat() {
    expectKeyword("repeat");
    if (checkKeyword("while")) {
        advance();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::RepeatWhile;
        stmt->condition = parseExpression();
        stmt->block = parseBlock("end");
        expectKeyword("end");
        expectKeyword("repeat");
        return stmt;
    }
    expectKeyword("with");
    auto stmt = std::make_unique<Stmt>();
    stmt->loop_var = Expr::var(takeIdent());
    if (checkKeyword("in")) {
        advance();
        stmt->kind = Stmt::Kind::RepeatWithIn;
        stmt->value = parseExpression();
        stmt->block = parseBlock("end");
        expectKeyword("end");
        expectKeyword("repeat");
        return stmt;
    }
    // `repeat with var = start [down] to end`.
    expect(TokenType::Assign);
    stmt->kind = Stmt::Kind::RepeatWithTo;
    stmt->start = parseExpression();
    stmt->up = true;
    if (checkKeyword("down")) {
        advance();
        stmt->up = false;
    }
    expectKeyword("to");
    stmt->end = parseExpression();
    stmt->block = parseBlock("end");
    expectKeyword("end");
    expectKeyword("repeat");
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseTell() {
    expectKeyword("tell");
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::Tell;
    stmt->window = parseExpression();
    stmt->block = parseBlock("end");
    expectKeyword("end");
    expectKeyword("tell");
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseCase() {
    expectKeyword("case");
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::Case;
    stmt->condition = parseExpression();
    expectKeyword("of");
    // Branches: `<label>[, <label>...]: <block>` and a trailing `otherwise: <block>`.
    while (!atEof()) {
        skipNewlines();
        if (checkKeyword("end")) {
            break;
        }
        Stmt::CaseBranch branch;
        if (checkKeyword("otherwise")) {
            advance();
            expect(TokenType::Colon);
            branch.is_otherwise = true;
        } else {
            // One or more comma-separated labels, then a colon.
            branch.labels.push_back(parseCaseLabel());
            while (check(TokenType::Comma)) {
                advance();
                branch.labels.push_back(parseCaseLabel());
            }
            expect(TokenType::Colon);
        }
        branch.body = parseBlock("end");
        // Re-scan for the next branch or the `end case`.
        stmt->case_branches.push_back(std::move(branch));
    }
    expectKeyword("end");
    expectKeyword("case");
    return stmt;
}

std::unique_ptr<Expr> Parser::parseCaseLabel() {
    // A case label is a literal: int, float, string, symbol, or a bare word.
    if (check(TokenType::Minus) && peekAt(1).is(TokenType::Int)) {
        advance(); // -
        auto e = Expr::intLit(-std::stoll(cur().text));
        advance();
        return e;
    }
    if (check(TokenType::Int)) {
        auto e = Expr::intLit(std::stoll(cur().text));
        advance();
        return e;
    }
    if (check(TokenType::Float)) {
        auto e = Expr::floatLit(std::stod(cur().text));
        advance();
        return e;
    }
    if (check(TokenType::String)) {
        auto e = Expr::stringLit(cur().text);
        advance();
        return e;
    }
    if (check(TokenType::Symbol)) {
        auto e = Expr::symbol(cur().text);
        advance();
        return e;
    }
    if (check(TokenType::Identifier)) {
        // A bare word: either a literal (VOID, EMPTY, ...) or a symbol.
        const std::string word = cur().text;
        if (auto literal = makeBareWordLiteral(word)) {
            advance();
            return literal;
        }
        auto e = Expr::symbol(word);
        advance();
        return e;
    }
    throw CompileError("expected a case label but found " + cur().describe(),
                      cur().line, cur().column);
}

std::unique_ptr<Stmt> Parser::parseWhen() {
    expectKeyword("when");
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::When;
    // The event is a keyword: mouseDown, mouseUp, keyDown, keyUp, timeOut.
    if (checkKeyword("mouseDown")) {
        stmt->when_event = WhenEvent::MouseDown;
    } else if (checkKeyword("mouseUp")) {
        stmt->when_event = WhenEvent::MouseUp;
    } else if (checkKeyword("keyDown")) {
        stmt->when_event = WhenEvent::KeyDown;
    } else if (checkKeyword("keyUp")) {
        stmt->when_event = WhenEvent::KeyUp;
    } else if (checkKeyword("timeOut")) {
        stmt->when_event = WhenEvent::TimeOut;
    } else {
        throw CompileError("expected a `when` event but found " + cur().describe(),
                          cur().line, cur().column);
    }
    advance();
    expectKeyword("then");
    // The payload is raw Lingo text on the same logical line(s). Capture it
    // verbatim up to the newline; the code generator re-parses it as a block.
    std::string payload;
    while (!checkNewline() && !atEof()) {
        payload += cur().text;
        payload.push_back(' ');
        advance();
    }
    stmt->when_script = payload;
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseCallOrAssignment() {
    // Parse a call or lvalue: a primary followed by zero or more suffixes
    // (`(args)`, `.prop`, `[index]`, `.prop[index]`, `.method(args)`).
    auto head = parsePostfix();
    // If the head is followed by `=`, this is an assignment.
    if (check(TokenType::Assign)) {
        advance();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Assignment;
        stmt->variable = std::move(head);
        stmt->value = parseExpression();
        if (stmt->variable->kind == Expr::Kind::Var && stmt->variable->name == "gCore") {
            std::cerr << "  [parseCallOrAssignment] gCore = value kind=" << static_cast<int>(stmt->value->kind) << " head_kind=" << static_cast<int>(stmt->variable->kind) << "\n";
        }
        return stmt;
    }
    // Otherwise it is a call statement.
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::Call;
    if (head->kind == Expr::Kind::Call) {
        stmt->name = head->name;
        stmt->args = std::move(head->args);
    } else if (head->kind == Expr::Kind::ObjCall) {
        stmt->name = head->name;
        stmt->args = std::move(head->args);
        stmt->window = std::move(head->object);
    } else {
        // A bare lvalue as a statement is not valid Lingo; treat it as a no-op
        // call so the parse can continue (the decompiler never emits this).
        stmt->name = head->name;
    }
    return stmt;
}

// ---------------------------------------------------------------------------
// Lvalues (the left-hand side of an assignment / put target).
// ---------------------------------------------------------------------------

std::unique_ptr<Expr> Parser::parseLvalue() {
    // An lvalue is a primary with optional `.prop` / `[index]` suffixes. It is
    // never a full binary expression.
    return parsePostfix();
}

} // namespace libreshockwave::lingo::compiler
