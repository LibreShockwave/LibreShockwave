#include "libreshockwave/player/debug/ExpressionEvaluator.hpp"

#include "libreshockwave/lingo/vm/datum/DatumFormatter.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace libreshockwave::player::debug {
namespace {

using libreshockwave::lingo::Datum;

class EvalException : public std::runtime_error {
public:
    explicit EvalException(const std::string& message) : std::runtime_error(message) {}
};

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
            std::tolower(static_cast<unsigned char>(rhs[i]))) {
            return false;
        }
    }
    return true;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool isBlank(std::string_view value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
}

bool truthy(const Datum& datum) {
    if (datum.isVoid() || datum.isNull()) {
        return false;
    }
    if (const auto* value = datum.asInt()) {
        return value->value != 0;
    }
    if (const auto* value = datum.asFloat()) {
        return value->value != 0.0F;
    }
    if (datum.isString()) {
        return !datum.stringValue().empty();
    }
    return true;
}

double toDouble(const Datum& datum) {
    return static_cast<double>(datum.floatValue());
}

bool lingoEquals(const Datum& lhs, const Datum& rhs) {
    if ((lhs.isVoid() && rhs.isNumber()) || (lhs.isNumber() && rhs.isVoid()) ||
        (lhs.isNumber() && rhs.isNumber())) {
        return toDouble(lhs) == toDouble(rhs);
    }
    if ((lhs.isString() || lhs.isSymbol()) && (rhs.isString() || rhs.isSymbol())) {
        return equalsIgnoreCase(lhs.stringValue(), rhs.stringValue());
    }
    return lhs == rhs;
}

bool samePropertyKey(const Datum& key, const std::string& property) {
    if ((key.isString() || key.isSymbol()) && equalsIgnoreCase(key.stringValue(), property)) {
        return true;
    }
    return false;
}

Datum getPropListProperty(const Datum::PropList& propList, const std::string& property) {
    for (const auto& [key, value] : propList.properties()) {
        if (samePropertyKey(key, property)) {
            return value;
        }
    }
    if (equalsIgnoreCase(property, "txtColor")) {
        return getPropListProperty(propList, "color");
    }
    if (equalsIgnoreCase(property, "txtBgColor")) {
        return getPropListProperty(propList, "bgColor");
    }
    return Datum::voidValue();
}

std::string formatValue(const Datum& value) {
    if (value.asString() != nullptr) {
        return value.stringValue();
    }
    if (value.isVoid()) {
        return "<Void>";
    }
    return lingo::vm::datum::formatExpanded(value);
}

enum class TokenType {
    Number,
    String,
    Symbol,
    Identifier,
    LeftParen,
    RightParen,
    Plus,
    Minus,
    Star,
    Slash,
    Mod,
    LessThan,
    LessEqual,
    GreaterThan,
    GreaterEqual,
    Equal,
    NotEqual,
    And,
    Or,
    Not,
    Dot,
    Comma,
    End
};

struct Token {
    TokenType type = TokenType::End;
    std::string value;
    int position = 0;
};

class Tokenizer {
public:
    explicit Tokenizer(std::string input) : input_(std::move(input)) {}

    [[nodiscard]] std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (!isAtEnd()) {
            skipWhitespace();
            if (isAtEnd()) {
                break;
            }
            if (auto token = scanToken()) {
                tokens.push_back(std::move(*token));
            }
        }
        tokens.push_back(Token{TokenType::End, "", pos_});
        return tokens;
    }

private:
    [[nodiscard]] bool isAtEnd() const { return pos_ >= static_cast<int>(input_.size()); }

    [[nodiscard]] char peek() const {
        return isAtEnd() ? '\0' : input_[static_cast<std::size_t>(pos_)];
    }

    [[nodiscard]] char peekNext() const {
        return pos_ + 1 >= static_cast<int>(input_.size()) ? '\0' : input_[static_cast<std::size_t>(pos_ + 1)];
    }

    char advance() {
        return input_[static_cast<std::size_t>(pos_++)];
    }

    bool match(char expected) {
        if (isAtEnd() || peek() != expected) {
            return false;
        }
        ++pos_;
        return true;
    }

    void skipWhitespace() {
        while (!isAtEnd() && std::isspace(static_cast<unsigned char>(peek())) != 0) {
            ++pos_;
        }
    }

    [[nodiscard]] std::optional<Token> scanToken() {
        const int start = pos_;
        const char ch = advance();
        switch (ch) {
            case '(': return Token{TokenType::LeftParen, "(", start};
            case ')': return Token{TokenType::RightParen, ")", start};
            case '+': return Token{TokenType::Plus, "+", start};
            case '-':
                if (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                    --pos_;
                    return scanNumber();
                }
                return Token{TokenType::Minus, "-", start};
            case '*': return Token{TokenType::Star, "*", start};
            case '/': return Token{TokenType::Slash, "/", start};
            case '.': return Token{TokenType::Dot, ".", start};
            case ',': return Token{TokenType::Comma, ",", start};
            case '<':
                if (match('=')) return Token{TokenType::LessEqual, "<=", start};
                if (match('>')) return Token{TokenType::NotEqual, "<>", start};
                return Token{TokenType::LessThan, "<", start};
            case '>':
                if (match('=')) return Token{TokenType::GreaterEqual, ">=", start};
                return Token{TokenType::GreaterThan, ">", start};
            case '=':
                (void)match('=');
                return Token{TokenType::Equal, "=", start};
            case '!':
                if (match('=')) return Token{TokenType::NotEqual, "!=", start};
                throw EvalException("Unexpected character: !");
            case '"':
                return scanString(start);
            case '#':
                return scanSymbol(start);
            default:
                break;
        }

        if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            --pos_;
            return scanNumber();
        }
        if (std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
            --pos_;
            return scanIdentifier();
        }
        throw EvalException(std::string("Unexpected character: ") + ch);
    }

    [[nodiscard]] Token scanNumber() {
        const int start = pos_;
        if (peek() == '-') {
            advance();
        }
        while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            advance();
        }
        if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peekNext())) != 0) {
            advance();
            while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                advance();
            }
        }
        return Token{TokenType::Number,
                     input_.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(pos_ - start)),
                     start};
    }

    [[nodiscard]] Token scanString(int start) {
        std::string value;
        while (!isAtEnd() && peek() != '"') {
            if (peek() == '\\' && peekNext() == '"') {
                advance();
                value.push_back(advance());
            } else {
                value.push_back(advance());
            }
        }
        if (isAtEnd()) {
            throw EvalException("Unterminated string");
        }
        advance();
        return Token{TokenType::String, std::move(value), start};
    }

    [[nodiscard]] Token scanSymbol(int start) {
        while (std::isalnum(static_cast<unsigned char>(peek())) != 0 || peek() == '_') {
            advance();
        }
        return Token{TokenType::Symbol,
                     input_.substr(static_cast<std::size_t>(start + 1), static_cast<std::size_t>(pos_ - start - 1)),
                     start};
    }

    [[nodiscard]] Token scanIdentifier() {
        const int start = pos_;
        while (std::isalnum(static_cast<unsigned char>(peek())) != 0 || peek() == '_') {
            advance();
        }
        std::string name = input_.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(pos_ - start));
        const std::string lower = lowerAscii(name);
        if (lower == "and") return Token{TokenType::And, std::move(name), start};
        if (lower == "or") return Token{TokenType::Or, std::move(name), start};
        if (lower == "not") return Token{TokenType::Not, std::move(name), start};
        if (lower == "mod") return Token{TokenType::Mod, std::move(name), start};
        if (lower == "true") return Token{TokenType::Number, "1", start};
        if (lower == "false") return Token{TokenType::Number, "0", start};
        return Token{TokenType::Identifier, std::move(name), start};
    }

    std::string input_;
    int pos_ = 0;
};

struct Expr {
    enum class Kind {
        Literal,
        Variable,
        Property,
        Unary,
        Binary
    };

    Kind kind = Kind::Literal;
    Datum value = Datum::voidValue();
    std::string text;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

std::unique_ptr<Expr> literalExpr(Datum value) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Expr::Kind::Literal;
    expr->value = std::move(value);
    return expr;
}

std::unique_ptr<Expr> variableExpr(std::string name) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Expr::Kind::Variable;
    expr->text = std::move(name);
    return expr;
}

std::unique_ptr<Expr> propertyExpr(std::unique_ptr<Expr> object, std::string property) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Expr::Kind::Property;
    expr->left = std::move(object);
    expr->text = std::move(property);
    return expr;
}

std::unique_ptr<Expr> unaryExpr(std::string op, std::unique_ptr<Expr> operand) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Expr::Kind::Unary;
    expr->text = std::move(op);
    expr->left = std::move(operand);
    return expr;
}

std::unique_ptr<Expr> binaryExpr(std::unique_ptr<Expr> left, std::string op, std::unique_ptr<Expr> right) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Expr::Kind::Binary;
    expr->left = std::move(left);
    expr->right = std::move(right);
    expr->text = std::move(op);
    return expr;
}

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    [[nodiscard]] std::unique_ptr<Expr> parseExpression() {
        return parseOr();
    }

    [[nodiscard]] bool isAtEnd() const {
        return peek().type == TokenType::End;
    }

private:
    [[nodiscard]] std::unique_ptr<Expr> parseOr() {
        auto left = parseAnd();
        while (match(TokenType::Or)) {
            left = binaryExpr(std::move(left), "or", parseAnd());
        }
        return left;
    }

    [[nodiscard]] std::unique_ptr<Expr> parseAnd() {
        auto left = parseNot();
        while (match(TokenType::And)) {
            left = binaryExpr(std::move(left), "and", parseNot());
        }
        return left;
    }

    [[nodiscard]] std::unique_ptr<Expr> parseNot() {
        if (match(TokenType::Not)) {
            return unaryExpr("not", parseNot());
        }
        return parseComparison();
    }

    [[nodiscard]] std::unique_ptr<Expr> parseComparison() {
        auto left = parseAddition();
        if (match(TokenType::LessThan)) return binaryExpr(std::move(left), "<", parseAddition());
        if (match(TokenType::LessEqual)) return binaryExpr(std::move(left), "<=", parseAddition());
        if (match(TokenType::GreaterThan)) return binaryExpr(std::move(left), ">", parseAddition());
        if (match(TokenType::GreaterEqual)) return binaryExpr(std::move(left), ">=", parseAddition());
        if (match(TokenType::Equal)) return binaryExpr(std::move(left), "=", parseAddition());
        if (match(TokenType::NotEqual)) return binaryExpr(std::move(left), "<>", parseAddition());
        return left;
    }

    [[nodiscard]] std::unique_ptr<Expr> parseAddition() {
        auto left = parseMultiplication();
        while (true) {
            if (match(TokenType::Plus)) {
                left = binaryExpr(std::move(left), "+", parseMultiplication());
            } else if (match(TokenType::Minus)) {
                left = binaryExpr(std::move(left), "-", parseMultiplication());
            } else {
                return left;
            }
        }
    }

    [[nodiscard]] std::unique_ptr<Expr> parseMultiplication() {
        auto left = parseUnary();
        while (true) {
            if (match(TokenType::Star)) {
                left = binaryExpr(std::move(left), "*", parseUnary());
            } else if (match(TokenType::Slash)) {
                left = binaryExpr(std::move(left), "/", parseUnary());
            } else if (match(TokenType::Mod)) {
                left = binaryExpr(std::move(left), "mod", parseUnary());
            } else {
                return left;
            }
        }
    }

    [[nodiscard]] std::unique_ptr<Expr> parseUnary() {
        if (match(TokenType::Minus)) {
            return unaryExpr("-", parseUnary());
        }
        return parsePostfix();
    }

    [[nodiscard]] std::unique_ptr<Expr> parsePostfix() {
        auto expr = parsePrimary();
        while (match(TokenType::Dot)) {
            const Token name = consume(TokenType::Identifier, "Expected property name after '.'");
            expr = propertyExpr(std::move(expr), name.value);
        }
        return expr;
    }

    [[nodiscard]] std::unique_ptr<Expr> parsePrimary() {
        if (match(TokenType::Number)) {
            const auto& token = previous();
            if (token.value.find('.') != std::string::npos) {
                return literalExpr(Datum::of(std::stod(token.value)));
            }
            int value = 0;
            const auto* begin = token.value.data();
            const auto* end = token.value.data() + token.value.size();
            const auto parsed = std::from_chars(begin, end, value);
            if (parsed.ec != std::errc{} || parsed.ptr != end) {
                throw EvalException("Invalid number: " + token.value);
            }
            return literalExpr(Datum::of(value));
        }
        if (match(TokenType::String)) {
            return literalExpr(Datum::of(previous().value));
        }
        if (match(TokenType::Symbol)) {
            return literalExpr(Datum::symbol(previous().value));
        }
        if (match(TokenType::Identifier)) {
            return variableExpr(previous().value);
        }
        if (match(TokenType::LeftParen)) {
            auto expr = parseExpression();
            (void)consume(TokenType::RightParen, "Expected ')' after expression");
            return expr;
        }
        throw EvalException("Expected expression at position " + std::to_string(peek().position));
    }

    bool match(TokenType type) {
        if (!check(type)) {
            return false;
        }
        advance();
        return true;
    }

    [[nodiscard]] bool check(TokenType type) const {
        return !isAtEnd() && peek().type == type;
    }

    const Token& advance() {
        if (!isAtEnd()) {
            ++current_;
        }
        return previous();
    }

    [[nodiscard]] const Token& peek() const {
        return tokens_[static_cast<std::size_t>(current_)];
    }

    [[nodiscard]] const Token& previous() const {
        return tokens_[static_cast<std::size_t>(current_ - 1)];
    }

    const Token& consume(TokenType type, const std::string& message) {
        if (check(type)) {
            return advance();
        }
        throw EvalException(message + " at position " + std::to_string(peek().position));
    }

    std::vector<Token> tokens_;
    int current_ = 0;
};

Datum evaluateExpr(const Expr& expr, const ExpressionEvaluator::EvaluationContext& context);

Datum evaluateProperty(const Expr& expr, const ExpressionEvaluator::EvaluationContext& context) {
    const Datum object = evaluateExpr(*expr.left, context);
    if (object.isPropList()) {
        return getPropListProperty(object.propListValue(), expr.text);
    }
    if (object.type() == lingo::DatumType::ScriptInstanceRef) {
        return object.scriptInstanceValue().getProperty(expr.text);
    }
    throw EvalException("Cannot access property '" + expr.text + "' on " + object.typeString());
}

Datum evaluateUnary(const Expr& expr, const ExpressionEvaluator::EvaluationContext& context) {
    const Datum operand = evaluateExpr(*expr.left, context);
    if (expr.text == "-") {
        if (const auto* value = operand.asInt()) {
            return Datum::of(-value->value);
        }
        if (const auto* value = operand.asFloat()) {
            return Datum::of(-value->value);
        }
        throw EvalException("Cannot negate " + operand.typeString());
    }
    if (expr.text == "not") {
        return truthy(operand) ? Datum::FALSE : Datum::TRUE;
    }
    throw EvalException("Unknown operator: " + expr.text);
}

Datum evaluateAdd(const Datum& left, const Datum& right) {
    if (left.isString() || right.isString()) {
        return Datum::of(left.stringValue() + right.stringValue());
    }
    if (left.isFloat() || right.isFloat()) {
        return Datum::of(toDouble(left) + toDouble(right));
    }
    return Datum::of(left.intValue() + right.intValue());
}

Datum evaluateSubtract(const Datum& left, const Datum& right) {
    if (left.isFloat() || right.isFloat()) {
        return Datum::of(toDouble(left) - toDouble(right));
    }
    return Datum::of(left.intValue() - right.intValue());
}

Datum evaluateMultiply(const Datum& left, const Datum& right) {
    if (left.isFloat() || right.isFloat()) {
        return Datum::of(toDouble(left) * toDouble(right));
    }
    return Datum::of(left.intValue() * right.intValue());
}

Datum evaluateDivide(const Datum& left, const Datum& right) {
    const double divisor = toDouble(right);
    if (divisor == 0.0) {
        throw EvalException("Division by zero");
    }
    return Datum::of(toDouble(left) / divisor);
}

Datum evaluateMod(const Datum& left, const Datum& right) {
    const int divisor = right.intValue();
    if (divisor == 0) {
        throw EvalException("Modulo by zero");
    }
    return Datum::of(left.intValue() % divisor);
}

bool compareLessThan(const Datum& left, const Datum& right) {
    if (left.asString() != nullptr && right.asString() != nullptr) {
        return left.stringValue() < right.stringValue();
    }
    return toDouble(left) < toDouble(right);
}

bool compareLessThanOrEqual(const Datum& left, const Datum& right) {
    if (left.asString() != nullptr && right.asString() != nullptr) {
        return left.stringValue() <= right.stringValue();
    }
    return toDouble(left) <= toDouble(right);
}

Datum evaluateBinary(const Expr& expr, const ExpressionEvaluator::EvaluationContext& context) {
    if (expr.text == "and") {
        const Datum left = evaluateExpr(*expr.left, context);
        if (!truthy(left)) {
            return Datum::FALSE;
        }
        return truthy(evaluateExpr(*expr.right, context)) ? Datum::TRUE : Datum::FALSE;
    }
    if (expr.text == "or") {
        const Datum left = evaluateExpr(*expr.left, context);
        if (truthy(left)) {
            return Datum::TRUE;
        }
        return truthy(evaluateExpr(*expr.right, context)) ? Datum::TRUE : Datum::FALSE;
    }

    const Datum left = evaluateExpr(*expr.left, context);
    const Datum right = evaluateExpr(*expr.right, context);
    if (expr.text == "+") return evaluateAdd(left, right);
    if (expr.text == "-") return evaluateSubtract(left, right);
    if (expr.text == "*") return evaluateMultiply(left, right);
    if (expr.text == "/") return evaluateDivide(left, right);
    if (expr.text == "mod") return evaluateMod(left, right);
    if (expr.text == "<") return compareLessThan(left, right) ? Datum::TRUE : Datum::FALSE;
    if (expr.text == "<=") return compareLessThanOrEqual(left, right) ? Datum::TRUE : Datum::FALSE;
    if (expr.text == ">") return !compareLessThanOrEqual(left, right) ? Datum::TRUE : Datum::FALSE;
    if (expr.text == ">=") return !compareLessThan(left, right) ? Datum::TRUE : Datum::FALSE;
    if (expr.text == "=") return lingoEquals(left, right) ? Datum::TRUE : Datum::FALSE;
    if (expr.text == "<>") return !lingoEquals(left, right) ? Datum::TRUE : Datum::FALSE;
    throw EvalException("Unknown operator: " + expr.text);
}

Datum evaluateExpr(const Expr& expr, const ExpressionEvaluator::EvaluationContext& context) {
    switch (expr.kind) {
        case Expr::Kind::Literal:
            return expr.value;
        case Expr::Kind::Variable:
            return context.lookupVariable(expr.text);
        case Expr::Kind::Property:
            return evaluateProperty(expr, context);
        case Expr::Kind::Unary:
            return evaluateUnary(expr, context);
        case Expr::Kind::Binary:
            return evaluateBinary(expr, context);
    }
    return Datum::voidValue();
}

} // namespace

ExpressionEvaluator::EvaluationContext ExpressionEvaluator::EvaluationContext::empty() {
    return EvaluationContext{};
}

Datum ExpressionEvaluator::EvaluationContext::lookupVariable(const std::string& name) const {
    if (equalsIgnoreCase(name, "me")) {
        return receiver.value_or(Datum::voidValue());
    }
    if (const auto it = locals.find(name); it != locals.end()) {
        return it->second;
    }
    if (const auto it = params.find(name); it != params.end()) {
        return it->second;
    }
    if (const auto it = globals.find(name); it != globals.end()) {
        return it->second;
    }
    return Datum::voidValue();
}

ExpressionEvaluator::EvalResult ExpressionEvaluator::EvalResult::success(Datum value) {
    return EvalResult{std::move(value), std::nullopt};
}

ExpressionEvaluator::EvalResult ExpressionEvaluator::EvalResult::failure(std::string message) {
    return EvalResult{std::nullopt, std::move(message)};
}

bool ExpressionEvaluator::EvalResult::succeeded() const {
    return value.has_value() && !error.has_value();
}

ExpressionEvaluator::EvalResult ExpressionEvaluator::evaluate(const std::string& expression,
                                                              const EvaluationContext& context) const {
    if (expression.empty() || isBlank(expression)) {
        return EvalResult::failure("Empty expression");
    }

    try {
        Tokenizer tokenizer(expression);
        Parser parser(tokenizer.tokenize());
        auto ast = parser.parseExpression();
        if (!parser.isAtEnd()) {
            return EvalResult::failure("Unexpected input after expression");
        }
        return EvalResult::success(evaluateExpr(*ast, context));
    } catch (const EvalException& error) {
        return EvalResult::failure(error.what());
    } catch (const std::exception& error) {
        return EvalResult::failure(std::string("Parse error: ") + error.what());
    }
}

bool ExpressionEvaluator::evaluateCondition(const std::string& expression, const EvaluationContext& context) const {
    const auto result = evaluate(expression, context);
    return result.value.has_value() && truthy(*result.value);
}

std::string ExpressionEvaluator::interpolateLogMessage(const std::string& message,
                                                       const EvaluationContext& context) const {
    if (message.find('{') == std::string::npos) {
        return message;
    }

    std::string result;
    std::size_t index = 0;
    while (index < message.size()) {
        if (message[index] == '{') {
            const auto end = message.find('}', index + 1);
            if (end != std::string::npos && end > index) {
                const std::string expression = message.substr(index + 1, end - index - 1);
                const auto evalResult = evaluate(expression, context);
                result += evalResult.value.has_value()
                              ? formatValue(*evalResult.value)
                              : "<" + evalResult.error.value_or("error") + ">";
                index = end + 1;
                continue;
            }
        }
        result.push_back(message[index]);
        ++index;
    }
    return result;
}

} // namespace libreshockwave::player::debug
