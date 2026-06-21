#include "libreshockwave/lingo/vm/dispatch/ListMethodDispatcher.hpp"

#include "libreshockwave/bitmap/Bitmap.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

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

bool lessIgnoreCase(std::string_view lhs, std::string_view rhs) {
    const std::size_t length = std::min(lhs.size(), rhs.size());
    for (std::size_t index = 0; index < length; ++index) {
        const auto left = static_cast<unsigned char>(lhs[index]);
        const auto right = static_cast<unsigned char>(rhs[index]);
        const int lowerLeft = std::tolower(left);
        const int lowerRight = std::tolower(right);
        if (lowerLeft != lowerRight) {
            return lowerLeft < lowerRight;
        }
    }
    return lhs.size() < rhs.size();
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

std::optional<double> parseDoubleStrict(std::string_view value) {
    const std::string trimmed = trimCopy(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    char* end = nullptr;
    const double result = std::strtod(trimmed.c_str(), &end);
    if (end == trimmed.c_str() || *end != '\0') {
        return std::nullopt;
    }
    return result;
}

int packedColor(const Datum::ColorRef& color) {
    return (color.r << 16) | (color.g << 8) | color.b;
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
        return value->castLib;
    }
    if (const auto* value = datum.asSpriteRef()) {
        return value->channel;
    }
    if (const auto* color = datum.asColorRef()) {
        return packedColor(*color);
    }
    return 0;
}

double toDoubleLikeJava(const Datum& datum) {
    if (const auto* value = datum.asInt()) {
        return static_cast<double>(value->value);
    }
    if (const auto* value = datum.asFloat()) {
        return static_cast<double>(value->value);
    }
    if (const auto* string = datum.asString()) {
        return parseDoubleStrict(string->value).value_or(0.0);
    }
    if (const auto* value = datum.asCastLibRef()) {
        return static_cast<double>(value->castLib);
    }
    if (const auto* value = datum.asSpriteRef()) {
        return static_cast<double>(value->channel);
    }
    if (const auto* color = datum.asColorRef()) {
        return static_cast<double>(packedColor(*color));
    }
    return 0.0;
}

std::string toStringLikeJava(const Datum& datum);

std::string_view stringOrSymbolView(const Datum& datum) {
    if (const auto* string = datum.asString()) {
        return string->value;
    }
    if (const auto* symbol = datum.asSymbol()) {
        return symbol->name;
    }
    return {};
}

std::string datumReprLikeJava(const Datum& datum) {
    if (datum.isVoid()) {
        return "<Void>";
    }
    if (datum.isString()) {
        return "\"" + datum.stringValue() + "\"";
    }
    if (const auto* value = datum.asSymbol()) {
        return "#" + value->name;
    }
    return toStringLikeJava(datum);
}

std::string listStringLikeJava(const Datum::List& list) {
    std::ostringstream out;
    out << '[';
    for (std::size_t index = 0; index < list.items().size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        out << datumReprLikeJava(list.items()[index]);
    }
    out << ']';
    return out.str();
}

std::string propListStringLikeJava(const Datum::PropList& propList) {
    std::ostringstream out;
    out << '[';
    const auto& properties = propList.properties();
    for (std::size_t index = 0; index < properties.size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        out << datumReprLikeJava(properties[index].first) << ": " << datumReprLikeJava(properties[index].second);
    }
    out << ']';
    return out.str();
}

std::string toStringLikeJava(const Datum& datum) {
    if (datum.isVoid() || datum.isNull()) {
        return "";
    }
    if (datum.isString()) {
        return datum.stringValue();
    }
    if (const auto* value = datum.asSymbol()) {
        return value->name;
    }
    if (const auto* value = datum.asColorRef()) {
        if (value->paletteIndex.has_value()) {
            return "paletteIndex(" + std::to_string(*value->paletteIndex) + ")";
        }
        return "color(" + std::to_string(value->r) + ", " + std::to_string(value->g) + ", " +
               std::to_string(value->b) + ")";
    }
    if (const auto* value = datum.asSpriteRef()) {
        return "sprite(" + std::to_string(value->channel) + ")";
    }
    if (const auto* value = datum.asImageRef()) {
        if (!value->bitmap) {
            return "(image null)";
        }
        return "(image " + std::to_string(value->bitmap->width()) + "x" +
               std::to_string(value->bitmap->height()) + ")";
    }
    if (const auto* value = datum.asCastMemberRef()) {
        return "member(" + std::to_string(value->castMember) + ", " + std::to_string(value->castLib) + ")";
    }
    if (const auto* value = datum.asCastLibRef()) {
        return "castLib(" + std::to_string(value->castLib) + ")";
    }
    if (const auto* value = datum.asScriptRef()) {
        return "<script " + std::to_string(value->memberRef.castMember) + ", " +
               std::to_string(value->memberRef.castLib) + ">";
    }
    if (const auto* value = datum.asXtra()) {
        return "<Xtra \"" + value->name + "\">";
    }
    if (const auto* value = datum.asXtraInstance()) {
        return "<XtraInstance \"" + value->xtraName + "\" #" + std::to_string(value->instanceId) + ">";
    }
    if (datum.isList()) {
        return listStringLikeJava(datum.listValue());
    }
    if (datum.isPropList()) {
        return propListStringLikeJava(datum.propListValue());
    }
    if (datum.type() == DatumType::StageRef) {
        return "(the stage)";
    }
    if (datum.type() == DatumType::MovieRef) {
        return "(the movie)";
    }
    if (datum.type() == DatumType::PlayerRef) {
        return "(the player)";
    }
    try {
        return datum.stringValue();
    } catch (const LingoException&) {
        return datum.typeString();
    }
}

std::string_view stringViewLikeJava(const Datum& datum, std::string& storage) {
    if (datum.isVoid() || datum.isNull()) {
        return std::string_view();
    }
    if (const auto* value = datum.asString()) {
        return value->value;
    }
    if (const auto* value = datum.asSymbol()) {
        return value->name;
    }
    if (const auto* value = datum.asFieldText()) {
        return value->value;
    }
    if (const auto* value = datum.asStringChunk()) {
        return value->value;
    }
    storage = toStringLikeJava(datum);
    return storage;
}

bool lingoEquals(const Datum& a, const Datum& b) {
    if ((a.isVoid() && b.isNumber()) || (a.isNumber() && b.isVoid()) || (a.isNumber() && b.isNumber())) {
        return toDoubleLikeJava(a) == toDoubleLikeJava(b);
    }
    if ((a.isString() || a.isSymbol()) && (b.isString() || b.isSymbol())) {
        return equalsIgnoreCase(stringOrSymbolView(a), stringOrSymbolView(b));
    }
    return a == b;
}

void listSetAt(Datum::List& list, int index, Datum value) {
    if (index < 1) {
        return;
    }
    auto& items = list.items();
    const auto zeroIndex = static_cast<std::size_t>(index - 1);
    if (zeroIndex < items.size()) {
        items[zeroIndex] = std::move(value);
        return;
    }
    while (items.size() < zeroIndex) {
        items.push_back(Datum::voidValue());
    }
    items.push_back(std::move(value));
}

Datum::PropList* singlePropListWrapper(Datum::List& list) {
    auto& items = list.items();
    if (items.size() == 1 && items.front().isPropList()) {
        return &items.front().propListValue();
    }
    return nullptr;
}

const Datum::PropList* singlePropListWrapper(const Datum::List& list) {
    const auto& items = list.items();
    if (items.size() == 1 && items.front().isPropList()) {
        return &items.front().propListValue();
    }
    return nullptr;
}

} // namespace

Datum ListMethodDispatcher::dispatch(Datum::List& list,
                                     std::string_view methodName,
                                     std::span<const Datum> args) {
    auto& items = list.items();
    if (equalsIgnoreCase(methodName, "getAt")) {
        if (args.empty()) {
            return Datum::voidValue();
        }
        if (args[0].isString() || args[0].isSymbol()) {
            const auto* propList = singlePropListWrapper(list);
            if (propList != nullptr) {
                const int propIndex = propList->findTypedKey(args[0]);
                return propIndex >= 0 ? propList->properties()[static_cast<std::size_t>(propIndex)].second
                                      : Datum::voidValue();
            }
        }
        const int index = toIntLikeJava(args[0]);
        if (index < 1 || index > static_cast<int>(items.size())) {
            throw LingoException("getAt: index " + std::to_string(index) +
                                 " out of range (list size: " + std::to_string(items.size()) +
                                 ", list: " + listStringLikeJava(list) + ")");
        }
        return items[static_cast<std::size_t>(index - 1)];
    }
    if (equalsIgnoreCase(methodName, "setAt")) {
        if (args.size() >= 2) {
            if (args[0].isString() || args[0].isSymbol()) {
                if (auto* propList = singlePropListWrapper(list)) {
                    propList->putTyped(args[0], args[1]);
                } else {
                    listSetAt(list, toIntLikeJava(args[0]), args[1]);
                }
            } else {
                listSetAt(list, toIntLikeJava(args[0]), args[1]);
            }
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "count")) {
        return Datum::of(static_cast<int>(items.size()));
    }
    if (equalsIgnoreCase(methodName, "append") || equalsIgnoreCase(methodName, "add")) {
        if (!args.empty()) {
            items.push_back(args[0]);
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "addAt")) {
        if (args.size() >= 2) {
            int index = toIntLikeJava(args[0]) - 1;
            if (index < 0) {
                index = 0;
            }
            const auto insertIndex = static_cast<std::size_t>(index);
            if (insertIndex >= items.size()) {
                items.push_back(args[1]);
            } else {
                items.insert(items.begin() + static_cast<std::ptrdiff_t>(insertIndex), args[1]);
            }
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "deleteAt")) {
        if (!args.empty()) {
            const int index = toIntLikeJava(args[0]) - 1;
            if (index >= 0 && index < static_cast<int>(items.size())) {
                items.erase(items.begin() + index);
            }
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "getOne") || equalsIgnoreCase(methodName, "findPos") ||
        equalsIgnoreCase(methodName, "getPos")) {
        if (args.empty()) {
            return Datum::of(0);
        }
        if (equalsIgnoreCase(methodName, "findPos")) {
            if (const auto* propList = singlePropListWrapper(list)) {
                const int propIndex = propList->findUntypedKey(args[0]);
                return propIndex >= 0 ? Datum::of(propIndex + 1) : Datum::voidValue();
            }
        }
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (lingoEquals(items[index], args[0])) {
                return Datum::of(static_cast<int>(index + 1));
            }
        }
        return Datum::of(0);
    }
    if (equalsIgnoreCase(methodName, "getLast")) {
        return items.empty() ? Datum::voidValue() : items.back();
    }
    if (equalsIgnoreCase(methodName, "getFirst")) {
        return items.empty() ? Datum::voidValue() : items.front();
    }
    if (equalsIgnoreCase(methodName, "deleteOne")) {
        if (args.empty()) {
            return Datum::FALSE;
        }
        for (auto iterator = items.begin(); iterator != items.end(); ++iterator) {
            if (lingoEquals(*iterator, args[0])) {
                items.erase(iterator);
                return Datum::TRUE;
            }
        }
        return Datum::FALSE;
    }
    if (equalsIgnoreCase(methodName, "join")) {
        std::string separatorStorage;
        const std::string_view separator = args.empty() ? "&" : stringViewLikeJava(args[0], separatorStorage);
        std::string result;
        if (!items.empty()) {
            result.reserve(separator.size() * (items.size() - 1));
        }
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (index > 0) {
                result.append(separator);
            }
            std::string itemStorage;
            result.append(stringViewLikeJava(items[index], itemStorage));
        }
        return Datum::of(std::move(result));
    }
    if (equalsIgnoreCase(methodName, "sort")) {
        std::sort(items.begin(), items.end(), [](const Datum& lhs, const Datum& rhs) {
            if (lhs.isInt() && rhs.isInt()) {
                return lhs.intValue() < rhs.intValue();
            }
            std::string lhsStorage;
            std::string rhsStorage;
            return lessIgnoreCase(stringViewLikeJava(lhs, lhsStorage), stringViewLikeJava(rhs, rhsStorage));
        });
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "duplicate")) {
        return list.deepCopyDatum();
    }
    return Datum::voidValue();
}

} // namespace libreshockwave::lingo::vm::dispatch
