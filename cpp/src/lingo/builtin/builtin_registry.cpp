#include "libreshockwave/lingo/builtin/builtin_registry.hpp"

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/bitmap/palette.hpp"
#include "libreshockwave/lingo/lingo_value_parser.hpp"
#include "libreshockwave/lingo/vm/debug_config.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <sstream>
#include <utility>

namespace libreshockwave::lingo::builtin {
namespace {

Datum voidDatum() {
    return Datum::voidValue();
}

void debugPlaybackMessage(BuiltinContext& context, const std::string& message) {
    if (!context.debug_playback_enabled && !vm::DebugConfig::isDebugPlaybackEnabled()) {
        return;
    }
    if (context.output_handler) {
        context.output_handler("DEBUG", message);
    } else {
        std::cout << "[DEBUG] " << message << '\n';
    }
}

bool isMovieRef(const Datum& datum) {
    return datum.type() == DatumType::MovieRef;
}

void appendLowercase(std::string& output, std::string_view value) {
    for (const unsigned char ch : value) {
        output.push_back(static_cast<char>(std::tolower(ch)));
    }
}

bool isResetPaletteArg(const Datum& datum) {
    if (!datum.isInt()) {
        return false;
    }
    const int value = datum.intValue();
    return value == 0 || value == -1;
}

bool isEmptyRect(const Datum::IntRect& rect) {
    return rect.right <= rect.left || rect.bottom <= rect.top;
}

Datum boolDatum(bool value) {
    return value ? Datum::TRUE : Datum::FALSE;
}

std::string trimCopy(const std::string& value) {
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
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::optional<long long> parseLongStrict(const std::string& value, int base) {
    const std::string trimmed = trimCopy(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const long long result = std::strtoll(trimmed.c_str(), &end, base);
    if (end == trimmed.c_str() || *end != '\0') {
        return std::nullopt;
    }
    return result;
}

std::optional<int> parseIntStrict(const std::string& value) {
    const auto parsed = parseLongStrict(value, 10);
    if (!parsed || *parsed < std::numeric_limits<int>::min() || *parsed > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return static_cast<int>(*parsed);
}

std::optional<double> parseDoubleStrict(const std::string& value) {
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
    if (datum.isString()) {
        return parseIntStrict(datum.stringValue()).value_or(0);
    }
    if (const auto* value = datum.asCastLibRef()) {
        return value->cast_lib;
    }
    if (const auto* value = datum.asSpriteRef()) {
        return value->channel;
    }
    if (const auto* value = datum.asColorRef()) {
        return packedColor(*value);
    }
    return 0;
}

int resolveCastLibScope(BuiltinContext& context, const std::optional<Datum>& scope) {
    if (!scope.has_value() || scope->isVoid()) {
        return 0;
    }
    if (const auto* cast_lib = scope->asCastLibRef()) {
        return cast_lib->cast_lib;
    }
    if (scope->isInt() || scope->isFloat()) {
        return toIntLikeJava(*scope);
    }
    if (!context.cast_lib_name_resolver) {
        return 0;
    }
    const std::string name = trimCopy(scope->stringValue());
    if (name.empty()) {
        return 0;
    }
    return std::max(context.cast_lib_name_resolver(name), 0);
}

Datum scriptRefFromCastMemberDatum(const Datum& member_ref) {
    if (const auto* cast_member = member_ref.asCastMemberRef()) {
        return Datum::scriptRef(*cast_member);
    }
    return Datum::voidValue();
}

std::uint64_t scriptPropertyCacheKey(int cast_lib, int member_num) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cast_lib)) << 32U) |
           static_cast<std::uint32_t>(member_num);
}

const std::vector<std::string>& declaredScriptPropertyNames(BuiltinContext& context,
                                                            const Datum::CastMemberRef& member_ref) {
    const auto key = scriptPropertyCacheKey(member_ref.cast_lib, member_ref.memberNum());
    if (const auto found = context.script_property_names_cache.find(key);
        found != context.script_property_names_cache.end()) {
        return found->second;
    }
    auto property_names = context.script_property_names_resolver
        ? context.script_property_names_resolver(member_ref.cast_lib, member_ref.memberNum())
        : std::vector<std::string>{};
    auto [inserted, _] = context.script_property_names_cache.emplace(key, std::move(property_names));
    return inserted->second;
}

Datum createScriptInstanceFromRef(BuiltinContext& context,
                                  const Datum::CastMemberRef& member_ref,
                                  const std::vector<Datum>& constructor_args = {}) {
    Datum instance = Datum::scriptInstance("script", member_ref);
    if (context.script_property_names_resolver) {
        const auto& property_names = declaredScriptPropertyNames(context, member_ref);
        auto& script_instance = instance.scriptInstanceValue();
        script_instance.reserveLocalProperties(property_names.size());
        for (const auto& property_name : property_names) {
            script_instance.appendLocalProperty(property_name, Datum::voidValue());
        }
    }
    if (context.call_target_handler) {
        Datum result = context.call_target_handler(instance, "new", constructor_args);
        if (!result.isVoid()) {
            return result;
        }
    }
    return instance;
}

double toDoubleLikeJava(const Datum& datum) {
    if (const auto* value = datum.asInt()) {
        return static_cast<double>(value->value);
    }
    if (const auto* value = datum.asFloat()) {
        return static_cast<double>(value->value);
    }
    if (datum.isString()) {
        return parseDoubleStrict(datum.stringValue()).value_or(0.0);
    }
    if (const auto* value = datum.asCastLibRef()) {
        return static_cast<double>(value->cast_lib);
    }
    if (const auto* value = datum.asSpriteRef()) {
        return static_cast<double>(value->channel);
    }
    if (const auto* value = datum.asColorRef()) {
        return static_cast<double>(packedColor(*value));
    }
    return 0.0;
}

int javaRoundToInt(double value) {
    if (!std::isfinite(value)) {
        return 0;
    }
    return static_cast<int>(std::floor(value + 0.5));
}

std::string toStringLikeJava(const Datum& datum);
std::string datumReprLikeJava(const Datum& datum);
std::string keyName(const Datum& datum);

std::optional<std::string_view> directStringViewLikeJava(const Datum& datum) {
    if (datum.isVoid() || datum.isNull()) {
        return std::string_view();
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
    return std::nullopt;
}

std::string_view stringViewLikeJava(const Datum& datum, std::string& storage) {
    if (const auto direct_value = directStringViewLikeJava(datum)) {
        return *direct_value;
    }
    storage = toStringLikeJava(datum);
    return storage;
}

const std::string& stringRefLikeJava(const Datum& datum, std::string& storage) {
    static const std::string empty;
    if (datum.isVoid() || datum.isNull()) {
        return empty;
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
    storage = toStringLikeJava(datum);
    return storage;
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

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs);

std::string_view delimiterFromArg(const Datum& datum, std::string& storage) {
    const std::string_view delimiter = stringViewLikeJava(datum, storage);
    if (equalsIgnoreCase(delimiter, "return")) {
        return "\r";
    }
    if (equalsIgnoreCase(delimiter, "linefeed")) {
        return "\n";
    }
    if (equalsIgnoreCase(delimiter, "tab")) {
        return "\t";
    }
    return delimiter;
}

std::vector<std::string> splitPropertyText(std::string_view text, std::string_view delimiter) {
    std::vector<std::string> lines;
    std::string current;

    auto flush = [&]() {
        lines.push_back(std::move(current));
        current.clear();
    };

    if (!delimiter.empty()) {
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t next = text.find(delimiter, start);
            if (next == std::string_view::npos) {
                lines.emplace_back(text.substr(start));
                break;
            }
            lines.emplace_back(text.substr(start, next - start));
            start = next + delimiter.size();
        }
        return lines;
    }

    for (std::size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '\r') {
            flush();
            if (index + 1 < text.size() && text[index + 1] == '\n') {
                ++index;
            }
        } else if (ch == '\n') {
            flush();
        } else {
            current.push_back(ch);
        }
    }
    flush();
    return lines;
}

Datum parseDirectorPropertyText(std::string_view text, std::string_view delimiter) {
    Datum result = Datum::propList();
    auto& properties = result.propListValue();
    const auto lines = splitPropertyText(text, delimiter);
    properties.properties().reserve(lines.size());

    for (const auto& raw_line : lines) {
        std::string line = trimCopy(raw_line);
        if (line.empty() || line.front() == '#' || line.starts_with("--")) {
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        std::string key = trimCopy(line.substr(0, equals));
        if (key.empty()) {
            continue;
        }
        std::string value = trimCopy(line.substr(equals + 1));
        properties.put(Datum::of(std::move(key)), Datum::of(std::move(value)));
    }

    return result;
}

bool regionMatchesIgnoreCase(std::string_view value, std::size_t offset, std::string_view pattern) {
    if (offset + pattern.size() > value.size()) {
        return false;
    }
    for (std::size_t index = 0; index < pattern.size(); ++index) {
        const auto lhs = static_cast<unsigned char>(value[offset + index]);
        const auto rhs = static_cast<unsigned char>(pattern[index]);
        if (std::tolower(lhs) != std::tolower(rhs)) {
            return false;
        }
    }
    return true;
}

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

bool lingoEquals(const Datum& lhs, const Datum& rhs) {
    if ((lhs.isVoid() && rhs.isNumber()) || (lhs.isNumber() && rhs.isVoid())) {
        return toDoubleLikeJava(lhs) == toDoubleLikeJava(rhs);
    }
    if (lhs.isNumber() && rhs.isNumber()) {
        return toDoubleLikeJava(lhs) == toDoubleLikeJava(rhs);
    }
    if (lhs.isNumber()) {
        if (const auto string_value = directStringViewLikeJava(rhs)) {
            if (const auto numeric_value = parseDoubleStrict(std::string(*string_value))) {
                return toDoubleLikeJava(lhs) == *numeric_value;
            }
        }
    }
    if (rhs.isNumber()) {
        if (const auto string_value = directStringViewLikeJava(lhs)) {
            if (const auto numeric_value = parseDoubleStrict(std::string(*string_value))) {
                return toDoubleLikeJava(rhs) == *numeric_value;
            }
        }
    }
    if ((lhs.isString() || lhs.isSymbol()) && (rhs.isString() || rhs.isSymbol())) {
        const auto lhs_view = directStringViewLikeJava(lhs);
        const auto rhs_view = directStringViewLikeJava(rhs);
        return lhs_view.has_value() && rhs_view.has_value() && equalsIgnoreCase(*lhs_view, *rhs_view);
    }
    return lhs == rhs;
}

bool truthyLikeJava(const Datum& datum) {
    if (datum.isVoid() || datum.isNull()) {
        return false;
    }
    if (const auto* value = datum.asInt()) {
        return value->value != 0;
    }
    if (const auto* value = datum.asFloat()) {
        return value->value != 0.0F;
    }
    if (const auto* value = datum.asString()) {
        return !value->value.empty();
    }
    if (const auto* value = datum.asFieldText()) {
        return !value->value.empty();
    }
    if (const auto* value = datum.asStringChunk()) {
        return !value->value.empty();
    }
    return true;
}

int findPropIndexTyped(const Datum::PropList& prop_list, const Datum& key) {
    return prop_list.findTypedKey(key);
}

int findPropIndexSameType(const Datum::PropList& prop_list, const Datum& key) {
    return prop_list.findSameTypeKey(key);
}

int findPropIndexUntyped(const Datum::PropList& prop_list, const Datum& key) {
    return prop_list.findUntypedKey(key);
}

Datum propListBuiltinGetAtValue(const Datum::PropList& prop_list, const Datum& key_or_index) {
    int index = -1;
    if (key_or_index.asSymbol() != nullptr || key_or_index.isString()) {
        index = findPropIndexTyped(prop_list, key_or_index);
    } else {
        const int position = toIntLikeJava(key_or_index);
        if (position >= 1 && position <= prop_list.count()) {
            index = position - 1;
        }
    }
    if (index >= 0) {
        return prop_list.properties()[static_cast<std::size_t>(index)].second;
    }
    return Datum::voidValue();
}

void putPropTyped(Datum::PropList& prop_list, const Datum& key, Datum value) {
    prop_list.putTyped(key, std::move(value));
}

void putPropSameType(Datum::PropList& prop_list, const Datum& key, Datum value) {
    prop_list.putSameType(key, std::move(value));
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

void putStringProp(Datum& prop_list, std::string_view key, Datum value) {
    prop_list.propListValue().put(Datum::of(std::string(key)), std::move(value));
}

Datum defaultStreamStatusDatum() {
    auto props = Datum::propList();
    props.propListValue().properties().reserve(5);
    putStringProp(props, "URL", Datum::of(std::string()));
    putStringProp(props, "state", Datum::of(std::string("Error")));
    putStringProp(props, "bytesSoFar", Datum::of(0));
    putStringProp(props, "bytesTotal", Datum::of(0));
    putStringProp(props, "error", Datum::of(std::string("OK")));
    return props;
}

Datum getTypedSymbolProp(const Datum::PropList& prop_list, std::string_view key) {
    const int index = findPropIndexTyped(prop_list, Datum::symbol(std::string(key)));
    return index >= 0 ? prop_list.properties()[static_cast<std::size_t>(index)].second : Datum::voidValue();
}

bool isMessageStruct(const Datum::PropList& prop_list) {
    if (getTypedSymbolProp(prop_list, "connection").type() != DatumType::ScriptInstanceRef) {
        return false;
    }

    const Datum ilk = getTypedSymbolProp(prop_list, "ilk");
    if (const auto* symbol = ilk.asSymbol(); symbol != nullptr && equalsIgnoreCase(symbol->name, "struct")) {
        return true;
    }

    return !getTypedSymbolProp(prop_list, "subject").isVoid() &&
           !getTypedSymbolProp(prop_list, "content").isVoid();
}

Datum snapshotStructArgForCall(const Datum& arg) {
    if (!arg.isPropList() || !isMessageStruct(arg.propListValue())) {
        return arg;
    }
    return arg.deepCopy();
}

std::vector<Datum> snapshotStructArgsForCall(std::span<const Datum> args) {
    std::vector<Datum> snapshot;
    snapshot.reserve(args.size());
    for (const auto& arg : args) {
        snapshot.emplace_back(snapshotStructArgForCall(arg));
    }
    return snapshot;
}

bool hasCastProvider(const BuiltinContext& context) {
    return context.named_cast_member_creator ||
           context.cast_lib_number_resolver ||
           context.cast_lib_name_resolver ||
           context.cast_lib_count_supplier ||
           context.cast_member_count_supplier ||
           context.cast_member_resolver ||
           context.cast_member_name_resolver ||
           context.cast_member_exists_resolver ||
           context.registry_visible_member_resolver ||
           context.cast_member_property_getter ||
           context.cast_member_property_setter ||
           context.field_resolver ||
           context.field_setter;
}

Datum castLibRefOrVoid(int cast_lib) {
    if (cast_lib < 1) {
        return Datum::voidValue();
    }
    return Datum::castLibRef(id::CastLibId(cast_lib));
}

Datum getCastLibMemberAccessorValue(BuiltinContext& context,
                                    const Datum::CastLibMemberAccessor& accessor,
                                    const Datum& key_or_index) {
    if (key_or_index.isInt() || key_or_index.isFloat()) {
        return context.cast_member_resolver ? context.cast_member_resolver(accessor.cast_lib, toIntLikeJava(key_or_index))
                                          : Datum::voidValue();
    }
    return context.cast_member_name_resolver ? context.cast_member_name_resolver(accessor.cast_lib, toStringLikeJava(key_or_index))
                                          : Datum::voidValue();
}

Datum castMemberRefOrVoid(int cast_lib, int member_num) {
    if (cast_lib < 1 || member_num < 0) {
        return Datum::voidValue();
    }
    return Datum::castMemberRef(id::CastLibId(cast_lib), id::MemberId(member_num));
}

int resolveCastLibArg(BuiltinContext& context, const Datum& cast_arg) {
    if (cast_arg.isVoid() || cast_arg.isNull()) {
        return 0;
    }
    if (const auto* ref = cast_arg.asCastLibRef()) {
        return ref->cast_lib;
    }
    if (cast_arg.isInt() || cast_arg.isFloat()) {
        if (!context.cast_lib_number_resolver) {
            return 0;
        }
        const int resolved = context.cast_lib_number_resolver(toIntLikeJava(cast_arg));
        return resolved >= 0 ? resolved : 0;
    }
    if (cast_arg.isString() || cast_arg.isSymbol()) {
        if (!context.cast_lib_name_resolver) {
            return 0;
        }
        const int resolved = context.cast_lib_name_resolver(toStringLikeJava(cast_arg));
        return resolved >= 0 ? resolved : 0;
    }
    return 0;
}

Datum& mutableArg(const std::vector<Datum>& args, std::size_t index) {
    return const_cast<Datum&>(args[index]);
}

std::string keyName(const Datum& datum) {
    if (const auto* symbol = datum.asSymbol()) {
        return symbol->name;
    }
    if (datum.isVoid() || datum.isNull()) {
        return "";
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
    if (const auto* timeout = datum.asTimeoutRef()) {
        return timeout->name;
    }
    if (datum.isNumber()) {
        return datum.stringValue();
    }
    try {
        return datum.stringValue();
    } catch (const LingoException&) {
        return datum.typeString();
    }
}

std::string toKeyNameLikeJava(const Datum& datum) {
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
    if (const auto* timeout = datum.asTimeoutRef()) {
        return timeout->name;
    }
    return toStringLikeJava(datum);
}

std::string ilkType(const Datum& datum) {
    switch (datum.type()) {
        case DatumType::Void: return "void";
        case DatumType::Int: return "integer";
        case DatumType::Float: return "float";
        case DatumType::String:
        case DatumType::FieldText:
        case DatumType::StringChunk: return "string";
        case DatumType::Symbol: return "symbol";
        case DatumType::List: return "list";
        case DatumType::PropList: return "propList";
        case DatumType::IntPoint: return "point";
        case DatumType::IntRect: return "rect";
        case DatumType::ColorRef: return "color";
        case DatumType::Media: return "media";
        case DatumType::ImageRef: return "image";
        case DatumType::BitmapRef: return "image";
        case DatumType::SpriteRef: return "sprite";
        case DatumType::CastMemberRef: return "member";
        case DatumType::CastLibRef: return "castLib";
        case DatumType::ScriptInstanceRef: return "instance";
        case DatumType::SoundRef: return "sound";
        case DatumType::SoundChannel: return "instance";
        case DatumType::ScriptRef: return "script";
        case DatumType::Xtra: return "xtra";
        case DatumType::XtraInstance: return "xtraInstance";
        case DatumType::StageRef: return "stage";
        default: return "object";
    }
}

// The optional millisecond argument of fadeIn/fadeOut/fadeTo, defaulting as Director does.
int fadeMilliseconds(const std::vector<Datum>& args, std::size_t index) {
    if (args.size() <= index) {
        return player::audio::SoundManager::DEFAULT_FADE_MS;
    }
    return args[index].intValue();
}

} // namespace

BuiltinRegistry::BuiltinRegistry() {
    MathBuiltins::registerBuiltins(*this);
    StringBuiltins::registerBuiltins(*this);
    OutputBuiltins::registerBuiltins(*this);
    ListBuiltins::registerBuiltins(*this);
    ConstructorBuiltins::registerBuiltins(*this);
    TypeBuiltins::registerBuiltins(*this);
    SpriteBuiltins::registerBuiltins(*this);
    TimeoutBuiltins::registerBuiltins(*this);
    NetBuiltins::registerBuiltins(*this);
    ExternalParamBuiltins::registerBuiltins(*this);
    ImageBuiltins::registerBuiltins(*this);
    SoundBuiltins::registerBuiltins(*this);
    CastLibBuiltins::registerBuiltins(*this);
    XtraBuiltins::registerBuiltins(*this);
    ControlFlowBuiltins::registerBuiltins(*this);
    MovieBuiltins::registerBuiltins(*this);
}

bool BuiltinRegistry::contains(std::string_view name) const {
    return builtins_.contains(name);
}

Datum BuiltinRegistry::invoke(std::string_view name,
                              BuiltinContext& context,
                              const std::vector<Datum>& args) const {
    if (auto result = invokeIfPresent(name, context, args)) {
        return std::move(*result);
    }
    return Datum::voidValue();
}

std::optional<Datum> BuiltinRegistry::invokeIfPresent(std::string_view name,
                                                      BuiltinContext& context,
                                                      const std::vector<Datum>& args) const {
    const auto found = builtins_.find(name);
    if (found == builtins_.end()) {
        return std::nullopt;
    }
    return found->second(context, args);
}

const BuiltinFunction* BuiltinRegistry::get(std::string_view name) const {
    const auto found = builtins_.find(name);
    return found == builtins_.end() ? nullptr : &found->second;
}

void BuiltinRegistry::registerBuiltin(std::string_view name, BuiltinFunction function) {
    builtins_[normalizeName(name)] = std::move(function);
}

const BuiltinMap& BuiltinRegistry::map() const {
    return builtins_;
}

std::string BuiltinRegistry::normalizeName(std::string_view name) {
    std::string result(name);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

void MathBuiltins::registerBuiltins(BuiltinRegistry& registry) {
    registry.registerBuiltin("abs", MathBuiltins::abs);
    registry.registerBuiltin("sqrt", MathBuiltins::sqrt);
    registry.registerBuiltin("sin", MathBuiltins::sin);
    registry.registerBuiltin("cos", MathBuiltins::cos);
    registry.registerBuiltin("random", MathBuiltins::random);
    registry.registerBuiltin("integer", MathBuiltins::integer);
    registry.registerBuiltin("float", MathBuiltins::toFloat);
    registry.registerBuiltin("bitand", MathBuiltins::bitAnd);
    registry.registerBuiltin("bitor", MathBuiltins::bitOr);
    registry.registerBuiltin("bitxor", MathBuiltins::bitXor);
    registry.registerBuiltin("bitnot", MathBuiltins::bitNot);
    registry.registerBuiltin("power", MathBuiltins::power);
    registry.registerBuiltin("min", MathBuiltins::min);
    registry.registerBuiltin("max", MathBuiltins::max);
}

Datum MathBuiltins::abs(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::of(0);
    }
    if (args[0].isFloat()) {
        return Datum::of(std::fabs(toDoubleLikeJava(args[0])));
    }
    const int value = toIntLikeJava(args[0]);
    if (value == std::numeric_limits<int>::min()) {
        return Datum::of(value);
    }
    return Datum::of(std::abs(value));
}

Datum MathBuiltins::sqrt(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::of(0);
    }
    return Datum::of(std::sqrt(toDoubleLikeJava(args[0])));
}

Datum MathBuiltins::sin(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::of(0);
    }
    return Datum::of(std::sin(toDoubleLikeJava(args[0]) * 3.14159265358979323846 / 180.0));
}

Datum MathBuiltins::cos(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::of(0);
    }
    return Datum::of(std::cos(toDoubleLikeJava(args[0]) * 3.14159265358979323846 / 180.0));
}

Datum MathBuiltins::random(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::of(1);
    }
    const int max = toIntLikeJava(args[0]);
    if (max <= 0) {
        return Datum::of(1);
    }
    if (context.random_int_handler) {
        return Datum::of(context.random_int_handler(max));
    }
    return Datum::of(1);
}

Datum MathBuiltins::integer(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::of(0);
    }
    if (args[0].isString()) {
        const std::string trimmed = trimCopy(args[0].stringValue());
        if (trimmed.empty()) {
            return Datum::of(0);
        }
        if (trimmed.front() == '*' && trimmed.size() > 1) {
            if (const auto parsed = parseLongStrict(trimmed.substr(1), 16)) {
                return Datum::of(static_cast<int>(*parsed));
            }
        }
        if (const auto parsed_int = parseIntStrict(trimmed)) {
            return Datum::of(*parsed_int);
        }
        if (const auto parsed_double = parseDoubleStrict(trimmed)) {
            return Datum::of(javaRoundToInt(*parsed_double));
        }
        return Datum::voidValue();
    }
    return Datum::of(javaRoundToInt(toDoubleLikeJava(args[0])));
}

Datum MathBuiltins::toFloat(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::of(0.0);
    }
    if (args[0].isString()) {
        if (const auto parsed = parseDoubleStrict(args[0].stringValue())) {
            return Datum::of(*parsed);
        }
        return args[0];
    }
    return Datum::of(toDoubleLikeJava(args[0]));
}

Datum MathBuiltins::bitAnd(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 2) {
        return Datum::of(0);
    }
    return Datum::of(toIntLikeJava(args[0]) & toIntLikeJava(args[1]));
}

Datum MathBuiltins::bitOr(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 2) {
        return Datum::of(0);
    }
    return Datum::of(toIntLikeJava(args[0]) | toIntLikeJava(args[1]));
}

Datum MathBuiltins::bitXor(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 2) {
        return Datum::of(0);
    }
    return Datum::of(toIntLikeJava(args[0]) ^ toIntLikeJava(args[1]));
}

Datum MathBuiltins::bitNot(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::of(0);
    }
    return Datum::of(~toIntLikeJava(args[0]));
}

Datum MathBuiltins::power(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 2) {
        return Datum::of(0);
    }
    const double result = std::pow(toDoubleLikeJava(args[0]), toDoubleLikeJava(args[1]));
    if (result == static_cast<int>(result) && !args[0].isFloat() && !args[1].isFloat()) {
        return Datum::of(static_cast<int>(result));
    }
    return Datum::of(result);
}

Datum MathBuiltins::min(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() == 1 && args[0].isList()) {
        const auto& items = args[0].listValue().items();
        if (items.empty()) {
            return Datum::of(0);
        }
        bool float_result = items.front().isFloat();
        int int_result = toIntLikeJava(items.front());
        double double_result = float_result ? toDoubleLikeJava(items.front()) : static_cast<double>(int_result);
        for (std::size_t index = 1; index < items.size(); ++index) {
            if (float_result || items[index].isFloat()) {
                if (!float_result) {
                    double_result = static_cast<double>(int_result);
                    float_result = true;
                }
                double_result = std::min(double_result, toDoubleLikeJava(items[index]));
            } else {
                int_result = std::min(int_result, toIntLikeJava(items[index]));
            }
        }
        return float_result ? Datum::of(double_result) : Datum::of(int_result);
    }
    if (args.size() < 2) {
        return args.empty() ? Datum::of(0) : args[0];
    }
    if (args[0].isFloat() || args[1].isFloat()) {
        return Datum::of(std::min(toDoubleLikeJava(args[0]), toDoubleLikeJava(args[1])));
    }
    return Datum::of(std::min(toIntLikeJava(args[0]), toIntLikeJava(args[1])));
}

Datum MathBuiltins::max(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() == 1 && args[0].isList()) {
        const auto& items = args[0].listValue().items();
        if (items.empty()) {
            return Datum::of(0);
        }
        bool float_result = items.front().isFloat();
        int int_result = toIntLikeJava(items.front());
        double double_result = float_result ? toDoubleLikeJava(items.front()) : static_cast<double>(int_result);
        for (std::size_t index = 1; index < items.size(); ++index) {
            if (float_result || items[index].isFloat()) {
                if (!float_result) {
                    double_result = static_cast<double>(int_result);
                    float_result = true;
                }
                double_result = std::max(double_result, toDoubleLikeJava(items[index]));
            } else {
                int_result = std::max(int_result, toIntLikeJava(items[index]));
            }
        }
        return float_result ? Datum::of(double_result) : Datum::of(int_result);
    }
    if (args.size() < 2) {
        return args.empty() ? Datum::of(0) : args[0];
    }
    if (args[0].isFloat() || args[1].isFloat()) {
        return Datum::of(std::max(toDoubleLikeJava(args[0]), toDoubleLikeJava(args[1])));
    }
    return Datum::of(std::max(toIntLikeJava(args[0]), toIntLikeJava(args[1])));
}

void StringBuiltins::registerBuiltins(BuiltinRegistry& registry) {
    registry.registerBuiltin("string", StringBuiltins::string);
    registry.registerBuiltin("length", StringBuiltins::length);
    registry.registerBuiltin("chars", StringBuiltins::chars);
    registry.registerBuiltin("chartonum", StringBuiltins::charToNum);
    registry.registerBuiltin("numtochar", StringBuiltins::numToChar);
    registry.registerBuiltin("offset", StringBuiltins::offset);
    registry.registerBuiltin("stringReplace", StringBuiltins::stringReplace);
    registry.registerBuiltin("getpref", StringBuiltins::getPref);
    registry.registerBuiltin("setpref", StringBuiltins::setPref);
}

Datum StringBuiltins::string(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::of(std::string());
    }
    if (args[0].isString()) {
        return args[0];
    }
    return Datum::of(toStringLikeJava(args[0]));
}

Datum StringBuiltins::length(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::of(0);
    }
    if (args[0].isList()) {
        return Datum::of(args[0].listValue().count());
    }
    if (args[0].isPropList()) {
        return Datum::of(args[0].propListValue().count());
    }
    if (const auto* value = args[0].asString()) {
        return Datum::of(static_cast<int>(value->value.size()));
    }
    if (const auto* value = args[0].asFieldText()) {
        return Datum::of(static_cast<int>(value->value.size()));
    }
    if (const auto* value = args[0].asStringChunk()) {
        return Datum::of(static_cast<int>(value->value.size()));
    }
    std::string storage;
    return Datum::of(static_cast<int>(stringViewLikeJava(args[0], storage).size()));
}

Datum StringBuiltins::chars(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 3) {
        return Datum::of(std::string());
    }
    std::string value_storage;
    const std::string_view value = stringViewLikeJava(args[0], value_storage);
    int start = toIntLikeJava(args[1]) - 1;
    int end = toIntLikeJava(args[2]);
    if (start < 0) {
        start = 0;
    }
    if (end > static_cast<int>(value.size())) {
        end = static_cast<int>(value.size());
    }
    if (start >= end) {
        return Datum::of(std::string());
    }
    return Datum::of(std::string(value.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start))));
}

Datum StringBuiltins::charToNum(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::of(0);
    }
    std::string storage;
    const std::string_view value = stringViewLikeJava(args[0], storage);
    if (value.empty()) {
        return Datum::of(0);
    }
    return Datum::of(static_cast<int>(static_cast<unsigned char>(value.front())));
}

Datum StringBuiltins::numToChar(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::of(std::string());
    }
    const int numeric_value = args[0].asInt() != nullptr
        ? args[0].asInt()->value
        : toIntLikeJava(args[0]);
    const char value = static_cast<char>(numeric_value);
    return Datum::of(std::string(1, value));
}

Datum StringBuiltins::offset(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 2) {
        return Datum::of(0);
    }
    std::string needle_storage;
    std::string haystack_storage;
    const std::string_view needle = stringViewLikeJava(args[0], needle_storage);
    const std::string_view haystack = stringViewLikeJava(args[1], haystack_storage);
    if (needle.empty()) {
        return Datum::of(0);
    }
    if (needle.size() > haystack.size()) {
        return Datum::of(0);
    }
    const std::size_t limit = haystack.size() - needle.size();
    for (std::size_t index = 0; index <= limit; ++index) {
        if (regionMatchesIgnoreCase(haystack, index, needle)) {
            return Datum::of(static_cast<int>(index + 1));
        }
    }
    return Datum::of(0);
}

Datum StringBuiltins::stringReplace(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::of(std::string());
    }
    std::string result = toStringLikeJava(args[0]);
    if (args.size() < 3) {
        return Datum::of(std::move(result));
    }

    std::string from_storage;
    const std::string_view from = stringViewLikeJava(args[1], from_storage);
    if (from.empty()) {
        return Datum::of(std::move(result));
    }
    std::string to_storage;
    const std::string_view to = stringViewLikeJava(args[2], to_storage);
    std::size_t pos = 0;
    while ((pos = result.find(from.data(), pos, from.size())) != std::string::npos) {
        result.replace(pos, from.size(), to.data(), to.size());
        pos += to.size();
    }
    return Datum::of(std::move(result));
}

Datum StringBuiltins::getPref(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    std::string key_storage;
    const std::string& key = stringRefLikeJava(args[0], key_storage);
    if (key.empty() || !context.get_pref_handler) {
        return Datum::voidValue();
    }
    return context.get_pref_handler(key);
}

Datum StringBuiltins::setPref(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.size() < 2) {
        return Datum::voidValue();
    }
    std::string key_storage;
    const std::string& key = stringRefLikeJava(args[0], key_storage);
    if (key.empty() || !context.set_pref_handler) {
        return Datum::voidValue();
    }
    return context.set_pref_handler(key, args[1]);
}

void OutputBuiltins::registerBuiltins(BuiltinRegistry& registry) {
    registry.registerBuiltin("put", OutputBuiltins::put);
    registry.registerBuiltin("alert", OutputBuiltins::alert);
}

Datum OutputBuiltins::put(BuiltinContext& context, const std::vector<Datum>& args) {
    if (!context.debug_playback_enabled && !vm::DebugConfig::isDebugPlaybackEnabled()) {
        return Datum::voidValue();
    }

    std::string value;
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index > 0) {
            value.push_back(' ');
        }
        std::string arg_storage;
        value.append(stringViewLikeJava(args[index], arg_storage));
    }

    if (context.output_handler) {
        context.output_handler("PUT", value);
    } else {
        std::cout << "[PUT] " << value << '\n';
    }
    return Datum::voidValue();
}

Datum OutputBuiltins::alert(BuiltinContext& context, const std::vector<Datum>& args) {
    std::string message_storage;
    const std::string& message = args.empty() ? message_storage : stringRefLikeJava(args[0], message_storage);
    if (context.alert_hook_handler && context.alert_hook_handler("Alert", message)) {
        return Datum::voidValue();
    }
    if (context.alert_handler && context.alert_handler(message)) {
        return Datum::voidValue();
    }
    if (context.output_handler) {
        context.output_handler("ALERT", message);
    } else {
        std::cout << "[ALERT] " << message << '\n';
    }
    return Datum::voidValue();
}

void ListBuiltins::registerBuiltins(BuiltinRegistry& registry) {
    registry.registerBuiltin("count", ListBuiltins::count);
    registry.registerBuiltin("getat", ListBuiltins::getAt);
    registry.registerBuiltin("setat", ListBuiltins::setAt);
    registry.registerBuiltin("addat", ListBuiltins::addAt);
    registry.registerBuiltin("deleteat", ListBuiltins::deleteAt);
    registry.registerBuiltin("append", ListBuiltins::append);
    registry.registerBuiltin("add", ListBuiltins::append);
    registry.registerBuiltin("getaprop", ListBuiltins::getaProp);
    registry.registerBuiltin("setaprop", ListBuiltins::setaProp);
    registry.registerBuiltin("addprop", ListBuiltins::addProp);
    registry.registerBuiltin("deleteprop", ListBuiltins::deleteProp);
    registry.registerBuiltin("getpropat", ListBuiltins::getPropAt);
    registry.registerBuiltin("findpos", ListBuiltins::findPos);
    registry.registerBuiltin("getone", ListBuiltins::getOne);
    registry.registerBuiltin("getpos", ListBuiltins::getOne);
    registry.registerBuiltin("deleteone", ListBuiltins::deleteOne);
    registry.registerBuiltin("sort", ListBuiltins::sort);
    registry.registerBuiltin("listp", ListBuiltins::listp);
    registry.registerBuiltin("list", ListBuiltins::list);
    registry.registerBuiltin("join", ListBuiltins::join);
    registry.registerBuiltin("duplicate", ListBuiltins::duplicate);
    registry.registerBuiltin("convertToPropList", ListBuiltins::convertToPropList);
    registry.registerBuiltin("getfirst", ListBuiltins::getFirst);
    registry.registerBuiltin("getlast", ListBuiltins::getLast);
}

Datum ListBuiltins::count(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::of(0);
    }
    if (args[0].isList()) {
        return Datum::of(args[0].listValue().count());
    }
    if (args[0].isPropList()) {
        return Datum::of(args[0].propListValue().count());
    }
    return Datum::of(0);
}

Datum ListBuiltins::getAt(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.size() < 2) {
        return Datum::voidValue();
    }
    const Datum& container = args[0];
    const Datum& key_or_index = args[1];
    if (const auto* accessor = container.asCastLibMemberAccessor()) {
        return getCastLibMemberAccessorValue(context, *accessor, key_or_index);
    }
    if (container.isList()) {
        if (key_or_index.isString() || key_or_index.isSymbol()) {
            if (const auto* prop_list = singlePropListWrapper(container.listValue())) {
                return propListBuiltinGetAtValue(*prop_list, key_or_index);
            }
        }
        const int index = toIntLikeJava(key_or_index);
        const auto& items = container.listValue().items();
        if (index >= 1 && index <= static_cast<int>(items.size())) {
            return items[static_cast<std::size_t>(index - 1)];
        }
        return Datum::voidValue();
    }
    if (container.isPropList()) {
        return propListBuiltinGetAtValue(container.propListValue(), key_or_index);
    }
    if (const auto* point = container.asIntPoint()) {
        switch (toIntLikeJava(key_or_index)) {
            case 1: return Datum::of(point->x);
            case 2: return Datum::of(point->y);
            default: return Datum::voidValue();
        }
    }
    if (const auto* rect = container.asIntRect()) {
        switch (toIntLikeJava(key_or_index)) {
            case 1: return Datum::of(rect->left);
            case 2: return Datum::of(rect->top);
            case 3: return Datum::of(rect->right);
            case 4: return Datum::of(rect->bottom);
            default: return Datum::voidValue();
        }
    }
    return Datum::voidValue();
}

Datum ListBuiltins::setAt(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 3) {
        return Datum::voidValue();
    }
    const Datum& key_or_index = args[1];
    const Datum& value = args[2];
    if (args[0].isList()) {
        if (key_or_index.isString() || key_or_index.isSymbol()) {
            if (auto* prop_list = singlePropListWrapper(mutableArg(args, 0).listValue())) {
                putPropTyped(*prop_list, key_or_index, value);
                return Datum::voidValue();
            }
        }
        const int index = toIntLikeJava(key_or_index);
        auto& items = mutableArg(args, 0).listValue().items();
        if (index >= 1 && index <= static_cast<int>(items.size())) {
            items[static_cast<std::size_t>(index - 1)] = value;
        }
        return Datum::voidValue();
    }
    if (args[0].isPropList()) {
        auto& prop_list = mutableArg(args, 0).propListValue();
        if (key_or_index.asSymbol() != nullptr || key_or_index.isString()) {
            putPropSameType(prop_list, key_or_index, value);
        } else {
            const int position = toIntLikeJava(key_or_index);
            if (position >= 1 && position <= prop_list.count()) {
                prop_list.properties()[static_cast<std::size_t>(position - 1)].second = value;
            }
        }
        return Datum::voidValue();
    }
    if (auto* point = mutableArg(args, 0).asIntPoint()) {
        const int component = toIntLikeJava(key_or_index);
        const int new_value = toIntLikeJava(value);
        if (component == 1) {
            point->x = new_value;
        } else if (component == 2) {
            point->y = new_value;
        }
        return Datum::voidValue();
    }
    if (auto* rect = mutableArg(args, 0).asIntRect()) {
        const int component = toIntLikeJava(key_or_index);
        const int new_value = toIntLikeJava(value);
        switch (component) {
            case 1: rect->left = new_value; break;
            case 2: rect->top = new_value; break;
            case 3: rect->right = new_value; break;
            case 4: rect->bottom = new_value; break;
            default: break;
        }
        return Datum::voidValue();
    }
    return Datum::voidValue();
}

Datum ListBuiltins::addAt(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 3 || !args[0].isList()) {
        return Datum::voidValue();
    }
    auto& items = mutableArg(args, 0).listValue().items();
    int position = toIntLikeJava(args[1]) - 1;
    if (position < 0) {
        position = 0;
    }
    if (position > static_cast<int>(items.size())) {
        position = static_cast<int>(items.size());
    }
    items.insert(items.begin() + position, args[2]);
    return Datum::voidValue();
}

Datum ListBuiltins::deleteAt(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 2) {
        return Datum::voidValue();
    }
    const int position = toIntLikeJava(args[1]);
    if (args[0].isList()) {
        auto& items = mutableArg(args, 0).listValue().items();
        if (position >= 1 && position <= static_cast<int>(items.size())) {
            items.erase(items.begin() + (position - 1));
        }
        return Datum::voidValue();
    }
    if (args[0].isPropList()) {
        (void)mutableArg(args, 0).propListValue().erasePropertyAt(position - 1);
    }
    return Datum::voidValue();
}

Datum ListBuiltins::append(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 2 || !args[0].isList()) {
        return Datum::voidValue();
    }
    mutableArg(args, 0).listValue().items().push_back(args[1]);
    return Datum::voidValue();
}

Datum ListBuiltins::getaProp(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 2 || !args[0].isPropList()) {
        return Datum::voidValue();
    }
    const int index = findPropIndexTyped(args[0].propListValue(), args[1]);
    return index >= 0 ? args[0].propListValue().properties()[static_cast<std::size_t>(index)].second
                      : Datum::voidValue();
}

Datum ListBuiltins::setaProp(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 3 || !args[0].isPropList()) {
        return Datum::voidValue();
    }
    putPropTyped(mutableArg(args, 0).propListValue(), args[1], args[2]);
    return Datum::voidValue();
}

Datum ListBuiltins::addProp(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 3 || !args[0].isPropList()) {
        return Datum::voidValue();
    }
    mutableArg(args, 0).propListValue().appendProperty(args[1], args[2]);
    return Datum::voidValue();
}

Datum ListBuiltins::deleteProp(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 2 || !args[0].isPropList()) {
        return Datum::voidValue();
    }
    const int index = findPropIndexTyped(args[0].propListValue(), args[1]);
    if (index >= 0) {
        (void)mutableArg(args, 0).propListValue().erasePropertyAt(index);
    }
    return Datum::voidValue();
}

Datum ListBuiltins::getPropAt(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 2 || !args[0].isPropList()) {
        return Datum::voidValue();
    }
    const int position = toIntLikeJava(args[1]);
    const auto& properties = args[0].propListValue().properties();
    if (position >= 1 && position <= static_cast<int>(properties.size())) {
        return properties[static_cast<std::size_t>(position - 1)].first;
    }
    return Datum::voidValue();
}

Datum ListBuiltins::findPos(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 2) {
        return Datum::voidValue();
    }
    if (args[0].isList()) {
        if (const auto* prop_list = singlePropListWrapper(args[0].listValue())) {
            const int index = findPropIndexUntyped(*prop_list, args[1]);
            return index >= 0 ? Datum::of(index + 1) : Datum::voidValue();
        }
        const auto& items = args[0].listValue().items();
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (lingoEquals(items[index], args[1])) {
                return Datum::of(static_cast<int>(index + 1));
            }
        }
        return Datum::of(0);
    }
    if (!args[0].isPropList()) {
        return Datum::voidValue();
    }
    const int index = findPropIndexUntyped(args[0].propListValue(), args[1]);
    return index >= 0 ? Datum::of(index + 1) : Datum::voidValue();
}

Datum ListBuiltins::getOne(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 2) {
        return Datum::of(0);
    }
    if (args[0].isList()) {
        const auto& items = args[0].listValue().items();
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (lingoEquals(items[index], args[1])) {
                return Datum::of(static_cast<int>(index + 1));
            }
        }
    }
    return Datum::of(0);
}

Datum ListBuiltins::deleteOne(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 2 || !args[0].isList()) {
        return Datum::voidValue();
    }
    auto& items = mutableArg(args, 0).listValue().items();
    for (auto iterator = items.begin(); iterator != items.end(); ++iterator) {
        if (lingoEquals(*iterator, args[1])) {
            items.erase(iterator);
            break;
        }
    }
    return Datum::voidValue();
}

Datum ListBuiltins::sort(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty() || !args[0].isList()) {
        return Datum::voidValue();
    }
    auto& items = mutableArg(args, 0).listValue().items();
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

Datum ListBuiltins::listp(BuiltinContext&, const std::vector<Datum>& args) {
    return boolDatum(!args.empty() && (args[0].isList() || args[0].isPropList()));
}

Datum ListBuiltins::list(BuiltinContext&, const std::vector<Datum>& args) {
    return Datum::list(args);
}

Datum ListBuiltins::join(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty() || !args[0].isList()) {
        return Datum::of(std::string());
    }
    std::string separator_storage;
    const std::string_view separator = args.size() > 1 ? stringViewLikeJava(args[1], separator_storage) : "&";
    const auto& items = args[0].listValue().items();
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

Datum ListBuiltins::duplicate(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty() || (!args[0].isList() && !args[0].isPropList())) {
        return Datum::voidValue();
    }
    return args[0].deepCopy();
}

Datum ListBuiltins::convertToPropList(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::propList();
    }
    std::string text_storage;
    const std::string_view text = stringViewLikeJava(args[0], text_storage);
    std::string delimiter_storage;
    const std::string_view delimiter = args.size() > 1 ? delimiterFromArg(args[1], delimiter_storage) : std::string_view();
    return parseDirectorPropertyText(text, delimiter);
}

Datum ListBuiltins::getFirst(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    if (args[0].isList()) {
        const auto& items = args[0].listValue().items();
        return items.empty() ? Datum::voidValue() : items.front();
    }
    if (args[0].isPropList()) {
        const auto& properties = args[0].propListValue().properties();
        return properties.empty() ? Datum::voidValue() : properties.front().second;
    }
    return Datum::voidValue();
}

Datum ListBuiltins::getLast(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    if (args[0].isList()) {
        const auto& items = args[0].listValue().items();
        return items.empty() ? Datum::voidValue() : items.back();
    }
    if (args[0].isPropList()) {
        const auto& properties = args[0].propListValue().properties();
        return properties.empty() ? Datum::voidValue() : properties.back().second;
    }
    return Datum::voidValue();
}

void TimeoutBuiltins::registerBuiltins(BuiltinRegistry& registry) {
    registry.registerBuiltin("timeout", TimeoutBuiltins::timeout);
}

Datum TimeoutBuiltins::timeout(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::timeoutRef("");
    }
    return Datum::timeoutRef(toStringLikeJava(args[0]));
}

Datum TimeoutBuiltins::handleMethod(BuiltinContext& context,
                                    const Datum::TimeoutRef& ref,
                                    std::string_view method_name,
                                    const std::vector<Datum>& args) {
    auto* manager = context.timeout_manager;

    if (equalsIgnoreCase(method_name, "new")) {
        if (manager == nullptr) {
            return Datum::voidValue();
        }

        std::string name = ref.name;
        std::size_t arg_offset = 0;
        if (name.empty() && !args.empty()) {
            name = toStringLikeJava(args[0]);
            arg_offset = 1;
        }
        if (name.empty()) {
            return Datum::voidValue();
        }

        const int period_ms = args.size() > arg_offset ? toIntLikeJava(args[arg_offset]) : 1000;
        const std::string handler = args.size() > arg_offset + 1 ? toKeyNameLikeJava(args[arg_offset + 1]) : "";
        const Datum target = args.size() > arg_offset + 2 ? args[arg_offset + 2] : Datum::voidValue();
        return manager->createTimeout(name, period_ms, handler, target);
    }

    if (equalsIgnoreCase(method_name, "forget")) {
        if (manager != nullptr && !ref.name.empty()) {
            manager->forgetTimeout(ref.name);
        }
        return Datum::voidValue();
    }

    if (manager != nullptr && !ref.name.empty()) {
        return manager->getTimeoutProp(ref.name, std::string(method_name));
    }
    return Datum::voidValue();
}

Datum TimeoutBuiltins::getProperty(BuiltinContext& context,
                                   const Datum::TimeoutRef& ref,
                                   std::string_view prop_name) {
    if (context.timeout_manager == nullptr || ref.name.empty()) {
        return Datum::voidValue();
    }
    return context.timeout_manager->getTimeoutProp(ref.name, std::string(prop_name));
}

bool TimeoutBuiltins::setProperty(BuiltinContext& context,
                                  const Datum::TimeoutRef& ref,
                                  std::string_view prop_name,
                                  Datum value) {
    if (context.timeout_manager == nullptr || ref.name.empty()) {
        return false;
    }
    return context.timeout_manager->setTimeoutProp(ref.name, std::string(prop_name), std::move(value));
}

void NetBuiltins::registerBuiltins(BuiltinRegistry& registry) {
    registry.registerBuiltin("preloadnetthing", NetBuiltins::preloadNetThing);
    registry.registerBuiltin("getnetthing", NetBuiltins::preloadNetThing);
    registry.registerBuiltin("getnettext", NetBuiltins::preloadNetThing);
    registry.registerBuiltin("postnetthing", NetBuiltins::postNetText);
    registry.registerBuiltin("postnettext", NetBuiltins::postNetText);
    registry.registerBuiltin("netdone", NetBuiltins::netDone);
    registry.registerBuiltin("nettextresult", NetBuiltins::netTextResult);
    registry.registerBuiltin("neterror", NetBuiltins::netError);
    registry.registerBuiltin("getstreamstatus", NetBuiltins::getStreamStatus);
    registry.registerBuiltin("tellstreamstatus", NetBuiltins::tellStreamStatus);
    registry.registerBuiltin("gotonetpage", NetBuiltins::gotoNetPage);
    registry.registerBuiltin("gotonetmovie", NetBuiltins::gotoNetMovie);
}

Datum NetBuiltins::preloadNetThing(BuiltinContext& context, const std::vector<Datum>& args) {
    if (context.net_manager == nullptr || args.empty()) {
        return Datum::of(-1);
    }
    return Datum::of(context.net_manager->preloadNetThing(toStringLikeJava(args[0])));
}

Datum NetBuiltins::postNetText(BuiltinContext& context, const std::vector<Datum>& args) {
    if (context.net_manager == nullptr || args.empty()) {
        return Datum::of(-1);
    }
    std::string post_data = args.size() > 1 ? toStringLikeJava(args[1]) : "";
    return Datum::of(context.net_manager->postNetText(toStringLikeJava(args[0]), std::move(post_data)));
}

Datum NetBuiltins::netDone(BuiltinContext& context, const std::vector<Datum>& args) {
    if (context.net_manager == nullptr) {
        return Datum::TRUE;
    }
    if (args.empty() && context.net_manager->getStreamStatus() == "Error" && context.net_manager->netError() == 0) {
        return Datum::TRUE;
    }
    const std::optional<int> task_id = args.empty() ? std::nullopt
                                                   : std::optional<int>{toIntLikeJava(args[0])};
    return boolDatum(context.net_manager->netDone(task_id));
}

Datum NetBuiltins::netTextResult(BuiltinContext& context, const std::vector<Datum>& args) {
    if (context.net_manager == nullptr) {
        return Datum::of(std::string());
    }
    const std::optional<int> task_id = args.empty() ? std::nullopt
                                                   : std::optional<int>{toIntLikeJava(args[0])};
    return Datum::of(context.net_manager->netTextResult(task_id));
}

Datum NetBuiltins::netError(BuiltinContext& context, const std::vector<Datum>& args) {
    if (context.net_manager == nullptr) {
        return Datum::of(std::string("OK"));
    }
    const std::optional<int> task_id = args.empty() ? std::nullopt
                                                   : std::optional<int>{toIntLikeJava(args[0])};
    const int error = context.net_manager->netError(task_id);
    return error == 0 ? Datum::of(std::string("OK")) : Datum::of(std::to_string(error));
}

Datum NetBuiltins::getStreamStatus(BuiltinContext& context, const std::vector<Datum>& args) {
    if (context.net_manager == nullptr) {
        return defaultStreamStatusDatum();
    }
    if (!args.empty() && (args[0].isString() || args[0].isSymbol())) {
        std::string value_storage;
        const std::string& value = stringRefLikeJava(args[0], value_storage);
        if (const auto task_id = parseIntStrict(trimCopy(value))) {
            return context.net_manager->getStreamStatusDatum(*task_id);
        }
        return context.net_manager->getStreamStatusDatum(value);
    }
    const std::optional<int> task_id = args.empty() ? std::nullopt
                                                   : std::optional<int>{toIntLikeJava(args[0])};
    return context.net_manager->getStreamStatusDatum(task_id);
}

Datum NetBuiltins::tellStreamStatus(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return boolDatum(context.tell_stream_status_enabled);
    }
    context.tell_stream_status_enabled = truthyLikeJava(args[0]);
    return boolDatum(context.tell_stream_status_enabled);
}

Datum NetBuiltins::gotoNetPage(BuiltinContext& context, const std::vector<Datum>& args) {
    if (context.movie_properties == nullptr || args.empty()) {
        return Datum::FALSE;
    }
    std::string url_storage;
    const std::string& url = stringRefLikeJava(args[0], url_storage);
    std::string target_storage;
    const std::string& target = args.size() > 1 ? stringRefLikeJava(args[1], target_storage) : target_storage;
    context.movie_properties->gotoNetPage(url, target);
    return Datum::TRUE;
}

Datum NetBuiltins::gotoNetMovie(BuiltinContext& context, const std::vector<Datum>& args) {
    if (context.movie_properties == nullptr || args.empty()) {
        return Datum::of(-1);
    }
    std::string url_storage;
    const std::string& url = stringRefLikeJava(args[0], url_storage);
    return Datum::of(context.movie_properties->gotoNetMovie(url));
}

void ExternalParamBuiltins::registerBuiltins(BuiltinRegistry& registry) {
    registry.registerBuiltin("externalparamvalue", ExternalParamBuiltins::externalParamValue);
    registry.registerBuiltin("externalparamname", ExternalParamBuiltins::externalParamName);
    registry.registerBuiltin("externalparamcount", ExternalParamBuiltins::externalParamCount);
}

Datum ExternalParamBuiltins::externalParamValue(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    if (const auto* value = args[0].asInt()) {
        const int index = value->value;
        if (index >= 1 && index <= static_cast<int>(context.external_params.size())) {
            return Datum::of(context.external_params[static_cast<std::size_t>(index - 1)].second);
        }
        return Datum::voidValue();
    }
    if (const auto key = directStringViewLikeJava(args[0]); key && !key->empty()) {
        for (const auto& [name, value] : context.external_params) {
            if (equalsIgnoreCase(name, *key)) {
                return Datum::of(value);
            }
        }
    }
    return Datum::voidValue();
}

Datum ExternalParamBuiltins::externalParamName(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    if (const auto* value = args[0].asInt()) {
        const int index = value->value;
        if (index >= 1 && index <= static_cast<int>(context.external_params.size())) {
            return Datum::of(context.external_params[static_cast<std::size_t>(index - 1)].first);
        }
        return Datum::voidValue();
    }
    if (const auto key = directStringViewLikeJava(args[0]); key && !key->empty()) {
        for (const auto& [name, value] : context.external_params) {
            (void)value;
            if (equalsIgnoreCase(name, *key)) {
                return Datum::of(name);
            }
        }
    }
    return Datum::voidValue();
}

Datum ExternalParamBuiltins::externalParamCount(BuiltinContext& context, const std::vector<Datum>&) {
    return Datum::of(static_cast<int>(context.external_params.size()));
}

void ImageBuiltins::registerBuiltins(BuiltinRegistry& registry) {
    registry.registerBuiltin("image", ImageBuiltins::image);
    registry.registerBuiltin("importfileinto", ImageBuiltins::importFileInto);
}

Datum ImageBuiltins::image(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.size() < 2) {
        return Datum::voidValue();
    }

    const int width = toIntLikeJava(args[0]);
    const int height = toIntLikeJava(args[1]);
    const int bit_depth = args.size() >= 3 ? toIntLikeJava(args[2]) : 32;
    if (width <= 0 || height <= 0) {
        return Datum::voidValue();
    }

    auto image = std::make_shared<bitmap::Bitmap>(width, height, bit_depth);
    if (args.size() >= 4) {
        const Datum& palette_arg = args[3];
        bool resolved = false;
        if (palette_arg.isString() || palette_arg.isSymbol()) {
            std::string name_storage;
            const std::string_view name = stringViewLikeJava(palette_arg, name_storage);
            if (const auto* palette = bitmap::Palette::builtInBySymbolName(name)) {
                image->setImagePalette(palette);
                if (auto normalized = bitmap::Palette::normalizeBuiltInSymbolName(name)) {
                    image->setPaletteRefSystemName(*normalized);
                }
                resolved = true;
            }
        }

        if (!resolved && context.image_palette_resolver) {
            if (auto palette = context.image_palette_resolver(palette_arg)) {
                if (palette->palette) {
                    image->setImagePalette(palette->palette);
                }
                if (palette->member_ref) {
                    image->setPaletteRefCastMember(palette->member_ref->cast_lib, palette->member_ref->memberNum());
                }
                if (palette->system_name) {
                    image->setPaletteRefSystemName(*palette->system_name);
                }
            }
        }
    }

    image->fill(0xFFFFFFFFU);
    image->setRectangularMedia(true);
    return Datum::imageRef(std::move(image));
}

Datum ImageBuiltins::importFileInto(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.size() < 2 || !context.import_file_into_handler) {
        return Datum::FALSE;
    }
    const auto* ref = args[0].asCastMemberRef();
    if (ref == nullptr) {
        return Datum::FALSE;
    }

    std::string url_storage;
    const std::string& url = stringRefLikeJava(args[1], url_storage);
    const Datum options = args.size() >= 3 ? args[2] : Datum::voidValue();
    return boolDatum(context.import_file_into_handler(*ref, url, options));
}

void SoundBuiltins::registerBuiltins(BuiltinRegistry& registry) {
    registry.registerBuiltin("beep", SoundBuiltins::beep);
    registry.registerBuiltin("sound", SoundBuiltins::sound);
    registry.registerBuiltin("soundenabled", SoundBuiltins::soundEnabled);
}

Datum SoundBuiltins::beep(BuiltinContext&, const std::vector<Datum>&) {
    return Datum::voidValue();
}

Datum SoundBuiltins::sound(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    const int channel = toIntLikeJava(args[0]);
    if (!player::audio::SoundManager::isValidChannel(channel)) {
        return Datum::voidValue();
    }
    return Datum::soundChannel(channel);
}

Datum SoundBuiltins::soundEnabled(BuiltinContext& context, const std::vector<Datum>&) {
    if (context.movie_properties != nullptr) {
        return boolDatum(context.movie_properties->getMovieProp("soundEnabled").boolValue());
    }
    if (context.sound_manager != nullptr) {
        return boolDatum(context.sound_manager->isEnabled());
    }
    return Datum::TRUE;
}

Datum SoundBuiltins::handleMethod(BuiltinContext& context,
                                  const Datum::SoundChannel& channel,
                                  std::string_view method_name,
                                  const std::vector<Datum>& args) {
    auto* manager = context.sound_manager;
    const int channel_num = channel.channel;

    if (equalsIgnoreCase(method_name, "play")) {
        if (manager != nullptr && args.empty()) {
            manager->play(channel_num);
        } else if (manager != nullptr) {
            manager->play(channel_num, args[0]);
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "queue")) {
        if (manager != nullptr && !args.empty()) {
            manager->queue(channel_num, args[0]);
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "stop")) {
        if (manager != nullptr) {
            manager->stop(channel_num);
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "fadein")) {
        if (manager != nullptr) {
            manager->fadeIn(channel_num, fadeMilliseconds(args, 0));
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "fadeout")) {
        if (manager != nullptr) {
            manager->fadeOut(channel_num, fadeMilliseconds(args, 0));
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "fadeto")) {
        if (manager != nullptr && !args.empty()) {
            manager->fadeTo(channel_num, args[0].intValue(), fadeMilliseconds(args, 1));
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "setplaylist")) {
        if (manager != nullptr) {
            manager->setPlaylist(channel_num, args.empty() ? Datum::voidValue() : args[0]);
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "pause") || equalsIgnoreCase(method_name, "resume") ||
        equalsIgnoreCase(method_name, "unpause") || equalsIgnoreCase(method_name, "playfile") ||
        equalsIgnoreCase(method_name, "breakloop") || equalsIgnoreCase(method_name, "rewind")) {
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "playnext")) {
        if (manager != nullptr) {
            manager->playNext(channel_num);
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "isbusy")) {
        return boolDatum(manager != nullptr && manager->isBusy(channel_num));
    }
    if (equalsIgnoreCase(method_name, "status")) {
        return Datum::of(manager != nullptr && manager->isPlaying(channel_num) ? 1 : 0);
    }
    if (equalsIgnoreCase(method_name, "elapsedtime") || equalsIgnoreCase(method_name, "currenttime")) {
        return Datum::of(manager != nullptr ? manager->getElapsedTime(channel_num) : 0);
    }
    if (equalsIgnoreCase(method_name, "getplaylist")) {
        return Datum::list(manager != nullptr ? manager->getPlaylist(channel_num) : std::vector<Datum>{});
    }
    if (equalsIgnoreCase(method_name, "volume")) {
        if (!args.empty()) {
            if (manager != nullptr) {
                manager->setVolume(channel_num, toIntLikeJava(args[0]));
            }
            return Datum::voidValue();
        }
        return Datum::of(manager != nullptr ? manager->getVolume(channel_num) : 255);
    }
    if (equalsIgnoreCase(method_name, "pan")) {
        if (!args.empty()) {
            if (manager != nullptr) {
                manager->setPan(channel_num, toIntLikeJava(args[0]));
            }
            return Datum::voidValue();
        }
        return Datum::of(manager != nullptr ? manager->getPan(channel_num) : 0);
    }
    if (equalsIgnoreCase(method_name, "member")) {
        if (!args.empty()) {
            if (manager != nullptr) {
                if (const auto* member = args[0].asCastMemberRef()) {
                    manager->setMember(channel_num, *member);
                }
            }
            return Datum::voidValue();
        }
        if (manager != nullptr) {
            if (auto member = manager->getMember(channel_num)) {
                return Datum::castMemberRef(*member);
            }
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "ilk")) {
        return Datum::symbol("instance");
    }
    return Datum::voidValue();
}

Datum SoundBuiltins::getProperty(BuiltinContext& context,
                                 const Datum::SoundChannel& channel,
                                 std::string_view prop_name) {
    auto* manager = context.sound_manager;
    const int channel_num = channel.channel;

    if (equalsIgnoreCase(prop_name, "volume")) {
        return Datum::of(manager != nullptr ? manager->getVolume(channel_num) : 255);
    }
    if (equalsIgnoreCase(prop_name, "pan")) {
        return Datum::of(manager != nullptr ? manager->getPan(channel_num) : 0);
    }
    if (equalsIgnoreCase(prop_name, "status")) {
        return Datum::of(manager != nullptr && manager->isPlaying(channel_num) ? 1 : 0);
    }
    if (equalsIgnoreCase(prop_name, "member")) {
        if (manager != nullptr) {
            if (auto member = manager->getMember(channel_num)) {
                return Datum::castMemberRef(*member);
            }
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(prop_name, "loopcount")) {
        return Datum::of(manager != nullptr ? manager->getLoopCount(channel_num) : 1);
    }
    if (equalsIgnoreCase(prop_name, "starttime")) {
        return Datum::of(manager != nullptr ? manager->getStartTime(channel_num) : 0);
    }
    if (equalsIgnoreCase(prop_name, "endtime")) {
        return Datum::of(manager != nullptr ? manager->getEndTime(channel_num) : 0);
    }
    if (equalsIgnoreCase(prop_name, "loopstarttime")) {
        return Datum::of(manager != nullptr ? manager->getLoopStartTime(channel_num) : 0);
    }
    if (equalsIgnoreCase(prop_name, "loopendtime")) {
        return Datum::of(manager != nullptr ? manager->getLoopEndTime(channel_num) : 0);
    }
    if (equalsIgnoreCase(prop_name, "elapsedtime") || equalsIgnoreCase(prop_name, "currenttime")) {
        return Datum::of(manager != nullptr ? manager->getElapsedTime(channel_num) : 0);
    }
    return Datum::voidValue();
}

bool SoundBuiltins::setProperty(BuiltinContext& context,
                                const Datum::SoundChannel& channel,
                                std::string_view prop_name,
                                Datum value) {
    if (equalsIgnoreCase(prop_name, "volume")) {
        if (context.sound_manager != nullptr) {
            context.sound_manager->setVolume(channel.channel, toIntLikeJava(value));
        }
        return true;
    }
    if (equalsIgnoreCase(prop_name, "loopcount")) {
        if (context.sound_manager != nullptr) {
            context.sound_manager->setLoopCount(channel.channel, toIntLikeJava(value));
        }
        return true;
    }
    if (equalsIgnoreCase(prop_name, "pan")) {
        if (context.sound_manager != nullptr) {
            context.sound_manager->setPan(channel.channel, toIntLikeJava(value));
        }
        return true;
    }
    if (equalsIgnoreCase(prop_name, "member")) {
        const auto* member = value.asCastMemberRef();
        if (member == nullptr) {
            return false;
        }
        if (context.sound_manager != nullptr) {
            context.sound_manager->setMember(channel.channel, *member);
        }
        return true;
    }
    if (equalsIgnoreCase(prop_name, "starttime")) {
        if (context.sound_manager != nullptr) {
            context.sound_manager->setStartTime(channel.channel, toIntLikeJava(value));
        }
        return true;
    }
    if (equalsIgnoreCase(prop_name, "endtime")) {
        if (context.sound_manager != nullptr) {
            context.sound_manager->setEndTime(channel.channel, toIntLikeJava(value));
        }
        return true;
    }
    if (equalsIgnoreCase(prop_name, "loopstarttime")) {
        if (context.sound_manager != nullptr) {
            context.sound_manager->setLoopStartTime(channel.channel, toIntLikeJava(value));
        }
        return true;
    }
    if (equalsIgnoreCase(prop_name, "loopendtime")) {
        if (context.sound_manager != nullptr) {
            context.sound_manager->setLoopEndTime(channel.channel, toIntLikeJava(value));
        }
        return true;
    }
    return false;
}

void CastLibBuiltins::registerBuiltins(BuiltinRegistry& registry) {
    registry.registerBuiltin("castlib", CastLibBuiltins::castLib);
    registry.registerBuiltin("member", CastLibBuiltins::member);
    registry.registerBuiltin("field", CastLibBuiltins::field);
    registry.registerBuiltin("createmember", CastLibBuiltins::createMember);
}

Datum CastLibBuiltins::castLib(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }

    int cast_lib_number = -1;
    if (args[0].isInt() || args[0].isFloat()) {
        if (!context.cast_lib_number_resolver) {
            return Datum::voidValue();
        }
        cast_lib_number = context.cast_lib_number_resolver(toIntLikeJava(args[0]));
    } else if (args[0].isString() || args[0].isSymbol()) {
        if (!context.cast_lib_name_resolver) {
            return Datum::voidValue();
        }
        cast_lib_number = context.cast_lib_name_resolver(toStringLikeJava(args[0]));
    } else {
        return Datum::voidValue();
    }

    return castLibRefOrVoid(cast_lib_number);
}

Datum CastLibBuiltins::member(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }

    const Datum& member_arg = args[0];
    if (!hasCastProvider(context)) {
        return castMemberRefOrVoid(1, toIntLikeJava(member_arg));
    }

    const int cast_lib_number = args.size() > 1 ? resolveCastLibArg(context, args[1]) : 0;

    if (member_arg.isInt() || member_arg.isFloat()) {
        const int member_number = toIntLikeJava(member_arg);
        if (member_number == 0) {
            return Datum::voidValue();
        }

        const int normalized_member_number = std::abs(member_number);
        if (cast_lib_number > 0) {
            return context.cast_member_resolver ? context.cast_member_resolver(cast_lib_number, normalized_member_number)
                                              : castMemberRefOrVoid(cast_lib_number, normalized_member_number);
        }

        const int encoded_cast = (normalized_member_number >> 16) & 0xFFFF;
        const int encoded_member = normalized_member_number & 0xFFFF;
        if (encoded_cast > 0 && encoded_member > 0) {
            return context.cast_member_resolver ? context.cast_member_resolver(encoded_cast, encoded_member)
                                              : castMemberRefOrVoid(encoded_cast, encoded_member);
        }

        const int total_casts = context.cast_lib_count_supplier ? context.cast_lib_count_supplier() : 0;
        if (context.registry_visible_member_resolver) {
            for (int cast_index = 1; cast_index <= total_casts; ++cast_index) {
                if (context.registry_visible_member_resolver(cast_index, normalized_member_number)) {
                    return context.cast_member_resolver ? context.cast_member_resolver(cast_index, normalized_member_number)
                                                      : castMemberRefOrVoid(cast_index, normalized_member_number);
                }
            }
        }

        if (context.cast_member_exists_resolver) {
            for (int cast_index = 1; cast_index <= total_casts; ++cast_index) {
                if (context.cast_member_exists_resolver(cast_index, normalized_member_number)) {
                    return context.cast_member_resolver ? context.cast_member_resolver(cast_index, normalized_member_number)
                                                      : castMemberRefOrVoid(cast_index, normalized_member_number);
                }
            }
        }

        return context.cast_member_resolver ? context.cast_member_resolver(1, normalized_member_number)
                                          : castMemberRefOrVoid(1, normalized_member_number);
    }

    if (member_arg.isString() || member_arg.isSymbol()) {
        if (!context.cast_member_name_resolver) {
            return Datum::voidValue();
        }
        Datum found = context.cast_member_name_resolver(cast_lib_number, toStringLikeJava(member_arg));
        return found.isVoid() ? Datum::voidValue() : found;
    }

    return Datum::voidValue();
}

Datum CastLibBuiltins::field(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::of(std::string());
    }
    if (!context.field_resolver) {
        return Datum::of(std::string());
    }

    const int cast_lib_number = args.size() > 1 ? resolveCastLibArg(context, args[1]) : 0;
    const Datum& field_arg = args[0];
    if (field_arg.isString() || field_arg.isInt()) {
        return context.field_resolver(field_arg, cast_lib_number);
    }
    std::string field_name = toStringLikeJava(field_arg);
    return context.field_resolver(Datum::of(std::move(field_name)), cast_lib_number);
}

Datum CastLibBuiltins::createMember(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.size() < 2 || !context.named_cast_member_creator) {
        return Datum::voidValue();
    }
    std::string member_name_storage;
    const std::string& member_name = stringRefLikeJava(args[0], member_name_storage);
    std::string member_type_storage;
    const std::string& member_type = stringRefLikeJava(args[1], member_type_storage);
    context.script_resolution_cache.clear();
    return context.named_cast_member_creator(member_name, member_type);
}

void XtraBuiltins::registerBuiltins(BuiltinRegistry& registry) {
    registry.registerBuiltin("xtra", XtraBuiltins::xtra);
    registry.registerBuiltin("SetNetBufferLimits", [](BuiltinContext& context, const std::vector<Datum>& args) {
        return XtraBuiltins::callInstanceGlobalHandler(context, "SetNetBufferLimits", args);
    });
    registry.registerBuiltin("SetNetMessageHandler", [](BuiltinContext& context, const std::vector<Datum>& args) {
        return XtraBuiltins::callInstanceGlobalHandler(context, "SetNetMessageHandler", args);
    });
    registry.registerBuiltin("ConnectToNetServer", [](BuiltinContext& context, const std::vector<Datum>& args) {
        return XtraBuiltins::callInstanceGlobalHandler(context, "ConnectToNetServer", args);
    });
    registry.registerBuiltin("SendNetMessage", [](BuiltinContext& context, const std::vector<Datum>& args) {
        return XtraBuiltins::callInstanceGlobalHandler(context, "SendNetMessage", args);
    });
    registry.registerBuiltin("GetNetMessage", [](BuiltinContext& context, const std::vector<Datum>& args) {
        return XtraBuiltins::callInstanceGlobalHandler(context, "GetNetMessage", args);
    });
    registry.registerBuiltin("CheckNetMessages", [](BuiltinContext& context, const std::vector<Datum>& args) {
        return XtraBuiltins::callInstanceGlobalHandler(context, "CheckNetMessages", args);
    });
    registry.registerBuiltin("GetNumberWaitingNetMessages", [](BuiltinContext& context, const std::vector<Datum>& args) {
        return XtraBuiltins::callInstanceGlobalHandler(context, "GetNumberWaitingNetMessages", args);
    });
    registry.registerBuiltin("GetNetErrorString", [](BuiltinContext& context, const std::vector<Datum>& args) {
        return XtraBuiltins::callInstanceGlobalHandler(context, "GetNetErrorString", args);
    });
}

Datum XtraBuiltins::xtra(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty() || !context.xtra_registered_resolver) {
        return Datum::voidValue();
    }
    std::string xtra_name = toStringLikeJava(args[0]);
    if (!context.xtra_registered_resolver(xtra_name)) {
        debugPlaybackMessage(context, "Unsupported Xtra: " + xtra_name);
        return Datum::voidValue();
    }
    return Datum::xtra(std::move(xtra_name));
}

Datum XtraBuiltins::createInstance(BuiltinContext& context,
                                   const Datum::Xtra& xtra_ref,
                                   const std::vector<Datum>& args) {
    if (!context.xtra_instance_creator) {
        debugPlaybackMessage(context, "Cannot create Xtra instance without an Xtra manager: " + xtra_ref.name);
        return Datum::voidValue();
    }
    return context.xtra_instance_creator(xtra_ref.name, args);
}

Datum XtraBuiltins::callHandler(BuiltinContext& context,
                                const Datum::XtraInstance& instance,
                                std::string_view handler_name,
                                const std::vector<Datum>& args) {
    if (!context.xtra_handler) {
        debugPlaybackMessage(context,
                             "Cannot call Xtra handler without an Xtra manager: " + instance.xtra_name +
                                 "." + std::string(handler_name));
        return Datum::voidValue();
    }
    return context.xtra_handler(instance, std::string(handler_name), args);
}

Datum XtraBuiltins::callInstanceGlobalHandler(BuiltinContext& context,
                                              std::string_view handler_name,
                                              const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    const auto* instance = args.front().asXtraInstance();
    if (instance == nullptr) {
        return Datum::voidValue();
    }
    if (!context.xtra_handler) {
        debugPlaybackMessage(context,
                             "Cannot call Xtra handler without an Xtra manager: " + instance->xtra_name +
                                 "." + std::string(handler_name));
        return Datum::voidValue();
    }
    static const std::vector<Datum> empty_method_args;
    if (args.size() == 1) {
        return context.xtra_handler(*instance, std::string(handler_name), empty_method_args);
    }
    std::vector<Datum> method_args(args.begin() + 1, args.end());
    return context.xtra_handler(*instance, std::string(handler_name), method_args);
}

Datum XtraBuiltins::getProperty(BuiltinContext& context,
                                const Datum::XtraInstance& instance,
                                std::string_view property_name) {
    if (!context.xtra_property_getter) {
        debugPlaybackMessage(context,
                             "Cannot get Xtra property without an Xtra manager: " + instance.xtra_name +
                                 "." + std::string(property_name));
        return Datum::voidValue();
    }
    return context.xtra_property_getter(instance, std::string(property_name));
}

void XtraBuiltins::setProperty(BuiltinContext& context,
                               const Datum::XtraInstance& instance,
                               std::string_view property_name,
                               const Datum& value) {
    if (context.xtra_property_setter) {
        context.xtra_property_setter(instance, std::string(property_name), value);
    } else {
        debugPlaybackMessage(context,
                             "Cannot set Xtra property without an Xtra manager: " + instance.xtra_name +
                                 "." + std::string(property_name));
    }
}

void ControlFlowBuiltins::registerBuiltins(BuiltinRegistry& registry) {
    registry.registerBuiltin("return", ControlFlowBuiltins::returnValue);
    registry.registerBuiltin("halt", ControlFlowBuiltins::halt);
    registry.registerBuiltin("abort", ControlFlowBuiltins::abort);
    registry.registerBuiltin("nothing", ControlFlowBuiltins::nothing);
    registry.registerBuiltin("param", ControlFlowBuiltins::param);
    registry.registerBuiltin("go", ControlFlowBuiltins::go);
    registry.registerBuiltin("call", ControlFlowBuiltins::call);
    registry.registerBuiltin("sendallsprites", ControlFlowBuiltins::sendAllSprites);
}

Datum ControlFlowBuiltins::returnValue(BuiltinContext& context, const std::vector<Datum>& args) {
    context.returned = true;
    context.return_value = args.empty() ? Datum::voidValue() : args[0];
    return Datum::voidValue();
}

Datum ControlFlowBuiltins::halt(BuiltinContext&, const std::vector<Datum>&) {
    return Datum::voidValue();
}

Datum ControlFlowBuiltins::abort(BuiltinContext& context, const std::vector<Datum>&) {
    context.returned = true;
    context.aborted = true;
    return Datum::voidValue();
}

Datum ControlFlowBuiltins::nothing(BuiltinContext&, const std::vector<Datum>&) {
    return Datum::voidValue();
}

Datum ControlFlowBuiltins::param(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    const int index = toIntLikeJava(args[0]) - 1;
    const auto& handler_args = context.current_handler_args_view != nullptr
                                  ? *context.current_handler_args_view
                                  : context.current_handler_args;
    if (index < 0 || index >= static_cast<int>(handler_args.size())) {
        return Datum::voidValue();
    }
    return handler_args[static_cast<std::size_t>(index)];
}

Datum ControlFlowBuiltins::go(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty() || context.movie_properties == nullptr) {
        return Datum::voidValue();
    }

    const Datum& target = args[0];
    if (const auto* value = target.asInt()) {
        context.movie_properties->goToFrame(value->value);
        return Datum::voidValue();
    }
    if (const auto* symbol = target.asSymbol()) {
        const int frame = context.movie_properties->getMovieProp("frame").intValue();
        if (equalsIgnoreCase(symbol->name, "next")) {
            context.movie_properties->goToFrame(frame + 1);
        } else if (equalsIgnoreCase(symbol->name, "previous")) {
            context.movie_properties->goToFrame(std::max(1, frame - 1));
        } else if (equalsIgnoreCase(symbol->name, "loop")) {
            context.movie_properties->goToFrame(frame);
        } else {
            context.movie_properties->goToLabel(symbol->name);
        }
        return Datum::voidValue();
    }
    if (target.isString()) {
        context.movie_properties->goToLabel(target.stringValue());
        return Datum::voidValue();
    }
    const int frame = toIntLikeJava(target);
    if (frame > 0) {
        context.movie_properties->goToFrame(frame);
    }
    return Datum::voidValue();
}

Datum ControlFlowBuiltins::call(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.size() < 2 || !context.call_target_handler) {
        return Datum::voidValue();
    }

    std::string handler_name_storage;
    const std::string& handler_name = stringRefLikeJava(args[0], handler_name_storage);
    const std::span<const Datum> extra_args(args.data() + 2, args.size() - 2);
    const Datum& target = args[1];
    Datum last_result = Datum::voidValue();
    static const std::vector<Datum> empty_call_args;
    auto make_call_args = [&extra_args]() {
        return snapshotStructArgsForCall(extra_args);
    };
    const std::vector<Datum> single_target_call_args = extra_args.empty()
                                                       ? std::vector<Datum>()
                                                       : make_call_args();
    const std::vector<Datum>& single_target_call_args_ref = extra_args.empty() ? empty_call_args : single_target_call_args;

    if (target.isList()) {
        const auto snapshot = target.listValue().items();
        for (const auto& item : snapshot) {
            const std::vector<Datum> call_args = extra_args.empty() ? std::vector<Datum>() : make_call_args();
            const std::vector<Datum>& call_args_ref = extra_args.empty() ? empty_call_args : call_args;
            last_result = context.call_target_handler(item, handler_name, call_args_ref);
        }
        return std::move(last_result);
    }
    if (target.isPropList()) {
        std::vector<Datum> snapshot;
        snapshot.reserve(target.propListValue().properties().size());
        for (const auto& entry : target.propListValue().properties()) {
            snapshot.emplace_back(entry.second);
        }
        for (const auto& item : snapshot) {
            const std::vector<Datum> call_args = extra_args.empty() ? std::vector<Datum>() : make_call_args();
            const std::vector<Datum>& call_args_ref = extra_args.empty() ? empty_call_args : call_args;
            last_result = context.call_target_handler(item, handler_name, call_args_ref);
        }
        return std::move(last_result);
    }

    return context.call_target_handler(target, handler_name, single_target_call_args_ref);
}

Datum ControlFlowBuiltins::sendAllSprites(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty() || !context.send_all_sprites_handler) {
        return Datum::voidValue();
    }

    std::string handler_name_storage;
    const std::string& handler_name = stringRefLikeJava(args[0], handler_name_storage);
    if (handler_name.empty()) {
        return Datum::voidValue();
    }

    const std::span<const Datum> extra_args(args.data() + 1, args.size() - 1);
    const std::vector<Datum> call_args = snapshotStructArgsForCall(extra_args);
    return context.send_all_sprites_handler(handler_name, call_args);
}

void ConstructorBuiltins::registerBuiltins(BuiltinRegistry& registry) {
    registry.registerBuiltin("point", ConstructorBuiltins::point);
    registry.registerBuiltin("rect", ConstructorBuiltins::rect);
    registry.registerBuiltin("union", ConstructorBuiltins::unionRect);
    registry.registerBuiltin("intersect", ConstructorBuiltins::intersect);
    registry.registerBuiltin("color", ConstructorBuiltins::color);
    registry.registerBuiltin("rgb", ConstructorBuiltins::rgb);
    registry.registerBuiltin("paletteindex", ConstructorBuiltins::paletteIndex);
    registry.registerBuiltin("sprite", ConstructorBuiltins::sprite);
    registry.registerBuiltin("new", ConstructorBuiltins::newInstance);
}

Datum ConstructorBuiltins::point(BuiltinContext&, const std::vector<Datum>& args) {
    const int x = args.empty() ? 0 : toIntLikeJava(args[0]);
    const int y = args.size() < 2 ? 0 : toIntLikeJava(args[1]);
    return Datum::intPoint(x, y);
}

Datum ConstructorBuiltins::rect(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() == 2 && args[0].asIntPoint() != nullptr && args[1].asIntPoint() != nullptr) {
        const auto* first = args[0].asIntPoint();
        const auto* second = args[1].asIntPoint();
        return Datum::intRect(first->x, first->y, second->x, second->y);
    }
    const int left = args.empty() ? 0 : toIntLikeJava(args[0]);
    const int top = args.size() < 2 ? 0 : toIntLikeJava(args[1]);
    const int right = args.size() < 3 ? 0 : toIntLikeJava(args[2]);
    const int bottom = args.size() < 4 ? 0 : toIntLikeJava(args[3]);
    return Datum::intRect(left, top, right, bottom);
}

Datum ConstructorBuiltins::unionRect(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 2) {
        return Datum::voidValue();
    }
    const auto* first = args[0].asIntRect();
    const auto* second = args[1].asIntRect();
    if (first == nullptr || second == nullptr) {
        return Datum::voidValue();
    }
    const bool first_empty = isEmptyRect(*first);
    const bool second_empty = isEmptyRect(*second);
    if (first_empty && second_empty) {
        return Datum::intRect(0, 0, 0, 0);
    }
    if (first_empty) {
        return args[1];
    }
    if (second_empty) {
        return args[0];
    }
    return Datum::intRect(std::min(first->left, second->left),
                          std::min(first->top, second->top),
                          std::max(first->right, second->right),
                          std::max(first->bottom, second->bottom));
}

Datum ConstructorBuiltins::intersect(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 2) {
        return Datum::voidValue();
    }
    const auto* first = args[0].asIntRect();
    const auto* second = args[1].asIntRect();
    if (first == nullptr || second == nullptr) {
        return Datum::voidValue();
    }
    const int left = std::max(first->left, second->left);
    const int top = std::max(first->top, second->top);
    const int right = std::min(first->right, second->right);
    const int bottom = std::min(first->bottom, second->bottom);
    if (right <= left || bottom <= top) {
        return Datum::intRect(0, 0, 0, 0);
    }
    return Datum::intRect(left, top, right, bottom);
}

Datum ConstructorBuiltins::color(BuiltinContext&, const std::vector<Datum>& args) {
    const int r = args.empty() ? 0 : toIntLikeJava(args[0]);
    const int g = args.size() < 2 ? 0 : toIntLikeJava(args[1]);
    const int b = args.size() < 3 ? 0 : toIntLikeJava(args[2]);
    return Datum::colorRef(r, g, b);
}

Datum ConstructorBuiltins::rgb(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::colorRef(0, 0, 0);
    }
    if (args.size() == 1) {
        if (args[0].asColorRef() != nullptr) {
            return args[0];
        }
        if (args[0].isString()) {
            std::string hex = trimCopy(args[0].stringValue());
            if (!hex.empty() && hex.front() == '#') {
                hex.erase(hex.begin());
            }
            char* end = nullptr;
            const long value = std::strtol(hex.c_str(), &end, 16);
            if (end == hex.c_str() || *end != '\0') {
                return Datum::colorRef(0, 0, 0);
            }
            return Datum::colorRef(static_cast<int>((value >> 16) & 0xFF),
                                   static_cast<int>((value >> 8) & 0xFF),
                                   static_cast<int>(value & 0xFF));
        }
    }
    if (args.size() >= 3) {
        return Datum::colorRef(toIntLikeJava(args[0]), toIntLikeJava(args[1]), toIntLikeJava(args[2]));
    }
    const int value = toIntLikeJava(args[0]);
    return Datum::colorRef((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
}

Datum ConstructorBuiltins::paletteIndex(BuiltinContext&, const std::vector<Datum>& args) {
    const int index = args.empty() ? 0 : toIntLikeJava(args[0]) & 0xFF;
    return Datum::paletteIndexColor(index);
}

Datum ConstructorBuiltins::sprite(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    const int channel = toIntLikeJava(args[0]);
    if (channel < 0) {
        return Datum::voidValue();
    }
    return Datum::spriteRef(id::ChannelId(channel));
}

Datum scriptFromArgs(BuiltinContext& context, std::span<const Datum> args);

Datum ConstructorBuiltins::newInstance(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }

    const Datum& target = args.front();
    if (const auto* type_symbol = target.asSymbol();
        type_symbol != nullptr && args.size() >= 2 && args[1].asCastLibRef() != nullptr) {
        if (context.cast_member_creator) {
            context.script_resolution_cache.clear();
            return context.cast_member_creator(args[1].asCastLibRef()->cast_lib, type_symbol->name);
        }
        return Datum::voidValue();
    }
    if (const auto* type_symbol = target.asSymbol();
        type_symbol != nullptr && args.size() >= 2 && args[1].isVoid()) {
        if (context.cast_member_creator) {
            context.script_resolution_cache.clear();
            return context.cast_member_creator(1, type_symbol->name);
        }
        return Datum::voidValue();
    }

    static const std::vector<Datum> empty_constructor_args;
    std::vector<Datum> constructor_arg_storage;
    const auto constructor_args = [&args, &constructor_arg_storage]() -> const std::vector<Datum>& {
        if (args.size() <= 1) {
            return empty_constructor_args;
        }
        if (constructor_arg_storage.empty()) {
            constructor_arg_storage.assign(args.begin() + 1, args.end());
        }
        return constructor_arg_storage;
    };
    if (const auto* xtra_ref = target.asXtra()) {
        return XtraBuiltins::createInstance(context, *xtra_ref, constructor_args());
    }
    if (context.new_instance_handler) {
        return context.new_instance_handler(target, constructor_args());
    }

    Datum resolved_target = target;
    if (target.asScriptRef() == nullptr && target.asCastMemberRef() == nullptr &&
        (target.isString() || target.isSymbol())) {
        const Datum resolved_script = scriptFromArgs(context, std::span<const Datum>(&target, 1));
        if (resolved_script.asScriptRef() != nullptr) {
            resolved_target = resolved_script;
        }
    }

    if (const auto* script_ref = resolved_target.asScriptRef()) {
        return createScriptInstanceFromRef(context, script_ref->member_ref, constructor_args());
    }
    if (const auto* member_ref = resolved_target.asCastMemberRef()) {
        return createScriptInstanceFromRef(context, *member_ref, constructor_args());
    }
    return Datum::voidValue();
}

void TypeBuiltins::registerBuiltins(BuiltinRegistry& registry) {
    registry.registerBuiltin("objectp", TypeBuiltins::objectp);
    registry.registerBuiltin("voidp", TypeBuiltins::voidp);
    registry.registerBuiltin("value", TypeBuiltins::value);
    registry.registerBuiltin("script", TypeBuiltins::script);
    registry.registerBuiltin("ilk", TypeBuiltins::ilk);
    registry.registerBuiltin("listp", TypeBuiltins::listp);
    registry.registerBuiltin("stringp", TypeBuiltins::stringp);
    registry.registerBuiltin("integerp", TypeBuiltins::integerp);
    registry.registerBuiltin("floatp", TypeBuiltins::floatp);
    registry.registerBuiltin("symbolp", TypeBuiltins::symbolp);
    registry.registerBuiltin("symbol", TypeBuiltins::symbol);
    registry.registerBuiltin("callancestor", TypeBuiltins::callAncestor);
}

Datum TypeBuiltins::objectp(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::FALSE;
    }
    const Datum& value = args[0];
    return boolDatum(!(value.isVoid() || value.isNumber() || value.isSymbol() || value.isString()));
}

Datum TypeBuiltins::voidp(BuiltinContext&, const std::vector<Datum>& args) {
    return boolDatum(args.empty() || args[0].isVoid());
}

Datum TypeBuiltins::value(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    auto identifier_resolver = [&context](std::string_view identifier) {
        if (!context.value_evaluator) {
            return Datum::voidValue();
        }
        return context.value_evaluator(Datum::of(std::string(identifier)));
    };
    if (const auto* field_text = args[0].asFieldText()) {
        if (context.field_parsed_value_resolver) {
            Datum parsed = context.field_parsed_value_resolver(field_text->cast_lib, field_text->member_num, field_text->revision);
            if (!parsed.isVoid()) {
                return parsed;
            }
        }
        return LingoValueParser::parseWithPartial(field_text->value, identifier_resolver);
    }
    if (!args[0].isString()) {
        return args[0];
    }

    std::string raw_storage;
    const std::string_view raw = stringViewLikeJava(args[0], raw_storage);
    if (raw.empty()) {
        return Datum::of(std::string());
    }
    if (auto parsed_literal = LingoValueParser::parseComplete(raw, identifier_resolver);
        parsed_literal.has_value() && !parsed_literal->isVoid()) {
        return std::move(*parsed_literal);
    }
    if (context.value_evaluator) {
        Datum evaluated = context.value_evaluator(args[0]);
        if (!evaluated.isVoid()) {
            return evaluated;
        }
    }
    Datum parsed = LingoValueParser::parseWithPartial(raw, identifier_resolver);
    if (!parsed.isVoid()) {
        return parsed;
    }

    const std::string_view trimmed = trimView(raw);
    if (trimmed.find(' ') != std::string::npos && context.cast_member_name_resolver) {
        const Datum member_ref = context.cast_member_name_resolver(0, std::string(trimmed));
        if (member_ref.asCastMemberRef() != nullptr) {
            return Datum::of(std::string(trimmed));
        }
    }
    return parsed;
}

Datum scriptFromArgs(BuiltinContext& context, std::span<const Datum> args) {
    if (args.empty()) {
        return Datum::voidValue();
    }

    const Datum& identifier = args[0];
    std::optional<Datum> scope;
    if (args.size() >= 2) {
        scope = args[1];
    }
    const int scoped_cast_lib = resolveCastLibScope(context, scope);

    if (const auto* member = identifier.asCastMemberRef()) {
        return Datum::scriptRef(*member);
    }
    if (identifier.isString() || identifier.isSymbol()) {
        const std::string script_name = keyName(identifier);
        std::string cache_key = std::to_string(scoped_cast_lib);
        cache_key.reserve(cache_key.size() + 1 + script_name.size());
        cache_key.push_back(':');
        appendLowercase(cache_key, script_name);
        if (const auto cached = context.script_resolution_cache.find(cache_key);
            cached != context.script_resolution_cache.end()) {
            return cached->second;
        }
        if (context.cast_member_name_resolver) {
            const Datum member_ref = context.cast_member_name_resolver(scoped_cast_lib, script_name);
            const Datum resolved = scriptRefFromCastMemberDatum(member_ref);
            if (!resolved.isVoid()) {
                context.script_resolution_cache.emplace(cache_key, resolved);
                return resolved;
            }
        }
    }
    if (identifier.isInt() || identifier.isFloat()) {
        const int value = toIntLikeJava(identifier);
        if (value <= 0) {
            return Datum::voidValue();
        }
        if (value > 65535) {
            const id::SlotId slot_id(value);
            if (slot_id.castLib() >= 1 && slot_id.member() >= 1) {
                return Datum::scriptRef(Datum::CastMemberRef{slot_id.castLib(), slot_id.member()});
            }
            return Datum::voidValue();
        }
        return Datum::scriptRef(Datum::CastMemberRef{1, value});
    }

    if (identifier.isList()) {
        for (const auto& item : identifier.listValue().items()) {
            Datum result = scriptFromArgs(context, std::span<const Datum>(&item, 1));
            if (!result.isVoid()) {
                return result;
            }
        }
        return Datum::voidValue();
    }

    if (context.script_resolver) {
        return context.script_resolver(identifier, scope);
    }
    return Datum::voidValue();
}

Datum TypeBuiltins::script(BuiltinContext& context, const std::vector<Datum>& args) {
    return scriptFromArgs(context, std::span<const Datum>(args));
}

Datum TypeBuiltins::ilk(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::symbol("void");
    }

    std::string type_name = ilkType(args[0]);
    if (args.size() < 2) {
        return Datum::symbol(std::move(type_name));
    }

    const std::string check_name = keyName(args[1]);
    if (equalsIgnoreCase(type_name, check_name)) {
        return Datum::TRUE;
    }
    if (equalsIgnoreCase(check_name, "list") &&
        (equalsIgnoreCase(type_name, "proplist") || equalsIgnoreCase(type_name, "rect") ||
         equalsIgnoreCase(type_name, "point"))) {
        return Datum::TRUE;
    }
    if (equalsIgnoreCase(check_name, "linearlist") && equalsIgnoreCase(type_name, "list")) {
        return Datum::TRUE;
    }
    if (equalsIgnoreCase(check_name, "number") &&
        (equalsIgnoreCase(type_name, "integer") || equalsIgnoreCase(type_name, "float"))) {
        return Datum::TRUE;
    }
    if (equalsIgnoreCase(check_name, "object") &&
        (equalsIgnoreCase(type_name, "instance") || equalsIgnoreCase(type_name, "member") ||
         equalsIgnoreCase(type_name, "xtra") || equalsIgnoreCase(type_name, "xtrainstance") ||
         equalsIgnoreCase(type_name, "script") || equalsIgnoreCase(type_name, "castlib") ||
         equalsIgnoreCase(type_name, "sprite") || equalsIgnoreCase(type_name, "stage") ||
         equalsIgnoreCase(type_name, "sound") || equalsIgnoreCase(type_name, "image"))) {
        return Datum::TRUE;
    }
    return Datum::FALSE;
}

Datum TypeBuiltins::listp(BuiltinContext&, const std::vector<Datum>& args) {
    return boolDatum(!args.empty() && (args[0].isList() || args[0].isPropList()));
}

Datum TypeBuiltins::stringp(BuiltinContext&, const std::vector<Datum>& args) {
    return boolDatum(!args.empty() && args[0].isString());
}

Datum TypeBuiltins::integerp(BuiltinContext&, const std::vector<Datum>& args) {
    return boolDatum(!args.empty() && args[0].isInt());
}

Datum TypeBuiltins::floatp(BuiltinContext&, const std::vector<Datum>& args) {
    return boolDatum(!args.empty() && args[0].isFloat());
}

Datum TypeBuiltins::symbolp(BuiltinContext&, const std::vector<Datum>& args) {
    return boolDatum(!args.empty() && args[0].isSymbol());
}

Datum TypeBuiltins::symbol(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    if (args[0].isSymbol()) {
        return args[0];
    }
    if (!args[0].isString()) {
        return Datum::voidValue();
    }
    const std::string text = args[0].stringValue();
    std::string_view value = text;
    if (!value.empty() && value.front() == '#') {
        value.remove_prefix(1);
    }
    return Datum::symbol(std::string(value));
}

Datum TypeBuiltins::callAncestor(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.size() < 2) {
        return Datum::voidValue();
    }
    if (args[1].isList()) {
        Datum result = Datum::voidValue();
        std::vector<Datum> nested_args;
        nested_args.reserve(args.size());
        for (const auto& item : args[1].listValue().items()) {
            if (item.type() != DatumType::ScriptInstanceRef) {
                continue;
            }
            nested_args.clear();
            nested_args.push_back(args[0]);
            nested_args.push_back(item);
            nested_args.insert(nested_args.end(), args.begin() + 2, args.end());
            result = callAncestor(context, nested_args);
        }
        return result;
    }
    if (args[1].type() != DatumType::ScriptInstanceRef) {
        return Datum::voidValue();
    }
    if (context.ancestor_call_handler) {
        return context.ancestor_call_handler(args);
    }
    return Datum::voidValue();
}

void MovieBuiltins::registerBuiltins(BuiltinRegistry& registry) {
    registry.registerBuiltin("label", MovieBuiltins::label);
    registry.registerBuiltin("marker", MovieBuiltins::marker);
}

Datum MovieBuiltins::label(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty() || context.movie_properties == nullptr) {
        return Datum::of(0);
    }

    const Datum& label_arg = args.size() >= 2 && isMovieRef(args[0]) ? args[1] : args[0];
    return Datum::of(std::max(context.movie_properties->getFrameForLabel(label_arg.stringValue()), 0));
}

Datum MovieBuiltins::marker(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty() || context.movie_properties == nullptr) {
        return Datum::of(0);
    }

    const Datum& marker_arg = args.size() >= 2 && isMovieRef(args[0]) ? args[1] : args[0];
    if (marker_arg.isString()) {
        return Datum::of(std::max(context.movie_properties->getFrameForLabel(marker_arg.stringValue()), 0));
    }
    return Datum::of(std::max(context.movie_properties->getMarkerFrame(marker_arg.intValue()), 0));
}

void SpriteBuiltins::registerBuiltins(BuiltinRegistry& registry) {
    registry.registerBuiltin("puppettempo", SpriteBuiltins::puppetTempo);
    registry.registerBuiltin("puppetsprite", SpriteBuiltins::puppetSprite);
    registry.registerBuiltin("puppetpalette", SpriteBuiltins::puppetPalette);
    registry.registerBuiltin("cursor", SpriteBuiltins::cursor);
    registry.registerBuiltin("setcursor", SpriteBuiltins::cursor);
    registry.registerBuiltin("pauseupdate", [](BuiltinContext&, const std::vector<Datum>&) { return voidDatum(); });
    registry.registerBuiltin("updatestage", [](BuiltinContext&, const std::vector<Datum>&) { return voidDatum(); });
    registry.registerBuiltin("movetofront", [](BuiltinContext&, const std::vector<Datum>&) { return voidDatum(); });
    registry.registerBuiltin("movetoback", [](BuiltinContext&, const std::vector<Datum>&) { return voidDatum(); });
    registry.registerBuiltin("spritebox", SpriteBuiltins::spriteBox);
}

Datum SpriteBuiltins::puppetTempo(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    if (context.movie_properties != nullptr) {
        (void)context.movie_properties->setMovieProp("puppetTempo", Datum::of(toIntLikeJava(args[0])));
    }
    return Datum::voidValue();
}

Datum SpriteBuiltins::puppetSprite(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.size() < 2) {
        return Datum::voidValue();
    }
    if (context.sprite_properties != nullptr) {
        (void)context.sprite_properties->setSpriteProp(toIntLikeJava(args[0]),
                                                      "puppet",
                                                      Datum::of(truthyLikeJava(args[1]) ? 1 : 0));
    }
    return Datum::voidValue();
}

Datum SpriteBuiltins::puppetPalette(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    if (context.puppet_palette_handler) {
        if (isResetPaletteArg(args[0])) {
            context.puppet_palette_handler(std::nullopt);
        } else {
            context.puppet_palette_handler(args[0]);
        }
    }
    return Datum::voidValue();
}

Datum SpriteBuiltins::cursor(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    if (context.movie_properties != nullptr) {
        (void)context.movie_properties->setMovieProp("cursor", args[0]);
    }
    return Datum::voidValue();
}

Datum SpriteBuiltins::spriteBox(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty() || context.sprite_properties == nullptr) {
        return Datum::intRect(0, 0, 0, 0);
    }
    return context.sprite_properties->getSpriteProp(toIntLikeJava(args[0]), "rect");
}

} // namespace libreshockwave::lingo::builtin
