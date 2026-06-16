#include "libreshockwave/lingo/vm/parse/LingoExpressionParser.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "libreshockwave/lingo/vm/LingoVM.hpp"

namespace libreshockwave::lingo::vm::parse {
namespace {

std::string trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool endsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
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
        const auto byte = static_cast<unsigned char>(ch);
        return std::isalnum(byte) != 0 || ch == '_';
    });
}

bool containsColon(std::string_view value) {
    return value.find(':') != std::string_view::npos;
}

std::optional<int> parseInteger(std::string_view value) {
    const std::string text(value);
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno == ERANGE || end == text.c_str() || *end != '\0' ||
        parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return static_cast<int>(parsed);
}

std::optional<double> parseDouble(std::string_view value) {
    const std::string text(value);
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(text.c_str(), &end);
    if (errno == ERANGE || end == text.c_str() || *end != '\0') {
        return std::nullopt;
    }
    return parsed;
}

std::vector<std::string> splitListElements(std::string_view content) {
    std::vector<std::string> elements;
    std::string current;
    int bracketDepth = 0;
    bool inQuote = false;

    for (std::size_t i = 0; i < content.size(); ++i) {
        const char ch = content[i];
        if (ch == '"' && (i == 0 || content[i - 1] != '\\')) {
            inQuote = !inQuote;
            current.push_back(ch);
        } else if (inQuote) {
            current.push_back(ch);
        } else if (ch == '[') {
            ++bracketDepth;
            current.push_back(ch);
        } else if (ch == ']') {
            --bracketDepth;
            current.push_back(ch);
        } else if (ch == ',' && bracketDepth == 0) {
            elements.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }

    if (!current.empty()) {
        elements.push_back(current);
    }
    return elements;
}

int findPropListColon(std::string_view element) {
    int bracketDepth = 0;
    bool inQuote = false;

    for (std::size_t i = 0; i < element.size(); ++i) {
        const char ch = element[i];
        if (ch == '"' && (i == 0 || element[i - 1] != '\\')) {
            inQuote = !inQuote;
        } else if (!inQuote) {
            if (ch == '[') {
                ++bracketDepth;
            } else if (ch == ']') {
                --bracketDepth;
            } else if (ch == ':' && bracketDepth == 0) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

Datum parseListOrPropList(std::string_view content, LingoVM* vm) {
    const std::string trimmed = trim(content);
    if (trimmed.empty()) {
        return Datum::list();
    }

    const auto elements = splitListElements(trimmed);
    if (elements.empty()) {
        return Datum::list();
    }

    const std::string first = trim(elements.front());
    if (startsWith(first, "#") && containsColon(first)) {
        Datum props = Datum::propList();
        for (const auto& rawElement : elements) {
            const std::string element = trim(rawElement);
            const int colonIndex = findPropListColon(element);
            if (colonIndex > 0 && startsWith(element, "#")) {
                const Datum key = Datum::symbol(trim(std::string_view(element).substr(1, static_cast<std::size_t>(colonIndex - 1))));
                const Datum value = LingoExpressionParser::parse(
                    std::string_view(element).substr(static_cast<std::size_t>(colonIndex + 1)),
                    vm);
                props.propListValue().put(key, value);
            }
        }
        return props;
    }

    std::vector<Datum> items;
    items.reserve(elements.size());
    for (const auto& rawElement : elements) {
        items.push_back(LingoExpressionParser::parse(trim(rawElement), vm));
    }
    return Datum::list(std::move(items));
}

} // namespace

Datum LingoExpressionParser::parse(std::string_view expression, LingoVM* vm) {
    const std::string expr = trim(expression);
    if (expr.empty()) {
        return Datum::voidValue();
    }

    if (const auto parsed = parseInteger(expr)) {
        return Datum::of(*parsed);
    }
    if (const auto parsed = parseDouble(expr)) {
        return Datum::of(*parsed);
    }

    if (startsWith(expr, "#") && expr.size() > 1 && !containsColon(expr)) {
        const std::string symbolName(std::string_view(expr).substr(1));
        if (isIdentifier(symbolName)) {
            return Datum::symbol(symbolName);
        }
    }

    if (startsWith(expr, "\"") && endsWith(expr, "\"") && expr.size() >= 2) {
        return Datum::of(std::string(std::string_view(expr).substr(1, expr.size() - 2)));
    }

    if (startsWith(expr, "[") && endsWith(expr, "]")) {
        return parseListOrPropList(std::string_view(expr).substr(1, expr.size() - 2), vm);
    }

    if (vm != nullptr && isIdentifier(expr)) {
        if (auto handler = vm->findHandler(expr)) {
            return vm->executeHandler(*handler);
        }
        const Datum global = vm->getGlobal(expr);
        if (!global.isVoid()) {
            return global;
        }
    }

    return Datum::voidValue();
}

} // namespace libreshockwave::lingo::vm::parse
