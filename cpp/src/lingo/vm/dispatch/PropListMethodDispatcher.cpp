#include "libreshockwave/lingo/vm/dispatch/PropListMethodDispatcher.hpp"

#include "libreshockwave/bitmap/Bitmap.hpp"

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
    std::string result;
    result.push_back('[');
    for (std::size_t index = 0; index < list.items().size(); ++index) {
        if (index > 0) {
            result.append(", ");
        }
        result.append(datumReprLikeJava(list.items()[index]));
    }
    result.push_back(']');
    return result;
}

std::string propListStringLikeJava(const Datum::PropList& propList) {
    std::string result;
    result.push_back('[');
    const auto& properties = propList.properties();
    for (std::size_t index = 0; index < properties.size(); ++index) {
        if (index > 0) {
            result.append(", ");
        }
        result.append(datumReprLikeJava(properties[index].first));
        result.append(": ");
        result.append(datumReprLikeJava(properties[index].second));
    }
    result.push_back(']');
    return result;
}

std::string toStringLikeJava(const Datum& datum) {
    if (datum.isVoid() || datum.isNull()) {
        return "";
    }
    if (datum.isString()) {
        return datum.stringValue();
    }
    if (const auto* value = datum.asFieldText()) {
        return value->value;
    }
    if (const auto* value = datum.asStringChunk()) {
        return value->value;
    }
    if (const auto* value = datum.asSymbol()) {
        return value->name;
    }
    if (const auto* value = datum.asTimeoutRef()) {
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

std::string keyNameLikeJava(const Datum& datum) {
    if (const auto* symbol = datum.asSymbol()) {
        return symbol->name;
    }
    return toStringLikeJava(datum);
}

std::string_view keyNameLikeJavaView(const Datum& datum, std::string& storage) {
    if (datum.isVoid() || datum.isNull()) {
        return std::string_view();
    }
    if (const auto* symbol = datum.asSymbol()) {
        return symbol->name;
    }
    if (const auto* string = datum.asString()) {
        return string->value;
    }
    if (const auto* field = datum.asFieldText()) {
        return field->value;
    }
    if (const auto* chunk = datum.asStringChunk()) {
        return chunk->value;
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

Datum getPropListKey(const Datum::PropList& propList, std::string_view keyName) {
    const int index = propList.findUntypedKeyName(keyName);
    return index >= 0 ? propList.properties()[static_cast<std::size_t>(index)].second : Datum::voidValue();
}

int findPropIndexByKey(const Datum::PropList& propList, const Datum& key) {
    return propList.findUntypedKey(key);
}

int findPropIndexTypedKey(const Datum::PropList& propList, const Datum& key) {
    return propList.findTypedKey(key);
}

Datum getPropListTypedKey(const Datum::PropList& propList, const Datum& key) {
    const int index = findPropIndexTypedKey(propList, key);
    return index >= 0 ? propList.properties()[static_cast<std::size_t>(index)].second : Datum::voidValue();
}

Datum stringKeyFromInt(const Datum& key) {
    return Datum::of(std::to_string(toIntLikeJava(key)));
}

void putPropListTypedKey(Datum::PropList& propList, const Datum& key, Datum value) {
    propList.putTyped(key, std::move(value));
}

} // namespace

Datum PropListMethodDispatcher::dispatch(Datum::PropList& propList,
                                         std::string_view methodName,
                                         std::span<const Datum> args) {
    if (equalsIgnoreCase(methodName, "count")) {
        if (!args.empty()) {
            std::string keyStorage;
            const Datum value = getPropListKey(propList, keyNameLikeJavaView(args[0], keyStorage));
            if (value.isList()) return Datum::of(value.listValue().count());
            if (value.isPropList()) return Datum::of(value.propListValue().count());
            return Datum::of(0);
        }
        return Datum::of(propList.count());
    }
    if (equalsIgnoreCase(methodName, "getProp") || equalsIgnoreCase(methodName, "getPropRef") ||
        equalsIgnoreCase(methodName, "getAProp") || equalsIgnoreCase(methodName, "getProperty")) {
        if (args.empty()) {
            return Datum::voidValue();
        }
        std::string keyStorage;
        Datum value = getPropListKey(propList, keyNameLikeJavaView(args[0], keyStorage));
        if (args.size() >= 2 && value.isList()) {
            const int index = toIntLikeJava(args[1]);
            if (index >= 1 && index <= value.listValue().count()) {
                return value.listValue().getAt(index);
            }
            return Datum::voidValue();
        }
        return value;
    }
    if (equalsIgnoreCase(methodName, "setProp") || equalsIgnoreCase(methodName, "setAProp")) {
        if (args.size() >= 2) {
            putPropListTypedKey(propList, args[0], args[1]);
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "addProp")) {
        if (args.size() >= 2) {
            propList.appendProperty(args[0], args[1]);
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "getAt")) {
        if (args.empty()) {
            return Datum::voidValue();
        }
        if (args[0].isString() || args[0].isSymbol()) {
            return getPropListTypedKey(propList, args[0]);
        }
        const int index = toIntLikeJava(args[0]) - 1;
        const auto& properties = std::as_const(propList).properties();
        if (index >= 0 && index < static_cast<int>(properties.size())) {
            return properties[static_cast<std::size_t>(index)].second;
        }
        if (args[0].isInt()) {
            return getPropListTypedKey(propList, stringKeyFromInt(args[0]));
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "setAt")) {
        if (args.size() >= 2) {
            const int index = toIntLikeJava(args[0]) - 1;
            if (args[0].isInt() && index >= 0 &&
                index < static_cast<int>(std::as_const(propList).properties().size())) {
                auto& properties = propList.properties();
                properties[static_cast<std::size_t>(index)].second = args[1];
            } else if (args[0].isInt()) {
                putPropListTypedKey(propList, stringKeyFromInt(args[0]), args[1]);
            } else {
                putPropListTypedKey(propList, args[0], args[1]);
            }
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "getOne")) {
        if (args.empty()) {
            return Datum::of(0);
        }
        const auto& properties = std::as_const(propList).properties();
        for (const auto& entry : properties) {
            if (lingoEquals(entry.second, args[0])) {
                return entry.first;
            }
        }
        return Datum::of(0);
    }
    if (equalsIgnoreCase(methodName, "deleteProp")) {
        if (!args.empty()) {
            const int index = findPropIndexTypedKey(propList, args[0]);
            if (index >= 0) {
                (void)propList.erasePropertyAt(index);
            }
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "findPos")) {
        if (args.empty()) {
            return Datum::voidValue();
        }
        const int index = findPropIndexByKey(propList, args[0]);
        return index >= 0 ? Datum::of(index + 1) : Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "getPropAt")) {
        if (!args.empty()) {
            const int index = toIntLikeJava(args[0]) - 1;
            const auto& properties = std::as_const(propList).properties();
            if (index >= 0 && index < static_cast<int>(properties.size())) {
                return properties[static_cast<std::size_t>(index)].first;
            }
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "deleteAt")) {
        if (!args.empty()) {
            const int index = toIntLikeJava(args[0]) - 1;
            (void)propList.erasePropertyAt(index);
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "getLast")) {
        const auto& properties = std::as_const(propList).properties();
        return properties.empty() ? Datum::voidValue() : properties.back().second;
    }
    if (equalsIgnoreCase(methodName, "getFirst")) {
        const auto& properties = std::as_const(propList).properties();
        return properties.empty() ? Datum::voidValue() : properties.front().second;
    }
    if (equalsIgnoreCase(methodName, "duplicate")) {
        Datum copy = Datum::propList(propList.sorted());
        const auto& properties = std::as_const(propList).properties();
        auto& copiedProperties = copy.propListValue().properties();
        copiedProperties.reserve(properties.size());
        for (const auto& entry : properties) {
            copiedProperties.emplace_back(entry.first.deepCopy(), entry.second.deepCopy());
        }
        return copy;
    }
    Datum value = getPropListKey(propList, methodName);
    if (!value.isVoid()) {
        if (!args.empty() && value.isList()) {
            const int index = toIntLikeJava(args[0]);
            if (index >= 1 && index <= value.listValue().count()) {
                return value.listValue().getAt(index);
            }
            return Datum::voidValue();
        }
        return value;
    }
    return Datum::voidValue();
}

} // namespace libreshockwave::lingo::vm::dispatch
