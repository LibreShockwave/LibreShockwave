#include "libreshockwave/lingo/vm/dispatch/StringMethodDispatcher.hpp"

#include "libreshockwave/lingo/vm/util/StringChunkUtils.hpp"

#include <charconv>
#include <cctype>
#include <optional>
#include <string>

namespace libreshockwave::lingo::vm::dispatch {
namespace {

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    if (lhs == rhs) {
        return true;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const auto left = static_cast<unsigned char>(lhs[index]);
        const auto right = static_cast<unsigned char>(rhs[index]);
        if (std::tolower(left) != std::tolower(right)) {
            return false;
        }
    }
    return true;
}

std::string trimCopy(std::string_view value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(begin, end);
}

std::optional<int> parseIntStrict(std::string_view value) {
    const std::string trimmed = trimCopy(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    int result = 0;
    const auto* begin = trimmed.data();
    const auto* end = trimmed.data() + trimmed.size();
    const auto parsed = std::from_chars(begin, end, result);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return std::nullopt;
    }
    return result;
}

int toIntLikeJava(const Datum& datum) {
    if (const auto* value = datum.asInt()) {
        return value->value;
    }
    if (const auto* value = datum.asFloat()) {
        return static_cast<int>(value->value);
    }
    if (datum.isString()) {
        return parseIntStrict(datum.stringValue()).value_or(0);
    }
    if (const auto* value = datum.asCastLibRef()) {
        return value->castLib;
    }
    if (const auto* value = datum.asSpriteRef()) {
        return value->channel;
    }
    return 0;
}

std::optional<StringChunkType> symbolChunkType(const Datum& datum) {
    const auto* symbol = datum.asSymbol();
    if (symbol == nullptr) {
        return std::nullopt;
    }
    try {
        return stringChunkTypeFromName(symbol->name);
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    }
}

std::string getStringChunk(std::string_view value,
                           StringChunkType chunkType,
                           int start,
                           int end,
                           char itemDelimiter) {
    if (value.empty() || start < 1) {
        return "";
    }
    return util::getChunkRange(value, chunkType, start, end, itemDelimiter);
}

} // namespace

Datum StringMethodDispatcher::dispatch(std::string_view value,
                                       std::string_view methodName,
                                       const std::vector<Datum>& args,
                                       char itemDelimiter) {
    if (equalsIgnoreCase(methodName, "length")) {
        return Datum::of(static_cast<int>(value.size()));
    }
    if (equalsIgnoreCase(methodName, "char")) {
        if (args.empty()) {
            return Datum::of(std::string());
        }
        const int index = toIntLikeJava(args[0]);
        if (index >= 1 && index <= static_cast<int>(value.size())) {
            return Datum::of(std::string(value.substr(static_cast<std::size_t>(index - 1), 1)));
        }
        return Datum::of(std::string());
    }
    if (equalsIgnoreCase(methodName, "count")) {
        if (args.empty()) {
            return Datum::of(static_cast<int>(value.size()));
        }
        const auto chunkType = symbolChunkType(args[0]);
        if (!chunkType.has_value()) {
            return Datum::of(static_cast<int>(value.size()));
        }
        return Datum::of(util::countChunks(value, *chunkType, itemDelimiter));
    }
    if (equalsIgnoreCase(methodName, "getPropRef")) {
        if (args.size() < 2) {
            return Datum::of(std::string());
        }
        const auto chunkType = symbolChunkType(args[0]);
        if (!chunkType.has_value()) {
            return Datum::of(std::string());
        }
        const int index = toIntLikeJava(args[1]);
        return Datum::of(getStringChunk(value, *chunkType, index, index, itemDelimiter));
    }
    if (equalsIgnoreCase(methodName, "getProp")) {
        if (args.size() < 2) {
            return Datum::of(std::string());
        }
        const auto chunkType = symbolChunkType(args[0]);
        if (!chunkType.has_value()) {
            return Datum::of(std::string());
        }
        const int start = toIntLikeJava(args[1]);
        const int end = args.size() >= 3 ? toIntLikeJava(args[2]) : start;
        return Datum::of(getStringChunk(value, *chunkType, start, end, itemDelimiter));
    }
    return Datum::voidValue();
}

} // namespace libreshockwave::lingo::vm::dispatch
