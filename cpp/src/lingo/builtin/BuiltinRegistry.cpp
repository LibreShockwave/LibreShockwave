#include "libreshockwave/lingo/builtin/BuiltinRegistry.hpp"

#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/bitmap/Palette.hpp"
#include "libreshockwave/lingo/LingoValueParser.hpp"
#include "libreshockwave/lingo/vm/DebugConfig.hpp"

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
    if (!context.debugPlaybackEnabled && !vm::DebugConfig::isDebugPlaybackEnabled()) {
        return;
    }
    if (context.outputHandler) {
        context.outputHandler("DEBUG", message);
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
        return value->castLib;
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
    if (const auto* castLib = scope->asCastLibRef()) {
        return castLib->castLib;
    }
    if (scope->isInt() || scope->isFloat()) {
        return toIntLikeJava(*scope);
    }
    if (!context.castLibNameResolver) {
        return 0;
    }
    const std::string name = trimCopy(scope->stringValue());
    if (name.empty()) {
        return 0;
    }
    return std::max(context.castLibNameResolver(name), 0);
}

Datum scriptRefFromCastMemberDatum(const Datum& memberRef) {
    if (const auto* castMember = memberRef.asCastMemberRef()) {
        return Datum::scriptRef(*castMember);
    }
    return Datum::voidValue();
}

std::uint64_t scriptPropertyCacheKey(int castLib, int memberNum) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(castLib)) << 32U) |
           static_cast<std::uint32_t>(memberNum);
}

const std::vector<std::string>& declaredScriptPropertyNames(BuiltinContext& context,
                                                            const Datum::CastMemberRef& memberRef) {
    const auto key = scriptPropertyCacheKey(memberRef.castLib, memberRef.memberNum());
    if (const auto found = context.scriptPropertyNamesCache.find(key);
        found != context.scriptPropertyNamesCache.end()) {
        return found->second;
    }
    auto propertyNames = context.scriptPropertyNamesResolver
        ? context.scriptPropertyNamesResolver(memberRef.castLib, memberRef.memberNum())
        : std::vector<std::string>{};
    auto [inserted, _] = context.scriptPropertyNamesCache.emplace(key, std::move(propertyNames));
    return inserted->second;
}

Datum createScriptInstanceFromRef(BuiltinContext& context,
                                  const Datum::CastMemberRef& memberRef,
                                  const std::vector<Datum>& constructorArgs = {}) {
    Datum instance = Datum::scriptInstance("script", memberRef);
    if (context.scriptPropertyNamesResolver) {
        const auto& propertyNames = declaredScriptPropertyNames(context, memberRef);
        auto& scriptInstance = instance.scriptInstanceValue();
        scriptInstance.reserveLocalProperties(propertyNames.size());
        for (const auto& propertyName : propertyNames) {
            scriptInstance.appendLocalProperty(propertyName, Datum::voidValue());
        }
    }
    if (context.callTargetHandler) {
        Datum result = context.callTargetHandler(instance, "new", constructorArgs);
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
        return static_cast<double>(value->castLib);
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
    return std::nullopt;
}

std::string_view stringViewLikeJava(const Datum& datum, std::string& storage) {
    if (const auto directValue = directStringViewLikeJava(datum)) {
        return *directValue;
    }
    storage = toStringLikeJava(datum);
    return storage;
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

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs);

std::string delimiterFromArg(const Datum& datum) {
    std::string delimiter = toStringLikeJava(datum);
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
        lines.push_back(current);
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

    for (const auto& rawLine : splitPropertyText(text, delimiter)) {
        std::string line = trimCopy(rawLine);
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
        const int lowerLeft = std::tolower(left);
        const int lowerRight = std::tolower(right);
        if (lowerLeft != lowerRight) {
            return lowerLeft < lowerRight;
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
    if ((lhs.isString() || lhs.isSymbol()) && (rhs.isString() || rhs.isSymbol())) {
        const auto lhsView = directStringViewLikeJava(lhs);
        const auto rhsView = directStringViewLikeJava(rhs);
        return lhsView.has_value() && rhsView.has_value() && equalsIgnoreCase(*lhsView, *rhsView);
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

int findPropIndexTyped(const Datum::PropList& propList, const Datum& key) {
    return propList.findTypedKey(key);
}

int findPropIndexSameType(const Datum::PropList& propList, const Datum& key) {
    return propList.findSameTypeKey(key);
}

int findPropIndexUntyped(const Datum::PropList& propList, const Datum& key) {
    return propList.findUntypedKey(key);
}

Datum propListBuiltinGetAtValue(const Datum::PropList& propList, const Datum& keyOrIndex) {
    int index = -1;
    if (keyOrIndex.asSymbol() != nullptr || keyOrIndex.isString()) {
        index = findPropIndexTyped(propList, keyOrIndex);
    } else {
        const int position = toIntLikeJava(keyOrIndex);
        if (position >= 1 && position <= propList.count()) {
            index = position - 1;
        }
    }
    if (index >= 0) {
        return propList.properties()[static_cast<std::size_t>(index)].second;
    }
    return Datum::voidValue();
}

void putPropTyped(Datum::PropList& propList, const Datum& key, Datum value) {
    propList.putTyped(key, std::move(value));
}

void putPropSameType(Datum::PropList& propList, const Datum& key, Datum value) {
    propList.putSameType(key, std::move(value));
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

void putStringProp(Datum& propList, const std::string& key, Datum value) {
    propList.propListValue().put(Datum::of(key), std::move(value));
}

Datum defaultStreamStatusDatum() {
    auto props = Datum::propList();
    putStringProp(props, "URL", Datum::of(std::string()));
    putStringProp(props, "state", Datum::of(std::string("Error")));
    putStringProp(props, "bytesSoFar", Datum::of(0));
    putStringProp(props, "bytesTotal", Datum::of(0));
    putStringProp(props, "error", Datum::of(std::string("OK")));
    return props;
}

Datum getTypedSymbolProp(const Datum::PropList& propList, std::string_view key) {
    const int index = findPropIndexTyped(propList, Datum::symbol(std::string(key)));
    return index >= 0 ? propList.properties()[static_cast<std::size_t>(index)].second : Datum::voidValue();
}

bool isMessageStruct(const Datum::PropList& propList) {
    if (getTypedSymbolProp(propList, "connection").type() != DatumType::ScriptInstanceRef) {
        return false;
    }

    const Datum ilk = getTypedSymbolProp(propList, "ilk");
    if (const auto* symbol = ilk.asSymbol(); symbol != nullptr && equalsIgnoreCase(symbol->name, "struct")) {
        return true;
    }

    return !getTypedSymbolProp(propList, "subject").isVoid() &&
           !getTypedSymbolProp(propList, "content").isVoid();
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
        snapshot.push_back(snapshotStructArgForCall(arg));
    }
    return snapshot;
}

bool hasCastProvider(const BuiltinContext& context) {
    return context.namedCastMemberCreator ||
           context.castLibNumberResolver ||
           context.castLibNameResolver ||
           context.castLibCountSupplier ||
           context.castMemberCountSupplier ||
           context.castMemberResolver ||
           context.castMemberNameResolver ||
           context.castMemberExistsResolver ||
           context.registryVisibleMemberResolver ||
           context.castMemberPropertyGetter ||
           context.castMemberPropertySetter ||
           context.fieldResolver ||
           context.fieldSetter;
}

Datum castLibRefOrVoid(int castLib) {
    if (castLib < 1) {
        return Datum::voidValue();
    }
    return Datum::castLibRef(id::CastLibId(castLib));
}

Datum getCastLibMemberAccessorValue(BuiltinContext& context,
                                    const Datum::CastLibMemberAccessor& accessor,
                                    const Datum& keyOrIndex) {
    if (keyOrIndex.isInt() || keyOrIndex.isFloat()) {
        return context.castMemberResolver ? context.castMemberResolver(accessor.castLib, toIntLikeJava(keyOrIndex))
                                          : Datum::voidValue();
    }
    return context.castMemberNameResolver ? context.castMemberNameResolver(accessor.castLib, toStringLikeJava(keyOrIndex))
                                          : Datum::voidValue();
}

Datum castMemberRefOrVoid(int castLib, int memberNum) {
    if (castLib < 1 || memberNum < 0) {
        return Datum::voidValue();
    }
    return Datum::castMemberRef(id::CastLibId(castLib), id::MemberId(memberNum));
}

int resolveCastLibArg(BuiltinContext& context, const Datum& castArg) {
    if (castArg.isVoid() || castArg.isNull()) {
        return 0;
    }
    if (const auto* ref = castArg.asCastLibRef()) {
        return ref->castLib;
    }
    if (castArg.isInt() || castArg.isFloat()) {
        if (!context.castLibNumberResolver) {
            return 0;
        }
        const int resolved = context.castLibNumberResolver(toIntLikeJava(castArg));
        return resolved >= 0 ? resolved : 0;
    }
    if (castArg.isString() || castArg.isSymbol()) {
        if (!context.castLibNameResolver) {
            return 0;
        }
        const int resolved = context.castLibNameResolver(toStringLikeJava(castArg));
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
    if (datum.isVoid()) {
        return "";
    }
    if (datum.isString() || datum.isNumber()) {
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
        return *result;
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
    if (context.randomIntHandler) {
        return Datum::of(context.randomIntHandler(max));
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
        if (const auto parsedInt = parseIntStrict(trimmed)) {
            return Datum::of(*parsedInt);
        }
        if (const auto parsedDouble = parseDoubleStrict(trimmed)) {
            return Datum::of(javaRoundToInt(*parsedDouble));
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
        Datum result = items.front();
        bool floatResult = result.isFloat();
        for (std::size_t index = 1; index < items.size(); ++index) {
            floatResult = floatResult || items[index].isFloat();
            if (floatResult) {
                result = Datum::of(std::min(toDoubleLikeJava(result), toDoubleLikeJava(items[index])));
            } else {
                result = Datum::of(std::min(toIntLikeJava(result), toIntLikeJava(items[index])));
            }
        }
        return result;
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
        Datum result = items.front();
        bool floatResult = result.isFloat();
        for (std::size_t index = 1; index < items.size(); ++index) {
            floatResult = floatResult || items[index].isFloat();
            if (floatResult) {
                result = Datum::of(std::max(toDoubleLikeJava(result), toDoubleLikeJava(items[index])));
            } else {
                result = Datum::of(std::max(toIntLikeJava(result), toIntLikeJava(items[index])));
            }
        }
        return result;
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
    std::string valueStorage;
    const std::string_view value = stringViewLikeJava(args[0], valueStorage);
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
    const int numericValue = args[0].asInt() != nullptr
        ? args[0].asInt()->value
        : toIntLikeJava(args[0]);
    const char value = static_cast<char>(numericValue);
    return Datum::of(std::string(1, value));
}

Datum StringBuiltins::offset(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.size() < 2) {
        return Datum::of(0);
    }
    std::string needleStorage;
    std::string haystackStorage;
    const std::string_view needle = stringViewLikeJava(args[0], needleStorage);
    const std::string_view haystack = stringViewLikeJava(args[1], haystackStorage);
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
        return Datum::of(result);
    }

    std::string fromStorage;
    const std::string_view from = stringViewLikeJava(args[1], fromStorage);
    if (from.empty()) {
        return Datum::of(result);
    }
    std::string toStorage;
    const std::string_view to = stringViewLikeJava(args[2], toStorage);
    std::size_t pos = 0;
    while ((pos = result.find(from.data(), pos, from.size())) != std::string::npos) {
        result.replace(pos, from.size(), to.data(), to.size());
        pos += to.size();
    }
    return Datum::of(result);
}

Datum StringBuiltins::getPref(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    const std::string key = toStringLikeJava(args[0]);
    if (key.empty() || !context.getPrefHandler) {
        return Datum::voidValue();
    }
    return context.getPrefHandler(key);
}

Datum StringBuiltins::setPref(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.size() < 2) {
        return Datum::voidValue();
    }
    const std::string key = toStringLikeJava(args[0]);
    if (key.empty() || !context.setPrefHandler) {
        return Datum::voidValue();
    }
    return context.setPrefHandler(key, args[1]);
}

void OutputBuiltins::registerBuiltins(BuiltinRegistry& registry) {
    registry.registerBuiltin("put", OutputBuiltins::put);
    registry.registerBuiltin("alert", OutputBuiltins::alert);
}

Datum OutputBuiltins::put(BuiltinContext& context, const std::vector<Datum>& args) {
    std::ostringstream text;
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index > 0) {
            text << ' ';
        }
        text << toStringLikeJava(args[index]);
    }

    if (!context.debugPlaybackEnabled && !vm::DebugConfig::isDebugPlaybackEnabled()) {
        return Datum::voidValue();
    }

    const std::string value = text.str();
    if (context.outputHandler) {
        context.outputHandler("PUT", value);
    } else {
        std::cout << "[PUT] " << value << '\n';
    }
    return Datum::voidValue();
}

Datum OutputBuiltins::alert(BuiltinContext& context, const std::vector<Datum>& args) {
    const std::string message = args.empty() ? "" : toStringLikeJava(args[0]);
    if (context.alertHookHandler && context.alertHookHandler("Alert", message)) {
        return Datum::voidValue();
    }
    if (context.alertHandler && context.alertHandler(message)) {
        return Datum::voidValue();
    }
    if (context.outputHandler) {
        context.outputHandler("ALERT", message);
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
    const Datum& keyOrIndex = args[1];
    if (const auto* accessor = container.asCastLibMemberAccessor()) {
        return getCastLibMemberAccessorValue(context, *accessor, keyOrIndex);
    }
    if (container.isList()) {
        if (keyOrIndex.isString() || keyOrIndex.isSymbol()) {
            if (const auto* propList = singlePropListWrapper(container.listValue())) {
                return propListBuiltinGetAtValue(*propList, keyOrIndex);
            }
        }
        const int index = toIntLikeJava(keyOrIndex);
        const auto& items = container.listValue().items();
        if (index >= 1 && index <= static_cast<int>(items.size())) {
            return items[static_cast<std::size_t>(index - 1)];
        }
        return Datum::voidValue();
    }
    if (container.isPropList()) {
        return propListBuiltinGetAtValue(container.propListValue(), keyOrIndex);
    }
    if (const auto* point = container.asIntPoint()) {
        switch (toIntLikeJava(keyOrIndex)) {
            case 1: return Datum::of(point->x);
            case 2: return Datum::of(point->y);
            default: return Datum::voidValue();
        }
    }
    if (const auto* rect = container.asIntRect()) {
        switch (toIntLikeJava(keyOrIndex)) {
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
    const Datum& keyOrIndex = args[1];
    const Datum& value = args[2];
    if (args[0].isList()) {
        if (keyOrIndex.isString() || keyOrIndex.isSymbol()) {
            if (auto* propList = singlePropListWrapper(mutableArg(args, 0).listValue())) {
                putPropTyped(*propList, keyOrIndex, value);
                return Datum::voidValue();
            }
        }
        const int index = toIntLikeJava(keyOrIndex);
        auto& items = mutableArg(args, 0).listValue().items();
        if (index >= 1 && index <= static_cast<int>(items.size())) {
            items[static_cast<std::size_t>(index - 1)] = value;
        }
        return Datum::voidValue();
    }
    if (args[0].isPropList()) {
        auto& propList = mutableArg(args, 0).propListValue();
        if (keyOrIndex.asSymbol() != nullptr || keyOrIndex.isString()) {
            putPropSameType(propList, keyOrIndex, value);
        } else {
            const int position = toIntLikeJava(keyOrIndex);
            if (position >= 1 && position <= propList.count()) {
                propList.properties()[static_cast<std::size_t>(position - 1)].second = value;
            }
        }
        return Datum::voidValue();
    }
    if (auto* point = mutableArg(args, 0).asIntPoint()) {
        const int component = toIntLikeJava(keyOrIndex);
        const int newValue = toIntLikeJava(value);
        if (component == 1) {
            point->x = newValue;
        } else if (component == 2) {
            point->y = newValue;
        }
        return Datum::voidValue();
    }
    if (auto* rect = mutableArg(args, 0).asIntRect()) {
        const int component = toIntLikeJava(keyOrIndex);
        const int newValue = toIntLikeJava(value);
        switch (component) {
            case 1: rect->left = newValue; break;
            case 2: rect->top = newValue; break;
            case 3: rect->right = newValue; break;
            case 4: rect->bottom = newValue; break;
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
        if (const auto* propList = singlePropListWrapper(args[0].listValue())) {
            const int index = findPropIndexUntyped(*propList, args[1]);
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
        std::string lhsStorage;
        std::string rhsStorage;
        return lessIgnoreCase(stringViewLikeJava(lhs, lhsStorage), stringViewLikeJava(rhs, rhsStorage));
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
    std::string separatorStorage;
    const std::string_view separator = args.size() > 1 ? stringViewLikeJava(args[1], separatorStorage) : "&";
    std::ostringstream out;
    const auto& items = args[0].listValue().items();
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (index > 0) {
            out << separator;
        }
        out << toStringLikeJava(items[index]);
    }
    return Datum::of(out.str());
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
    const std::string text = toStringLikeJava(args[0]);
    const std::string delimiter = args.size() > 1 ? delimiterFromArg(args[1]) : std::string();
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
                                    std::string_view methodName,
                                    const std::vector<Datum>& args) {
    auto* manager = context.timeoutManager;

    if (equalsIgnoreCase(methodName, "new")) {
        if (manager == nullptr) {
            return Datum::voidValue();
        }

        std::string name = ref.name;
        std::size_t argOffset = 0;
        if (name.empty() && !args.empty()) {
            name = toStringLikeJava(args[0]);
            argOffset = 1;
        }
        if (name.empty()) {
            return Datum::voidValue();
        }

        const int periodMs = args.size() > argOffset ? toIntLikeJava(args[argOffset]) : 1000;
        const std::string handler = args.size() > argOffset + 1 ? toKeyNameLikeJava(args[argOffset + 1]) : "";
        const Datum target = args.size() > argOffset + 2 ? args[argOffset + 2] : Datum::voidValue();
        return manager->createTimeout(name, periodMs, handler, target);
    }

    if (equalsIgnoreCase(methodName, "forget")) {
        if (manager != nullptr && !ref.name.empty()) {
            manager->forgetTimeout(ref.name);
        }
        return Datum::voidValue();
    }

    if (manager != nullptr && !ref.name.empty()) {
        return manager->getTimeoutProp(ref.name, std::string(methodName));
    }
    return Datum::voidValue();
}

Datum TimeoutBuiltins::getProperty(BuiltinContext& context,
                                   const Datum::TimeoutRef& ref,
                                   std::string_view propName) {
    if (context.timeoutManager == nullptr || ref.name.empty()) {
        return Datum::voidValue();
    }
    return context.timeoutManager->getTimeoutProp(ref.name, std::string(propName));
}

bool TimeoutBuiltins::setProperty(BuiltinContext& context,
                                  const Datum::TimeoutRef& ref,
                                  std::string_view propName,
                                  Datum value) {
    if (context.timeoutManager == nullptr || ref.name.empty()) {
        return false;
    }
    return context.timeoutManager->setTimeoutProp(ref.name, std::string(propName), std::move(value));
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
    if (context.netManager == nullptr || args.empty()) {
        return Datum::of(-1);
    }
    return Datum::of(context.netManager->preloadNetThing(toStringLikeJava(args[0])));
}

Datum NetBuiltins::postNetText(BuiltinContext& context, const std::vector<Datum>& args) {
    if (context.netManager == nullptr || args.empty()) {
        return Datum::of(-1);
    }
    const std::string postData = args.size() > 1 ? toStringLikeJava(args[1]) : "";
    return Datum::of(context.netManager->postNetText(toStringLikeJava(args[0]), postData));
}

Datum NetBuiltins::netDone(BuiltinContext& context, const std::vector<Datum>& args) {
    if (context.netManager == nullptr) {
        return Datum::TRUE;
    }
    if (args.empty() && context.netManager->getStreamStatus() == "Error" && context.netManager->netError() == 0) {
        return Datum::TRUE;
    }
    const std::optional<int> taskId = args.empty() ? std::nullopt
                                                   : std::optional<int>{toIntLikeJava(args[0])};
    return boolDatum(context.netManager->netDone(taskId));
}

Datum NetBuiltins::netTextResult(BuiltinContext& context, const std::vector<Datum>& args) {
    if (context.netManager == nullptr) {
        return Datum::of(std::string());
    }
    const std::optional<int> taskId = args.empty() ? std::nullopt
                                                   : std::optional<int>{toIntLikeJava(args[0])};
    return Datum::of(context.netManager->netTextResult(taskId));
}

Datum NetBuiltins::netError(BuiltinContext& context, const std::vector<Datum>& args) {
    if (context.netManager == nullptr) {
        return Datum::of(std::string("OK"));
    }
    const std::optional<int> taskId = args.empty() ? std::nullopt
                                                   : std::optional<int>{toIntLikeJava(args[0])};
    const int error = context.netManager->netError(taskId);
    return error == 0 ? Datum::of(std::string("OK")) : Datum::of(std::to_string(error));
}

Datum NetBuiltins::getStreamStatus(BuiltinContext& context, const std::vector<Datum>& args) {
    if (context.netManager == nullptr) {
        return defaultStreamStatusDatum();
    }
    if (!args.empty() && (args[0].isString() || args[0].isSymbol())) {
        const std::string value = toStringLikeJava(args[0]);
        if (const auto taskId = parseIntStrict(trimCopy(value))) {
            return context.netManager->getStreamStatusDatum(*taskId);
        }
        return context.netManager->getStreamStatusDatum(value);
    }
    const std::optional<int> taskId = args.empty() ? std::nullopt
                                                   : std::optional<int>{toIntLikeJava(args[0])};
    return context.netManager->getStreamStatusDatum(taskId);
}

Datum NetBuiltins::tellStreamStatus(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return boolDatum(context.tellStreamStatusEnabled);
    }
    context.tellStreamStatusEnabled = truthyLikeJava(args[0]);
    return boolDatum(context.tellStreamStatusEnabled);
}

Datum NetBuiltins::gotoNetPage(BuiltinContext& context, const std::vector<Datum>& args) {
    if (context.movieProperties == nullptr || args.empty()) {
        return Datum::FALSE;
    }
    const std::string target = args.size() > 1 ? toStringLikeJava(args[1]) : "";
    context.movieProperties->gotoNetPage(toStringLikeJava(args[0]), target);
    return Datum::TRUE;
}

Datum NetBuiltins::gotoNetMovie(BuiltinContext& context, const std::vector<Datum>& args) {
    if (context.movieProperties == nullptr || args.empty()) {
        return Datum::of(-1);
    }
    return Datum::of(context.movieProperties->gotoNetMovie(toStringLikeJava(args[0])));
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
    if (const auto* str = args[0].asString()) {
        for (const auto& [name, value] : context.externalParams) {
            if (equalsIgnoreCase(name, str->value)) {
                return Datum::of(value);
            }
        }
        return Datum::voidValue();
    }
    if (const auto* value = args[0].asInt()) {
        const int index = value->value;
        if (index >= 1 && index <= static_cast<int>(context.externalParams.size())) {
            return Datum::of(context.externalParams[static_cast<std::size_t>(index - 1)].second);
        }
    }
    return Datum::voidValue();
}

Datum ExternalParamBuiltins::externalParamName(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    if (const auto* str = args[0].asString()) {
        for (const auto& [name, value] : context.externalParams) {
            (void)value;
            if (equalsIgnoreCase(name, str->value)) {
                return Datum::of(name);
            }
        }
        return Datum::voidValue();
    }
    if (const auto* value = args[0].asInt()) {
        const int index = value->value;
        if (index >= 1 && index <= static_cast<int>(context.externalParams.size())) {
            return Datum::of(context.externalParams[static_cast<std::size_t>(index - 1)].first);
        }
    }
    return Datum::voidValue();
}

Datum ExternalParamBuiltins::externalParamCount(BuiltinContext& context, const std::vector<Datum>&) {
    return Datum::of(static_cast<int>(context.externalParams.size()));
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
    const int bitDepth = args.size() >= 3 ? toIntLikeJava(args[2]) : 32;
    if (width <= 0 || height <= 0) {
        return Datum::voidValue();
    }

    auto image = std::make_shared<bitmap::Bitmap>(width, height, bitDepth);
    if (args.size() >= 4) {
        const Datum& paletteArg = args[3];
        bool resolved = false;
        if (paletteArg.isString() || paletteArg.isSymbol()) {
            const std::string name = toStringLikeJava(paletteArg);
            if (const auto* palette = bitmap::Palette::builtInBySymbolName(name)) {
                image->setImagePalette(palette);
                if (auto normalized = bitmap::Palette::normalizeBuiltInSymbolName(name)) {
                    image->setPaletteRefSystemName(*normalized);
                }
                resolved = true;
            }
        }

        if (!resolved && context.imagePaletteResolver) {
            if (auto palette = context.imagePaletteResolver(paletteArg)) {
                if (palette->palette) {
                    image->setImagePalette(palette->palette);
                }
                if (palette->memberRef) {
                    image->setPaletteRefCastMember(palette->memberRef->castLib, palette->memberRef->memberNum());
                }
                if (palette->systemName) {
                    image->setPaletteRefSystemName(*palette->systemName);
                }
            }
        }
    }

    image->fill(0xFFFFFFFFU);
    return Datum::imageRef(std::move(image));
}

Datum ImageBuiltins::importFileInto(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.size() < 2 || !context.importFileIntoHandler) {
        return Datum::FALSE;
    }
    const auto* ref = args[0].asCastMemberRef();
    if (ref == nullptr) {
        return Datum::FALSE;
    }

    const std::string url = toStringLikeJava(args[1]);
    const Datum options = args.size() >= 3 ? args[2] : Datum::voidValue();
    return boolDatum(context.importFileIntoHandler(*ref, url, options));
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
    if (context.movieProperties != nullptr) {
        return boolDatum(context.movieProperties->getMovieProp("soundEnabled").boolValue());
    }
    if (context.soundManager != nullptr) {
        return boolDatum(context.soundManager->isEnabled());
    }
    return Datum::TRUE;
}

Datum SoundBuiltins::handleMethod(BuiltinContext& context,
                                  const Datum::SoundChannel& channel,
                                  std::string_view methodName,
                                  const std::vector<Datum>& args) {
    auto* manager = context.soundManager;
    const int channelNum = channel.channel;

    if (equalsIgnoreCase(methodName, "play")) {
        if (manager != nullptr && !args.empty()) {
            manager->play(channelNum, args[0]);
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "queue")) {
        if (manager != nullptr && !args.empty()) {
            manager->queue(channelNum, args[0]);
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "stop") || equalsIgnoreCase(methodName, "fadeout")) {
        if (manager != nullptr) {
            manager->stop(channelNum);
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "setplaylist")) {
        if (manager != nullptr) {
            manager->setPlaylist(channelNum, args.empty() ? Datum::voidValue() : args[0]);
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "pause") || equalsIgnoreCase(methodName, "resume") ||
        equalsIgnoreCase(methodName, "unpause") || equalsIgnoreCase(methodName, "playfile") ||
        equalsIgnoreCase(methodName, "breakloop") || equalsIgnoreCase(methodName, "rewind") ||
        equalsIgnoreCase(methodName, "fadein") || equalsIgnoreCase(methodName, "fadeto")) {
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "playnext")) {
        if (manager != nullptr) {
            manager->playNext(channelNum);
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "isbusy")) {
        return boolDatum(manager != nullptr && manager->isPlaying(channelNum));
    }
    if (equalsIgnoreCase(methodName, "status")) {
        return Datum::of(manager != nullptr && manager->isPlaying(channelNum) ? 1 : 0);
    }
    if (equalsIgnoreCase(methodName, "elapsedtime") || equalsIgnoreCase(methodName, "currenttime")) {
        return Datum::of(manager != nullptr ? manager->getElapsedTime(channelNum) : 0);
    }
    if (equalsIgnoreCase(methodName, "getplaylist")) {
        return Datum::list(manager != nullptr ? manager->getPlaylist(channelNum) : std::vector<Datum>{});
    }
    if (equalsIgnoreCase(methodName, "volume")) {
        if (!args.empty()) {
            if (manager != nullptr) {
                manager->setVolume(channelNum, toIntLikeJava(args[0]));
            }
            return Datum::voidValue();
        }
        return Datum::of(manager != nullptr ? manager->getVolume(channelNum) : 255);
    }
    if (equalsIgnoreCase(methodName, "pan")) {
        if (!args.empty()) {
            if (manager != nullptr) {
                manager->setPan(channelNum, toIntLikeJava(args[0]));
            }
            return Datum::voidValue();
        }
        return Datum::of(manager != nullptr ? manager->getPan(channelNum) : 0);
    }
    if (equalsIgnoreCase(methodName, "member")) {
        if (!args.empty()) {
            if (manager != nullptr) {
                if (const auto* member = args[0].asCastMemberRef()) {
                    manager->setMember(channelNum, *member);
                }
            }
            return Datum::voidValue();
        }
        if (manager != nullptr) {
            if (auto member = manager->getMember(channelNum)) {
                return Datum::castMemberRef(*member);
            }
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "ilk")) {
        return Datum::symbol("instance");
    }
    return Datum::voidValue();
}

Datum SoundBuiltins::getProperty(BuiltinContext& context,
                                 const Datum::SoundChannel& channel,
                                 std::string_view propName) {
    auto* manager = context.soundManager;
    const int channelNum = channel.channel;

    if (equalsIgnoreCase(propName, "volume")) {
        return Datum::of(manager != nullptr ? manager->getVolume(channelNum) : 255);
    }
    if (equalsIgnoreCase(propName, "pan")) {
        return Datum::of(manager != nullptr ? manager->getPan(channelNum) : 0);
    }
    if (equalsIgnoreCase(propName, "status")) {
        return Datum::of(manager != nullptr && manager->isPlaying(channelNum) ? 1 : 0);
    }
    if (equalsIgnoreCase(propName, "member")) {
        if (manager != nullptr) {
            if (auto member = manager->getMember(channelNum)) {
                return Datum::castMemberRef(*member);
            }
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(propName, "loopcount")) {
        return Datum::of(manager != nullptr ? manager->getLoopCount(channelNum) : 1);
    }
    if (equalsIgnoreCase(propName, "starttime")) {
        return Datum::of(manager != nullptr ? manager->getStartTime(channelNum) : 0);
    }
    if (equalsIgnoreCase(propName, "endtime")) {
        return Datum::of(manager != nullptr ? manager->getEndTime(channelNum) : 0);
    }
    if (equalsIgnoreCase(propName, "loopstarttime")) {
        return Datum::of(manager != nullptr ? manager->getLoopStartTime(channelNum) : 0);
    }
    if (equalsIgnoreCase(propName, "loopendtime")) {
        return Datum::of(manager != nullptr ? manager->getLoopEndTime(channelNum) : 0);
    }
    if (equalsIgnoreCase(propName, "elapsedtime") || equalsIgnoreCase(propName, "currenttime")) {
        return Datum::of(manager != nullptr ? manager->getElapsedTime(channelNum) : 0);
    }
    return Datum::voidValue();
}

bool SoundBuiltins::setProperty(BuiltinContext& context,
                                const Datum::SoundChannel& channel,
                                std::string_view propName,
                                Datum value) {
    if (equalsIgnoreCase(propName, "volume")) {
        if (context.soundManager != nullptr) {
            context.soundManager->setVolume(channel.channel, toIntLikeJava(value));
        }
        return true;
    }
    if (equalsIgnoreCase(propName, "loopcount")) {
        if (context.soundManager != nullptr) {
            context.soundManager->setLoopCount(channel.channel, toIntLikeJava(value));
        }
        return true;
    }
    if (equalsIgnoreCase(propName, "pan")) {
        if (context.soundManager != nullptr) {
            context.soundManager->setPan(channel.channel, toIntLikeJava(value));
        }
        return true;
    }
    if (equalsIgnoreCase(propName, "member")) {
        const auto* member = value.asCastMemberRef();
        if (member == nullptr) {
            return false;
        }
        if (context.soundManager != nullptr) {
            context.soundManager->setMember(channel.channel, *member);
        }
        return true;
    }
    if (equalsIgnoreCase(propName, "starttime")) {
        if (context.soundManager != nullptr) {
            context.soundManager->setStartTime(channel.channel, toIntLikeJava(value));
        }
        return true;
    }
    if (equalsIgnoreCase(propName, "endtime")) {
        if (context.soundManager != nullptr) {
            context.soundManager->setEndTime(channel.channel, toIntLikeJava(value));
        }
        return true;
    }
    if (equalsIgnoreCase(propName, "loopstarttime")) {
        if (context.soundManager != nullptr) {
            context.soundManager->setLoopStartTime(channel.channel, toIntLikeJava(value));
        }
        return true;
    }
    if (equalsIgnoreCase(propName, "loopendtime")) {
        if (context.soundManager != nullptr) {
            context.soundManager->setLoopEndTime(channel.channel, toIntLikeJava(value));
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

    int castLibNumber = -1;
    if (args[0].isInt() || args[0].isFloat()) {
        if (!context.castLibNumberResolver) {
            return Datum::voidValue();
        }
        castLibNumber = context.castLibNumberResolver(toIntLikeJava(args[0]));
    } else if (args[0].isString() || args[0].isSymbol()) {
        if (!context.castLibNameResolver) {
            return Datum::voidValue();
        }
        castLibNumber = context.castLibNameResolver(toStringLikeJava(args[0]));
    } else {
        return Datum::voidValue();
    }

    return castLibRefOrVoid(castLibNumber);
}

Datum CastLibBuiltins::member(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }

    const Datum& memberArg = args[0];
    if (!hasCastProvider(context)) {
        return castMemberRefOrVoid(1, toIntLikeJava(memberArg));
    }

    const int castLibNumber = args.size() > 1 ? resolveCastLibArg(context, args[1]) : 0;

    if (memberArg.isInt() || memberArg.isFloat()) {
        const int memberNumber = toIntLikeJava(memberArg);
        if (memberNumber == 0) {
            return Datum::voidValue();
        }

        const int normalizedMemberNumber = std::abs(memberNumber);
        if (castLibNumber > 0) {
            return context.castMemberResolver ? context.castMemberResolver(castLibNumber, normalizedMemberNumber)
                                              : castMemberRefOrVoid(castLibNumber, normalizedMemberNumber);
        }

        const int encodedCast = (normalizedMemberNumber >> 16) & 0xFFFF;
        const int encodedMember = normalizedMemberNumber & 0xFFFF;
        if (encodedCast > 0 && encodedMember > 0) {
            return context.castMemberResolver ? context.castMemberResolver(encodedCast, encodedMember)
                                              : castMemberRefOrVoid(encodedCast, encodedMember);
        }

        const int totalCasts = context.castLibCountSupplier ? context.castLibCountSupplier() : 0;
        if (context.registryVisibleMemberResolver) {
            for (int castIndex = 1; castIndex <= totalCasts; ++castIndex) {
                if (context.registryVisibleMemberResolver(castIndex, normalizedMemberNumber)) {
                    return context.castMemberResolver ? context.castMemberResolver(castIndex, normalizedMemberNumber)
                                                      : castMemberRefOrVoid(castIndex, normalizedMemberNumber);
                }
            }
        }

        if (context.castMemberExistsResolver) {
            for (int castIndex = 1; castIndex <= totalCasts; ++castIndex) {
                if (context.castMemberExistsResolver(castIndex, normalizedMemberNumber)) {
                    return context.castMemberResolver ? context.castMemberResolver(castIndex, normalizedMemberNumber)
                                                      : castMemberRefOrVoid(castIndex, normalizedMemberNumber);
                }
            }
        }

        return context.castMemberResolver ? context.castMemberResolver(1, normalizedMemberNumber)
                                          : castMemberRefOrVoid(1, normalizedMemberNumber);
    }

    if (memberArg.isString() || memberArg.isSymbol()) {
        if (!context.castMemberNameResolver) {
            return Datum::voidValue();
        }
        Datum found = context.castMemberNameResolver(castLibNumber, toStringLikeJava(memberArg));
        return found.isVoid() ? Datum::voidValue() : found;
    }

    return Datum::voidValue();
}

Datum CastLibBuiltins::field(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::of(std::string());
    }
    if (!context.fieldResolver) {
        return Datum::of(std::string());
    }

    const int castLibNumber = args.size() > 1 ? resolveCastLibArg(context, args[1]) : 0;
    const Datum& fieldArg = args[0];
    if (fieldArg.isString() || fieldArg.isInt()) {
        return context.fieldResolver(fieldArg, castLibNumber);
    }
    return context.fieldResolver(Datum::of(toStringLikeJava(fieldArg)), castLibNumber);
}

Datum CastLibBuiltins::createMember(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.size() < 2 || !context.namedCastMemberCreator) {
        return Datum::voidValue();
    }
    const std::string memberName = toStringLikeJava(args[0]);
    const std::string memberType = args[1].asSymbol() != nullptr ? args[1].asSymbol()->name : toStringLikeJava(args[1]);
    context.scriptResolutionCache.clear();
    return context.namedCastMemberCreator(memberName, memberType);
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
    if (args.empty() || !context.xtraRegisteredResolver) {
        return Datum::voidValue();
    }
    const std::string xtraName = toStringLikeJava(args[0]);
    if (!context.xtraRegisteredResolver(xtraName)) {
        debugPlaybackMessage(context, "Unsupported Xtra: " + xtraName);
        return Datum::voidValue();
    }
    return Datum::xtra(xtraName);
}

Datum XtraBuiltins::createInstance(BuiltinContext& context,
                                   const Datum::Xtra& xtraRef,
                                   const std::vector<Datum>& args) {
    if (!context.xtraInstanceCreator) {
        debugPlaybackMessage(context, "Cannot create Xtra instance without an Xtra manager: " + xtraRef.name);
        return Datum::voidValue();
    }
    return context.xtraInstanceCreator(xtraRef.name, args);
}

Datum XtraBuiltins::callHandler(BuiltinContext& context,
                                const Datum::XtraInstance& instance,
                                std::string_view handlerName,
                                const std::vector<Datum>& args) {
    if (!context.xtraHandler) {
        debugPlaybackMessage(context,
                             "Cannot call Xtra handler without an Xtra manager: " + instance.xtraName +
                                 "." + std::string(handlerName));
        return Datum::voidValue();
    }
    return context.xtraHandler(instance, std::string(handlerName), args);
}

Datum XtraBuiltins::callInstanceGlobalHandler(BuiltinContext& context,
                                              std::string_view handlerName,
                                              const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    const auto* instance = args.front().asXtraInstance();
    if (instance == nullptr) {
        return Datum::voidValue();
    }
    if (!context.xtraHandler) {
        debugPlaybackMessage(context,
                             "Cannot call Xtra handler without an Xtra manager: " + instance->xtraName +
                                 "." + std::string(handlerName));
        return Datum::voidValue();
    }
    std::vector<Datum> methodArgs(args.begin() + 1, args.end());
    return callHandler(context, *instance, handlerName, methodArgs);
}

Datum XtraBuiltins::getProperty(BuiltinContext& context,
                                const Datum::XtraInstance& instance,
                                std::string_view propertyName) {
    if (!context.xtraPropertyGetter) {
        debugPlaybackMessage(context,
                             "Cannot get Xtra property without an Xtra manager: " + instance.xtraName +
                                 "." + std::string(propertyName));
        return Datum::voidValue();
    }
    return context.xtraPropertyGetter(instance, std::string(propertyName));
}

void XtraBuiltins::setProperty(BuiltinContext& context,
                               const Datum::XtraInstance& instance,
                               std::string_view propertyName,
                               const Datum& value) {
    if (context.xtraPropertySetter) {
        context.xtraPropertySetter(instance, std::string(propertyName), value);
    } else {
        debugPlaybackMessage(context,
                             "Cannot set Xtra property without an Xtra manager: " + instance.xtraName +
                                 "." + std::string(propertyName));
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
}

Datum ControlFlowBuiltins::returnValue(BuiltinContext& context, const std::vector<Datum>& args) {
    context.returned = true;
    context.returnValue = args.empty() ? Datum::voidValue() : args[0];
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
    const auto& handlerArgs = context.currentHandlerArgsView != nullptr
                                  ? *context.currentHandlerArgsView
                                  : context.currentHandlerArgs;
    if (index < 0 || index >= static_cast<int>(handlerArgs.size())) {
        return Datum::voidValue();
    }
    return handlerArgs[static_cast<std::size_t>(index)];
}

Datum ControlFlowBuiltins::go(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty() || context.movieProperties == nullptr) {
        return Datum::voidValue();
    }

    const Datum& target = args[0];
    if (const auto* value = target.asInt()) {
        context.movieProperties->goToFrame(value->value);
        return Datum::voidValue();
    }
    if (const auto* symbol = target.asSymbol()) {
        const int frame = context.movieProperties->getMovieProp("frame").intValue();
        if (equalsIgnoreCase(symbol->name, "next")) {
            context.movieProperties->goToFrame(frame + 1);
        } else if (equalsIgnoreCase(symbol->name, "previous")) {
            context.movieProperties->goToFrame(std::max(1, frame - 1));
        } else if (equalsIgnoreCase(symbol->name, "loop")) {
            context.movieProperties->goToFrame(frame);
        } else {
            context.movieProperties->goToLabel(symbol->name);
        }
        return Datum::voidValue();
    }
    if (target.isString()) {
        context.movieProperties->goToLabel(target.stringValue());
        return Datum::voidValue();
    }
    const int frame = toIntLikeJava(target);
    if (frame > 0) {
        context.movieProperties->goToFrame(frame);
    }
    return Datum::voidValue();
}

Datum ControlFlowBuiltins::call(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.size() < 2 || !context.callTargetHandler) {
        return Datum::voidValue();
    }

    const std::string handlerName = args[0].asSymbol() != nullptr ? args[0].asSymbol()->name : toStringLikeJava(args[0]);
    const std::span<const Datum> extraArgs(args.data() + 2, args.size() - 2);
    const Datum& target = args[1];
    Datum lastResult = Datum::voidValue();

    if (target.isList()) {
        const auto snapshot = target.listValue().items();
        for (const auto& item : snapshot) {
            const auto callArgs = snapshotStructArgsForCall(extraArgs);
            lastResult = context.callTargetHandler(item, handlerName, callArgs);
        }
        return lastResult;
    }
    if (target.isPropList()) {
        std::vector<Datum> snapshot;
        snapshot.reserve(target.propListValue().properties().size());
        for (const auto& entry : target.propListValue().properties()) {
            snapshot.push_back(entry.second);
        }
        for (const auto& item : snapshot) {
            const auto callArgs = snapshotStructArgsForCall(extraArgs);
            lastResult = context.callTargetHandler(item, handlerName, callArgs);
        }
        return lastResult;
    }

    const auto callArgs = snapshotStructArgsForCall(extraArgs);
    return context.callTargetHandler(target, handlerName, callArgs);
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
    const bool firstEmpty = isEmptyRect(*first);
    const bool secondEmpty = isEmptyRect(*second);
    if (firstEmpty && secondEmpty) {
        return Datum::intRect(0, 0, 0, 0);
    }
    if (firstEmpty) {
        return args[1];
    }
    if (secondEmpty) {
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

Datum ConstructorBuiltins::newInstance(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }

    const Datum& target = args.front();
    if (const auto* typeSymbol = target.asSymbol();
        typeSymbol != nullptr && args.size() >= 2 && args[1].asCastLibRef() != nullptr) {
        if (context.castMemberCreator) {
            context.scriptResolutionCache.clear();
            return context.castMemberCreator(args[1].asCastLibRef()->castLib, typeSymbol->name);
        }
        return Datum::voidValue();
    }
    if (const auto* typeSymbol = target.asSymbol();
        typeSymbol != nullptr && args.size() >= 2 && args[1].isVoid()) {
        if (context.castMemberCreator) {
            context.scriptResolutionCache.clear();
            return context.castMemberCreator(1, typeSymbol->name);
        }
        return Datum::voidValue();
    }

    const auto constructorArgs = [&args]() {
        return std::vector<Datum>(args.begin() + 1, args.end());
    };
    if (const auto* xtraRef = target.asXtra()) {
        return XtraBuiltins::createInstance(context, *xtraRef, constructorArgs());
    }
    if (context.newInstanceHandler) {
        return context.newInstanceHandler(target, constructorArgs());
    }

    Datum resolvedTarget = target;
    if (target.asScriptRef() == nullptr && target.asCastMemberRef() == nullptr &&
        (target.isString() || target.isSymbol())) {
        const Datum resolvedScript = TypeBuiltins::script(context, {target});
        if (resolvedScript.asScriptRef() != nullptr) {
            resolvedTarget = resolvedScript;
        }
    }

    if (const auto* scriptRef = resolvedTarget.asScriptRef()) {
        return createScriptInstanceFromRef(context, scriptRef->memberRef, constructorArgs());
    }
    if (const auto* memberRef = resolvedTarget.asCastMemberRef()) {
        return createScriptInstanceFromRef(context, *memberRef, constructorArgs());
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
    auto identifierResolver = [&context](std::string_view identifier) {
        if (!context.valueEvaluator) {
            return Datum::voidValue();
        }
        return context.valueEvaluator(Datum::of(std::string(identifier)));
    };
    if (const auto* fieldText = args[0].asFieldText()) {
        if (context.fieldParsedValueResolver) {
            Datum parsed = context.fieldParsedValueResolver(fieldText->castLib, fieldText->memberNum, fieldText->revision);
            if (!parsed.isVoid()) {
                return parsed;
            }
        }
        return LingoValueParser::parseWithPartial(fieldText->value, identifierResolver);
    }
    if (!args[0].isString()) {
        return args[0];
    }

    std::string rawStorage;
    const std::string_view raw = stringViewLikeJava(args[0], rawStorage);
    if (raw.empty()) {
        return Datum::of(std::string());
    }
    if (const auto parsedLiteral = LingoValueParser::parseComplete(raw, identifierResolver);
        parsedLiteral.has_value() && !parsedLiteral->isVoid()) {
        return *parsedLiteral;
    }
    if (context.valueEvaluator) {
        Datum evaluated = context.valueEvaluator(args[0]);
        if (!evaluated.isVoid()) {
            return evaluated;
        }
    }
    Datum parsed = LingoValueParser::parseWithPartial(raw, identifierResolver);
    if (!parsed.isVoid()) {
        return parsed;
    }

    const std::string_view trimmed = trimView(raw);
    if (trimmed.find(' ') != std::string::npos && context.castMemberNameResolver) {
        const Datum memberRef = context.castMemberNameResolver(0, std::string(trimmed));
        if (memberRef.asCastMemberRef() != nullptr) {
            return Datum::of(std::string(trimmed));
        }
    }
    return parsed;
}

Datum TypeBuiltins::script(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }

    const Datum& identifier = args[0];
    std::optional<Datum> scope;
    if (args.size() >= 2) {
        scope = args[1];
    }
    const int scopedCastLib = resolveCastLibScope(context, scope);

    if (const auto* member = identifier.asCastMemberRef()) {
        return Datum::scriptRef(*member);
    }
    if (identifier.isString() || identifier.isSymbol()) {
        const std::string scriptName = keyName(identifier);
        std::string cacheKey = std::to_string(scopedCastLib);
        cacheKey.reserve(cacheKey.size() + 1 + scriptName.size());
        cacheKey.push_back(':');
        appendLowercase(cacheKey, scriptName);
        if (const auto cached = context.scriptResolutionCache.find(cacheKey);
            cached != context.scriptResolutionCache.end()) {
            return cached->second;
        }
        if (context.castMemberNameResolver) {
            const Datum memberRef = context.castMemberNameResolver(scopedCastLib, scriptName);
            const Datum resolved = scriptRefFromCastMemberDatum(memberRef);
            if (!resolved.isVoid()) {
                context.scriptResolutionCache.emplace(cacheKey, resolved);
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
            const id::SlotId slotId(value);
            if (slotId.castLib() >= 1 && slotId.member() >= 1) {
                return Datum::scriptRef(Datum::CastMemberRef{slotId.castLib(), slotId.member()});
            }
            return Datum::voidValue();
        }
        return Datum::scriptRef(Datum::CastMemberRef{1, value});
    }

    if (identifier.isList()) {
        for (const auto& item : identifier.listValue().items()) {
            Datum result = script(context, {item});
            if (!result.isVoid()) {
                return result;
            }
        }
        return Datum::voidValue();
    }

    if (context.scriptResolver) {
        return context.scriptResolver(identifier, scope);
    }
    return Datum::voidValue();
}

Datum TypeBuiltins::ilk(BuiltinContext&, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::symbol("void");
    }

    const std::string typeName = ilkType(args[0]);
    if (args.size() < 2) {
        return Datum::symbol(typeName);
    }

    const std::string checkName = keyName(args[1]);
    if (equalsIgnoreCase(typeName, checkName)) {
        return Datum::TRUE;
    }
    if (equalsIgnoreCase(checkName, "list") &&
        (equalsIgnoreCase(typeName, "proplist") || equalsIgnoreCase(typeName, "rect") ||
         equalsIgnoreCase(typeName, "point"))) {
        return Datum::TRUE;
    }
    if (equalsIgnoreCase(checkName, "linearlist") && equalsIgnoreCase(typeName, "list")) {
        return Datum::TRUE;
    }
    if (equalsIgnoreCase(checkName, "number") &&
        (equalsIgnoreCase(typeName, "integer") || equalsIgnoreCase(typeName, "float"))) {
        return Datum::TRUE;
    }
    if (equalsIgnoreCase(checkName, "object") &&
        (equalsIgnoreCase(typeName, "instance") || equalsIgnoreCase(typeName, "member") ||
         equalsIgnoreCase(typeName, "xtra") || equalsIgnoreCase(typeName, "xtrainstance") ||
         equalsIgnoreCase(typeName, "script") || equalsIgnoreCase(typeName, "castlib") ||
         equalsIgnoreCase(typeName, "sprite") || equalsIgnoreCase(typeName, "stage") ||
         equalsIgnoreCase(typeName, "sound") || equalsIgnoreCase(typeName, "image"))) {
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
        for (const auto& item : args[1].listValue().items()) {
            if (item.type() != DatumType::ScriptInstanceRef) {
                continue;
            }
            std::vector<Datum> nestedArgs;
            nestedArgs.reserve(args.size());
            nestedArgs.push_back(args[0]);
            nestedArgs.push_back(item);
            nestedArgs.insert(nestedArgs.end(), args.begin() + 2, args.end());
            result = callAncestor(context, nestedArgs);
        }
        return result;
    }
    if (args[1].type() != DatumType::ScriptInstanceRef) {
        return Datum::voidValue();
    }
    if (context.ancestorCallHandler) {
        return context.ancestorCallHandler(args);
    }
    return Datum::voidValue();
}

void MovieBuiltins::registerBuiltins(BuiltinRegistry& registry) {
    registry.registerBuiltin("label", MovieBuiltins::label);
    registry.registerBuiltin("marker", MovieBuiltins::marker);
}

Datum MovieBuiltins::label(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty() || context.movieProperties == nullptr) {
        return Datum::of(0);
    }

    const Datum& labelArg = args.size() >= 2 && isMovieRef(args[0]) ? args[1] : args[0];
    return Datum::of(std::max(context.movieProperties->getFrameForLabel(labelArg.stringValue()), 0));
}

Datum MovieBuiltins::marker(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty() || context.movieProperties == nullptr) {
        return Datum::of(0);
    }

    const Datum& markerArg = args.size() >= 2 && isMovieRef(args[0]) ? args[1] : args[0];
    if (markerArg.isString()) {
        return Datum::of(std::max(context.movieProperties->getFrameForLabel(markerArg.stringValue()), 0));
    }
    return Datum::of(std::max(context.movieProperties->getMarkerFrame(markerArg.intValue()), 0));
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
    if (context.movieProperties != nullptr) {
        (void)context.movieProperties->setMovieProp("puppetTempo", Datum::of(toIntLikeJava(args[0])));
    }
    return Datum::voidValue();
}

Datum SpriteBuiltins::puppetSprite(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.size() < 2) {
        return Datum::voidValue();
    }
    if (context.spriteProperties != nullptr) {
        (void)context.spriteProperties->setSpriteProp(toIntLikeJava(args[0]),
                                                      "puppet",
                                                      Datum::of(truthyLikeJava(args[1]) ? 1 : 0));
    }
    return Datum::voidValue();
}

Datum SpriteBuiltins::puppetPalette(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    if (context.puppetPaletteHandler) {
        if (isResetPaletteArg(args[0])) {
            context.puppetPaletteHandler(std::nullopt);
        } else {
            context.puppetPaletteHandler(args[0]);
        }
    }
    return Datum::voidValue();
}

Datum SpriteBuiltins::cursor(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return Datum::voidValue();
    }
    if (context.movieProperties != nullptr) {
        (void)context.movieProperties->setMovieProp("cursor", args[0]);
    }
    return Datum::voidValue();
}

Datum SpriteBuiltins::spriteBox(BuiltinContext& context, const std::vector<Datum>& args) {
    if (args.empty() || context.spriteProperties == nullptr) {
        return Datum::intRect(0, 0, 0, 0);
    }
    return context.spriteProperties->getSpriteProp(toIntLikeJava(args[0]), "rect");
}

} // namespace libreshockwave::lingo::builtin
