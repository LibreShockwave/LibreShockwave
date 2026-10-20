#include "libreshockwave/lingo/vm/dispatch/list_method_dispatcher.hpp"

#include "libreshockwave/bitmap/bitmap.hpp"

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
        const int lower_left = std::tolower(left);
        const int lower_right = std::tolower(right);
        if (lower_left != lower_right) {
            return lower_left < lower_right;
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
        return value->cast_lib;
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
        return static_cast<double>(value->cast_lib);
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

std::optional<std::string_view> directStringViewLikeJava(const Datum& datum) {
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
    if (const auto* value = datum.asTimeoutRef()) {
        return value->name;
    }
    return std::nullopt;
}

std::string datumReprLikeJava(const Datum& datum) {
    if (datum.isVoid()) {
        return "<Void>";
    }
    if (const auto* value = datum.asString()) {
        return "\"" + value->value + "\"";
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

std::string propListStringLikeJava(const Datum::PropList& prop_list) {
    std::string result;
    result.push_back('[');
    const auto& properties = prop_list.properties();
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
    if (const auto* value = datum.asString()) {
        return value->value;
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
        if (value->palette_index.has_value()) {
            return "paletteIndex(" + std::to_string(*value->palette_index) + ")";
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
        return "member(" + std::to_string(value->cast_member) + ", " + std::to_string(value->cast_lib) + ")";
    }
    if (const auto* value = datum.asCastLibRef()) {
        return "castLib(" + std::to_string(value->cast_lib) + ")";
    }
    if (const auto* value = datum.asScriptRef()) {
        return "<script " + std::to_string(value->member_ref.cast_member) + ", " +
               std::to_string(value->member_ref.cast_lib) + ">";
    }
    if (const auto* value = datum.asXtra()) {
        return "<Xtra \"" + value->name + "\">";
    }
    if (const auto* value = datum.asXtraInstance()) {
        return "<XtraInstance \"" + value->xtra_name + "\" #" + std::to_string(value->instance_id) + ">";
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
    if (const auto direct_value = directStringViewLikeJava(datum)) {
        return *direct_value;
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
    const auto zero_index = static_cast<std::size_t>(index - 1);
    if (zero_index < items.size()) {
        items[zero_index] = std::move(value);
        return;
    }
    while (items.size() < zero_index) {
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
                                     std::string_view method_name,
                                     std::span<const Datum> args) {
    auto& items = list.items();
    if (equalsIgnoreCase(method_name, "getAt")) {
        if (args.empty()) {
            return Datum::voidValue();
        }
        if (args[0].isString() || args[0].isSymbol()) {
            const auto* prop_list = singlePropListWrapper(list);
            if (prop_list != nullptr) {
                const int prop_index = prop_list->findTypedKey(args[0]);
                return prop_index >= 0 ? prop_list->properties()[static_cast<std::size_t>(prop_index)].second
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
    if (equalsIgnoreCase(method_name, "setAt")) {
        if (args.size() >= 2) {
            if (args[0].isString() || args[0].isSymbol()) {
                if (auto* prop_list = singlePropListWrapper(list)) {
                    prop_list->putTyped(args[0], args[1]);
                } else {
                    listSetAt(list, toIntLikeJava(args[0]), args[1]);
                }
            } else {
                listSetAt(list, toIntLikeJava(args[0]), args[1]);
            }
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "count")) {
        return Datum::of(static_cast<int>(items.size()));
    }
    if (equalsIgnoreCase(method_name, "append") || equalsIgnoreCase(method_name, "add")) {
        if (!args.empty()) {
            items.push_back(args[0]);
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "addAt")) {
        if (args.size() >= 2) {
            int index = toIntLikeJava(args[0]) - 1;
            if (index < 0) {
                index = 0;
            }
            const auto insert_index = static_cast<std::size_t>(index);
            if (insert_index >= items.size()) {
                items.push_back(args[1]);
            } else {
                items.insert(items.begin() + static_cast<std::ptrdiff_t>(insert_index), args[1]);
            }
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "deleteAt")) {
        if (!args.empty()) {
            const int index = toIntLikeJava(args[0]) - 1;
            if (index >= 0 && index < static_cast<int>(items.size())) {
                items.erase(items.begin() + index);
            }
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "getOne") || equalsIgnoreCase(method_name, "findPos") ||
        equalsIgnoreCase(method_name, "getPos")) {
        if (args.empty()) {
            return Datum::of(0);
        }
        if (equalsIgnoreCase(method_name, "findPos")) {
            if (const auto* prop_list = singlePropListWrapper(list)) {
                const int prop_index = prop_list->findUntypedKey(args[0]);
                return prop_index >= 0 ? Datum::of(prop_index + 1) : Datum::voidValue();
            }
        }
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (lingoEquals(items[index], args[0])) {
                return Datum::of(static_cast<int>(index + 1));
            }
        }
        return Datum::of(0);
    }
    if (equalsIgnoreCase(method_name, "getLast")) {
        return items.empty() ? Datum::voidValue() : items.back();
    }
    if (equalsIgnoreCase(method_name, "getFirst")) {
        return items.empty() ? Datum::voidValue() : items.front();
    }
    if (equalsIgnoreCase(method_name, "deleteOne")) {
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
    if (equalsIgnoreCase(method_name, "join")) {
        std::string separator_storage;
        const std::string_view separator = args.empty() ? "&" : stringViewLikeJava(args[0], separator_storage);
        std::string result;
        if (!items.empty()) {
            std::size_t direct_item_size = 0;
            for (const auto& item : items) {
                if (const auto direct_value = directStringViewLikeJava(item)) {
                    direct_item_size += direct_value->size();
                }
            }
            result.reserve(separator.size() * (items.size() - 1) + direct_item_size);
        }
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (index > 0) {
                result.append(separator);
            }
            std::string item_storage;
            result.append(stringViewLikeJava(items[index], item_storage));
        }
        return Datum::of(std::move(result));
    }
    if (equalsIgnoreCase(method_name, "sort")) {
        std::sort(items.begin(), items.end(), [](const Datum& lhs, const Datum& rhs) {
            if (lhs.isInt() && rhs.isInt()) {
                return lhs.intValue() < rhs.intValue();
            }
            std::string lhs_storage;
            std::string rhs_storage;
            return lessIgnoreCase(stringViewLikeJava(lhs, lhs_storage), stringViewLikeJava(rhs, rhs_storage));
        });
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "duplicate")) {
        return list.deepCopyDatum();
    }
    return Datum::voidValue();
}

} // namespace libreshockwave::lingo::vm::dispatch
