#include "libreshockwave/lingo/LingoValueParser.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace libreshockwave::lingo {
namespace {

using IdentifierResolver = LingoValueParser::IdentifierResolver;

std::string_view trimView(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool endsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

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

bool isIdentifier(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    const auto first = static_cast<unsigned char>(value.front());
    if (!std::isalpha(first) && value.front() != '_') {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(), [](char ch) {
        const auto uch = static_cast<unsigned char>(ch);
        return std::isalnum(uch) || ch == '_';
    });
}

bool isFloatLiteral(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    std::size_t start = value.front() == '-' ? 1 : 0;
    if (start == value.size()) {
        return false;
    }
    std::optional<std::size_t> dotIndex;
    for (std::size_t i = start; i < value.size(); ++i) {
        if (value[i] == '.') {
            if (dotIndex.has_value()) {
                return false;
            }
            dotIndex = i;
        } else if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
            return false;
        }
    }
    return dotIndex.has_value() && *dotIndex > start && *dotIndex < value.size() - 1;
}

std::optional<int> parseInt(std::string_view value) {
    value = trimView(value);
    if (value.empty()) {
        return std::nullopt;
    }

    int result = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto parsed = std::from_chars(begin, end, result);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return std::nullopt;
    }
    return result;
}

std::optional<float> parseFloat(std::string_view value) {
    value = trimView(value);
    if (!isFloatLiteral(value)) {
        return std::nullopt;
    }
    try {
        return std::stof(std::string(value));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string unescapeString(std::string_view value) {
    if (value.find('\\') == std::string_view::npos) {
        return std::string(value);
    }

    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        char ch = value[i];
        if (ch == '\\' && i + 1 < value.size()) {
            char escaped = value[++i];
            switch (escaped) {
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                default: result.push_back(escaped); break;
            }
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

struct ListElement {
    std::string_view value;
    int colonIndex = -1;
};

std::vector<ListElement> splitListElements(std::string_view content) {
    std::vector<ListElement> elements;
    elements.reserve(std::min<std::size_t>(content.size() / 8 + 1, 1024));
    std::size_t elementStart = 0;
    int elementColonIndex = -1;
    int bracketDepth = 0;
    int parenDepth = 0;
    bool inQuote = false;

    for (std::size_t i = 0; i < content.size(); ++i) {
        char ch = content[i];
        if (ch == '"' && (i == 0 || content[i - 1] != '\\')) {
            inQuote = !inQuote;
        } else if (!inQuote) {
            if (ch == '[') {
                ++bracketDepth;
            } else if (ch == ']') {
                --bracketDepth;
            } else if (ch == '(') {
                ++parenDepth;
            } else if (ch == ')') {
                --parenDepth;
            } else if (ch == ':' && bracketDepth == 0 && parenDepth == 0 && elementColonIndex < 0) {
                elementColonIndex = static_cast<int>(i - elementStart);
            } else if (ch == ',' && bracketDepth == 0 && parenDepth == 0) {
                elements.push_back(ListElement{content.substr(elementStart, i - elementStart), elementColonIndex});
                elementStart = i + 1;
                elementColonIndex = -1;
            }
        }
    }

    if (elementStart < content.size()) {
        elements.push_back(ListElement{content.substr(elementStart), elementColonIndex});
    }
    return elements;
}

int findPropListColon(std::string_view element) {
    int bracketDepth = 0;
    int parenDepth = 0;
    bool inQuote = false;

    for (std::size_t i = 0; i < element.size(); ++i) {
        char ch = element[i];
        if (ch == '"' && (i == 0 || element[i - 1] != '\\')) {
            inQuote = !inQuote;
        } else if (!inQuote) {
            if (ch == '[') {
                ++bracketDepth;
            } else if (ch == ']') {
                --bracketDepth;
            } else if (ch == '(') {
                ++parenDepth;
            } else if (ch == ')') {
                --parenDepth;
            } else if (ch == ':' && bracketDepth == 0 && parenDepth == 0) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

std::optional<Datum> tryParseComplete(std::string_view expression,
                                      const IdentifierResolver& identifierResolver);

bool isPropListElement(const ListElement& element) {
    if (element.colonIndex < 0) {
        return false;
    }
    const std::string_view rawKey = trimView(element.value.substr(0, static_cast<std::size_t>(element.colonIndex)));
    if (rawKey.empty()) {
        return true;
    }
    if (startsWith(rawKey, "#")) {
        const std::string_view key = trimView(rawKey.substr(1));
        return isIdentifier(key) || tryParseComplete(key, {}).has_value();
    }
    if (startsWith(rawKey, "\"") && endsWith(rawKey, "\"") && rawKey.size() >= 2) {
        return true;
    }
    return isIdentifier(rawKey);
}

Datum parsePropListKey(std::string_view rawKey, const IdentifierResolver& identifierResolver) {
    const std::string_view key = trimView(rawKey);
    if (startsWith(key, "#") && key.size() > 1) {
        const std::string_view symbolText = trimView(key.substr(1));
        if (isIdentifier(symbolText)) {
            return Datum::symbol(std::string(symbolText));
        }
        const auto parsed = tryParseComplete(symbolText, identifierResolver);
        return parsed.has_value() && !parsed->isVoid() ? *parsed : Datum::symbol(std::string(symbolText));
    }
    if (startsWith(key, "\"") && endsWith(key, "\"") && key.size() >= 2) {
        std::string unquoted = unescapeString(key.substr(1, key.size() - 2));
        const auto parsed = tryParseComplete(unquoted, identifierResolver);
        return parsed.has_value() && !parsed->isVoid() ? *parsed : Datum::of(std::move(unquoted));
    }
    const auto parsed = tryParseComplete(key, identifierResolver);
    return parsed.has_value() && !parsed->isVoid() ? *parsed : Datum::of(std::string(key));
}

Datum parseListOrPropList(std::string_view content, const IdentifierResolver& identifierResolver) {
    const std::string_view trimmed = trimView(content);
    if (trimmed.empty()) {
        return Datum::list();
    }
    if (trimmed == ":") {
        return Datum::propList();
    }

    const auto elements = splitListElements(trimmed);
    if (elements.empty()) {
        return Datum::list();
    }

    if (isPropListElement(elements.front())) {
        Datum props = Datum::propList();
        for (const auto& rawElement : elements) {
            const std::string_view element = rawElement.value;
            const int colonIndex = rawElement.colonIndex;
            if (colonIndex <= 0) {
                continue;
            }
            const Datum key = parsePropListKey(std::string_view(element).substr(0, static_cast<std::size_t>(colonIndex)),
                                               identifierResolver);
            const Datum value = LingoValueParser::parseWithPartial(
                std::string_view(element).substr(static_cast<std::size_t>(colonIndex + 1)),
                identifierResolver);
            props.propListValue().properties().emplace_back(key, value);
        }
        return props;
    }

    std::vector<Datum> items;
    items.reserve(elements.size());
    for (const auto& element : elements) {
        items.push_back(LingoValueParser::parseWithPartial(trimView(element.value), identifierResolver));
    }
    return Datum::list(std::move(items));
}

std::optional<Datum> parseNumericCall(std::string_view expression, std::string_view name, int expectedParts) {
    if (expression.size() <= name.size() + 1 ||
        expression.substr(0, name.size()) != name ||
        expression[name.size()] != '(' ||
        expression.back() != ')') {
        return std::nullopt;
    }
    const std::string_view inner = trimView(expression.substr(name.size() + 1,
                                                             expression.size() - name.size() - 2));
    const auto parts = splitListElements(inner);
    if (static_cast<int>(parts.size()) != expectedParts) {
        return std::nullopt;
    }
    std::vector<int> values;
    values.reserve(parts.size());
    for (const auto& part : parts) {
        const auto parsed = parseInt(trimView(part.value));
        if (!parsed.has_value()) {
            return std::nullopt;
        }
        values.push_back(*parsed);
    }
    if (name == "color" && values.size() == 3) {
        return Datum::colorRef(values[0], values[1], values[2]);
    }
    if (name == "point" && values.size() == 2) {
        return Datum::intPoint(values[0], values[1]);
    }
    if (name == "rect" && values.size() == 4) {
        return Datum::intRect(values[0], values[1], values[2], values[3]);
    }
    return std::nullopt;
}

std::optional<Datum> parseRgb(std::string_view expression) {
    if (!startsWith(expression, "rgb(") || !endsWith(expression, ")")) {
        return std::nullopt;
    }
    const std::string_view inner = trimView(expression.substr(4, expression.size() - 5));
    if (startsWith(inner, "\"") && endsWith(inner, "\"") && inner.size() >= 2) {
        std::string_view hex = trimView(inner.substr(1, inner.size() - 2));
        if (startsWith(hex, "#")) {
            hex.remove_prefix(1);
        }
        unsigned int value = 0;
        const auto* begin = hex.data();
        const auto* end = hex.data() + hex.size();
        const auto parsed = std::from_chars(begin, end, value, 16);
        if (parsed.ec == std::errc{} && parsed.ptr == end) {
            return Datum::colorRef(static_cast<int>((value >> 16U) & 0xFFU),
                                   static_cast<int>((value >> 8U) & 0xFFU),
                                   static_cast<int>(value & 0xFFU));
        }
        return std::nullopt;
    }

    const auto parts = splitListElements(inner);
    if (parts.size() == 1) {
        const auto parsed = parseInt(trimView(parts[0].value));
        if (parsed.has_value()) {
            const unsigned int value = static_cast<unsigned int>(*parsed);
            return Datum::colorRef(static_cast<int>((value >> 16U) & 0xFFU),
                                   static_cast<int>((value >> 8U) & 0xFFU),
                                   static_cast<int>(value & 0xFFU));
        }
    }
    if (parts.size() == 3) {
        std::vector<int> values;
        for (const auto& part : parts) {
            const auto parsed = parseInt(trimView(part.value));
            if (!parsed.has_value()) {
                return std::nullopt;
            }
            values.push_back(*parsed);
        }
        return Datum::colorRef(values[0], values[1], values[2]);
    }
    return std::nullopt;
}

std::optional<Datum> tryParseComplete(std::string_view expression,
                                      const IdentifierResolver& identifierResolver) {
    const std::string_view expr = trimView(expression);
    if (expr.empty()) {
        return Datum::voidValue();
    }
    if (const auto parsed = parseInt(expr)) {
        return Datum::of(*parsed);
    }
    if (const auto parsed = parseFloat(expr)) {
        return Datum::of(*parsed);
    }
    if (startsWith(expr, "#") && expr.size() > 1) {
        const std::string_view symbolName = expr.substr(1);
        if (isIdentifier(symbolName)) {
            return Datum::symbol(std::string(symbolName));
        }
    }
    if (startsWith(expr, "\"") && endsWith(expr, "\"") && expr.size() >= 2) {
        return Datum::of(unescapeString(std::string_view(expr).substr(1, expr.size() - 2)));
    }
    if (const auto parsed = parseNumericCall(expr, "color", 3)) {
        return *parsed;
    }
    if (const auto parsed = parseRgb(expr)) {
        return *parsed;
    }
    if (const auto parsed = parseNumericCall(expr, "rect", 4)) {
        return *parsed;
    }
    if (const auto parsed = parseNumericCall(expr, "point", 2)) {
        return *parsed;
    }
    if (startsWith(expr, "[") && endsWith(expr, "]")) {
        return parseListOrPropList(std::string_view(expr).substr(1, expr.size() - 2), identifierResolver);
    }
    if (isIdentifier(expr)) {
        if (equalsIgnoreCase(expr, "TRUE")) return Datum::TRUE;
        if (equalsIgnoreCase(expr, "FALSE")) return Datum::FALSE;
        if (equalsIgnoreCase(expr, "VOID")) return Datum::voidValue();
        if (equalsIgnoreCase(expr, "EMPTY")) return Datum::of(std::string());
        if (identifierResolver) {
            const Datum resolved = identifierResolver(expr);
            if (!resolved.isVoid()) {
                return resolved;
            }
        }
        return Datum::voidValue();
    }
    return std::nullopt;
}

Datum parseFirstValidExpression(std::string_view expression,
                                const IdentifierResolver& identifierResolver) {
    const std::string_view expr = trimView(expression);
    if (expr.empty()) {
        return Datum::voidValue();
    }
    std::size_t pos = 0;
    const char first = expr[pos];
    if (std::isdigit(static_cast<unsigned char>(first)) ||
        (first == '-' && pos + 1 < expr.size() && std::isdigit(static_cast<unsigned char>(expr[pos + 1])))) {
        const std::size_t start = pos;
        if (first == '-') {
            ++pos;
        }
        while (pos < expr.size() && std::isdigit(static_cast<unsigned char>(expr[pos]))) {
            ++pos;
        }
        if (pos < expr.size() && expr[pos] == '.' && pos + 1 < expr.size() &&
            std::isdigit(static_cast<unsigned char>(expr[pos + 1]))) {
            ++pos;
            while (pos < expr.size() && std::isdigit(static_cast<unsigned char>(expr[pos]))) {
                ++pos;
            }
            if (const auto parsed = parseFloat(std::string_view(expr).substr(start, pos - start))) {
                return Datum::of(*parsed);
            }
            return Datum::voidValue();
        }
        if (const auto parsed = parseInt(std::string_view(expr).substr(start, pos - start))) {
            return Datum::of(*parsed);
        }
        return Datum::voidValue();
    }

    if (first == '"') {
        const std::size_t closingQuote = expr.find('"', 1);
        if (closingQuote != std::string_view::npos &&
            expr.substr(1, closingQuote - 1).find('\\') == std::string_view::npos) {
            return Datum::of(std::string(expr.substr(1, closingQuote - 1)));
        }

        std::string value;
        for (++pos; pos < expr.size(); ++pos) {
            if (expr[pos] == '\\' && pos + 1 < expr.size()) {
                value.push_back(expr[++pos]);
            } else if (expr[pos] == '"') {
                return Datum::of(value);
            } else {
                value.push_back(expr[pos]);
            }
        }
        return Datum::voidValue();
    }

    if (first == '#') {
        ++pos;
        const std::size_t start = pos;
        while (pos < expr.size() &&
               (std::isalnum(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_')) {
            ++pos;
        }
        return pos > start ? Datum::symbol(std::string(expr.substr(start, pos - start))) : Datum::voidValue();
    }

    if (first == '[') {
        int bracketDepth = 1;
        bool inQuote = false;
        for (++pos; pos < expr.size() && bracketDepth > 0; ++pos) {
            char ch = expr[pos];
            if (ch == '"' && (pos == 0 || expr[pos - 1] != '\\')) {
                inQuote = !inQuote;
            } else if (!inQuote) {
                if (ch == '[') {
                    ++bracketDepth;
                } else if (ch == ']') {
                    --bracketDepth;
                }
            }
        }
        if (bracketDepth == 0) {
            return parseListOrPropList(std::string_view(expr).substr(1, pos - 2), identifierResolver);
        }
        return Datum::voidValue();
    }

    if (std::isalpha(static_cast<unsigned char>(first)) || first == '_') {
        const std::size_t start = pos;
        while (pos < expr.size() &&
               (std::isalnum(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_')) {
            ++pos;
        }
        const std::string_view identifier = expr.substr(start, pos - start);
        if (equalsIgnoreCase(identifier, "TRUE")) return Datum::TRUE;
        if (equalsIgnoreCase(identifier, "FALSE")) return Datum::FALSE;
        if (equalsIgnoreCase(identifier, "VOID")) return Datum::voidValue();
        if (equalsIgnoreCase(identifier, "EMPTY")) return Datum::of(std::string());
        if (identifierResolver) {
            const Datum resolved = identifierResolver(identifier);
            if (!resolved.isVoid()) {
                return resolved;
            }
        }
    }
    return Datum::voidValue();
}

} // namespace

Datum LingoValueParser::parseLiteral(std::string_view expression) {
    return parseWithPartial(expression);
}

std::optional<Datum> LingoValueParser::parseComplete(std::string_view expression,
                                                     IdentifierResolver identifierResolver) {
    return tryParseComplete(expression, identifierResolver);
}

Datum LingoValueParser::parseWithPartial(std::string_view expression, IdentifierResolver identifierResolver) {
    const std::string_view expr = trimView(expression);
    if (expr.empty()) {
        return Datum::voidValue();
    }
    const auto complete = tryParseComplete(expr, identifierResolver);
    if (complete.has_value()) {
        return *complete;
    }
    return parseFirstValidExpression(expr, identifierResolver);
}

} // namespace libreshockwave::lingo
