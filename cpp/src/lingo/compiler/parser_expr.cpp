#include "libreshockwave/lingo/compiler/parser.hpp"

#include <cctype>

namespace libreshockwave::lingo::compiler {
namespace {

// The fixed multi-word `the` property names the decompiler emits from its
// property-ID tables (moviePropertyName / animation2PropertyName). These are the
// only `the` props that span more than one token.
bool isThePropWord(const std::string& word) {
    return word == "floatPrecision" || word == "mouseDownScript" ||
           word == "mouseUpScript" || word == "keyDownScript" ||
           word == "keyUpScript" || word == "timeoutScript" ||
           word == "beepOn" || word == "buttonStyle" || word == "centerStage" ||
           word == "checkBoxAccess" || word == "checkboxType" ||
           word == "colorDepth" || word == "colorQD" || word == "exitLock" ||
           word == "fixStageSize" || word == "fullColorPermit" ||
           word == "imageDirect" || word == "doubleClick" || word == "key" ||
           word == "lastClick" || word == "lastEvent" || word == "keyCode" ||
           word == "lastKey" || word == "lastRoll" || word == "timeoutLapsed" ||
           word == "multiSound" || word == "pauseState" ||
           word == "quickTimePresent" || word == "selEnd" || word == "selStart" ||
           word == "soundEnabled" || word == "soundLevel" || word == "stageColor" ||
           word == "switchColorDepth" || word == "timeoutKeyDown" ||
           word == "timeoutLength" || word == "timeoutMouse" ||
           word == "timeoutPlay" || word == "timer" || word == "preLoadRAM" ||
           word == "videoForWindowsPresent" || word == "netPresent" ||
           word == "safePlayer" || word == "soundMixMedia" ||
           word == "perFrameHook" || word == "itemDelimiter" ||
           word == "traceScript" || word == "milliSeconds" || word == "stage" ||
           word == "runMode" || word == "mouseV" || word == "mouseLoc" ||
           word == "mouseH" || word == "traceLogFile" || word == "randomSeed" ||
           word == "alertHook" || word == "keyboardFocusSprite" ||
           word == "moviePath" || word == "date" || word == "lastChannel" ||
           word == "transaction" || word == "time" || word == "stageTop" ||
           word == "stageRight" || word == "stageLeft" || word == "stageBottom" ||
           word == "server" || word == "reply" || word == "remote" ||
           word == "paramCount" || word == "maxinteger";
}

bool isChunkTypeWord(const std::string& word) {
    return word == "char" || word == "word" || word == "item" || word == "line";
}

// The decompiler prints `the number of <chunktype>s in` with a plural chunk
// type (`chars`, `words`, `items`, `lines`). Accept the plural forms here.
bool isChunkCountWord(const std::string& word) {
    return word == "chars" || word == "words" || word == "items" ||
           word == "lines" || isChunkTypeWord(word);
}

bool isMemberTypeWord(const std::string& word) {
    return word == "sprite" || word == "field" || word == "member" ||
           word == "castLib";
}

} // namespace

// ---------------------------------------------------------------------------
// Expression entry and binary-operator levels (loosest to tightest).
// ---------------------------------------------------------------------------

std::unique_ptr<Expr> Parser::parseExpression() {
    return parseJoin();
}

std::unique_ptr<Expr> Parser::parseJoin() {
    auto left = parseOr();
    while (true) {
        BinOp op = BinOp::Add;
        if (check(TokenType::Ampersand)) {
            op = BinOp::JoinStr;
        } else if (check(TokenType::And)) {
            op = BinOp::JoinPadStr;
        } else if (checkKeyword("contains")) {
            op = BinOp::ContainsStr;
        } else if (checkKeyword("starts")) {
            op = BinOp::Contains0Str;
        } else {
            break;
        }
        advance();
        auto right = parseOr();
        left = makeBinary(op, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseOr() {
    auto left = parseAnd();
    while (checkKeyword("or")) {
        advance();
        auto right = parseAnd();
        left = makeBinary(BinOp::Or, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseAnd() {
    auto left = parseComparison();
    while (checkKeyword("and")) {
        advance();
        auto right = parseComparison();
        left = makeBinary(BinOp::And, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseComparison() {
    auto left = parseAdditive();
    while (true) {
        BinOp op = BinOp::Add;
        if (check(TokenType::Lt)) {
            op = BinOp::Lt;
        } else if (check(TokenType::LtEq)) {
            op = BinOp::LtEq;
        } else if (check(TokenType::NotEq)) {
            op = BinOp::NotEq;
        } else if (check(TokenType::Eq) || check(TokenType::Assign)) {
            // `=` is both assignment (statement context) and equality
            // (expression context). In an expression it is always equality.
            op = BinOp::Eq;
        } else if (check(TokenType::Gt)) {
            op = BinOp::Gt;
        } else if (check(TokenType::GtEq)) {
            op = BinOp::GtEq;
        } else {
            break;
        }
        advance();
        auto right = parseAdditive();
        left = makeBinary(op, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseAdditive() {
    auto left = parseMultiplicative();
    while (true) {
        BinOp op = BinOp::Add;
        if (check(TokenType::Plus)) {
            op = BinOp::Add;
        } else if (check(TokenType::Minus)) {
            op = BinOp::Sub;
        } else {
            break;
        }
        advance();
        auto right = parseMultiplicative();
        left = makeBinary(op, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseMultiplicative() {
    auto left = parseUnary();
    while (true) {
        BinOp op = BinOp::Add;
        if (check(TokenType::Star)) {
            op = BinOp::Mul;
        } else if (check(TokenType::Slash)) {
            op = BinOp::Div;
        } else if (checkKeyword("mod")) {
            op = BinOp::Mod;
        } else {
            break;
        }
        advance();
        auto right = parseUnary();
        left = makeBinary(op, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseUnary() {
    if (checkKeyword("not")) {
        advance();
        auto operand = parseUnary();
        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::NotOp;
        expr->left = std::move(operand);
        return expr;
    }
    if (check(TokenType::Minus)) {
        advance();
        auto operand = parseUnary();
        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::InverseOp;
        expr->left = std::move(operand);
        return expr;
    }
    return parsePostfix();
}

std::unique_ptr<Expr> Parser::makeBinary(BinOp op, std::unique_ptr<Expr> left,
                                        std::unique_ptr<Expr> right) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Expr::Kind::BinaryOp;
    expr->bin_op = op;
    expr->left = std::move(left);
    expr->right = std::move(right);
    return expr;
}

// ---------------------------------------------------------------------------
// Postfix: a primary followed by zero or more suffixes.
// ---------------------------------------------------------------------------

std::unique_ptr<Expr> Parser::parsePostfix() {
    auto primary = parsePrimary();
    while (true) {
        if (check(TokenType::LParen)) {
            advance();
            auto args = parseArgList();
            expect(TokenType::RParen);
            if (primary->kind == Expr::Kind::Var) {
                auto call = std::make_unique<Expr>();
                call->kind = Expr::Kind::Call;
                call->name = std::move(primary->name);
                call->args = std::move(args);
                primary = std::move(call);
            } else {
                auto call = std::make_unique<Expr>();
                call->kind = Expr::Kind::ObjCallV4;
                call->object = std::move(primary);
                call->args = std::move(args);
                primary = std::move(call);
            }
        } else if (check(TokenType::Dot)) {
            advance();
            const std::string prop = takeIdent();
            if (check(TokenType::LParen)) {
                advance();
                auto args = parseArgList();
                expect(TokenType::RParen);
                auto call = std::make_unique<Expr>();
                call->kind = Expr::Kind::ObjCall;
                call->object = std::move(primary);
                call->name = prop;
                call->args = std::move(args);
                primary = std::move(call);
            } else if (check(TokenType::LBracket)) {
                advance();
                auto index = parseExpression();
                std::unique_ptr<Expr> index2;
                if (check(TokenType::DotDot)) {
                    advance();
                    index2 = parseExpression();
                }
                expect(TokenType::RBracket);
                auto expr = std::make_unique<Expr>();
                expr->kind = Expr::Kind::ObjPropIndexExpr;
                expr->object = std::move(primary);
                expr->name = prop;
                expr->index = std::move(index);
                expr->index2 = std::move(index2);
                primary = std::move(expr);
            } else {
                auto expr = std::make_unique<Expr>();
                expr->kind = Expr::Kind::ObjPropExpr;
                expr->object = std::move(primary);
                expr->name = prop;
                primary = std::move(expr);
            }
        } else if (check(TokenType::LBracket)) {
            advance();
            auto index = parseExpression();
            expect(TokenType::RBracket);
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::ObjBracketExpr;
            expr->object = std::move(primary);
            expr->index = std::move(index);
            primary = std::move(expr);
        } else {
            break;
        }
    }
    return primary;
}

// ---------------------------------------------------------------------------
// Primary expressions.
// ---------------------------------------------------------------------------

std::unique_ptr<Expr> Parser::parsePrimary() {
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
    if (check(TokenType::Symb)) {
        auto e = Expr::symb(std::stoll(cur().text));
        advance();
        return e;
    }
    if (check(TokenType::LParen)) {
        advance();
        auto e = parseExpression();
        expect(TokenType::RParen);
        return e;
    }
    if (check(TokenType::LBracket)) {
        return parseListLiteral();
    }
    if (check(TokenType::Identifier)) {
        const std::string word = cur().text;
        if (word == "the") {
            return parseTheExpr();
        }
        if (word == "new") {
            advance();
            if (check(TokenType::LParen)) {
                // `new(args)`: a call to a function named "new".
                advance();
                auto args = parseArgList();
                expect(TokenType::RParen);
                auto e = std::make_unique<Expr>();
                e->kind = Expr::Kind::Call;
                e->name = "new";
                e->args = std::move(args);
                return e;
            }
            // `new <type>(args)`: a NewObj.
            const std::string type = takeIdent();
            expect(TokenType::LParen);
            auto args = parseArgList();
            expect(TokenType::RParen);
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::NewObj;
            e->name = type;
            e->args = std::move(args);
            return e;
        }
        if (isMemberTypeWord(word)) {
            return parseMemberExpr();
        }
        if (isChunkTypeWord(word)) {
            return parseChunkExpr();
        }
        // Bare-word literals (VOID, EMPTY, ENTER, ...) and calls.
        if (auto literal = makeBareWordLiteral(word)) {
            advance();
            return literal;
        }
        if (word == "PI") {
            advance();
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::Call;
            e->name = "pi";
            return e;
        }
        if (word == "SPACE") {
            advance();
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::Call;
            e->name = "space";
            return e;
        }
        // A variable (a call is formed by the postfix `(args)` suffix).
        auto e = Expr::var(word);
        advance();
        return e;
    }
    throw CompileError("expected an expression but found " + cur().describe(),
                      cur().line, cur().column);
}

std::unique_ptr<Expr> Parser::parseListLiteral() {
    expect(TokenType::LBracket);
    auto list = std::make_unique<Expr>();
    list->kind = Expr::Kind::List;
    auto lit = std::make_unique<ListLit>();
    if (check(TokenType::RBracket)) {
        advance();
        list->list = std::move(lit);
        return list;
    }
    // Distinguish a list literal `[a, b]` from a prop-list literal `[k: v]`.
    // A prop-list has a `:` (or a leading `:` for the empty form) after the
    // first key.
    bool is_prop_list = false;
    if (check(TokenType::Colon)) {
        is_prop_list = true;
    } else {
        // Look ahead for a colon at the top level of the first entry.
        std::size_t depth = 0;
        std::size_t i = pos_;
        while (i < tokens_.size()) {
            const auto& t = tokens_[i];
            if (t.is(TokenType::LParen) || t.is(TokenType::LBracket)) {
                ++depth;
            } else if (t.is(TokenType::RParen) || t.is(TokenType::RBracket)) {
                if (depth == 0) {
                    break;
                }
                --depth;
            } else if (depth == 0 && t.is(TokenType::Colon)) {
                is_prop_list = true;
                break;
            } else if (depth == 0 && t.is(TokenType::Comma)) {
                break;
            }
            ++i;
        }
    }
    if (is_prop_list) {
        return parsePropListLiteral();
    }
    lit->items.push_back(parseExpression());
    while (check(TokenType::Comma)) {
        advance();
        lit->items.push_back(parseExpression());
    }
    expect(TokenType::RBracket);
    list->list = std::move(lit);
    return list;
}

std::unique_ptr<Expr> Parser::parsePropListLiteral() {
    // The opening `[` is consumed by parseListLiteral before this is called.
    auto expr = std::make_unique<Expr>();
    expr->kind = Expr::Kind::PropList;
    auto pl = std::make_unique<PropListLit>();
    if (check(TokenType::Colon)) {
        // Empty form `[:]`.
        advance();
        pl->empty_form = true;
        expect(TokenType::RBracket);
        expr->prop_list = std::move(pl);
        return expr;
    }
    while (true) {
        // Key: a symbol, a string, or an identifier (symbol), or an int.
        std::string key;
        PropListLit::KeyType key_type = PropListLit::KeyType::Symbol;
        if (check(TokenType::Symbol)) {
            key = cur().text;
            key_type = PropListLit::KeyType::Symbol;
            advance();
        } else if (check(TokenType::String)) {
            key = cur().text;
            key_type = PropListLit::KeyType::String;
            advance();
        } else if (check(TokenType::Identifier)) {
            key = cur().text;
            key_type = PropListLit::KeyType::Symbol;
            advance();
        } else if (check(TokenType::Int)) {
            key = cur().text;
            key_type = PropListLit::KeyType::Int;
            advance();
        } else {
            throw CompileError("expected a prop-list key but found " + cur().describe(),
                              cur().line, cur().column);
        }
        pl->keys.push_back(key);
        pl->key_types.push_back(key_type);
        if (check(TokenType::Colon)) {
            advance();
            pl->values.push_back(parseExpression());
        } else {
            pl->values.push_back(nullptr);
        }
        if (!check(TokenType::Comma)) {
            break;
        }
        advance();
    }
    expect(TokenType::RBracket);
    expr->prop_list = std::move(pl);
    return expr;
}

// ---------------------------------------------------------------------------
// `the` expressions.
// ---------------------------------------------------------------------------

std::unique_ptr<Expr> Parser::parseTheExpr() {
    expectKeyword("the");
    // `the number of <chunktype>s in <string>` (StringChunkCountExprNode).
    if (checkKeyword("number") && peekAt(1).is(TokenType::Identifier) &&
        peekAt(1).text == "of") {
        advance(); // number
        advance(); // of
        if (isChunkCountWord(cur().text) &&
            peekAt(1).is(TokenType::Identifier) && peekAt(1).text == "in") {
            // The decompiler prints the chunk type with a trailing `s`
            // (`chars`, `words`, `items`, `lines`). Accept both.
            const std::string chunk_word = cur().text;
            advance();
            expectKeyword("in");
            auto source = parsePostfix();
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::ChunkCountExpr;
            e->chunk_type = chunkTypeFromWord(chunk_word);
            e->source = std::move(source);
            return e;
        }
        // `the number of <fixedname>` (TheExprNode), where fixedname is one
        // of castLibs, castMembers, menus, xtras.
        if (cur().text == "castLibs" || cur().text == "castMembers" ||
            cur().text == "menus" || cur().text == "xtras") {
            const std::string prop = "number of " + takeIdent();
            return finishTheExpr(prop);
        }
        // `the number of <object>`: the decompiler prints `<object>.number`
        // this way. Parse the object and make it a property access.
        auto object = parsePostfix();
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::ObjPropExpr;
        e->object = std::move(object);
        e->name = "number";
        return e;
    }
    // `the last <chunktype> in <string>` (LastStringChunkExprNode).
    if (checkKeyword("last") && peekAt(1).is(TokenType::Identifier) &&
        isChunkTypeWord(peekAt(1).text) && peekAt(2).is(TokenType::Identifier) &&
        peekAt(2).text == "in") {
        advance(); // last
        const std::string chunk_word = cur().text;
        advance();
        expectKeyword("in");
        auto source = parsePostfix();
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::LastChunkExpr;
        e->chunk_type = chunkTypeFromWord(chunk_word);
        e->source = std::move(source);
        return e;
    }
    // Two-token fixed props: `short/abbr/long time|date`.
    if ((checkKeyword("short") || checkKeyword("abbr") || checkKeyword("long")) &&
        peekAt(1).is(TokenType::Identifier) &&
        (peekAt(1).text == "time" || peekAt(1).text == "date")) {
        const std::string prop = cur().text + " " + peekAt(1).text;
        advance();
        advance();
        return finishTheExpr(prop);
    }
    // Single-token prop.
    const std::string prop = takeIdent();
    return finishTheExpr(prop);
}

std::unique_ptr<Expr> Parser::finishTheExpr(const std::string& prop) {
    // `the <prop> of <object>` (ThePropExprNode).
    if (checkKeyword("of")) {
        advance();
        // menu / menuItem / sound / sprite property forms.
        if (checkKeyword("menu")) {
            advance();
            auto id = parsePostfix();
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::MenuProp;
            e->name = prop;
            e->index = std::move(id);
            return e;
        }
        if (checkKeyword("menuItem")) {
            advance();
            auto item_id = parsePostfix();
            expectKeyword("of");
            expectKeyword("menu");
            auto menu_id = parsePostfix();
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::MenuItemProp;
            e->name = prop;
            e->index = std::move(menu_id);
            e->index2 = std::move(item_id);
            return e;
        }
        if (checkKeyword("sound")) {
            advance();
            auto id = parsePostfix();
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::SoundProp;
            e->name = prop;
            e->index = std::move(id);
            return e;
        }
        if (checkKeyword("sprite")) {
            advance();
            auto id = parsePostfix();
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::SpriteProp;
            e->name = prop;
            e->index = std::move(id);
            return e;
        }
        auto object = parsePostfix();
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::ThePropExpr;
        e->object = std::move(object);
        e->name = prop;
        return e;
    }
    auto e = std::make_unique<Expr>();
    e->kind = Expr::Kind::TheExpr;
    e->name = prop;
    return e;
}

int Parser::chunkTypeFromWord(const std::string& word) {
    if (word.rfind("char", 0) == 0) return 1;
    if (word.rfind("word", 0) == 0) return 2;
    if (word.rfind("item", 0) == 0) return 3;
    if (word.rfind("line", 0) == 0) return 4;
    throw CompileError("unknown chunk type '" + word + "'", cur().line, cur().column);
}

// ---------------------------------------------------------------------------
// Member expressions (sprite / field / member / castLib).
// ---------------------------------------------------------------------------

std::unique_ptr<Expr> Parser::parseMemberExpr() {
    const std::string member_type = cur().text;
    advance();
    if (check(TokenType::LParen)) {
        advance();
        auto member_id = parseExpression();
        std::unique_ptr<Expr> cast_id;
        if (check(TokenType::Comma)) {
            advance();
            cast_id = parseExpression();
        }
        expect(TokenType::RParen);
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::MemberExpr;
        e->member_type = member_type;
        e->index = std::move(member_id);
        e->index2 = std::move(cast_id);
        return e;
    }
    // Non-paren form: `sprite <id>` or `sprite <first> intersects|within <second>`.
    auto first = parsePostfix();
    if (checkKeyword("intersects")) {
        advance();
        auto second = parsePostfix();
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::SpriteIntersects;
        e->left = std::move(first);
        e->right = std::move(second);
        return e;
    }
    if (checkKeyword("within")) {
        advance();
        auto second = parsePostfix();
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::SpriteWithin;
        e->left = std::move(first);
        e->right = std::move(second);
        return e;
    }
    auto e = std::make_unique<Expr>();
    e->kind = Expr::Kind::MemberExpr;
    e->member_type = member_type;
    e->index = std::move(first);
    return e;
}

// ---------------------------------------------------------------------------
// Chunk expressions (char / word / item / line N [to M] of X).
// ---------------------------------------------------------------------------

std::unique_ptr<Expr> Parser::parseChunkExpr() {
    const std::string chunk_word = cur().text;
    advance();
    auto first = parseExpression();
    std::unique_ptr<Expr> last;
    if (checkKeyword("to")) {
        advance();
        last = parseExpression();
    }
    expectKeyword("of");
    auto source = parsePostfix();
    auto e = std::make_unique<Expr>();
    e->kind = Expr::Kind::ChunkExpr;
    e->chunk_type = chunkTypeFromWord(chunk_word);
    e->first = std::move(first);
    e->last = std::move(last);
    e->source = std::move(source);
    return e;
}

// ---------------------------------------------------------------------------
// Argument lists and name lists.
// ---------------------------------------------------------------------------

std::vector<std::unique_ptr<Expr>> Parser::parseArgList() {
    std::vector<std::unique_ptr<Expr>> args;
    if (check(TokenType::RParen)) {
        return args;
    }
    args.push_back(parseExpression());
    while (check(TokenType::Comma)) {
        advance();
        args.push_back(parseExpression());
    }
    return args;
}

std::vector<std::string> Parser::parseNameList() {
    std::vector<std::string> names;
    names.push_back(takeIdent());
    while (check(TokenType::Comma)) {
        advance();
        names.push_back(takeIdent());
    }
    return names;
}

} // namespace libreshockwave::lingo::compiler
