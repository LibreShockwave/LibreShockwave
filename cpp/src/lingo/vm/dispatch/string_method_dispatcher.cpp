#include "libreshockwave/lingo/vm/dispatch/string_method_dispatcher.hpp"

#include "libreshockwave/lingo/vm/util/string_chunk_utils.hpp"

#include <charconv>
#include <cctype>
#include <optional>
#include <stdexcept>
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

std::string_view trimView(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end != begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::optional<int> parseIntStrict(std::string_view value) {
    const std::string_view trimmed = trimView(value);
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
    if (const auto* string = datum.asString()) {
        return parseIntStrict(string->value).value_or(0);
    }
    if (const auto* value = datum.asCastLibRef()) {
        return value->cast_lib;
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
                           StringChunkType chunk_type,
                           int start,
                           int end,
                           char item_delimiter) {
    if (value.empty() || start < 1) {
        return "";
    }
    return util::getChunkRange(value, chunk_type, start, end, item_delimiter);
}

} // namespace

Datum StringMethodDispatcher::dispatch(std::string_view value,
                                       std::string_view method_name,
                                       std::span<const Datum> args,
                                       char item_delimiter) {
    if (equalsIgnoreCase(method_name, "length")) {
        return Datum::of(static_cast<int>(value.size()));
    }
    if (equalsIgnoreCase(method_name, "char")) {
        if (args.empty()) {
            return Datum::of(std::string());
        }
        const int index = toIntLikeJava(args[0]);
        if (index >= 1 && index <= static_cast<int>(value.size())) {
            return Datum::of(std::string(value.substr(static_cast<std::size_t>(index - 1), 1)));
        }
        return Datum::of(std::string());
    }
    if (equalsIgnoreCase(method_name, "count")) {
        if (args.empty()) {
            return Datum::of(static_cast<int>(value.size()));
        }
        const auto chunk_type = symbolChunkType(args[0]);
        if (!chunk_type.has_value()) {
            return Datum::of(static_cast<int>(value.size()));
        }
        return Datum::of(util::countChunks(value, *chunk_type, item_delimiter));
    }
    if (equalsIgnoreCase(method_name, "getPropRef")) {
        if (args.size() < 2) {
            return Datum::of(std::string());
        }
        const auto chunk_type = symbolChunkType(args[0]);
        if (!chunk_type.has_value()) {
            return Datum::of(std::string());
        }
        const int index = toIntLikeJava(args[1]);
        return Datum::of(getStringChunk(value, *chunk_type, index, index, item_delimiter));
    }
    if (equalsIgnoreCase(method_name, "getProp")) {
        if (args.size() < 2) {
            return Datum::of(std::string());
        }
        const auto chunk_type = symbolChunkType(args[0]);
        if (!chunk_type.has_value()) {
            return Datum::of(std::string());
        }
        const int start = toIntLikeJava(args[1]);
        const int end = args.size() >= 3 ? toIntLikeJava(args[2]) : start;
        return Datum::of(getStringChunk(value, *chunk_type, start, end, item_delimiter));
    }
    return Datum::voidValue();
}

} // namespace libreshockwave::lingo::vm::dispatch
