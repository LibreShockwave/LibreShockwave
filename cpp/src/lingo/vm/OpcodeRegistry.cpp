#include "libreshockwave/lingo/vm/OpcodeRegistry.hpp"

#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/bitmap/ColorRef.hpp"
#include "libreshockwave/bitmap/Drawing.hpp"
#include "libreshockwave/bitmap/Palette.hpp"
#include "libreshockwave/lingo/vm/dispatch/ImageMethodDispatcher.hpp"
#include "libreshockwave/lingo/vm/dispatch/ListMethodDispatcher.hpp"
#include "libreshockwave/lingo/vm/dispatch/PropListMethodDispatcher.hpp"
#include "libreshockwave/lingo/vm/dispatch/ScriptInstanceMethodDispatcher.hpp"
#include "libreshockwave/lingo/vm/dispatch/SoundChannelMethodDispatcher.hpp"
#include "libreshockwave/lingo/vm/dispatch/StringMethodDispatcher.hpp"
#include "libreshockwave/lingo/vm/PropertyIdMappings.hpp"
#include "libreshockwave/lingo/vm/util/AncestorChainWalker.hpp"
#include "libreshockwave/lingo/vm/util/StringChunkUtils.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <limits>
#include <optional>
#include <queue>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

namespace libreshockwave::lingo::vm {
namespace {

std::function<void()> imageMutationCallback;
void* imageMutationCallbackOwner = nullptr;
std::deque<std::string> imageOperationTrace;
int imageOperationTraceNextId = 1;
constexpr std::size_t kMaxImageOperationTraceEntries = 512;

void appendTraceJsonString(std::ostringstream& out, std::string_view value) {
    out << '"';
    for (const char ch : value) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    out << "\\u00";
                    constexpr char hex[] = "0123456789abcdef";
                    out << hex[(static_cast<unsigned char>(ch) >> 4U) & 0x0FU]
                        << hex[static_cast<unsigned char>(ch) & 0x0FU];
                } else {
                    out << ch;
                }
                break;
        }
    }
    out << '"';
}

void appendTraceRectJson(std::ostringstream& out, const Datum::IntRect* rect) {
    if (rect == nullptr) {
        out << "null";
        return;
    }
    out << "{\"left\":" << rect->left
        << ",\"top\":" << rect->top
        << ",\"right\":" << rect->right
        << ",\"bottom\":" << rect->bottom
        << ",\"width\":" << rect->width()
        << ",\"height\":" << rect->height()
        << '}';
}

int countOpaqueRgb(const bitmap::Bitmap& bitmap, std::uint32_t rgb) {
    int count = 0;
    for (const auto pixel : bitmap.pixels()) {
        if (((pixel >> 24U) & 0xFFU) != 0 && (pixel & 0x00FFFFFFU) == rgb) {
            ++count;
        }
    }
    return count;
}

void appendTraceBitmapSummary(std::ostringstream& out, const char* key, const bitmap::Bitmap& bitmap) {
    int minX = bitmap.width();
    int minY = bitmap.height();
    int maxX = -1;
    int maxY = -1;
    int opaquePixels = 0;
    int transparentPixels = 0;
    for (int y = 0; y < bitmap.height(); ++y) {
        for (int x = 0; x < bitmap.width(); ++x) {
            const auto pixel = bitmap.getPixel(x, y);
            const int alpha = static_cast<int>((pixel >> 24U) & 0xFFU);
            if (alpha == 0) {
                ++transparentPixels;
                continue;
            }
            ++opaquePixels;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    out << ",\"" << key << "\":{\"width\":" << bitmap.width()
        << ",\"height\":" << bitmap.height()
        << ",\"bitDepth\":" << bitmap.bitDepth()
        << ",\"nativeAlpha\":" << (bitmap.isNativeAlpha() ? "true" : "false")
        << ",\"scriptModified\":" << (bitmap.isScriptModified() ? "true" : "false")
        << ",\"paletteIndices\":" << (bitmap.paletteIndices().has_value() ? "true" : "false")
        << ",\"opaquePixels\":" << opaquePixels
        << ",\"transparentPixels\":" << transparentPixels;
    if (maxX >= 0) {
        out << ",\"alphaBounds\":{\"left\":" << minX
            << ",\"top\":" << minY
            << ",\"right\":" << (maxX + 1)
            << ",\"bottom\":" << (maxY + 1)
            << '}';
    } else {
        out << ",\"alphaBounds\":null";
    }
    out << ",\"colors\":{\"black\":" << countOpaqueRgb(bitmap, 0x000000U)
        << ",\"white\":" << countOpaqueRgb(bitmap, 0xFFFFFFU)
        << ",\"cyan669999\":" << countOpaqueRgb(bitmap, 0x669999U)
        << ",\"gray777777\":" << countOpaqueRgb(bitmap, 0x777777U)
        << "}}";
}

void pushImageOperationTrace(std::string entry) {
    if (imageOperationTrace.size() >= kMaxImageOperationTraceEntries) {
        imageOperationTrace.pop_front();
    }
    imageOperationTrace.push_back(std::move(entry));
}

Datum literalToDatum(const chunks::ScriptChunk::LiteralEntry& literal) {
    switch (literal.type) {
        case 1:
            if (const auto* value = std::get_if<std::string>(&literal.value)) {
                return Datum::of(*value);
            }
            break;
        case 4:
            if (const auto* value = std::get_if<int>(&literal.value)) {
                return Datum::of(*value);
            }
            break;
        case 9:
            return Datum::of(literal.numericValue);
        default:
            break;
    }
    return Datum::voidValue();
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

std::string lowerAscii(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

void appendLowerAscii(std::string& target, std::string_view value) {
    target.reserve(target.size() + value.size());
    for (const unsigned char ch : value) {
        target.push_back(static_cast<char>(std::tolower(ch)));
    }
}

std::string_view trimView(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

std::string trimCopy(std::string_view value) {
    value = trimView(value);
    return std::string(value);
}

std::optional<int> parseIntStrictView(std::string_view value) {
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

std::optional<long long> parseLongStrictView(std::string_view value, int base) {
    value = trimView(value);
    if (value.empty()) {
        return std::nullopt;
    }

    long long result = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto parsed = std::from_chars(begin, end, result, base);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return std::nullopt;
    }
    return result;
}

bool couldBeStrictInteger(std::string_view value) {
    value = trimView(value);
    if (value.empty()) {
        return false;
    }
    const char first = value.front();
    return std::isdigit(static_cast<unsigned char>(first)) || first == '-' || first == '+';
}

std::optional<int> parseIntStrict(std::string_view value) {
    return parseIntStrictView(value);
}

std::optional<long long> parseLongStrict(std::string_view value, int base) {
    return parseLongStrictView(value, base);
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

std::string toStringLikeJava(const Datum& datum);

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
    if (const auto directValue = directStringViewLikeJava(datum)) {
        return *directValue;
    }
    storage = toStringLikeJava(datum);
    return storage;
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

bool containsIgnoreCase(std::string_view haystack, std::string_view needle) {
    if (needle.empty() || needle.size() > haystack.size()) {
        return false;
    }
    for (std::size_t offset = 0; offset + needle.size() <= haystack.size(); ++offset) {
        bool matches = true;
        for (std::size_t index = 0; index < needle.size(); ++index) {
            const auto left = static_cast<unsigned char>(haystack[offset + index]);
            const auto right = static_cast<unsigned char>(needle[index]);
            if (std::tolower(left) != std::tolower(right)) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

bool startsWithIgnoreCase(std::string_view haystack, std::string_view needle) {
    if (needle.empty() || needle.size() > haystack.size()) {
        return false;
    }
    for (std::size_t index = 0; index < needle.size(); ++index) {
        const auto left = static_cast<unsigned char>(haystack[index]);
        const auto right = static_cast<unsigned char>(needle[index]);
        if (std::tolower(left) != std::tolower(right)) {
            return false;
        }
    }
    return true;
}

int packedColor(const Datum::ColorRef& color) {
    return (color.r << 16) | (color.g << 8) | color.b;
}

std::uint32_t imageColorArgb(const Datum& datum) {
    if (const auto* color = datum.asColorRef()) {
        return 0xFF000000U |
               (static_cast<std::uint32_t>(color->r & 0xFF) << 16) |
               (static_cast<std::uint32_t>(color->g & 0xFF) << 8) |
               static_cast<std::uint32_t>(color->b & 0xFF);
    }
    if (const auto* value = datum.asInt()) {
        if (value->value > 255) {
            return 0xFF000000U | (static_cast<std::uint32_t>(value->value) & 0x00FFFFFFU);
        }
        const auto gray = static_cast<std::uint32_t>((255 - value->value) & 0xFF);
        return 0xFF000000U | (gray << 16) | (gray << 8) | gray;
    }
    if (datum.isString()) {
        std::string storage;
        std::string_view value = trimView(stringViewLikeJava(datum, storage));
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
            value = trimView(value.substr(1, value.size() - 2));
        }
        if (!value.empty() && value.front() == '#') {
            value.remove_prefix(1);
        }
        if (value.size() == 6) {
            try {
                return 0xFF000000U |
                       (static_cast<std::uint32_t>(std::stoul(std::string(value), nullptr, 16)) & 0x00FFFFFFU);
            } catch (...) {
            }
        }
    }
    return 0xFF000000U;
}

std::uint32_t imageColorArgb(const Datum& datum, const bitmap::Bitmap* target) {
    if (const auto* color = datum.asColorRef();
        color != nullptr && color->paletteIndex.has_value() && target != nullptr) {
        return 0xFF000000U | (target->resolvePaletteIndex(*color->paletteIndex, nullptr) & 0x00FFFFFFU);
    }
    if (const auto* value = datum.asInt(); value != nullptr &&
        target != nullptr &&
        target->imagePalette() != nullptr &&
        value->value >= 0 &&
        value->value <= 255) {
        return 0xFF000000U | (target->resolvePaletteIndex(value->value, nullptr) & 0x00FFFFFFU);
    }
    return imageColorArgb(datum);
}

std::optional<int> imageFillPaletteIndex(const Datum& colorDatum, const bitmap::Bitmap& target) {
    if (target.bitDepth() > 8 || target.imagePalette() == nullptr) {
        return std::nullopt;
    }
    if (const auto* color = colorDatum.asColorRef(); color != nullptr && color->paletteIndex.has_value()) {
        return *color->paletteIndex & 0xFF;
    }
    if (const auto* value = colorDatum.asInt(); value != nullptr && value->value >= 0 && value->value <= 255) {
        return value->value;
    }
    return std::nullopt;
}

std::uint32_t imageCopyPixelsRemapColorArgb(const Datum& datum,
                                            const bitmap::Bitmap& src,
                                            const bitmap::Bitmap& dest) {
    const auto* target = &dest;
    if (const auto* color = datum.asColorRef();
        color != nullptr && color->paletteIndex.has_value() && src.imagePalette() != nullptr) {
        target = &src;
    }
    return imageColorArgb(datum, target);
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
    if (const auto* field = datum.asFieldText()) {
        return parseIntStrict(field->value).value_or(0);
    }
    if (const auto* chunk = datum.asStringChunk()) {
        return parseIntStrict(chunk->value).value_or(0);
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
    if (const auto* field = datum.asFieldText()) {
        return parseDoubleStrict(field->value).value_or(0.0);
    }
    if (const auto* chunk = datum.asStringChunk()) {
        return parseDoubleStrict(chunk->value).value_or(0.0);
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

int javaRoundToInt(double value) {
    return static_cast<int>(std::floor(value + 0.5));
}

bool isFloatLike(const Datum& datum) {
    return datum.asFloat() != nullptr;
}

Datum numericResult(const Datum& a, const Datum& b, double value) {
    if (isFloatLike(a) || isFloatLike(b)) {
        return Datum::of(value);
    }
    return Datum::of(static_cast<int>(value));
}

Datum::IntPoint listAsPointDelta(const Datum::List& list) {
    if (list.items().size() >= 2) {
        return Datum::IntPoint{toIntLikeJava(list.items()[0]), toIntLikeJava(list.items()[1])};
    }
    return Datum::IntPoint{0, 0};
}

Datum::IntRect listAsRectDelta(const Datum::List& list) {
    if (list.items().size() >= 4) {
        return Datum::IntRect{toIntLikeJava(list.items()[0]),
                              toIntLikeJava(list.items()[1]),
                              toIntLikeJava(list.items()[2]),
                              toIntLikeJava(list.items()[3])};
    }
    return Datum::IntRect{0, 0, 0, 0};
}

Datum scaleList(const Datum::List& list, double scalar, bool scalarIsFloat) {
    std::vector<Datum> result;
    result.reserve(list.items().size());
    for (const auto& item : list.items()) {
        if (isFloatLike(item) || scalarIsFloat) {
            result.push_back(Datum::of(toDoubleLikeJava(item) * scalar));
        } else {
            result.push_back(Datum::of(static_cast<int>(toIntLikeJava(item) * scalar)));
        }
    }
    return Datum::list(std::move(result));
}

Datum addScalarToList(const Datum::List& list, const Datum& scalarDatum) {
    const double scalar = toDoubleLikeJava(scalarDatum);
    std::vector<Datum> result;
    result.reserve(list.items().size());
    for (const auto& item : list.items()) {
        result.push_back(numericResult(item, scalarDatum, toDoubleLikeJava(item) + scalar));
    }
    return Datum::list(std::move(result));
}

Datum subtractScalarFromList(const Datum::List& list, const Datum& scalarDatum) {
    const double scalar = toDoubleLikeJava(scalarDatum);
    std::vector<Datum> result;
    result.reserve(list.items().size());
    for (const auto& item : list.items()) {
        result.push_back(numericResult(item, scalarDatum, toDoubleLikeJava(item) - scalar));
    }
    return Datum::list(std::move(result));
}

Datum subtractListFromScalar(const Datum& scalarDatum, const Datum::List& list) {
    const double scalar = toDoubleLikeJava(scalarDatum);
    std::vector<Datum> result;
    result.reserve(list.items().size());
    for (const auto& item : list.items()) {
        result.push_back(numericResult(scalarDatum, item, scalar - toDoubleLikeJava(item)));
    }
    return Datum::list(std::move(result));
}

Datum divideList(const Datum::List& list, const Datum& divisor) {
    const double scalar = toDoubleLikeJava(divisor);
    const bool divisorIsFloat = isFloatLike(divisor);
    const int intDivisor = divisorIsFloat ? 0 : toIntLikeJava(divisor);
    std::vector<Datum> result;
    result.reserve(list.items().size());
    for (const auto& item : list.items()) {
        if (isFloatLike(item) || divisorIsFloat) {
            result.push_back(Datum::of(toDoubleLikeJava(item) / scalar));
        } else {
            result.push_back(Datum::of(toIntLikeJava(item) / intDivisor));
        }
    }
    return Datum::list(std::move(result));
}

Datum modList(const Datum::List& list, int divisor) {
    std::vector<Datum> result;
    result.reserve(list.items().size());
    for (const auto& item : list.items()) {
        result.push_back(Datum::of(toIntLikeJava(item) % divisor));
    }
    return Datum::list(std::move(result));
}

bool lingoEquals(const Datum& a, const Datum& b) {
    if ((a.isVoid() && b.isNumber()) || (a.isNumber() && b.isVoid()) || (a.isNumber() && b.isNumber())) {
        return toDoubleLikeJava(a) == toDoubleLikeJava(b);
    }
    if ((a.isString() || a.isSymbol()) && (b.isString() || b.isSymbol())) {
        std::string lhsStorage;
        std::string rhsStorage;
        const std::string_view lhs = stringViewLikeJava(a, lhsStorage);
        const std::string_view rhs = stringViewLikeJava(b, rhsStorage);
        return equalsIgnoreCase(lhs, rhs) ||
               ((equalsIgnoreCase(lhs, "field") && equalsIgnoreCase(rhs, "text")) ||
                (equalsIgnoreCase(lhs, "text") && equalsIgnoreCase(rhs, "field")));
    }
    return a == b;
}

std::vector<Datum> argListItems(const Datum& datum) {
    if (datum.type() == DatumType::ArgList) {
        return datum.argListValue().args();
    }
    if (datum.type() == DatumType::ArgListNoRet) {
        return datum.argListNoRetValue().args();
    }
    return {};
}

const std::vector<Datum>* argListItemsPtr(const Datum& datum) {
    if (datum.type() == DatumType::ArgList) {
        return &datum.argListValue().args();
    }
    if (datum.type() == DatumType::ArgListNoRet) {
        return &datum.argListNoRetValue().args();
    }
    return nullptr;
}

const std::vector<Datum>& argListItemsRef(const Datum& datum, std::vector<Datum>& storage) {
    if (const auto* args = argListItemsPtr(datum)) {
        return *args;
    }
    storage = argListItems(datum);
    return storage;
}

std::string keyNameLikeJava(const Datum& datum) {
    if (const auto* symbol = datum.asSymbol()) {
        return symbol->name;
    }
    return toStringLikeJava(datum);
}

std::string_view keyNameLikeJavaView(const Datum& datum, std::string& storage) {
    if (const auto* symbol = datum.asSymbol()) {
        return symbol->name;
    }
    if (const auto* string = datum.asString()) {
        return string->value;
    }
    if (const auto* field = datum.asFieldText()) {
        return field->value;
    }
    storage = toStringLikeJava(datum);
    return storage;
}

std::optional<Datum> builtinConstant(std::string_view name) {
    if (equalsIgnoreCase(name, "pi")) return Datum::of(3.14159265358979323846);
    if (equalsIgnoreCase(name, "true")) return Datum::TRUE;
    if (equalsIgnoreCase(name, "false")) return Datum::FALSE;
    if (equalsIgnoreCase(name, "void")) return Datum::voidValue();
    if (equalsIgnoreCase(name, "empty") || equalsIgnoreCase(name, "emptystring")) return Datum::of(std::string());
    if (equalsIgnoreCase(name, "return")) return Datum::of(std::string("\r"));
    if (equalsIgnoreCase(name, "enter")) return Datum::of(std::string("\n"));
    if (equalsIgnoreCase(name, "tab")) return Datum::of(std::string("\t"));
    if (equalsIgnoreCase(name, "quote")) return Datum::of(std::string("\""));
    if (equalsIgnoreCase(name, "backspace")) return Datum::of(std::string("\b"));
    if (equalsIgnoreCase(name, "space")) return Datum::of(std::string(" "));
    return std::nullopt;
}

std::string ilkTypeName(const Datum& datum) {
    switch (datum.type()) {
        case DatumType::Int: return "integer";
        case DatumType::Float: return "float";
        case DatumType::String:
        case DatumType::StringChunk: return "string";
        case DatumType::Symbol: return "symbol";
        case DatumType::List: return "list";
        case DatumType::PropList: return "propList";
        case DatumType::ScriptInstanceRef: return "instance";
        case DatumType::IntPoint: return "point";
        case DatumType::IntRect: return "rect";
        case DatumType::ColorRef: return "color";
        case DatumType::Media: return "media";
        case DatumType::ImageRef: return "image";
        case DatumType::CastLibRef: return "castLib";
        case DatumType::CastMemberRef: return "member";
        case DatumType::SpriteRef: return "sprite";
        case DatumType::SoundChannel: return "sound";
        case DatumType::TimeoutRef: return "timeout";
        case DatumType::Xtra: return "xtra";
        case DatumType::XtraInstance: return "xtraInstance";
        case DatumType::Void: return "void";
        case DatumType::Null: return "null";
        default: return std::string(typeName(datum.type()));
    }
}

Datum getPropListProp(const Datum::PropList& propList, std::string_view propName) {
    if (equalsIgnoreCase(propName, "count") || equalsIgnoreCase(propName, "length")) {
        return Datum::of(propList.count());
    }
    for (const auto& entry : propList.properties()) {
        std::string keyStorage;
        if (equalsIgnoreCase(keyNameLikeJavaView(entry.first, keyStorage), propName)) {
            return entry.second;
        }
    }
    if (equalsIgnoreCase(propName, "ilk")) {
        return Datum::symbol("propList");
    }
    return Datum::voidValue();
}

Datum getPropListKey(const Datum::PropList& propList, std::string_view keyName) {
    for (const auto& entry : propList.properties()) {
        std::string keyStorage;
        if (equalsIgnoreCase(keyNameLikeJavaView(entry.first, keyStorage), keyName)) {
            return entry.second;
        }
    }
    return Datum::voidValue();
}

bool isCallMessageStruct(const Datum::PropList& propList) {
    if (getPropListKey(propList, "connection").type() != DatumType::ScriptInstanceRef) {
        return false;
    }

    const Datum ilk = getPropListKey(propList, "ilk");
    if (const auto* symbol = ilk.asSymbol(); symbol != nullptr && equalsIgnoreCase(symbol->name, "struct")) {
        return true;
    }

    return !getPropListKey(propList, "subject").isVoid() &&
           !getPropListKey(propList, "content").isVoid();
}

Datum snapshotCallArg(const Datum& arg) {
    if (!arg.isPropList() || !isCallMessageStruct(arg.propListValue())) {
        return arg;
    }
    return arg.deepCopy();
}

std::vector<Datum> snapshotCallArgsFromStack(ExecutionContext& context, int argCount) {
    std::vector<Datum> callArgs;
    if (argCount <= 2) {
        return callArgs;
    }
    callArgs.reserve(static_cast<std::size_t>(argCount - 2));
    for (int index = 2; index < argCount; ++index) {
        callArgs.push_back(snapshotCallArg(context.peekRef(argCount - 1 - index)));
    }
    return callArgs;
}

void putPropListProp(Datum::PropList& propList, std::string_view propName, Datum value) {
    for (auto& entry : propList.properties()) {
        std::string keyStorage;
        if (equalsIgnoreCase(keyNameLikeJavaView(entry.first, keyStorage), propName)) {
            entry.second = std::move(value);
            return;
        }
    }
    propList.appendProperty(Datum::symbol(std::string(propName)), std::move(value));
}

std::vector<std::string> splitLines(std::string_view value);
int countLines(std::string_view value);

Datum getStringProp(std::string_view value, std::string_view propName) {
    if (equalsIgnoreCase(propName, "length")) {
        return Datum::of(static_cast<int>(value.size()));
    }
    if (equalsIgnoreCase(propName, "linecount")) {
        return Datum::of(countLines(value));
    }
    if (equalsIgnoreCase(propName, "line")) {
        const auto chunks = splitLines(value);
        std::vector<Datum> lines;
        lines.reserve(chunks.size());
        for (const auto& line : chunks) {
            lines.push_back(Datum::of(line));
        }
        return Datum::list(std::move(lines));
    }
    if (equalsIgnoreCase(propName, "ilk")) {
        return Datum::symbol("string");
    }
    return Datum::voidValue();
}

Datum getListProp(const Datum::List& list, std::string_view propName) {
    if (equalsIgnoreCase(propName, "count") || equalsIgnoreCase(propName, "length")) {
        return Datum::of(list.count());
    }
    if (equalsIgnoreCase(propName, "ilk")) {
        return Datum::symbol("list");
    }
    if (const auto index = parseIntStrict(propName)) {
        if (*index >= 1 && *index <= list.count()) {
            return list.getAt(*index);
        }
    }
    return Datum::voidValue();
}

Datum getPointProp(const Datum::IntPoint& point, std::string_view propName) {
    if (equalsIgnoreCase(propName, "loch") || equalsIgnoreCase(propName, "x")) return Datum::of(point.x);
    if (equalsIgnoreCase(propName, "locv") || equalsIgnoreCase(propName, "y")) return Datum::of(point.y);
    if (equalsIgnoreCase(propName, "ilk")) return Datum::symbol("point");
    return Datum::voidValue();
}

void setPointProp(Datum::IntPoint& point, std::string_view propName, const Datum& value) {
    const int newValue = toIntLikeJava(value);
    if (equalsIgnoreCase(propName, "loch") || equalsIgnoreCase(propName, "x")) {
        point.x = newValue;
    } else if (equalsIgnoreCase(propName, "locv") || equalsIgnoreCase(propName, "y")) {
        point.y = newValue;
    }
}

Datum getRectProp(const Datum::IntRect& rect, std::string_view propName) {
    if (equalsIgnoreCase(propName, "left")) return Datum::of(rect.left);
    if (equalsIgnoreCase(propName, "top")) return Datum::of(rect.top);
    if (equalsIgnoreCase(propName, "right")) return Datum::of(rect.right);
    if (equalsIgnoreCase(propName, "bottom")) return Datum::of(rect.bottom);
    if (equalsIgnoreCase(propName, "width")) return Datum::of(rect.width());
    if (equalsIgnoreCase(propName, "height")) return Datum::of(rect.height());
    if (equalsIgnoreCase(propName, "ilk")) return Datum::symbol("rect");
    return Datum::voidValue();
}

void setRectProp(Datum::IntRect& rect, std::string_view propName, const Datum& value) {
    const int newValue = toIntLikeJava(value);
    if (equalsIgnoreCase(propName, "left")) {
        rect.left = newValue;
    } else if (equalsIgnoreCase(propName, "top")) {
        rect.top = newValue;
    } else if (equalsIgnoreCase(propName, "right")) {
        rect.right = newValue;
    } else if (equalsIgnoreCase(propName, "bottom")) {
        rect.bottom = newValue;
    }
}

Datum getColorProp(const Datum::ColorRef& color, std::string_view propName) {
    if (color.paletteIndex.has_value()) {
        if (equalsIgnoreCase(propName, "paletteindex")) return Datum::of(*color.paletteIndex);
        if (equalsIgnoreCase(propName, "ilk")) return Datum::symbol("color");
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(propName, "red")) return Datum::of(color.r);
    if (equalsIgnoreCase(propName, "green")) return Datum::of(color.g);
    if (equalsIgnoreCase(propName, "blue")) return Datum::of(color.b);
    if (equalsIgnoreCase(propName, "ilk")) return Datum::symbol("color");
    return Datum::voidValue();
}

Datum getImageProp(const Datum::ImageRef& image, std::string_view propName) {
    const auto& bitmap = image.bitmap;
    if (equalsIgnoreCase(propName, "rect")) {
        return bitmap != nullptr ? Datum::intRect(0, 0, bitmap->width(), bitmap->height()) : Datum::intRect(0, 0, 0, 0);
    }
    if (equalsIgnoreCase(propName, "width")) return Datum::of(bitmap != nullptr ? bitmap->width() : 0);
    if (equalsIgnoreCase(propName, "height")) return Datum::of(bitmap != nullptr ? bitmap->height() : 0);
    if (equalsIgnoreCase(propName, "depth")) return Datum::of(bitmap != nullptr ? bitmap->bitDepth() : 0);
    if (equalsIgnoreCase(propName, "usealpha")) return bitmap != nullptr && bitmap->isNativeAlpha() ? Datum::TRUE : Datum::FALSE;
    if (equalsIgnoreCase(propName, "ilk")) return Datum::symbol("image");
    if (equalsIgnoreCase(propName, "image")) return Datum::imageRef(bitmap, image.mutationCallback);
    if (equalsIgnoreCase(propName, "paletteref") && bitmap != nullptr) {
        if (bitmap->paletteRefCastLib() >= 1 && bitmap->paletteRefMemberNum() >= 1) {
            return Datum::castMemberRef(id::CastLibId(bitmap->paletteRefCastLib()), id::MemberId(bitmap->paletteRefMemberNum()));
        }
        if (bitmap->paletteRefSystemName()) {
            return Datum::symbol(*bitmap->paletteRefSystemName());
        }
    }
    return Datum::voidValue();
}

void notifyImageMutation(bitmap::Bitmap& bmp) {
    bmp.markScriptModified();
    if (imageMutationCallback) {
        imageMutationCallback();
    }
}

void notifyImageMutation(const Datum::ImageRef& image) {
    if (image.bitmap == nullptr) {
        return;
    }
    notifyImageMutation(*image.bitmap);
    if (image.mutationCallback) {
        image.mutationCallback(*image.bitmap);
    }
}

bool applyImagePaletteProperty(bitmap::Bitmap& bmp, const Datum& value, builtin::BuiltinContext* builtinContext) {
    bool resolved = false;
    if (value.isString() || value.isSymbol()) {
        const std::string name = toStringLikeJava(value);
        const auto normalizedName = bitmap::Palette::normalizeBuiltInSymbolName(name);
        const bitmap::Palette* palette = bitmap::Palette::builtInBySymbolName(name);
        if (palette != nullptr && normalizedName) {
            if (bmp.bitDepth() <= 8) {
                (void)bmp.remapImagePalette(palette);
            } else {
                bmp.setImagePalette(palette);
            }
            bmp.setPaletteRefSystemName(*normalizedName);
            resolved = true;
        }
    }

    if (!resolved && builtinContext != nullptr && builtinContext->imagePaletteResolver) {
        if (auto palette = builtinContext->imagePaletteResolver(value); palette && palette->palette) {
            if (bmp.bitDepth() <= 8) {
                (void)bmp.remapImagePalette(palette->palette);
            } else {
                bmp.setImagePalette(palette->palette);
            }
            if (palette->memberRef) {
                bmp.setPaletteRefCastMember(palette->memberRef->castLib, palette->memberRef->memberNum());
            } else if (palette->systemName) {
                bmp.setPaletteRefSystemName(*palette->systemName);
            }
            resolved = true;
        }
    }

    return resolved;
}

void setImageProp(builtin::BuiltinContext* builtinContext, const Datum::ImageRef& image, std::string_view propName, const Datum& value) {
    if (image.bitmap == nullptr) {
        return;
    }
    auto& bitmap = *image.bitmap;
    if (equalsIgnoreCase(propName, "usealpha")) {
        bitmap.setNativeAlpha(truthy(value));
        notifyImageMutation(image);
        return;
    }
    if (equalsIgnoreCase(propName, "paletteref")) {
        if (applyImagePaletteProperty(bitmap, value, builtinContext)) {
            notifyImageMutation(image);
        }
    }
}

Datum getCastMemberProp(ExecutionContext& context, const Datum::CastMemberRef& member, std::string_view propName) {
    auto* builtinContext = context.builtinContext();
    auto invalidRef = [&]() {
        return member.castMember <= 0 ||
               (builtinContext != nullptr && builtinContext->castMemberExistsResolver &&
                !builtinContext->castMemberExistsResolver(member.castLib, member.memberNum()));
    };
    if (equalsIgnoreCase(propName, "number")) {
        return invalidRef() ? Datum::of(0) : Datum::of((member.castLib << 16) | (member.castMember & 0xFFFF));
    }
    if (equalsIgnoreCase(propName, "membernum")) {
        return invalidRef() ? Datum::of(0) : Datum::of(member.memberNum());
    }
    if (equalsIgnoreCase(propName, "castlibnum")) {
        return Datum::of(member.castLib);
    }
    if (equalsIgnoreCase(propName, "castlib")) {
        return member.castLib >= 1 ? Datum::castLibRef(id::CastLibId(member.castLib)) : Datum::voidValue();
    }
    if (builtinContext != nullptr && builtinContext->castMemberPropertyGetter) {
        return builtinContext->castMemberPropertyGetter(member.castLib, member.memberNum(), std::string(propName));
    }
    return Datum::voidValue();
}

Datum getCastLibMemberAccessorValue(ExecutionContext& context,
                                    const Datum::CastLibMemberAccessor& accessor,
                                    const Datum& keyOrIndex) {
    auto* builtinContext = context.builtinContext();
    if (builtinContext == nullptr) {
        return Datum::voidValue();
    }
    if (keyOrIndex.isInt() || keyOrIndex.isFloat()) {
        return builtinContext->castMemberResolver ? builtinContext->castMemberResolver(accessor.castLib,
                                                                                       toIntLikeJava(keyOrIndex))
                                                  : Datum::voidValue();
    }
    return builtinContext->castMemberNameResolver
               ? builtinContext->castMemberNameResolver(accessor.castLib, toStringLikeJava(keyOrIndex))
               : Datum::voidValue();
}

Datum getObjectProperty(ExecutionContext& context, const Datum& object, std::string_view propName);

Datum getChainedObjectProperty(ExecutionContext& context, const Datum& object, std::string_view propName) {
    auto* builtinContext = context.builtinContext();
    if (object.type() == DatumType::ScriptInstanceRef) {
        if (couldBeStrictInteger(propName) && parseIntStrictView(propName).has_value()) {
            return Datum::voidValue();
        }
        return util::getProperty(object.scriptInstanceValue(), propName);
    }
    if (object.isList()) {
        const auto numericIndex = parseIntStrictView(propName);
        if (numericIndex.has_value() && *numericIndex >= 1 && *numericIndex <= object.listValue().count()) {
            return object.listValue().getAt(*numericIndex);
        }
        return numericIndex.has_value() ? Datum::voidValue() : getListProp(object.listValue(), propName);
    }
    if (object.isPropList()) {
        return getPropListProp(object.propListValue(), propName);
    }
    if (const auto* str = object.asString()) {
        return propName == "length" ? Datum::of(static_cast<int>(str->value.size())) : Datum::voidValue();
    }
    if (object.asFieldText() != nullptr) {
        return Datum::voidValue();
    }
    if (const auto* point = object.asIntPoint()) {
        return equalsIgnoreCase(propName, "ilk") ? Datum::voidValue() : getPointProp(*point, propName);
    }
    if (const auto* rect = object.asIntRect()) {
        return equalsIgnoreCase(propName, "ilk") ? Datum::voidValue() : getRectProp(*rect, propName);
    }
    if (const auto* color = object.asColorRef()) {
        if (color->paletteIndex.has_value()) {
            return Datum::voidValue();
        }
        return equalsIgnoreCase(propName, "ilk") ? Datum::voidValue() : getColorProp(*color, propName);
    }
    if (const auto* accessor = object.asCastLibMemberAccessor()) {
        const auto numericIndex = parseIntStrictView(propName);
        return numericIndex.has_value()
                   ? getCastLibMemberAccessorValue(context, *accessor, Datum::of(*numericIndex))
                   : getCastLibMemberAccessorValue(context, *accessor, Datum::of(std::string(propName)));
    }
    if (const auto* sprite = object.asSpriteRef()) {
        return builtinContext != nullptr && builtinContext->spriteProperties != nullptr
                   ? builtinContext->spriteProperties->getSpriteProp(sprite->channel, propName)
                   : Datum::voidValue();
    }
    if (const auto* value = object.asInt(); value != nullptr) {
        if (!equalsIgnoreCase(propName, "ilk") && builtinContext != nullptr && builtinContext->spriteProperties != nullptr) {
            return builtinContext->spriteProperties->getSpriteProp(value->value, propName);
        }
        return equalsIgnoreCase(propName, "ilk") ? Datum::symbol(ilkTypeName(object)) : Datum::voidValue();
    }
    return getObjectProperty(context, object, propName);
}

Datum getObjectProperty(ExecutionContext& context, const Datum& object, std::string_view propName) {
    if (object.isVoid()) {
        return Datum::voidValue();
    }
    auto* builtinContext = context.builtinContext();
    if (object.type() == DatumType::MovieRef) {
        return builtinContext != nullptr && builtinContext->movieProperties != nullptr
                   ? builtinContext->movieProperties->getMovieProp(propName)
                   : Datum::voidValue();
    }
    if (object.type() == DatumType::PlayerRef) {
        if (const auto constant = builtinConstant(propName)) {
            return *constant;
        }
        return builtinContext != nullptr && builtinContext->movieProperties != nullptr
                   ? builtinContext->movieProperties->getMovieProp(propName)
                   : Datum::voidValue();
    }
    if (object.type() == DatumType::StageRef) {
        return builtinContext != nullptr && builtinContext->movieProperties != nullptr
                   ? builtinContext->movieProperties->getStageProp(propName)
                   : Datum::voidValue();
    }
    if (const auto* castLib = object.asCastLibRef()) {
        if (equalsIgnoreCase(propName, "member")) {
            return Datum::castLibMemberAccessor(id::CastLibId(castLib->castLib));
        }
        return builtinContext != nullptr && builtinContext->castLibPropertyGetter
                   ? builtinContext->castLibPropertyGetter(castLib->castLib, std::string(propName))
                   : Datum::voidValue();
    }
    if (const auto* accessor = object.asCastLibMemberAccessor()) {
        return getCastLibMemberAccessorValue(context, *accessor, Datum::of(std::string(propName)));
    }
    if (const auto* sprite = object.asSpriteRef()) {
        return builtinContext != nullptr && builtinContext->spriteProperties != nullptr
                   ? builtinContext->spriteProperties->getSpriteProp(sprite->channel, propName)
                   : Datum::voidValue();
    }
    if (const auto* value = object.asInt(); value != nullptr && !equalsIgnoreCase(propName, "ilk")) {
        return builtinContext != nullptr && builtinContext->spriteProperties != nullptr
                   ? builtinContext->spriteProperties->getSpriteProp(value->value, propName)
                   : Datum::voidValue();
    }
    if (const auto* timeout = object.asTimeoutRef()) {
        return builtinContext != nullptr ? builtin::TimeoutBuiltins::getProperty(*builtinContext, *timeout, propName)
                                         : Datum::voidValue();
    }
    if (object.asSoundRef() != nullptr) {
        if (equalsIgnoreCase(propName, "ilk")) {
            return Datum::symbol("sound");
        }
        const bool isSoundProperty = equalsIgnoreCase(propName, "soundEnabled") ||
                                     equalsIgnoreCase(propName, "soundLevel") ||
                                     equalsIgnoreCase(propName, "soundKeepDevice") ||
                                     equalsIgnoreCase(propName, "soundMixMedia");
        if (!isSoundProperty) {
            return Datum::voidValue();
        }
        if (builtinContext != nullptr && builtinContext->movieProperties != nullptr) {
            return builtinContext->movieProperties->getMovieProp(propName);
        }
        if (builtinContext != nullptr && builtinContext->soundManager != nullptr) {
            if (equalsIgnoreCase(propName, "soundEnabled")) {
                return builtinContext->soundManager->isEnabled() ? Datum::TRUE : Datum::FALSE;
            }
            if (equalsIgnoreCase(propName, "soundLevel")) {
                return Datum::of(builtinContext->soundManager->getSoundLevel());
            }
            if (equalsIgnoreCase(propName, "soundKeepDevice")) {
                return builtinContext->soundManager->soundKeepDevice() ? Datum::TRUE : Datum::FALSE;
            }
            return builtinContext->soundManager->soundMixMedia() ? Datum::TRUE : Datum::FALSE;
        }
        if (equalsIgnoreCase(propName, "soundLevel")) {
            return Datum::of(7);
        }
        if (equalsIgnoreCase(propName, "soundEnabled") ||
            equalsIgnoreCase(propName, "soundKeepDevice") ||
            equalsIgnoreCase(propName, "soundMixMedia")) {
            return Datum::TRUE;
        }
        return Datum::voidValue();
    }
    if (const auto* soundChannel = object.asSoundChannel()) {
        return dispatch::SoundChannelMethodDispatcher::getProperty(builtinContext, *soundChannel, propName);
    }
    if (const auto* xtraInstance = object.asXtraInstance()) {
        return builtinContext != nullptr ? builtin::XtraBuiltins::getProperty(*builtinContext, *xtraInstance, propName)
                                         : Datum::voidValue();
    }
    if (object.type() == DatumType::ScriptInstanceRef) {
        if (equalsIgnoreCase(propName, "ilk")) {
            return Datum::symbol("instance");
        }
        return util::getProperty(object.scriptInstanceValue(), propName);
    }
    if (object.isPropList()) {
        return getPropListProp(object.propListValue(), propName);
    }
    if (object.isList()) {
        return getListProp(object.listValue(), propName);
    }
    if (object.isString()) {
        std::string storage;
        return getStringProp(stringViewLikeJava(object, storage), propName);
    }
    if (const auto* point = object.asIntPoint()) {
        return getPointProp(*point, propName);
    }
    if (const auto* rect = object.asIntRect()) {
        return getRectProp(*rect, propName);
    }
    if (const auto* color = object.asColorRef()) {
        return getColorProp(*color, propName);
    }
    if (const auto* image = object.asImageRef()) {
        return dispatch::ImageMethodDispatcher::getProperty(*image, propName);
    }
    if (const auto* member = object.asCastMemberRef()) {
        return getCastMemberProp(context, *member, propName);
    }
    if (equalsIgnoreCase(propName, "ilk")) {
        return Datum::symbol(ilkTypeName(object));
    }
    return Datum::voidValue();
}

void setObjectProperty(ExecutionContext& context, Datum& object, std::string_view propName, Datum value) {
    auto* builtinContext = context.builtinContext();
    if (object.type() == DatumType::MovieRef || object.type() == DatumType::PlayerRef) {
        if (builtinContext != nullptr && builtinContext->movieProperties != nullptr) {
            (void)builtinContext->movieProperties->setMovieProp(propName, value);
        }
        return;
    }
    if (object.type() == DatumType::StageRef) {
        if (builtinContext != nullptr && builtinContext->movieProperties != nullptr) {
            (void)builtinContext->movieProperties->setStageProp(propName, value);
        }
        return;
    }
    if (const auto* castLib = object.asCastLibRef()) {
        if (builtinContext != nullptr && builtinContext->castLibPropertySetter) {
            (void)builtinContext->castLibPropertySetter(castLib->castLib, std::string(propName), value);
        }
        return;
    }
    if (const auto* sprite = object.asSpriteRef()) {
        if (builtinContext != nullptr && builtinContext->spriteProperties != nullptr) {
            (void)builtinContext->spriteProperties->setSpriteProp(sprite->channel, propName, value);
        }
        return;
    }
    if (const auto* intValue = object.asInt()) {
        if (builtinContext != nullptr && builtinContext->spriteProperties != nullptr) {
            (void)builtinContext->spriteProperties->setSpriteProp(intValue->value, propName, value);
        }
        return;
    }
    if (const auto* member = object.asCastMemberRef()) {
        if (builtinContext != nullptr && builtinContext->castMemberPropertySetter) {
            (void)builtinContext->castMemberPropertySetter(member->castLib, member->memberNum(), std::string(propName), value);
        }
        return;
    }
    if (const auto* timeout = object.asTimeoutRef()) {
        if (builtinContext != nullptr) {
            (void)builtin::TimeoutBuiltins::setProperty(*builtinContext, *timeout, propName, std::move(value));
        }
        return;
    }
    if (object.asSoundRef() != nullptr) {
        const bool isSoundProperty = equalsIgnoreCase(propName, "soundEnabled") ||
                                     equalsIgnoreCase(propName, "soundLevel") ||
                                     equalsIgnoreCase(propName, "soundKeepDevice") ||
                                     equalsIgnoreCase(propName, "soundMixMedia");
        if (isSoundProperty && builtinContext != nullptr && builtinContext->movieProperties != nullptr) {
            (void)builtinContext->movieProperties->setMovieProp(propName, value);
        } else if (isSoundProperty && builtinContext != nullptr && builtinContext->soundManager != nullptr) {
            if (equalsIgnoreCase(propName, "soundEnabled")) {
                builtinContext->soundManager->setEnabled(value.boolValue());
            } else if (equalsIgnoreCase(propName, "soundLevel")) {
                builtinContext->soundManager->setSoundLevel(value.intValue());
            } else if (equalsIgnoreCase(propName, "soundKeepDevice")) {
                builtinContext->soundManager->setSoundKeepDevice(value.boolValue());
            } else {
                builtinContext->soundManager->setSoundMixMedia(value.boolValue());
            }
        }
        return;
    }
    if (const auto* soundChannel = object.asSoundChannel()) {
        (void)dispatch::SoundChannelMethodDispatcher::setProperty(builtinContext, *soundChannel, propName, std::move(value));
        return;
    }
    if (const auto* xtraInstance = object.asXtraInstance()) {
        if (builtinContext != nullptr) {
            builtin::XtraBuiltins::setProperty(*builtinContext, *xtraInstance, propName, value);
        }
        return;
    }
    if (object.type() == DatumType::ScriptInstanceRef) {
        if (!context.hasVariableSetListener()) {
            util::setProperty(object.scriptInstanceValue(), propName, std::move(value));
            return;
        }
        const Datum tracedValue = value;
        util::setProperty(object.scriptInstanceValue(), propName, std::move(value));
        context.tracePropertySet(propName, tracedValue);
        return;
    }
    if (object.isPropList()) {
        putPropListProp(object.propListValue(), propName, std::move(value));
        return;
    }
    if (auto* point = object.asIntPoint()) {
        setPointProp(*point, propName, value);
        return;
    }
    if (auto* rect = object.asIntRect()) {
        setRectProp(*rect, propName, value);
        return;
    }
    if (const auto* image = object.asImageRef()) {
        dispatch::ImageMethodDispatcher::setProperty(context.builtinContext(), *image, propName, value);
        return;
    }
}

bool isNoReturnArgList(const Datum& datum) {
    return datum.type() == DatumType::ArgListNoRet;
}

HandlerRef handlerRefFromLocation(const builtin::BuiltinContext::ScriptHandlerLocation& location) {
    return HandlerRef{
        location.script,
        location.handler,
        location.scriptOwner,
        location.fileOwner,
        location.scriptNamesOwner,
        location.scriptType
    };
}

Datum safeExecuteHandler(ExecutionContext& context,
                         const HandlerRef& handler,
                         std::span<const Datum> args,
                         const Datum& receiver);

Datum safeExecuteHandler(ExecutionContext& context,
                         const HandlerRef& handler,
                         const std::vector<Datum>& args,
                         const Datum& receiver) {
    return safeExecuteHandler(context, handler, std::span<const Datum>(args), receiver);
}

Datum safeExecuteHandler(ExecutionContext& context,
                         const HandlerRef& handler,
                         std::span<const Datum> args,
                         const Datum& receiver) {
    try {
        return context.executeHandler(handler, args, receiver);
    } catch (const LingoException&) {
        context.setErrorState(true);
        return Datum::voidValue();
    }
}

Datum safeExecuteHandler(ExecutionContext& context,
                         const chunks::ScriptChunk& script,
                         const chunks::ScriptChunk::Handler& handler,
                         const std::vector<Datum>& args,
                         const Datum& receiver) {
    return safeExecuteHandler(context, HandlerRef{&script, &handler}, args, receiver);
}

Datum safeExecuteHandler(ExecutionContext& context,
                         const chunks::ScriptChunk& script,
                         const chunks::ScriptChunk::Handler& handler,
                         std::span<const Datum> args,
                         const Datum& receiver) {
    return safeExecuteHandler(context, HandlerRef{&script, &handler}, args, receiver);
}

const std::vector<Datum>& emptyDatumArgs();

std::string pickLineDelimiter(std::string_view value) {
    return util::pickLineDelimiter(value);
}

std::vector<std::string> splitLines(std::string_view value) {
    return util::splitIntoChunks(value, StringChunkType::Line);
}

int countLines(std::string_view value) {
    return util::countChunks(value, StringChunkType::Line);
}

std::vector<std::string> splitChunks(std::string_view value, StringChunkType type, char itemDelimiter = ',') {
    return util::splitIntoChunks(value, type, itemDelimiter);
}

std::string chunkDelimiter(StringChunkType type, char itemDelimiter = ',') {
    return util::chunkDelimiter(type, itemDelimiter);
}

std::string sourceChunkDelimiter(std::string_view value, StringChunkType type, char itemDelimiter = ',') {
    return type == StringChunkType::Line ? pickLineDelimiter(value) : chunkDelimiter(type, itemDelimiter);
}

int countChunks(std::string_view value, StringChunkType type, char itemDelimiter = ',') {
    return util::countChunks(value, type, itemDelimiter);
}

void appendInt(std::string& output, int value) {
    std::array<char, 16> buffer{};
    const auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (ec == std::errc{}) {
        output.append(buffer.data(), ptr);
    }
}

std::string fieldLineIndexCacheKey(const Datum::FieldText& field) {
    std::string key;
    key.reserve(32);
    appendInt(key, field.castLib);
    key.push_back(':');
    appendInt(key, field.memberNum);
    key.push_back(':');
    appendInt(key, field.revision);
    return key;
}

bool lineIndexMatches(std::string_view value, const util::LineIndex& index) {
    return index.sourceSize == value.size() &&
           index.sourceHash == std::hash<std::string_view>{}(value);
}

const util::LineIndex* cachedFieldLineIndex(const Datum& target, builtin::BuiltinContext* builtinContext) {
    const auto* field = target.asFieldText();
    if (field == nullptr || field->revision == 0 || builtinContext == nullptr) {
        return nullptr;
    }

    if (builtinContext->fieldLineIndexCache.size() > 128) {
        builtinContext->fieldLineIndexCache.clear();
    }

    const std::string key = fieldLineIndexCacheKey(*field);
    auto found = builtinContext->fieldLineIndexCache.find(key);
    if (found == builtinContext->fieldLineIndexCache.end()) {
        found = builtinContext->fieldLineIndexCache
            .emplace(key, util::buildLineIndex(field->value))
            .first;
    } else if (!lineIndexMatches(field->value, found->second)) {
        found->second = util::buildLineIndex(field->value);
    }
    return &found->second;
}

std::string getChunkValue(std::string_view value, StringChunkType type, int index, char itemDelimiter = ',') {
    return util::getChunk(value, type, index, itemDelimiter);
}

std::string getChunkRangeValue(std::string_view value,
                               StringChunkType type,
                               int start,
                               int end,
                               char itemDelimiter = ',') {
    return util::getChunkRange(value, type, start, end, itemDelimiter);
}

std::string resolveChunkRange(std::string_view value,
                              StringChunkType type,
                              int first,
                              int last,
                              char itemDelimiter = ',') {
    if (first == 0 && last == 0) {
        return std::string(value);
    }
    if (first < 0 || last < 0) {
        const int count = countChunks(value, type, itemDelimiter);
        if (first < 0) {
            first = count;
        }
        if (last < 0) {
            last = count;
        }
    }

    const int effectiveLast = last == 0 ? first : last;
    return first == effectiveLast
        ? util::getChunk(value, type, first, itemDelimiter)
        : util::getChunkRange(value, type, first, effectiveLast, itemDelimiter);
}

std::optional<StringChunkType> stringChunkTypeByCode(int value) {
    switch (value) {
        case 0x01:
            return StringChunkType::Item;
        case 0x02:
            return StringChunkType::Word;
        case 0x03:
            return StringChunkType::Char;
        case 0x04:
            return StringChunkType::Line;
        default:
            return std::nullopt;
    }
}

std::optional<StringChunkType> legacyChunkPropertyTypeByCode(int value) {
    switch (value) {
        case 0x01:
            return StringChunkType::Char;
        case 0x02:
            return StringChunkType::Word;
        case 0x03:
            return StringChunkType::Item;
        case 0x04:
            return StringChunkType::Line;
        default:
            return std::nullopt;
    }
}

std::optional<StringChunkType> stringChunkTypeByNameNoThrow(std::string_view value) {
    if (equalsIgnoreCase(value, "item")) {
        return StringChunkType::Item;
    }
    if (equalsIgnoreCase(value, "word")) {
        return StringChunkType::Word;
    }
    if (equalsIgnoreCase(value, "char")) {
        return StringChunkType::Char;
    }
    if (equalsIgnoreCase(value, "line")) {
        return StringChunkType::Line;
    }
    return std::nullopt;
}

char currentItemDelimiter(ExecutionContext& context) {
    if (auto* builtinContext = context.builtinContext(); builtinContext != nullptr && builtinContext->movieProperties != nullptr) {
        return builtinContext->movieProperties->getItemDelimiter();
    }
    return ',';
}

std::string getLastChunkValue(std::string_view value, StringChunkType type, char itemDelimiter = ',') {
    return util::getLastChunk(value, type, itemDelimiter);
}

std::optional<std::pair<int, int>> itemByteRange(std::string_view value, int first, int last, char delimiter) {
    int chunkNum = 1;
    int chunkStart = 0;
    int rangeStart = -1;
    for (int index = 0; index <= static_cast<int>(value.size()); ++index) {
        if (index == static_cast<int>(value.size()) || value[static_cast<std::size_t>(index)] == delimiter) {
            if (chunkNum == first) {
                rangeStart = chunkStart;
            }
            if (chunkNum == last) {
                if (rangeStart < 0) {
                    return std::nullopt;
                }
                return std::pair{rangeStart, index};
            }
            if (chunkNum > last) {
                break;
            }
            ++chunkNum;
            chunkStart = index + 1;
        }
    }
    if (rangeStart >= 0) {
        return std::pair{rangeStart, static_cast<int>(value.size())};
    }
    return std::nullopt;
}

std::optional<std::pair<int, int>> chunkByteRange(std::string_view value,
                                                  StringChunkType type,
                                                  int first,
                                                  int last,
                                                  char itemDelimiter = ',') {
    if (value.empty()) {
        return std::nullopt;
    }
    last = last == 0 ? first : last;
    if (first < 1) {
        first = 1;
    }
    if (last < first) {
        return std::nullopt;
    }

    if (type == StringChunkType::Char) {
        if (first > static_cast<int>(value.size())) {
            return std::nullopt;
        }
        return std::pair{first - 1, std::min(last, static_cast<int>(value.size()))};
    }
    if (type == StringChunkType::Item) {
        return itemByteRange(value, first, last, itemDelimiter);
    }

    const auto chunks = splitChunks(value, type, itemDelimiter);
    if (chunks.empty() || first > static_cast<int>(chunks.size())) {
        return std::nullopt;
    }
    last = std::min(last, static_cast<int>(chunks.size()));
    const std::string delimiter = sourceChunkDelimiter(value, type, itemDelimiter);

    int start = 0;
    for (int index = 0; index < first - 1; ++index) {
        start += static_cast<int>(chunks[static_cast<std::size_t>(index)].size());
        if (index < static_cast<int>(chunks.size()) - 1) {
            start += static_cast<int>(delimiter.size());
        }
    }

    int end = start;
    for (int index = first - 1; index < last; ++index) {
        end += static_cast<int>(chunks[static_cast<std::size_t>(index)].size());
        if (index < last - 1 && index < static_cast<int>(chunks.size()) - 1) {
            end += static_cast<int>(delimiter.size());
        }
    }
    return std::pair{start, end};
}

bool startsWithAt(std::string_view value, std::string_view needle, int offset) {
    if (needle.empty() || offset < 0 || offset + static_cast<int>(needle.size()) > static_cast<int>(value.size())) {
        return false;
    }
    return value.substr(static_cast<std::size_t>(offset), needle.size()) == needle;
}

bool prefixEndsWith(std::string_view value, int end, std::string_view needle) {
    if (needle.empty() || end < static_cast<int>(needle.size()) || end > static_cast<int>(value.size())) {
        return false;
    }
    return value.substr(static_cast<std::size_t>(end) - needle.size(), needle.size()) == needle;
}

std::string deleteChunkValue(std::string_view value,
                             StringChunkType type,
                             int first,
                             int last,
                             char itemDelimiter = ',') {
    auto range = chunkByteRange(value, type, first, last, itemDelimiter);
    if (!range) {
        return std::string(value);
    }

    auto [deleteStart, deleteEnd] = *range;
    if (type != StringChunkType::Char) {
        const std::string delimiter = sourceChunkDelimiter(value, type, itemDelimiter);
        if (deleteEnd < static_cast<int>(value.size()) && startsWithAt(value, delimiter, deleteEnd)) {
            deleteEnd += static_cast<int>(delimiter.size());
        } else if (deleteStart > 0 && prefixEndsWith(value, deleteStart, delimiter)) {
            deleteStart -= static_cast<int>(delimiter.size());
        }
    }

    if (deleteStart == 0) {
        return std::string(value.substr(static_cast<std::size_t>(deleteEnd)));
    }
    if (deleteEnd >= static_cast<int>(value.size())) {
        return std::string(value.substr(0, static_cast<std::size_t>(deleteStart)));
    }
    return std::string(value.substr(0, static_cast<std::size_t>(deleteStart))) +
           std::string(value.substr(static_cast<std::size_t>(deleteEnd)));
}

std::string deleteCharChunkRefValue(std::string_view value, int first, int last) {
    if (value.empty() || first < 1) {
        return std::string(value);
    }
    const int start = std::max(0, first - 1);
    const int end = std::max(start, std::min(static_cast<int>(value.size()), last));
    if (start >= static_cast<int>(value.size()) || start >= end) {
        return std::string(value);
    }
    return std::string(value.substr(0, static_cast<std::size_t>(start))) +
           std::string(value.substr(static_cast<std::size_t>(end)));
}

std::string getCharChunkRefValue(std::string_view value, int first, int last) {
    if (value.empty() || first < 1) {
        return "";
    }
    const int start = std::max(0, first - 1);
    const int end = std::max(start, std::min(static_cast<int>(value.size()), last));
    if (start >= static_cast<int>(value.size()) || start >= end) {
        return "";
    }
    return std::string(value.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start)));
}

int normalizeCharChunkIndex(int index, int length, bool before) {
    if (index < 0) {
        return length;
    }
    if (before) {
        return index < 1 ? 1 : index;
    }
    if (index == 0) {
        return 0;
    }
    return index;
}

std::optional<std::pair<int, int>> charChunkReplaceRange(std::string_view value, int first, int last) {
    const int length = static_cast<int>(value.size());
    const int normalizedFirst = normalizeCharChunkIndex(first, length, true);
    const int normalizedLast = normalizeCharChunkIndex(last == 0 ? first : last, length, true);
    if (normalizedFirst < 1 || normalizedFirst > length || normalizedLast < normalizedFirst) {
        return std::nullopt;
    }
    const int rangeStart = normalizedFirst - 1;
    const int rangeEnd = std::min(normalizedLast, length);
    if (rangeEnd < rangeStart) {
        return std::nullopt;
    }
    return std::pair{rangeStart, rangeEnd};
}

int chunkInsertionIndex(std::string_view value,
                        StringChunkType type,
                        int targetIndex,
                        bool before,
                        char itemDelimiter = ',') {
    const int length = static_cast<int>(value.size());
    if (type == StringChunkType::Char) {
        const int normalized = normalizeCharChunkIndex(targetIndex, length, before);
        if (before) {
            if (normalized <= 1) {
                return 0;
            }
            if (normalized > length) {
                return length;
            }
            return normalized - 1;
        }
        if (normalized <= 0) {
            return 0;
        }
        if (normalized >= length) {
            return length;
        }
        return normalized;
    }

    if (const auto range = chunkByteRange(value, type, targetIndex, targetIndex, itemDelimiter)) {
        return before ? range->first : range->second;
    }
    if (before) {
        return targetIndex <= 1 ? 0 : length;
    }
    return targetIndex <= 0 ? 0 : length;
}

std::string putIntoChunkValue(std::string_view value,
                              StringChunkType type,
                              int first,
                              int last,
                              std::string_view replacement,
                              char itemDelimiter = ',') {
    auto range = chunkByteRange(value, type, first, last, itemDelimiter);
    if (!range) {
        return std::string(value);
    }
    return std::string(value.substr(0, static_cast<std::size_t>(range->first))) +
           std::string(replacement) +
           std::string(value.substr(static_cast<std::size_t>(range->second)));
}

std::string putBeforeChunkValue(std::string_view value,
                                StringChunkType type,
                                int first,
                                std::string_view inserted,
                                char itemDelimiter = ',') {
    const int insertAt = chunkInsertionIndex(value, type, first, true, itemDelimiter);
    return std::string(value.substr(0, static_cast<std::size_t>(insertAt))) +
           std::string(inserted) +
           std::string(value.substr(static_cast<std::size_t>(insertAt)));
}

std::string putAfterChunkValue(std::string_view value,
                               StringChunkType type,
                               int targetIndex,
                               std::string_view inserted,
                               char itemDelimiter = ',') {
    const int insertAt = chunkInsertionIndex(value, type, targetIndex, false, itemDelimiter);
    return std::string(value.substr(0, static_cast<std::size_t>(insertAt))) +
           std::string(inserted) +
           std::string(value.substr(static_cast<std::size_t>(insertAt)));
}

struct ChunkSpec {
    StringChunkType type;
    int first;
    int last;
};

ChunkSpec chooseSingleChunkSpec(int firstChar,
                                int lastChar,
                                int firstWord,
                                int lastWord,
                                int firstItem,
                                int lastItem,
                                int firstLine,
                                int lastLine) {
    if (firstLine != 0 || lastLine != 0) {
        return {StringChunkType::Line, firstLine, lastLine};
    }
    if (firstItem != 0 || lastItem != 0) {
        return {StringChunkType::Item, firstItem, lastItem};
    }
    if (firstWord != 0 || lastWord != 0) {
        return {StringChunkType::Word, firstWord, lastWord};
    }
    if (firstChar != 0 || lastChar != 0) {
        return {StringChunkType::Char, firstChar, lastChar};
    }
    return {StringChunkType::Char, 1, 1};
}

Datum pointObjectMethod(Datum::IntPoint& point, std::string_view methodName, std::span<const Datum> args) {
    if (equalsIgnoreCase(methodName, "getAt")) {
        if (args.empty()) return Datum::voidValue();
        const int index = toIntLikeJava(args[0]);
        if (index == 1) return Datum::of(point.x);
        if (index == 2) return Datum::of(point.y);
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "duplicate")) {
        return Datum::intPoint(point.x, point.y);
    }
    if (equalsIgnoreCase(methodName, "setAt")) {
        if (args.size() < 2) return Datum::voidValue();
        const int component = toIntLikeJava(args[0]);
        const int newValue = toIntLikeJava(args[1]);
        if (component == 1) {
            point.x = newValue;
        } else if (component == 2) {
            point.y = newValue;
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "inside")) {
        if (!args.empty()) {
            if (const auto* rect = args[0].asIntRect()) {
                const bool inside = point.x >= rect->left && point.x < rect->right &&
                                    point.y >= rect->top && point.y < rect->bottom;
                return inside ? Datum::TRUE : Datum::FALSE;
            }
        }
        return Datum::FALSE;
    }
    return Datum::voidValue();
}

Datum rectObjectMethod(Datum::IntRect& rect, std::string_view methodName, std::span<const Datum> args) {
    if (equalsIgnoreCase(methodName, "getAt")) {
        if (args.empty()) return Datum::voidValue();
        switch (toIntLikeJava(args[0])) {
            case 1: return Datum::of(rect.left);
            case 2: return Datum::of(rect.top);
            case 3: return Datum::of(rect.right);
            case 4: return Datum::of(rect.bottom);
            default: return Datum::voidValue();
        }
    }
    if (equalsIgnoreCase(methodName, "duplicate")) {
        return Datum::intRect(rect.left, rect.top, rect.right, rect.bottom);
    }
    if (equalsIgnoreCase(methodName, "setAt")) {
        if (args.size() < 2) return Datum::voidValue();
        const int component = toIntLikeJava(args[0]);
        const int newValue = toIntLikeJava(args[1]);
        switch (component) {
            case 1: rect.left = newValue; break;
            case 2: rect.top = newValue; break;
            case 3: rect.right = newValue; break;
            case 4: rect.bottom = newValue; break;
            default: break;
        }
        return Datum::voidValue();
    }
    return Datum::voidValue();
}

Datum scriptInstanceCountValue(const Datum& value) {
    if (value.isVoid()) return Datum::of(0);
    if (value.isList()) return Datum::of(value.listValue().count());
    if (value.isPropList()) return Datum::of(value.propListValue().count());
    if (value.isString()) {
        std::string storage;
        return Datum::of(static_cast<int>(stringViewLikeJava(value, storage).size()));
    }
    return Datum::of(0);
}

Datum scriptInstanceNestedProperty(const Datum& container, const Datum& subKey) {
    if (container.isList()) {
        const int index = toIntLikeJava(subKey);
        const auto& items = container.listValue().items();
        if (index >= 1 && index <= static_cast<int>(items.size())) {
            return items[static_cast<std::size_t>(index - 1)];
        }
        return Datum::voidValue();
    }
    if (container.isPropList()) {
        const auto& properties = container.propListValue().properties();
        if (subKey.isInt() || subKey.isFloat()) {
            const int index = toIntLikeJava(subKey);
            if (index >= 1 && index <= static_cast<int>(properties.size())) {
                return properties[static_cast<std::size_t>(index - 1)].second;
            }
            return Datum::voidValue();
        }
        std::string keyNameStorage;
        return getPropListKey(container.propListValue(), keyNameLikeJavaView(subKey, keyNameStorage));
    }
    return Datum::voidValue();
}

Datum scriptInstancePropertyValue(const Datum::ScriptInstanceRef& instance, std::string_view propName) {
    if (const auto* value = util::findPropertyValue(instance, propName)) {
        return *value;
    }
    if (equalsIgnoreCase(propName, "ancestor")) {
        return util::getProperty(instance, propName);
    }
    return Datum::voidValue();
}

Datum scriptInstancePropertyCountValue(const Datum::ScriptInstanceRef& instance, std::string_view propName) {
    if (const auto* value = util::findPropertyValue(instance, propName)) {
        return scriptInstanceCountValue(*value);
    }
    return Datum::of(0);
}

Datum scriptInstanceNestedPropertyValue(const Datum::ScriptInstanceRef& instance,
                                        std::string_view propName,
                                        const Datum& subKey) {
    if (const auto* value = util::findPropertyValue(instance, propName)) {
        return scriptInstanceNestedProperty(*value, subKey);
    }
    if (equalsIgnoreCase(propName, "ancestor")) {
        return scriptInstanceNestedProperty(util::getProperty(instance, propName), subKey);
    }
    return Datum::voidValue();
}

void scriptInstanceSetNestedProperty(Datum& container, const Datum& subKey, Datum value) {
    if (container.isList()) {
        const int index = toIntLikeJava(subKey);
        if (index < 1) {
            return;
        }
        auto& items = container.listValue().items();
        while (static_cast<int>(items.size()) < index) {
            items.push_back(Datum::voidValue());
        }
        items[static_cast<std::size_t>(index - 1)] = std::move(value);
        return;
    }
    if (container.isPropList()) {
        auto& properties = container.propListValue().properties();
        if (subKey.isInt() || subKey.isFloat()) {
            const int index = toIntLikeJava(subKey);
            if (index >= 1 && index <= static_cast<int>(properties.size())) {
                properties[static_cast<std::size_t>(index - 1)].second = std::move(value);
            }
            return;
        }
        container.propListValue().put(subKey, std::move(value));
    }
}

void scriptInstancePutLocalProperty(Datum::ScriptInstanceRef& instance, std::string_view propName, Datum value) {
    if (propName == "ancestor") {
        instance.setProperty(std::string(propName), std::move(value));
        return;
    }
    instance.putLocalPropertyExact(std::string(propName), std::move(value));
}

void scriptInstanceDeleteLocalProperty(Datum::ScriptInstanceRef& instance, std::string_view propName) {
    if (propName == "ancestor") {
        instance.setProperty(std::string(propName), Datum::voidValue());
        return;
    }
    (void)instance.eraseLocalPropertyExact(propName);
}

bool shouldDeferNumericCloseThread(ExecutionContext& context,
                                   const Datum& receiver,
                                   std::string_view methodName,
                                   std::span<const Datum> args) {
    if (!equalsIgnoreCase(methodName, "closeThread") || args.size() != 1 ||
        (!args.front().isInt() && !args.front().isFloat())) {
        return false;
    }
    auto* builtinContext = context.builtinContext();
    if (builtinContext == nullptr || !builtinContext->scriptInstanceMethodDeferrer) {
        return false;
    }
    const std::vector<Datum> argVector(args.begin(), args.end());
    return builtinContext->scriptInstanceMethodDeferrer(receiver, std::string(methodName), argVector);
}

std::optional<builtin::BuiltinContext::ScriptHandlerLocation> findScriptInstanceScriptHandler(
    ExecutionContext& context,
    Datum::ScriptInstanceRef& instance,
    std::string_view methodName) {
    auto* builtinContext = context.builtinContext();
    if (builtinContext == nullptr || !builtinContext->scriptHandlerFinder) {
        return std::nullopt;
    }

    if (instance.ancestor() == nullptr) {
        if (const auto& scriptRef = instance.scriptRef(); scriptRef.has_value()) {
            const int castLib = scriptRef->castLib > 0 ? scriptRef->castLib : 1;
            const int memberNum = scriptRef->memberNum();
            const auto scriptKey = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(castLib)) << 32U) |
                                   static_cast<std::uint32_t>(memberNum);
            auto& scriptCache = builtinContext->directScriptInstanceHandlerCache[scriptKey];
            if (const auto cached = scriptCache.find(methodName); cached != scriptCache.end()) {
                return cached->second;
            }
            if (auto handler = builtinContext->scriptHandlerFinder(castLib, memberNum, std::string(methodName));
                handler.has_value() && handler->script != nullptr) {
                scriptCache.emplace(std::string(methodName), handler);
                return handler;
            }
            scriptCache.emplace(std::string(methodName), std::nullopt);
        }
        return std::nullopt;
    }

    std::string cacheKey;
    cacheKey.reserve(static_cast<std::size_t>(util::MAX_ANCESTOR_DEPTH * 4) + methodName.size());
    auto* current = &instance;
    std::shared_ptr<Datum::ScriptInstanceRef> currentOwner;
    for (int depth = 0; current != nullptr && depth < util::MAX_ANCESTOR_DEPTH; ++depth) {
        appendInt(cacheKey, current->identityId());
        cacheKey.push_back('/');
        currentOwner = current->ancestor();
        current = currentOwner.get();
    }
    appendLowerAscii(cacheKey, methodName);
    if (const auto cached = builtinContext->scriptInstanceHandlerCache.find(cacheKey);
        cached != builtinContext->scriptInstanceHandlerCache.end()) {
        return cached->second;
    }

    current = &instance;
    currentOwner.reset();
    for (int depth = 0; current != nullptr && depth < util::MAX_ANCESTOR_DEPTH; ++depth) {
        if (const auto& scriptRef = current->scriptRef(); scriptRef.has_value()) {
            const int castLib = scriptRef->castLib > 0 ? scriptRef->castLib : 1;
            const int memberNum = scriptRef->memberNum();
            if (auto handler = builtinContext->scriptHandlerFinder(castLib, memberNum, std::string(methodName));
                handler.has_value() && handler->script != nullptr) {
                builtinContext->scriptInstanceHandlerCache[cacheKey] = handler;
                return handler;
            }
        }

        currentOwner = current->ancestor();
        current = currentOwner.get();
    }
    builtinContext->scriptInstanceHandlerCache[cacheKey] = std::nullopt;
    return std::nullopt;
}

enum class ImmediateObjectMethod {
    Unknown,
    Add,
    AddAt,
    AddProp,
    Append,
    Count,
    DeleteProp,
    DeleteAt,
    Duplicate,
    GetAProp,
    GetAt,
    GetFirst,
    GetLast,
    GetProp,
    GetProperty,
    GetPropRef,
    Handler,
    Ilk,
    SetAProp,
    SetAt,
    SetProp
};

ImmediateObjectMethod classifyImmediateObjectMethod(std::string_view methodName) {
    switch (methodName.size()) {
        case 3:
            if (equalsIgnoreCase(methodName, "add")) return ImmediateObjectMethod::Add;
            if (equalsIgnoreCase(methodName, "ilk")) return ImmediateObjectMethod::Ilk;
            break;
        case 5:
            if (equalsIgnoreCase(methodName, "addAt")) return ImmediateObjectMethod::AddAt;
            if (equalsIgnoreCase(methodName, "count")) return ImmediateObjectMethod::Count;
            if (equalsIgnoreCase(methodName, "getAt")) return ImmediateObjectMethod::GetAt;
            if (equalsIgnoreCase(methodName, "setAt")) return ImmediateObjectMethod::SetAt;
            break;
        case 6:
            if (equalsIgnoreCase(methodName, "append")) return ImmediateObjectMethod::Append;
            break;
        case 7:
            if (equalsIgnoreCase(methodName, "addProp")) return ImmediateObjectMethod::AddProp;
            if (equalsIgnoreCase(methodName, "getLast")) return ImmediateObjectMethod::GetLast;
            if (equalsIgnoreCase(methodName, "getProp")) return ImmediateObjectMethod::GetProp;
            if (equalsIgnoreCase(methodName, "handler")) return ImmediateObjectMethod::Handler;
            if (equalsIgnoreCase(methodName, "setProp")) return ImmediateObjectMethod::SetProp;
            break;
        case 8:
            if (equalsIgnoreCase(methodName, "deleteAt")) return ImmediateObjectMethod::DeleteAt;
            if (equalsIgnoreCase(methodName, "getAProp")) return ImmediateObjectMethod::GetAProp;
            if (equalsIgnoreCase(methodName, "getFirst")) return ImmediateObjectMethod::GetFirst;
            if (equalsIgnoreCase(methodName, "setAProp")) return ImmediateObjectMethod::SetAProp;
            break;
        case 9:
            if (equalsIgnoreCase(methodName, "duplicate")) return ImmediateObjectMethod::Duplicate;
            break;
        case 10:
            if (equalsIgnoreCase(methodName, "deleteProp")) return ImmediateObjectMethod::DeleteProp;
            if (equalsIgnoreCase(methodName, "getPropRef")) return ImmediateObjectMethod::GetPropRef;
            break;
        case 11:
            if (equalsIgnoreCase(methodName, "getProperty")) return ImmediateObjectMethod::GetProperty;
            break;
        default:
            break;
    }
    return ImmediateObjectMethod::Unknown;
}

Datum scriptInstanceObjectMethod(ExecutionContext& context,
                                 Datum& receiver,
                                 std::string_view methodName,
                                 ImmediateObjectMethod method,
                                 std::span<const Datum> args) {
    auto& instance = receiver.scriptInstanceValue();
    if (shouldDeferNumericCloseThread(context, receiver, methodName, args)) {
        return Datum::TRUE;
    }
    if (method == ImmediateObjectMethod::SetAt || method == ImmediateObjectMethod::SetAProp) {
        if (args.size() >= 2) {
            std::string propNameStorage;
            util::setProperty(instance, keyNameLikeJavaView(args[0], propNameStorage), args[1]);
        }
        return Datum::voidValue();
    }
    if (method == ImmediateObjectMethod::SetProp) {
        if (args.size() == 2) {
            std::string propNameStorage;
            util::setProperty(instance, keyNameLikeJavaView(args[0], propNameStorage), args[1]);
        } else if (args.size() >= 3) {
            std::string propNameStorage;
            const std::string_view propName = keyNameLikeJavaView(args[0], propNameStorage);
            Datum localProp = util::getProperty(instance, propName);
            if (localProp.isVoid()) {
                localProp = Datum::propList();
                util::setProperty(instance, propName, localProp);
            }
            scriptInstanceSetNestedProperty(localProp, args[1], args[2]);
            util::setProperty(instance, propName, std::move(localProp));
        }
        return Datum::voidValue();
    }
    if (method == ImmediateObjectMethod::GetAt) {
        if (args.empty()) return Datum::voidValue();
        std::string keyStorage;
        const std::string_view key = keyNameLikeJavaView(args[0], keyStorage);
        if (equalsIgnoreCase(key, "ancestor")) {
            const Datum ancestor = util::getProperty(instance, key);
            return ancestor.isVoid() ? Datum::of(0) : ancestor;
        }
        return util::getProperty(instance, key);
    }
    if (method == ImmediateObjectMethod::GetAProp) {
        if (args.empty()) return Datum::voidValue();
        std::string propNameStorage;
        return scriptInstancePropertyValue(instance, keyNameLikeJavaView(args[0], propNameStorage));
    }
    if (method == ImmediateObjectMethod::GetProp || method == ImmediateObjectMethod::GetPropRef) {
        if (args.empty()) return Datum::voidValue();
        std::string propNameStorage;
        const std::string_view propName = keyNameLikeJavaView(args[0], propNameStorage);
        if (args.size() >= 2) {
            return scriptInstanceNestedPropertyValue(instance, propName, args[1]);
        }
        return scriptInstancePropertyValue(instance, propName);
    }
    if (method == ImmediateObjectMethod::AddProp) {
        if (args.size() >= 2) {
            std::string propNameStorage;
            scriptInstancePutLocalProperty(instance, keyNameLikeJavaView(args[0], propNameStorage), args[1]);
        }
        return Datum::voidValue();
    }
    if (method == ImmediateObjectMethod::DeleteProp) {
        if (!args.empty()) {
            std::string propNameStorage;
            scriptInstanceDeleteLocalProperty(instance, keyNameLikeJavaView(args[0], propNameStorage));
        }
        return Datum::voidValue();
    }
    if (method == ImmediateObjectMethod::Count) {
        if (!args.empty()) {
            std::string propNameStorage;
            return scriptInstancePropertyCountValue(instance, keyNameLikeJavaView(args[0], propNameStorage));
        }
        return Datum::of(static_cast<int>(instance.properties().size()) + (instance.ancestor() ? 1 : 0));
    }
    if (method == ImmediateObjectMethod::Ilk) {
        return Datum::symbol("instance");
    }
    if (method == ImmediateObjectMethod::AddAt) {
        return Datum::voidValue();
    }
    if (method == ImmediateObjectMethod::Handler) {
        if (args.empty()) return Datum::FALSE;
        std::string handlerNameStorage;
        const std::string_view handlerName = keyNameLikeJavaView(args[0], handlerNameStorage);
        return findScriptInstanceScriptHandler(context, instance, handlerName).has_value() ? Datum::TRUE : Datum::FALSE;
    }
    const auto handler = findScriptInstanceScriptHandler(context, instance, methodName);
    if (handler) {
        return safeExecuteHandler(context, handlerRefFromLocation(*handler), args, receiver);
    }

    Datum property = util::getProperty(instance, methodName);
    return property.isVoid() ? Datum::voidValue() : std::move(property);
}

Datum scriptInstanceObjectMethod(ExecutionContext& context,
                                 Datum& receiver,
                                 std::string_view methodName,
                                 std::span<const Datum> args) {
    return scriptInstanceObjectMethod(context,
                                      receiver,
                                      methodName,
                                      classifyImmediateObjectMethod(methodName),
                                      args);
}

bool imageFill(bitmap::Bitmap& bmp, const std::vector<Datum>& args) {
    if (args.size() < 2) return false;

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    Datum colorDatum = Datum::voidValue();

    if (const auto* rect = args[0].asIntRect()) {
        left = rect->left;
        top = rect->top;
        right = rect->right;
        bottom = rect->bottom;
        colorDatum = args[1];
    } else if (args.size() >= 5) {
        left = toIntLikeJava(args[0]);
        top = toIntLikeJava(args[1]);
        right = toIntLikeJava(args[2]);
        bottom = toIntLikeJava(args[3]);
        colorDatum = args[4];
    } else {
        return false;
    }

    if (colorDatum.isPropList()) {
        const Datum propColor = getPropListKey(colorDatum.propListValue(), "color");
        if (!propColor.isVoid()) {
            colorDatum = propColor;
        }
    }
    if (colorDatum.isVoid()) {
        return false;
    }

    const int width = right - left;
    const int height = bottom - top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    const auto colorArgb = imageColorArgb(colorDatum, &bmp);
    if (const auto paletteIndex = imageFillPaletteIndex(colorDatum, bmp)) {
        bmp.fillRectPaletteIndex(left, top, width, height, *paletteIndex, colorArgb);
    } else {
        bmp.fillRect(left, top, width, height, colorArgb);
    }
    return true;
}

Datum imageDraw(bitmap::Bitmap& bmp, const std::vector<Datum>& args) {
    if (args.size() < 2) return Datum::voidValue();

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    Datum propsArg = Datum::voidValue();

    if (const auto* rect = args[0].asIntRect()) {
        left = rect->left;
        top = rect->top;
        right = rect->right;
        bottom = rect->bottom;
        propsArg = args[1];
    } else if (args.size() >= 5) {
        left = toIntLikeJava(args[0]);
        top = toIntLikeJava(args[1]);
        right = toIntLikeJava(args[2]);
        bottom = toIntLikeJava(args[3]);
        propsArg = args[4];
    } else {
        return Datum::voidValue();
    }

    std::uint32_t colorArgb = 0xFF000000U;
    std::string shapeType = "rect";
    if (propsArg.isPropList()) {
        const Datum colorDatum = getPropListKey(propsArg.propListValue(), "color");
        if (!colorDatum.isVoid()) {
            colorArgb = imageColorArgb(colorDatum, &bmp);
        }
        const Datum shapeDatum = getPropListKey(propsArg.propListValue(), "shapeType");
        if (const auto* shape = shapeDatum.asSymbol()) {
            shapeType = shape->name;
        }
    } else {
        colorArgb = imageColorArgb(propsArg, &bmp);
    }

    const int width = right - left;
    const int height = bottom - top;
    if (width <= 0 || height <= 0) {
        return Datum::voidValue();
    }

    if (equalsIgnoreCase(shapeType, "oval") || equalsIgnoreCase(shapeType, "ellipse")) {
        bitmap::Drawing::drawEllipse(bmp, left + width / 2, top + height / 2, width / 2, height / 2, colorArgb);
    } else if (equalsIgnoreCase(shapeType, "line")) {
        bitmap::Drawing::drawLine(bmp, left, top, right, bottom, colorArgb);
    } else {
        bitmap::Drawing::drawRect(bmp, left, top, width, height, colorArgb);
    }
    return Datum::voidValue();
}

int imageMaskAlphaFromPixel(std::uint32_t pixel) {
    const int r = static_cast<int>((pixel >> 16) & 0xFFU);
    const int g = static_cast<int>((pixel >> 8) & 0xFFU);
    const int b = static_cast<int>(pixel & 0xFFU);
    return ((77 * r) + (150 * g) + (29 * b) + 128) >> 8;
}

bool imageAlphaHasTransparency(const bitmap::Bitmap& alpha) {
    for (int y = 0; y < alpha.height(); ++y) {
        for (int x = 0; x < alpha.width(); ++x) {
            if (((alpha.getPixel(x, y) >> 24) & 0xFFU) < 255U) {
                return true;
            }
        }
    }
    return false;
}

bool imageHasWhiteEdgeAndDarkInterior(const bitmap::Bitmap& alpha) {
    const int width = alpha.width();
    const int height = alpha.height();
    if (width <= 0 || height <= 0) {
        return false;
    }

    int edgePixels = 0;
    int whiteEdgePixels = 0;
    bool hasDarkPixel = false;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int luma = imageMaskAlphaFromPixel(alpha.getPixel(x, y));
            if (luma < 250) {
                hasDarkPixel = true;
            }
            if (x == 0 || y == 0 || x == width - 1 || y == height - 1) {
                ++edgePixels;
                if (luma >= 250) {
                    ++whiteEdgePixels;
                }
            }
        }
    }
    return hasDarkPixel && edgePixels > 0 && whiteEdgePixels * 4 >= edgePixels * 3;
}

std::vector<std::pair<int, int>> imageUniqueCorners(int width, int height) {
    if (width == 1 && height == 1) {
        return {{0, 0}};
    }
    if (height == 1) {
        return {{0, 0}, {width - 1, 0}};
    }
    if (width == 1) {
        return {{0, 0}, {0, height - 1}};
    }
    return {{0, 0}, {width - 1, 0}, {0, height - 1}, {width - 1, height - 1}};
}

bool imageHasWhiteCornersAndDarkPixels(const bitmap::Bitmap& alpha) {
    const int width = alpha.width();
    const int height = alpha.height();
    if (width <= 0 || height <= 0) {
        return false;
    }

    const auto corners = imageUniqueCorners(width, height);
    int whiteCorners = 0;
    for (const auto& [x, y] : corners) {
        if (imageMaskAlphaFromPixel(alpha.getPixel(x, y)) >= 250) {
            ++whiteCorners;
        }
    }
    if (whiteCorners < static_cast<int>(corners.size())) {
        return false;
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (imageMaskAlphaFromPixel(alpha.getPixel(x, y)) < 250) {
                return true;
            }
        }
    }
    return false;
}

bool imageHasMattePolarity(const bitmap::Bitmap& alpha) {
    return imageAlphaHasTransparency(alpha) ||
           imageHasWhiteEdgeAndDarkInterior(alpha) ||
           imageHasWhiteCornersAndDarkPixels(alpha);
}

Datum imageSetAlpha(bitmap::Bitmap& bmp, const std::vector<Datum>& args) {
    if (bmp.bitDepth() != 32 || args.empty()) {
        return Datum::FALSE;
    }

    if (const auto* alphaRef = args[0].asImageRef()) {
        const auto& alpha = alphaRef->bitmap;
        if (alpha == nullptr ||
            alpha->bitDepth() != 8 ||
            alpha->width() != bmp.width() ||
            alpha->height() != bmp.height()) {
            return Datum::FALSE;
        }

        const bool mattePolarity = imageHasMattePolarity(*alpha);
        for (int y = 0; y < bmp.height(); ++y) {
            for (int x = 0; x < bmp.width(); ++x) {
                int alphaLevel = imageMaskAlphaFromPixel(alpha->getPixel(x, y));
                if (mattePolarity) {
                    alphaLevel = 255 - alphaLevel;
                }
                const auto pixel = bmp.getPixel(x, y);
                bmp.setPixel(x, y, (static_cast<std::uint32_t>(alphaLevel & 0xFF) << 24) | (pixel & 0x00FFFFFFU));
            }
        }
        bmp.setNativeAlpha(true);
        return Datum::TRUE;
    }

    const int alphaLevel = std::clamp(toIntLikeJava(args[0]), 0, 255);
    for (int y = 0; y < bmp.height(); ++y) {
        for (int x = 0; x < bmp.width(); ++x) {
            const auto pixel = bmp.getPixel(x, y);
            bmp.setPixel(x, y, (static_cast<std::uint32_t>(alphaLevel & 0xFF) << 24) | (pixel & 0x00FFFFFFU));
        }
    }
    bmp.setNativeAlpha(true);
    return Datum::TRUE;
}

struct ImageFloodFillMatte {
    std::optional<int> paletteIndex;
    int colorRgb = 0xFFFFFF;
    int tolerance = 0;

    [[nodiscard]] bool usesPaletteIndex() const { return paletteIndex.has_value(); }
};

std::vector<int> imageCornerIndices(int width, int height) {
    return {
        0,
        std::max(0, width - 1),
        std::max(0, (height - 1) * width),
        std::max(0, (height - 1) * width + (width - 1)),
    };
}

std::vector<int> imageEdgeIndices(int width, int height) {
    std::vector<int> indices;
    indices.reserve(static_cast<std::size_t>(std::max(1, (width * 2) + std::max(0, height - 2) * 2)));
    for (int x = 0; x < width; ++x) {
        indices.push_back(x);
        if (height > 1) {
            indices.push_back((height - 1) * width + x);
        }
    }
    for (int y = 1; y < height - 1; ++y) {
        indices.push_back(y * width);
        if (width > 1) {
            indices.push_back(y * width + (width - 1));
        }
    }
    return indices;
}

bool imageHasPaletteIndices(const std::optional<std::vector<std::uint8_t>>& paletteIndices, int width, int height) {
    return paletteIndices.has_value() && paletteIndices->size() >= static_cast<std::size_t>(width * height);
}

bool imageIsUniformPaletteIndex(const std::vector<std::uint8_t>& paletteIndices, int paletteIndex) {
    return std::all_of(paletteIndices.begin(), paletteIndices.end(), [&](std::uint8_t entry) {
        return static_cast<int>(entry) == paletteIndex;
    });
}

int imageResolvePaletteIndexRgb(const std::vector<std::uint32_t>& pixels,
                                const std::vector<std::uint8_t>& paletteIndices,
                                int paletteIndex) {
    for (std::size_t index = 0; index < pixels.size() && index < paletteIndices.size(); ++index) {
        if (static_cast<int>(paletteIndices[index]) == paletteIndex) {
            return static_cast<int>(pixels[index] & 0x00FFFFFFU);
        }
    }
    return 0xFFFFFF;
}

bool imageCornerContainsPaletteIndex(const std::vector<std::uint8_t>& paletteIndices,
                                     int width,
                                     int height,
                                     int paletteIndex) {
    for (const int index : imageCornerIndices(width, height)) {
        if (index >= 0 && static_cast<std::size_t>(index) < paletteIndices.size() &&
            static_cast<int>(paletteIndices[static_cast<std::size_t>(index)]) == paletteIndex) {
            return true;
        }
    }
    return false;
}

bool imageDefaultIndexedMatteRgb(int rgb) {
    return rgb == 0x000000 || rgb == 0xFFFFFF;
}

bool imageIsNearWhiteGrayscale(int rgb, int minChannel, int maxDelta) {
    const int r = (rgb >> 16) & 0xFF;
    const int g = (rgb >> 8) & 0xFF;
    const int b = rgb & 0xFF;
    return r >= minChannel && g >= minChannel && b >= minChannel &&
           std::abs(r - g) <= maxDelta &&
           std::abs(g - b) <= maxDelta &&
           std::abs(r - b) <= maxDelta;
}

std::optional<int> imageInferDominantEdgePaletteIndex(const std::vector<std::uint32_t>& pixels,
                                                      const std::vector<std::uint8_t>& paletteIndices,
                                                      int width,
                                                      int height) {
    if (width <= 0 || height <= 0) return std::nullopt;

    std::array<int, 256> counts{};
    int opaqueEdgeCount = 0;
    int dominantIndex = -1;
    int dominantCount = 0;
    for (const int index : imageEdgeIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        const int paletteIndex = static_cast<int>(paletteIndices[static_cast<std::size_t>(index)]);
        const int count = ++counts[static_cast<std::size_t>(paletteIndex)];
        ++opaqueEdgeCount;
        if (count > dominantCount) {
            dominantCount = count;
            dominantIndex = paletteIndex;
        }
    }
    if (opaqueEdgeCount == 0 || dominantIndex < 0 || imageIsUniformPaletteIndex(paletteIndices, dominantIndex)) {
        return std::nullopt;
    }

    int opaqueCornerCount = 0;
    for (const int index : imageCornerIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        ++opaqueCornerCount;
        if (static_cast<int>(paletteIndices[static_cast<std::size_t>(index)]) != dominantIndex) {
            return std::nullopt;
        }
    }
    if (opaqueCornerCount == 0 || dominantCount * 4 < opaqueEdgeCount * 3) {
        return std::nullopt;
    }
    return dominantIndex;
}

std::optional<int> imageInferWhiteEdgePaletteIndex(const std::vector<std::uint32_t>& pixels,
                                                   const std::vector<std::uint8_t>& paletteIndices,
                                                   int width,
                                                   int height) {
    if (width <= 0 || height <= 0) return std::nullopt;

    std::array<int, 256> counts{};
    int bestIndex = -1;
    int bestCount = 0;
    for (const int index : imageEdgeIndices(width, height)) {
        const auto offset = static_cast<std::size_t>(index);
        if (offset >= pixels.size() || offset >= paletteIndices.size() ||
            ((pixels[offset] >> 24) & 0xFFU) == 0) {
            continue;
        }
        const int paletteIndex = static_cast<int>(paletteIndices[offset]);
        const int rgb = imageResolvePaletteIndexRgb(pixels, paletteIndices, paletteIndex);
        if (rgb != 0xFFFFFF) {
            continue;
        }
        const int count = ++counts[static_cast<std::size_t>(paletteIndex)];
        if (count > bestCount) {
            bestCount = count;
            bestIndex = paletteIndex;
        }
    }
    return bestIndex >= 0 && !imageIsUniformPaletteIndex(paletteIndices, bestIndex)
        ? std::optional<int>(bestIndex)
        : std::nullopt;
}

std::optional<ImageFloodFillMatte> imageResolveIndexedFloodFillMatte(
    const std::vector<std::uint32_t>& pixels,
    const std::vector<std::uint8_t>& paletteIndices,
    int width,
    int height) {
    if (const auto whiteEdge = imageInferWhiteEdgePaletteIndex(pixels, paletteIndices, width, height)) {
        return ImageFloodFillMatte{
            *whiteEdge, imageResolvePaletteIndexRgb(pixels, paletteIndices, *whiteEdge), 0};
    }
    if (const auto dominant = imageInferDominantEdgePaletteIndex(pixels, paletteIndices, width, height)) {
        const int matteRgb = imageResolvePaletteIndexRgb(pixels, paletteIndices, *dominant);
        if (matteRgb == 0xFFFFFF ||
            (*dominant == 0 && imageDefaultIndexedMatteRgb(matteRgb))) {
            return ImageFloodFillMatte{*dominant, matteRgb, 0};
        }
    }
    if (imageCornerContainsPaletteIndex(paletteIndices, width, height, 0)) {
        const int indexZeroRgb = imageResolvePaletteIndexRgb(pixels, paletteIndices, 0);
        if (imageDefaultIndexedMatteRgb(indexZeroRgb)) {
            return ImageFloodFillMatte{0, indexZeroRgb, 0};
        }
    }
    return std::nullopt;
}

bool imageIsUniformRgb(const std::vector<std::uint32_t>& pixels, int rgb) {
    for (const auto pixel : pixels) {
        if (((pixel >> 24) & 0xFFU) != 0 && static_cast<int>(pixel & 0x00FFFFFFU) != rgb) {
            return false;
        }
    }
    return true;
}

std::optional<int> imageInferDominantEdgeRgb(const std::vector<std::uint32_t>& pixels, int width, int height) {
    if (width <= 0 || height <= 0) return std::nullopt;

    std::unordered_map<int, int> counts;
    int opaqueEdgeCount = 0;
    int dominantRgb = -1;
    int dominantCount = 0;
    for (const int index : imageEdgeIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        const int rgb = static_cast<int>(pixel & 0x00FFFFFFU);
        const int count = ++counts[rgb];
        ++opaqueEdgeCount;
        if (count > dominantCount) {
            dominantCount = count;
            dominantRgb = rgb;
        }
    }
    if (opaqueEdgeCount == 0 || dominantRgb < 0 || imageIsUniformRgb(pixels, dominantRgb)) {
        return std::nullopt;
    }

    int opaqueCornerCount = 0;
    for (const int index : imageCornerIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        ++opaqueCornerCount;
        if (static_cast<int>(pixel & 0x00FFFFFFU) != dominantRgb) {
            return std::nullopt;
        }
    }
    if (opaqueCornerCount == 0 || dominantCount * 4 < opaqueEdgeCount * 3) {
        return std::nullopt;
    }
    return dominantRgb;
}

bool imageCornerContainsOpaqueRgb(const std::vector<std::uint32_t>& pixels, int width, int height, int rgb) {
    for (const int index : imageCornerIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) != 0 && static_cast<int>(pixel & 0x00FFFFFFU) == rgb) {
            return true;
        }
    }
    return false;
}

bool imageEdgeContainsOpaqueRgb(const std::vector<std::uint32_t>& pixels, int width, int height, int rgb) {
    for (const int index : imageEdgeIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) != 0 && static_cast<int>(pixel & 0x00FFFFFFU) == rgb) {
            return true;
        }
    }
    return false;
}

std::optional<ImageFloodFillMatte> imageResolveRgbFloodFillMatte(const std::vector<std::uint32_t>& pixels,
                                                                 int width,
                                                                 int height) {
    if (imageEdgeContainsOpaqueRgb(pixels, width, height, 0xFFFFFF)) {
        return ImageFloodFillMatte{std::nullopt, 0xFFFFFF, 0};
    }
    if (const auto dominant = imageInferDominantEdgeRgb(pixels, width, height)) {
        if (imageIsNearWhiteGrayscale(*dominant, 232, 16) && *dominant != 0xFFFFFF) {
            return std::nullopt;
        }
        return ImageFloodFillMatte{std::nullopt, *dominant, 0};
    }
    return std::nullopt;
}

std::optional<ImageFloodFillMatte> imageResolveFloodFillMatte(
    const std::vector<std::uint32_t>& pixels,
    const std::optional<std::vector<std::uint8_t>>& paletteIndices,
    int width,
    int height) {
    if (imageHasPaletteIndices(paletteIndices, width, height)) {
        return imageResolveIndexedFloodFillMatte(pixels, *paletteIndices, width, height);
    }
    return imageResolveRgbFloodFillMatte(pixels, width, height);
}

bool imageEdgeContainsOpaquePaletteIndex(const std::vector<std::uint32_t>& pixels,
                                         const std::vector<std::uint8_t>& paletteIndices,
                                         int width,
                                         int height,
                                         int paletteIndex) {
    for (const int index : imageEdgeIndices(width, height)) {
        const auto offset = static_cast<std::size_t>(index);
        if (((pixels[offset] >> 24) & 0xFFU) != 0 &&
            offset < paletteIndices.size() &&
            static_cast<int>(paletteIndices[offset]) == paletteIndex) {
            return true;
        }
    }
    return false;
}

bool imageHasOpaqueNonPaletteIndexContent(const std::vector<std::uint32_t>& pixels,
                                          const std::vector<std::uint8_t>& paletteIndices,
                                          int paletteIndex) {
    for (std::size_t index = 0; index < pixels.size() && index < paletteIndices.size(); ++index) {
        if (((pixels[index] >> 24) & 0xFFU) != 0 &&
            static_cast<int>(paletteIndices[index]) != paletteIndex) {
            return true;
        }
    }
    return false;
}

bool imageMatchesRgb(std::uint32_t pixel, int matteRgb, int tolerance) {
    const int pr = static_cast<int>((pixel >> 16) & 0xFFU);
    const int pg = static_cast<int>((pixel >> 8) & 0xFFU);
    const int pb = static_cast<int>(pixel & 0xFFU);
    const int mr = (matteRgb >> 16) & 0xFF;
    const int mg = (matteRgb >> 8) & 0xFF;
    const int mb = matteRgb & 0xFF;
    return std::abs(pr - mr) <= tolerance && std::abs(pg - mg) <= tolerance && std::abs(pb - mb) <= tolerance;
}

bool imageIsTransparentOrMatte(const std::vector<std::uint32_t>& pixels,
                               const std::optional<std::vector<std::uint8_t>>& paletteIndices,
                               int index,
                               const ImageFloodFillMatte& matte) {
    const auto pixel = pixels[static_cast<std::size_t>(index)];
    if (((pixel >> 24) & 0xFFU) == 0) {
        return true;
    }
    if (matte.usesPaletteIndex() && paletteIndices.has_value() &&
        static_cast<std::size_t>(index) < paletteIndices->size()) {
        return static_cast<int>((*paletteIndices)[static_cast<std::size_t>(index)]) == *matte.paletteIndex;
    }
    return imageMatchesRgb(pixel, matte.colorRgb, matte.tolerance);
}

std::vector<bool> imageComputeFloodFillTransparency(
    const std::vector<std::uint32_t>& pixels,
    const std::optional<std::vector<std::uint8_t>>& paletteIndices,
    int width,
    int height,
    const ImageFloodFillMatte& matte) {
    std::vector<bool> transparent(static_cast<std::size_t>(width * height), false);
    std::queue<int> queue;
    const auto seed = [&](int x, int y) {
        const int index = y * width + x;
        if (!transparent[static_cast<std::size_t>(index)] &&
            imageIsTransparentOrMatte(pixels, paletteIndices, index, matte)) {
            transparent[static_cast<std::size_t>(index)] = true;
            queue.push(index);
        }
    };

    for (int x = 0; x < width; ++x) {
        seed(x, 0);
        seed(x, height - 1);
    }
    for (int y = 1; y < height - 1; ++y) {
        seed(0, y);
        seed(width - 1, y);
    }

    while (!queue.empty()) {
        const int index = queue.front();
        queue.pop();
        const int x = index % width;
        const int y = index / width;
        if (x > 0) seed(x - 1, y);
        if (x < width - 1) seed(x + 1, y);
        if (y > 0) seed(x, y - 1);
        if (y < height - 1) seed(x, y + 1);
    }
    return transparent;
}

bool imageIsGrayscaleMaskSource(const std::vector<std::uint32_t>& pixels, const std::vector<bool>& transparent) {
    int opaquePixels = 0;
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        if (transparent[index] || ((pixels[index] >> 24) & 0xFFU) == 0) {
            continue;
        }
        const auto r = (pixels[index] >> 16) & 0xFFU;
        const auto g = (pixels[index] >> 8) & 0xFFU;
        const auto b = pixels[index] & 0xFFU;
        if (r != g || g != b) {
            return false;
        }
        ++opaquePixels;
    }
    return opaquePixels > 0 && opaquePixels * 4 <= static_cast<int>(pixels.size()) * 3;
}

bool imageIsWhiteBackedMaskSource(const std::vector<std::uint32_t>& pixels,
                                  const std::vector<bool>& transparent,
                                  const ImageFloodFillMatte& matte) {
    if (imageMaskAlphaFromPixel(0xFF000000U | static_cast<std::uint32_t>(matte.colorRgb & 0x00FFFFFF)) < 250) {
        return false;
    }

    bool hasTransparentMatte = false;
    bool hasOpaqueInk = false;
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        if (((pixels[index] >> 24) & 0xFFU) == 0) {
            continue;
        }
        if (transparent[index]) {
            hasTransparentMatte = true;
        } else {
            hasOpaqueInk = true;
        }
        if (hasTransparentMatte && hasOpaqueInk) {
            return true;
        }
    }
    return false;
}

bool imageIsMaskSource(const std::vector<std::uint32_t>& pixels,
                       const std::vector<bool>& transparent,
                       const ImageFloodFillMatte& matte) {
    return imageIsGrayscaleMaskSource(pixels, transparent) ||
           imageIsWhiteBackedMaskSource(pixels, transparent, matte);
}

bool imageIsMostlyWhiteRegion(const bitmap::Bitmap& bitmap, const Datum::IntRect& rect) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    int sampled = 0;
    int white = 0;
    const int step = std::max(1, (width * height) / 64);
    for (int index = 0; index < width * height; index += step) {
        const int x = rect.left + (index % width);
        const int y = rect.top + (index / width);
        if (x < 0 || x >= bitmap.width() || y < 0 || y >= bitmap.height()) {
            continue;
        }
        ++sampled;
        if ((bitmap.getPixel(x, y) & 0x00FFFFFFU) == 0x00FFFFFFU) {
            ++white;
        }
    }
    return sampled > 0 && white * 4 >= sampled * 3;
}

std::optional<int> imageRegionSolidOpaqueRgb(const bitmap::Bitmap& bitmap, const Datum::IntRect& rect) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }

    std::optional<int> rgb;
    for (int y = rect.top; y < rect.bottom; ++y) {
        if (y < 0 || y >= bitmap.height()) {
            return std::nullopt;
        }
        for (int x = rect.left; x < rect.right; ++x) {
            if (x < 0 || x >= bitmap.width()) {
                return std::nullopt;
            }
            const auto pixel = bitmap.getPixel(x, y);
            if (((pixel >> 24U) & 0xFFU) == 0U) {
                return std::nullopt;
            }
            const int pixelRgb = static_cast<int>(pixel & 0x00FFFFFFU);
            if (!rgb.has_value()) {
                rgb = pixelRgb;
            } else if (*rgb != pixelRgb) {
                return std::nullopt;
            }
        }
    }
    return rgb;
}

std::optional<int> imageRegionSolidDegenerateRgb(const bitmap::Bitmap& bitmap, const Datum::IntRect& rect) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }

    std::optional<int> rgb;
    for (int y = rect.top; y < rect.bottom; ++y) {
        if (y < 0 || y >= bitmap.height()) {
            return std::nullopt;
        }
        for (int x = rect.left; x < rect.right; ++x) {
            if (x < 0 || x >= bitmap.width()) {
                return std::nullopt;
            }
            const auto pixel = bitmap.getPixel(x, y);
            if (((pixel >> 24U) & 0xFFU) > 1U) {
                return std::nullopt;
            }
            const int pixelRgb = static_cast<int>(pixel & 0x00FFFFFFU);
            if (!rgb.has_value()) {
                rgb = pixelRgb;
            } else if (*rgb != pixelRgb) {
                return std::nullopt;
            }
        }
    }
    return rgb;
}

bool imageIsDefaultWhiteRgb(int rgb) {
    return (rgb & 0x00FFFFFF) == static_cast<int>(bitmap::ColorRef::white().toPacked());
}

void imageNormalizeBackingThroughPalette(bitmap::Bitmap& bitmap,
                                         int sourceRgb,
                                         const bitmap::Palette& palette) {
    const auto targetRgb = palette.getColor(palette.nearestIndex(static_cast<std::uint32_t>(sourceRgb)));
    for (auto& pixel : bitmap.pixels()) {
        if ((pixel & 0x00FFFFFFU) == (static_cast<std::uint32_t>(sourceRgb) & 0x00FFFFFFU)) {
            pixel = (pixel & 0xFF000000U) | (targetRgb & 0x00FFFFFFU);
        }
    }
}

bool imageCopyMatteToMaskImage(bitmap::Bitmap& dest,
                               const bitmap::Bitmap& src,
                               const Datum::IntRect& destRect,
                               const Datum::IntRect& srcRect) {
    const int destWidth = destRect.right - destRect.left;
    const int destHeight = destRect.bottom - destRect.top;
    const int srcWidth = srcRect.right - srcRect.left;
    const int srcHeight = srcRect.bottom - srcRect.top;
    if (src.width() <= 0 || src.height() <= 0 ||
        srcWidth <= 0 || srcHeight <= 0 ||
        destWidth <= 0 || destHeight <= 0 ||
        dest.imagePalette() != nullptr ||
        !imageIsMostlyWhiteRegion(dest, destRect)) {
        return false;
    }

    const auto pixels = src.pixels();
    const auto paletteIndices = src.paletteIndices();
    const auto matte = imageResolveFloodFillMatte(pixels, paletteIndices, src.width(), src.height());
    if (!matte.has_value()) {
        return false;
    }
    const auto transparent =
        imageComputeFloodFillTransparency(pixels, paletteIndices, src.width(), src.height(), *matte);
    if (!imageIsMaskSource(pixels, transparent, *matte)) {
        return false;
    }

    const int matteLuma =
        imageMaskAlphaFromPixel(0xFF000000U | static_cast<std::uint32_t>(matte->colorRgb & 0x00FFFFFF));
    const bool lightMatte = matteLuma >= 128;
    for (int dy = 0; dy < destHeight; ++dy) {
        const int sy = srcRect.top + (dy * srcHeight / destHeight);
        const int py = destRect.top + dy;
        if (sy < 0 || sy >= src.height() || py < 0 || py >= dest.height()) {
            continue;
        }
        for (int dx = 0; dx < destWidth; ++dx) {
            const int sx = srcRect.left + (dx * srcWidth / destWidth);
            const int px = destRect.left + dx;
            if (sx < 0 || sx >= src.width() || px < 0 || px >= dest.width()) {
                continue;
            }
            const auto srcOffset = static_cast<std::size_t>(sy * src.width() + sx);
            if (srcOffset >= transparent.size() || transparent[srcOffset]) {
                continue;
            }
            int maskLuma = imageMaskAlphaFromPixel(pixels[srcOffset]);
            if (!lightMatte) {
                maskLuma = 255 - maskLuma;
            }
            const auto luma = static_cast<std::uint32_t>(maskLuma & 0xFF);
            dest.setPixelPreservePaletteIndex(px, py, 0xFF000000U | (luma << 16) | (luma << 8) | luma);
        }
    }
    return true;
}

int imagePercentToBlendAlpha(const Datum& blendDatum) {
    return std::clamp(static_cast<int>(std::lround(toDoubleLikeJava(blendDatum) * 255.0 / 100.0)), 0, 255);
}

bool imageIsOpaqueKeyPixel(std::uint32_t pixel, int keyRgb) {
    return ((pixel >> 24) & 0xFFU) == 255 && static_cast<int>(pixel & 0x00FFFFFFU) == (keyRgb & 0x00FFFFFF);
}

bool imageHasOpaqueBackgroundKeyBorder(const bitmap::Bitmap& src, const Datum::IntRect& rect, int keyRgb) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (src.width() <= 0 || src.height() <= 0 || width <= 0 || height <= 0) {
        return false;
    }

    const int left = std::clamp(rect.left, 0, src.width() - 1);
    const int top = std::clamp(rect.top, 0, src.height() - 1);
    const int right = std::clamp(rect.right - 1, 0, src.width() - 1);
    const int bottom = std::clamp(rect.bottom - 1, 0, src.height() - 1);

    for (int x = left; x <= right; ++x) {
        if (imageIsOpaqueKeyPixel(src.getPixel(x, top), keyRgb) ||
            imageIsOpaqueKeyPixel(src.getPixel(x, bottom), keyRgb)) {
            return true;
        }
    }
    for (int y = top + 1; y < bottom; ++y) {
        if (imageIsOpaqueKeyPixel(src.getPixel(left, y), keyRgb) ||
            imageIsOpaqueKeyPixel(src.getPixel(right, y), keyRgb)) {
            return true;
        }
    }
    return false;
}

bool imageIsInverseWhiteAlphaMask(const bitmap::Bitmap& src, const Datum::IntRect& rect) {
    bool hasOpaqueWhite = false;
    bool hasTransparentInk = false;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    for (int y = 0; y < height; ++y) {
        const int py = rect.top + y;
        if (py < 0 || py >= src.height()) {
            continue;
        }
        for (int x = 0; x < width; ++x) {
            const int px = rect.left + x;
            if (px < 0 || px >= src.width()) {
                continue;
            }
            const auto pixel = src.getPixel(px, py);
            const int alpha = static_cast<int>((pixel >> 24) & 0xFFU);
            if (alpha == 0) {
                hasTransparentInk = true;
                continue;
            }
            if ((pixel & 0x00FFFFFFU) != 0x00FFFFFFU) {
                return false;
            }
            hasOpaqueWhite = true;
        }
    }
    return hasOpaqueWhite && hasTransparentInk;
}

int imageInverseWhiteAlphaMaskInkRgb(std::optional<int> bgColorRemap) {
    return bgColorRemap.has_value() ? 0x000000 : 0x7B9498;
}

bool imageInvertedWhiteAlphaMaskHasOpaqueKeyBorder(const bitmap::Bitmap& src,
                                                   const Datum::IntRect& rect,
                                                   int keyRgb,
                                                   int inkRgb) {
    if ((inkRgb & 0x00FFFFFF) != (keyRgb & 0x00FFFFFF)) {
        return false;
    }
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (src.width() <= 0 || src.height() <= 0 || width <= 0 || height <= 0) {
        return false;
    }

    const int left = std::clamp(rect.left, 0, src.width() - 1);
    const int top = std::clamp(rect.top, 0, src.height() - 1);
    const int right = std::clamp(rect.right - 1, 0, src.width() - 1);
    const int bottom = std::clamp(rect.bottom - 1, 0, src.height() - 1);
    const auto transparentHole = [&src](int x, int y) {
        return ((src.getPixel(x, y) >> 24) & 0xFFU) == 0;
    };

    for (int x = left; x <= right; ++x) {
        if (transparentHole(x, top) || transparentHole(x, bottom)) {
            return true;
        }
    }
    for (int y = top + 1; y < bottom; ++y) {
        if (transparentHole(left, y) || transparentHole(right, y)) {
            return true;
        }
    }
    return false;
}

std::uint32_t imageInvertWhiteAlphaMaskPixel(std::uint32_t pixel, int inkRgb) {
    return ((pixel >> 24) & 0xFFU) == 0
        ? (0xFF000000U | static_cast<std::uint32_t>(inkRgb & 0x00FFFFFF))
        : 0x00000000U;
}

bool imageShouldTreatWhiteTextBackgroundAsTransparent(const bitmap::Bitmap& src,
                                                      const Datum::IntRect& rect,
                                                      id::InkMode ink,
                                                      int blend,
                                                      const std::shared_ptr<bitmap::Bitmap>& mask,
                                                      std::optional<int> colorRemap,
                                                      std::optional<int> bgColorRemap) {
    if (src.bitDepth() < 32 ||
        src.hasNativeMatteAlpha() ||
        ink != id::InkMode::COPY ||
        blend < 255 ||
        mask != nullptr ||
        colorRemap.has_value() ||
        bgColorRemap.has_value()) {
        return false;
    }

    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    int white = 0;
    int nonWhite = 0;
    int colored = 0;
    for (int y = 0; y < height; ++y) {
        const int sy = rect.top + y;
        if (sy < 0 || sy >= src.height()) {
            continue;
        }
        for (int x = 0; x < width; ++x) {
            const int sx = rect.left + x;
            if (sx < 0 || sx >= src.width()) {
                continue;
            }
            const auto pixel = src.getPixel(sx, sy);
            if (((pixel >> 24) & 0xFFU) != 255) {
                return false;
            }
            const int r = static_cast<int>((pixel >> 16) & 0xFFU);
            const int g = static_cast<int>((pixel >> 8) & 0xFFU);
            const int b = static_cast<int>(pixel & 0xFFU);
            if (r == 255 && g == 255 && b == 255) {
                ++white;
            } else {
                ++nonWhite;
                if (std::abs(r - g) > 2 || std::abs(g - b) > 2) {
                    ++colored;
                }
            }
        }
    }
    return white > 0 && nonWhite > 0 && colored == 0 && white >= nonWhite;
}

std::uint32_t imageMakeWhiteTextBackgroundTransparentPixel(std::uint32_t pixel) {
    return (pixel & 0x00FFFFFFU) == 0x00FFFFFFU ? (pixel & 0x00FFFFFFU) : pixel;
}

bool imageIsNearWhiteMattePixel(std::uint32_t pixel) {
    if (((pixel >> 24) & 0xFFU) == 0) {
        return false;
    }
    const int r = static_cast<int>((pixel >> 16) & 0xFFU);
    const int g = static_cast<int>((pixel >> 8) & 0xFFU);
    const int b = static_cast<int>(pixel & 0xFFU);
    return r >= 240 && g >= 240 && b >= 240 &&
           std::abs(r - g) <= 2 &&
           std::abs(g - b) <= 2;
}

bool imageShouldKeyNearWhiteMatte(const bitmap::Bitmap& src, const Datum::IntRect& rect, int backgroundKeyRgb) {
    if (src.bitDepth() < 32 ||
        !src.hasTransparentPixels() ||
        (backgroundKeyRgb & 0x00FFFFFF) != 0xFFFFFF ||
        rect.right <= rect.left ||
        rect.bottom <= rect.top ||
        src.width() <= 0 ||
        src.height() <= 0) {
        return false;
    }

    const int maxX = src.width() - 1;
    const int maxY = src.height() - 1;
    const int left = std::clamp(rect.left, 0, maxX);
    const int top = std::clamp(rect.top, 0, maxY);
    const int right = std::clamp(rect.right - 1, 0, maxX);
    const int bottom = std::clamp(rect.bottom - 1, 0, maxY);
    for (int x = left; x <= right; ++x) {
        if (imageIsNearWhiteMattePixel(src.getPixel(x, top)) ||
            imageIsNearWhiteMattePixel(src.getPixel(x, bottom))) {
            return true;
        }
    }
    for (int y = top + 1; y < bottom; ++y) {
        if (imageIsNearWhiteMattePixel(src.getPixel(left, y)) ||
            imageIsNearWhiteMattePixel(src.getPixel(right, y))) {
            return true;
        }
    }
    return false;
}

bool imageShouldKeyDefaultIndexedMatte(const bitmap::Bitmap& src,
                                       int backgroundKeyRgb,
                                       const std::optional<std::vector<std::uint8_t>>& paletteIndices) {
    if (src.bitDepth() > 8 ||
        (backgroundKeyRgb & 0x00FFFFFF) != 0xFFFFFF ||
        !imageHasPaletteIndices(paletteIndices, src.width(), src.height()) ||
        imageIsUniformPaletteIndex(*paletteIndices, 0)) {
        return false;
    }

    const auto& pixels = src.pixels();
    if (!imageEdgeContainsOpaquePaletteIndex(pixels, *paletteIndices, src.width(), src.height(), 0)) {
        return false;
    }

    const int indexZeroRgb = imageResolvePaletteIndexRgb(pixels, *paletteIndices, 0);
    return imageIsNearWhiteGrayscale(indexZeroRgb, 232, 16) &&
           imageHasOpaqueNonPaletteIndexContent(pixels, *paletteIndices, 0);
}

bool imagePalettesCompatibleForIndexPreserve(const bitmap::Bitmap& dest, const bitmap::Bitmap& src) {
    const auto destPalette = dest.imagePalette();
    const auto srcPalette = src.imagePalette();
    return destPalette == nullptr || srcPalette == nullptr || destPalette.get() == srcPalette.get();
}

bool imageCanPreservePaletteIndices(const bitmap::Bitmap& dest,
                                    const bitmap::Bitmap& src,
                                    const std::optional<std::vector<std::uint8_t>>& srcPaletteIndices,
                                    id::InkMode ink,
                                    int blend,
                                    const std::shared_ptr<bitmap::Bitmap>& mask,
                                    std::optional<int> colorRemap,
                                    std::optional<int> bgColorRemap) {
    return srcPaletteIndices.has_value() &&
           srcPaletteIndices->size() == src.pixels().size() &&
           src.bitDepth() <= 8 &&
           dest.bitDepth() <= 8 &&
           dest.bitDepth() >= src.bitDepth() &&
           imagePalettesCompatibleForIndexPreserve(dest, src) &&
           (ink == id::InkMode::COPY ||
            ink == id::InkMode::MATTE ||
            ink == id::InkMode::BACKGROUND_TRANSPARENT) &&
           blend >= 255 &&
           mask == nullptr &&
           !colorRemap.has_value() &&
           !bgColorRemap.has_value();
}

bool imageCanRefreshDestinationPaletteIndices(const bitmap::Bitmap& dest) {
    return dest.bitDepth() <= 8 &&
           dest.imagePalette() != nullptr &&
           dest.paletteIndices().has_value();
}

bool imageShouldSkipPaletteIndexPreserve(std::uint32_t sourcePixel, id::InkMode ink, int backgroundKeyRgb) {
    if (((sourcePixel >> 24) & 0xFFU) == 0) {
        return true;
    }
    if (ink == id::InkMode::BACKGROUND_TRANSPARENT) {
        return static_cast<int>(sourcePixel & 0x00FFFFFFU) == (backgroundKeyRgb & 0x00FFFFFF);
    }
    return false;
}

bool imageCopiedWhiteCanKeepMatteIndex(const bitmap::Palette& palette, std::uint32_t pixel, int previousIndex) {
    return previousIndex == 0 &&
           palette.size() > 0 &&
           (palette.getColor(0) & 0x00FFFFFFU) == 0x00FFFFFFU &&
           (pixel & 0x00FFFFFFU) == 0x00FFFFFFU;
}

int imageNearestCopiedRgbPaletteIndex(const bitmap::Palette& palette, std::uint32_t pixel) {
    const auto rgb = pixel & 0x00FFFFFFU;
    if (rgb != 0x00FFFFFFU || palette.size() <= 1 ||
        (palette.getColor(0) & 0x00FFFFFFU) != 0x00FFFFFFU) {
        return palette.nearestIndex(pixel);
    }

    int bestIndex = 1;
    int bestDistance = std::numeric_limits<int>::max();
    for (int index = 1; index < palette.size(); ++index) {
        const auto color = palette.getColor(index) & 0x00FFFFFFU;
        const int dr = 0xFF - static_cast<int>((color >> 16) & 0xFFU);
        const int dg = 0xFF - static_cast<int>((color >> 8) & 0xFFU);
        const int db = 0xFF - static_cast<int>(color & 0xFFU);
        const int distance = (dr * dr) + (dg * dg) + (db * db);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = index;
            if (distance == 0) {
                break;
            }
        }
    }
    return bestIndex;
}

bool imageRegionIsMostlyGrayscale(const bitmap::Bitmap& src, const Datum::IntRect& rect) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return false;

    const int step = std::max(1, (width * height) / 64);
    for (int index = 0; index < width * height; index += step) {
        const int x = rect.left + (index % width);
        const int y = rect.top + (index / width);
        if (x < 0 || x >= src.width() || y < 0 || y >= src.height()) {
            continue;
        }
        const auto pixel = src.getPixel(x, y);
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        const auto r = (pixel >> 16) & 0xFFU;
        const auto g = (pixel >> 8) & 0xFFU;
        const auto b = pixel & 0xFFU;
        if (std::abs(static_cast<int>(r) - static_cast<int>(g)) > 2 ||
            std::abs(static_cast<int>(g) - static_cast<int>(b)) > 2) {
            return false;
        }
    }
    return true;
}

bool imageIsWhiteBackedAlreadyColorized(const bitmap::Bitmap& src, const Datum::IntRect& rect, int colorRgb) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    constexpr int keyRgb = 0xFFFFFF;
    const int targetRgb = colorRgb & 0x00FFFFFF;
    bool hasKey = false;
    bool hasColored = false;
    for (int y = 0; y < height; ++y) {
        const int sy = rect.top + y;
        if (sy < 0 || sy >= src.height()) {
            continue;
        }
        for (int x = 0; x < width; ++x) {
            const int sx = rect.left + x;
            if (sx < 0 || sx >= src.width()) {
                continue;
            }
            const auto pixel = src.getPixel(sx, sy);
            if (((pixel >> 24) & 0xFFU) == 0) {
                continue;
            }
            const int rgb = static_cast<int>(pixel & 0x00FFFFFFU);
            if (rgb == keyRgb) {
                hasKey = true;
            } else if (rgb == targetRgb) {
                hasColored = true;
            } else {
                return false;
            }
        }
    }
    return hasKey && hasColored;
}

bool imageUsesIndexedShadeForDarken(const bitmap::Bitmap& src) {
    if (!src.paletteIndices().has_value() || src.bitDepth() > 8 || src.imagePalette() == nullptr) {
        return false;
    }
    const auto& name = src.imagePalette()->name();
    return equalsIgnoreCase(name, "Grayscale") ||
           equalsIgnoreCase(name, "System - Mac") ||
           equalsIgnoreCase(name, "System Mac");
}

int imageShadeForDarken(const bitmap::Bitmap& src,
                        const std::optional<std::vector<std::uint8_t>>& paletteIndices,
                        int x,
                        int y,
                        int r,
                        bool indexedShade) {
    if (indexedShade && paletteIndices.has_value() &&
        x >= 0 && x < src.width() && y >= 0 && y < src.height()) {
        const auto offset = static_cast<std::size_t>(y * src.width() + x);
        if (offset < paletteIndices->size()) {
            return 255 - static_cast<int>((*paletteIndices)[offset]);
        }
    }
    return r;
}

int imageDarkenFixedChannel(int source, int tint, bool preserveFullTint) {
    return preserveFullTint && tint == 0xFF ? source : source * tint / 256;
}

std::uint32_t imageDarkenBgTintPixel(std::uint32_t pixel, int tintRgb, std::optional<int> indexedShade) {
    const int alpha = static_cast<int>((pixel >> 24) & 0xFFU);
    const int tintR = (tintRgb >> 16) & 0xFF;
    const int tintG = (tintRgb >> 8) & 0xFF;
    const int tintB = tintRgb & 0xFF;
    const int sourceR = indexedShade.value_or(static_cast<int>((pixel >> 16) & 0xFFU));
    const int sourceG = indexedShade.value_or(static_cast<int>((pixel >> 8) & 0xFFU));
    const int sourceB = indexedShade.value_or(static_cast<int>(pixel & 0xFFU));
    const int r = sourceR * tintR / 256;
    const int g = sourceG * tintG / 256;
    const int b = sourceB * tintB / 256;
    return (static_cast<std::uint32_t>(alpha & 0xFF) << 24) |
           (static_cast<std::uint32_t>(r & 0xFF) << 16) |
           (static_cast<std::uint32_t>(g & 0xFF) << 8) |
           static_cast<std::uint32_t>(b & 0xFF);
}

std::uint32_t imageMultiplyDarkenPixel(std::uint32_t pixel,
                                       int tintRgb,
                                       const bitmap::Bitmap& src,
                                       const std::optional<std::vector<std::uint8_t>>& paletteIndices,
                                       int x,
                                       int y,
                                       bool indexedShade) {
    if (tintRgb == 0xFFFFFF) {
        return pixel;
    }
    const int alpha = static_cast<int>((pixel >> 24) & 0xFFU);
    if (alpha == 0) {
        return 0;
    }

    const int tintR = (tintRgb >> 16) & 0xFF;
    const int tintG = (tintRgb >> 8) & 0xFF;
    const int tintB = tintRgb & 0xFF;
    const int srcR = static_cast<int>((pixel >> 16) & 0xFFU);
    const int srcG = static_cast<int>((pixel >> 8) & 0xFFU);
    const int srcB = static_cast<int>(pixel & 0xFFU);
    int r = srcR * tintR / 255;
    int g = srcG * tintG / 255;
    int b = srcB * tintB / 255;

    if (paletteIndices.has_value() && src.bitDepth() <= 8) {
        const bool customPaletteColorShade = !indexedShade && src.imagePalette() != nullptr;
        if (customPaletteColorShade) {
            r = imageDarkenFixedChannel(srcR, tintR, true);
            g = imageDarkenFixedChannel(srcG, tintG, true);
            b = imageDarkenFixedChannel(srcB, tintB, true);
        } else {
            const int shade = imageShadeForDarken(src, paletteIndices, x, y, srcR, indexedShade);
            r = imageDarkenFixedChannel(shade, tintR, indexedShade);
            g = imageDarkenFixedChannel(shade, tintG, indexedShade);
            b = imageDarkenFixedChannel(shade, tintB, indexedShade);
        }
    }

    return (static_cast<std::uint32_t>(alpha & 0xFF) << 24) |
           (static_cast<std::uint32_t>(r & 0xFF) << 16) |
           (static_cast<std::uint32_t>(g & 0xFF) << 8) |
           static_cast<std::uint32_t>(b & 0xFF);
}

std::uint32_t imageRemapGrayscalePixel(std::uint32_t pixel,
                                       std::optional<int> colorRemap,
                                       std::optional<int> bgColorRemap) {
    if (!colorRemap.has_value() && !bgColorRemap.has_value()) {
        return pixel;
    }
    const int alpha = static_cast<int>((pixel >> 24) & 0xFFU);
    const int gray = static_cast<int>(((pixel >> 16) & 0xFFU));
    const int fg = colorRemap.value_or(0);
    const int bg = bgColorRemap.value_or(0xFFFFFF);
    const int fgR = (fg >> 16) & 0xFF;
    const int fgG = (fg >> 8) & 0xFF;
    const int fgB = fg & 0xFF;
    const int bgR = (bg >> 16) & 0xFF;
    const int bgG = (bg >> 8) & 0xFF;
    const int bgB = bg & 0xFF;

    if (colorRemap.has_value() && !bgColorRemap.has_value()) {
        const int maskAlpha = (255 - gray) * alpha / 255;
        return (static_cast<std::uint32_t>(maskAlpha & 0xFF) << 24) |
               (static_cast<std::uint32_t>(fgR & 0xFF) << 16) |
               (static_cast<std::uint32_t>(fgG & 0xFF) << 8) |
               static_cast<std::uint32_t>(fgB & 0xFF);
    }

    const float t = static_cast<float>(gray) / 255.0F;
    const int r = static_cast<int>((1.0F - t) * static_cast<float>(fgR) + t * static_cast<float>(bgR) + 0.5F);
    const int g = static_cast<int>((1.0F - t) * static_cast<float>(fgG) + t * static_cast<float>(bgG) + 0.5F);
    const int b = static_cast<int>((1.0F - t) * static_cast<float>(fgB) + t * static_cast<float>(bgB) + 0.5F);
    return (static_cast<std::uint32_t>(alpha & 0xFF) << 24) |
           (static_cast<std::uint32_t>(r & 0xFF) << 16) |
           (static_cast<std::uint32_t>(g & 0xFF) << 8) |
           static_cast<std::uint32_t>(b & 0xFF);
}

std::optional<std::pair<std::array<int, 4>, std::array<int, 4>>> imageQuadPoints(const Datum::List& quad) {
    if (quad.items().size() != 4) {
        return std::nullopt;
    }

    std::array<int, 4> px{};
    std::array<int, 4> py{};
    for (std::size_t index = 0; index < quad.items().size(); ++index) {
        const auto* point = quad.items()[index].asIntPoint();
        if (point == nullptr) {
            return std::nullopt;
        }
        px[index] = point->x;
        py[index] = point->y;
    }
    return std::pair{px, py};
}

Datum imagePropListWithMaskImage(const Datum::PropList& propList, std::shared_ptr<bitmap::Bitmap> mask) {
    auto copy = Datum::propList(propList.sorted());
    bool replaced = false;
    for (const auto& [key, value] : propList.properties()) {
        std::string keyStorage;
        if (equalsIgnoreCase(keyNameLikeJavaView(key, keyStorage), "maskImage")) {
            copy.propListValue().put(key, Datum::imageRef(mask));
            replaced = true;
        } else {
            copy.propListValue().put(key, value);
        }
    }
    if (!replaced) {
        copy.propListValue().put(Datum::symbol("maskImage"), Datum::imageRef(mask));
    }
    return copy;
}

Datum imageCopyPixels(bitmap::Bitmap& dest, const std::vector<Datum>& args) {
    if (args.size() < 3) return Datum::voidValue();
    const auto* srcRef = args[0].asImageRef();
    if (srcRef == nullptr || srcRef->bitmap == nullptr) return Datum::voidValue();
    const auto* destRect = args[1].asIntRect();
    const auto* srcRect = args[2].asIntRect();
    if (srcRect == nullptr) return Datum::voidValue();

    const auto& src = *srcRef->bitmap;
    const int srcWidth = srcRect->right - srcRect->left;
    const int srcHeight = srcRect->bottom - srcRect->top;
    if (destRect == nullptr) {
        if (!args[1].isList()) {
            return Datum::voidValue();
        }
        const auto quadPoints = imageQuadPoints(args[1].listValue());
        if (!quadPoints.has_value() || srcWidth <= 0 || srcHeight <= 0) {
            return Datum::voidValue();
        }

        const auto& [px, py] = *quadPoints;
        const int minX = std::min({px[0], px[1], px[2], px[3]});
        const int minY = std::min({py[0], py[1], py[2], py[3]});
        const int maxX = std::max({px[0], px[1], px[2], px[3]});
        const int maxY = std::max({py[0], py[1], py[2], py[3]});
        const int destWidth = maxX - minX;
        const int destHeight = maxY - minY;
        if (destWidth <= 0 || destHeight <= 0) {
            return Datum::voidValue();
        }

        const auto transformed = bitmap::Drawing::transformQuadBitmap(
            src, srcRect->left, srcRect->top, srcRect->right, srcRect->bottom, px, py, minX, minY, maxX, maxY);
        std::vector<Datum> transformedArgs{
            Datum::imageRef(transformed),
            Datum::intRect(minX, minY, maxX, maxY),
            Datum::intRect(0, 0, destWidth, destHeight),
        };
        if (args.size() >= 4 && args[3].isPropList()) {
            const Datum maskDatum = getPropListKey(args[3].propListValue(), "maskImage");
            if (const auto* maskRef = maskDatum.asImageRef(); maskRef != nullptr && maskRef->bitmap != nullptr) {
                const auto transformedMask = bitmap::Drawing::transformQuadBitmap(
                    *maskRef->bitmap,
                    srcRect->left,
                    srcRect->top,
                    srcRect->right,
                    srcRect->bottom,
                    px,
                    py,
                    minX,
                    minY,
                    maxX,
                    maxY);
                transformedArgs.push_back(imagePropListWithMaskImage(args[3].propListValue(), transformedMask));
            } else {
                transformedArgs.push_back(args[3]);
            }
        }
        return imageCopyPixels(dest, transformedArgs);
    }

    const int destWidth = destRect->right - destRect->left;
    const int destHeight = destRect->bottom - destRect->top;
    if (srcWidth <= 0 || srcHeight <= 0 || destWidth <= 0 || destHeight <= 0) {
        return Datum::voidValue();
    }

    int blend = 255;
    id::InkMode ink = id::InkMode::COPY;
    int backgroundKeyRgb = 0xFFFFFF;
    std::optional<int> colorRemap;
    std::optional<int> bgColorRemap;
    std::shared_ptr<bitmap::Bitmap> mask;
    if (args.size() >= 4 && args[3].isPropList()) {
        const Datum blendDatum = getPropListKey(args[3].propListValue(), "blend");
        if (!blendDatum.isVoid()) {
            blend = imagePercentToBlendAlpha(blendDatum);
        }
        const Datum inkDatum = getPropListKey(args[3].propListValue(), "ink");
        if (!inkDatum.isVoid()) {
            if (inkDatum.isInt() || inkDatum.isFloat()) {
                ink = id::inkModeFromCode(toIntLikeJava(inkDatum));
            } else if (const auto parsed = id::inkModeFromName(keyNameLikeJava(inkDatum))) {
                ink = *parsed;
            }
        }
        const Datum bgColorDatum = getPropListKey(args[3].propListValue(), "bgColor");
        if (!bgColorDatum.isVoid()) {
            bgColorRemap = static_cast<int>(imageCopyPixelsRemapColorArgb(bgColorDatum, src, dest) & 0x00FFFFFFU);
            backgroundKeyRgb = *bgColorRemap;
        }
        const Datum colorDatum = getPropListKey(args[3].propListValue(), "color");
        if (!colorDatum.isVoid()) {
            colorRemap = static_cast<int>(imageCopyPixelsRemapColorArgb(colorDatum, src, dest) & 0x00FFFFFFU);
        }
        const Datum maskDatum = getPropListKey(args[3].propListValue(), "maskImage");
        if (const auto* maskRef = maskDatum.asImageRef()) {
            mask = maskRef->bitmap;
        }
    }

    if (dest.imagePalette() == nullptr && src.imagePalette() != nullptr) {
        dest.copyPaletteMetadataFrom(&src);
        if (dest.bitDepth() > 8) {
            dest.clearPaletteIndices();
        }
    }
    if (!dest.hasAnchorPoint() && src.hasAnchorPoint()) {
        dest.setAnchorPoint(destRect->left + src.anchorX() - srcRect->left,
                            destRect->top + src.anchorY() - srcRect->top);
    }

    if (ink == id::InkMode::MATTE &&
        dest.bitDepth() <= 8 &&
        imageCopyMatteToMaskImage(dest, src, *destRect, *srcRect)) {
        return Datum::voidValue();
    }

    std::shared_ptr<bitmap::Bitmap> matteSource;
    std::shared_ptr<bitmap::Bitmap> backgroundTransparentSource;
    std::shared_ptr<bitmap::Bitmap> degenerateAlphaSource;
    const bitmap::Bitmap* effectiveSource = &src;
    if (ink == id::InkMode::MATTE) {
        matteSource = std::make_shared<bitmap::Bitmap>(bitmap::Drawing::applyFloodFillTransparency(src));
        effectiveSource = matteSource.get();
    } else if (ink == id::InkMode::BACKGROUND_TRANSPARENT) {
        backgroundTransparentSource = bitmap::Drawing::preprocessBackgroundTransparent(src, backgroundKeyRgb);
        if (backgroundTransparentSource != nullptr) {
            effectiveSource = backgroundTransparentSource.get();
        }
    }
    if (ink == id::InkMode::COPY && effectiveSource == &src && src.hasDegenerateAlphaWithRgbContent()) {
        const auto sourceBackingRgb = imageRegionSolidDegenerateRgb(src, *srcRect);
        const auto destBackingRgb = imageRegionSolidOpaqueRgb(dest, *destRect);
        const bool paletteBackedDynamicBuffer =
            sourceBackingRgb.has_value() &&
            destBackingRgb.has_value() &&
            imageIsDefaultWhiteRgb(*destBackingRgb) &&
            src.imagePalette() != nullptr;
        degenerateAlphaSource = std::make_shared<bitmap::Bitmap>(src.copyWithDegenerateAlphaOpaque());
        if (paletteBackedDynamicBuffer) {
            imageNormalizeBackingThroughPalette(*degenerateAlphaSource, *sourceBackingRgb, *src.imagePalette());
        }
        effectiveSource = degenerateAlphaSource.get();
    }
    const auto& copySrc = *effectiveSource;
    const auto srcPaletteIndices = copySrc.paletteIndices();

    const bool transparentBackgroundRemap = colorRemap.has_value() && !bgColorRemap.has_value();
    const bool alreadyColorizedWhiteBacked =
        transparentBackgroundRemap && imageIsWhiteBackedAlreadyColorized(copySrc, *srcRect, *colorRemap);
    const bool applyGrayscaleRemap =
        (colorRemap.has_value() || bgColorRemap.has_value()) &&
        !copySrc.hasNativeMatteAlpha() &&
        imageRegionIsMostlyGrayscale(copySrc, *srcRect) &&
        !alreadyColorizedWhiteBacked;
    const bool darkenBgTint =
        ink == id::InkMode::DARKEN && bgColorRemap.has_value() && !colorRemap.has_value();
    const bool indexedShadeForDarken = imageUsesIndexedShadeForDarken(copySrc);
    const bool inverseWhiteAlphaMask =
        ink == id::InkMode::BACKGROUND_TRANSPARENT &&
        src.hasNativeMatteAlpha() &&
        imageIsInverseWhiteAlphaMask(src, *srcRect);
    const int inverseWhiteAlphaMaskInkRgb = imageInverseWhiteAlphaMaskInkRgb(bgColorRemap);
    id::InkMode effectiveInk = ink;
    if (inverseWhiteAlphaMask) {
        if (!imageInvertedWhiteAlphaMaskHasOpaqueKeyBorder(
                src, *srcRect, backgroundKeyRgb, inverseWhiteAlphaMaskInkRgb)) {
            effectiveInk = id::InkMode::COPY;
        }
    } else if (effectiveInk == id::InkMode::BACKGROUND_TRANSPARENT &&
        src.hasNativeMatteAlpha() &&
        !imageHasOpaqueBackgroundKeyBorder(src, *srcRect, backgroundKeyRgb)) {
        effectiveInk = id::InkMode::COPY;
    }
    if (blend < 255 && effectiveInk == id::InkMode::COPY) {
        effectiveInk = id::InkMode::BLEND;
    }
    const bool whiteTextBackgroundTransparent = imageShouldTreatWhiteTextBackgroundAsTransparent(
        copySrc, *srcRect, effectiveInk, blend, mask, colorRemap, bgColorRemap);
    const bool keyNearWhiteMatte =
        effectiveInk == id::InkMode::BACKGROUND_TRANSPARENT &&
        imageShouldKeyNearWhiteMatte(copySrc, *srcRect, backgroundKeyRgb);
    const bool keyDefaultIndexedMatte =
        effectiveInk == id::InkMode::BACKGROUND_TRANSPARENT &&
        imageShouldKeyDefaultIndexedMatte(copySrc, backgroundKeyRgb, srcPaletteIndices);
    const bool preservePaletteIndices = imageCanPreservePaletteIndices(
        dest, copySrc, srcPaletteIndices, effectiveInk, blend, mask, colorRemap, bgColorRemap);
    const bool refreshPreservedBackgroundTransparentIndices =
        preservePaletteIndices &&
        effectiveInk == id::InkMode::BACKGROUND_TRANSPARENT &&
        imageCanRefreshDestinationPaletteIndices(dest);
    const bool refreshDestPaletteIndices =
        !preservePaletteIndices && imageCanRefreshDestinationPaletteIndices(dest);
    std::optional<std::vector<std::uint8_t>> destPaletteIndices;
    if (preservePaletteIndices || refreshDestPaletteIndices) {
        destPaletteIndices = dest.paletteIndices().value_or(
            std::vector<std::uint8_t>(dest.pixels().size(), 0));
    } else {
        dest.clearPaletteIndices();
    }

    for (int dy = 0; dy < destHeight; ++dy) {
        const int sy = srcRect->top + (dy * srcHeight / destHeight);
        const int py = destRect->top + dy;
        if (sy < 0 || sy >= copySrc.height() || py < 0 || py >= dest.height()) {
            continue;
        }
        for (int dx = 0; dx < destWidth; ++dx) {
            const int sx = srcRect->left + (dx * srcWidth / destWidth);
            const int px = destRect->left + dx;
            if (sx < 0 || sx >= copySrc.width() || px < 0 || px >= dest.width()) {
                continue;
            }
            if (mask != nullptr && !bitmap::Drawing::maskAllowsPixel(*mask, sx, sy)) {
                continue;
            }
            const auto rawSourcePixel = copySrc.getPixel(sx, sy);
            const auto srcOffset = static_cast<std::size_t>(sy * copySrc.width() + sx);
            const bool skipSource =
                (keyNearWhiteMatte && imageIsNearWhiteMattePixel(rawSourcePixel)) ||
                (keyDefaultIndexedMatte &&
                 srcPaletteIndices.has_value() &&
                 srcOffset < srcPaletteIndices->size() &&
                 static_cast<int>((*srcPaletteIndices)[srcOffset]) == 0);
            std::uint32_t sourcePixel = rawSourcePixel;
            if (!skipSource) {
                if (inverseWhiteAlphaMask) {
                    sourcePixel = imageInvertWhiteAlphaMaskPixel(rawSourcePixel, inverseWhiteAlphaMaskInkRgb);
                } else if (whiteTextBackgroundTransparent) {
                    sourcePixel = imageMakeWhiteTextBackgroundTransparentPixel(rawSourcePixel);
                } else if (applyGrayscaleRemap) {
                    if (darkenBgTint) {
                        std::optional<int> indexedShade;
                        if (indexedShadeForDarken) {
                            const int r = static_cast<int>((rawSourcePixel >> 16) & 0xFFU);
                            indexedShade = imageShadeForDarken(copySrc, srcPaletteIndices, sx, sy, r, true);
                        }
                        sourcePixel = imageDarkenBgTintPixel(rawSourcePixel, *bgColorRemap, indexedShade);
                    } else {
                        sourcePixel = imageRemapGrayscalePixel(rawSourcePixel, colorRemap, bgColorRemap);
                    }
                } else if (ink == id::InkMode::DARKEN) {
                    sourcePixel = imageMultiplyDarkenPixel(
                        rawSourcePixel,
                        bgColorRemap.value_or(0xFFFFFF),
                        copySrc,
                        srcPaletteIndices,
                        sx,
                        sy,
                        indexedShadeForDarken);
                }
            }
            const auto currentDestPixel = dest.getPixel(px, py);
            const auto destPixel = skipSource
                ? currentDestPixel
                : bitmap::Drawing::applyInk(sourcePixel, currentDestPixel, effectiveInk, blend, backgroundKeyRgb);
            dest.setPixelPreservePaletteIndex(px, py, destPixel);
            const auto destOffset = static_cast<std::size_t>(py * dest.width() + px);
            if (preservePaletteIndices && destPaletteIndices.has_value() && srcPaletteIndices.has_value()) {
                if (!skipSource && !imageShouldSkipPaletteIndexPreserve(sourcePixel, effectiveInk, backgroundKeyRgb)) {
                    if (srcOffset < srcPaletteIndices->size() && destOffset < destPaletteIndices->size()) {
                        (*destPaletteIndices)[destOffset] = (*srcPaletteIndices)[srcOffset];
                    }
                }
                if (refreshPreservedBackgroundTransparentIndices && destOffset < destPaletteIndices->size()) {
                    const auto alpha = (destPixel >> 24) & 0xFFU;
                    const auto palette = dest.imagePalette();
                    if (alpha != 0 && palette != nullptr) {
                        const int previousIndex = static_cast<int>((*destPaletteIndices)[destOffset]);
                        const int paletteIndex = imageCopiedWhiteCanKeepMatteIndex(*palette, destPixel, previousIndex)
                            ? previousIndex
                            : imageNearestCopiedRgbPaletteIndex(*palette, destPixel);
                        (*destPaletteIndices)[destOffset] = static_cast<std::uint8_t>(paletteIndex & 0xFF);
                        dest.setPixelPreservePaletteIndex(
                            px,
                            py,
                            (destPixel & 0xFF000000U) | (palette->getColor(paletteIndex) & 0x00FFFFFFU));
                    }
                }
            } else if (refreshDestPaletteIndices && destPaletteIndices.has_value() &&
                       destOffset < destPaletteIndices->size()) {
                const auto alpha = (destPixel >> 24) & 0xFFU;
                const auto palette = dest.imagePalette();
                if (alpha != 0 && palette != nullptr) {
                    const int previousIndex = static_cast<int>((*destPaletteIndices)[destOffset]);
                    const int paletteIndex = imageCopiedWhiteCanKeepMatteIndex(*palette, destPixel, previousIndex)
                        ? previousIndex
                        : imageNearestCopiedRgbPaletteIndex(*palette, destPixel);
                    (*destPaletteIndices)[destOffset] = static_cast<std::uint8_t>(paletteIndex & 0xFF);
                    dest.setPixelPreservePaletteIndex(
                        px,
                        py,
                        (destPixel & 0xFF000000U) | (palette->getColor(paletteIndex) & 0x00FFFFFFU));
                }
            }
        }
    }

    if (destPaletteIndices.has_value()) {
        dest.setPaletteIndices(std::move(*destPaletteIndices));
    }
    return Datum::voidValue();
}

Datum imageObjectMethod(const Datum::ImageRef& image, std::string_view methodName, const std::vector<Datum>& args) {
    if (image.bitmap == nullptr) {
        if (equalsIgnoreCase(methodName, "duplicate")) {
            return Datum::imageRef(nullptr);
        }
        if (equalsIgnoreCase(methodName, "getAt")) {
            if (args.empty()) return Datum::voidValue();
            const int index = toIntLikeJava(args[0]);
            return index == 1 || index == 2 ? Datum::of(0) : Datum::voidValue();
        }
        return Datum::voidValue();
    }

    auto& bmp = *image.bitmap;
    if (equalsIgnoreCase(methodName, "fill")) {
        if (imageFill(bmp, args)) {
            notifyImageMutation(image);
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "setAlpha")) {
        notifyImageMutation(image);
        return imageSetAlpha(bmp, args);
    }
    if (equalsIgnoreCase(methodName, "draw")) {
        notifyImageMutation(image);
        return imageDraw(bmp, args);
    }
    if (equalsIgnoreCase(methodName, "createMatte")) {
        const int alphaThreshold = !args.empty() && !args[0].isVoid() ? toIntLikeJava(args[0]) : 0;
        return Datum::imageRef(bitmap::Drawing::createMatte(bmp, alphaThreshold));
    }
    if (equalsIgnoreCase(methodName, "createMask")) {
        const int alphaThreshold = !args.empty() && !args[0].isVoid() ? toIntLikeJava(args[0]) : 0;
        return Datum::imageRef(bitmap::Drawing::createMask(bmp, alphaThreshold));
    }
    if (equalsIgnoreCase(methodName, "copyPixels")) {
        const auto* srcRef = args.size() >= 1 ? args[0].asImageRef() : nullptr;
        const auto* destRect = args.size() >= 2 ? args[1].asIntRect() : nullptr;
        const auto* srcRect = args.size() >= 3 ? args[2].asIntRect() : nullptr;
        int traceInk = 0;
        if (args.size() >= 4 && args[3].isPropList()) {
            const Datum inkDatum = getPropListKey(args[3].propListValue(), "ink");
            if (!inkDatum.isVoid()) {
                traceInk = toIntLikeJava(inkDatum);
            }
        }
        std::ostringstream traceBefore;
        traceBefore << "{\"id\":" << imageOperationTraceNextId++
                    << ",\"op\":\"copyPixels.before\",\"ink\":" << traceInk
                    << ",\"destRect\":";
        appendTraceRectJson(traceBefore, destRect);
        traceBefore << ",\"srcRect\":";
        appendTraceRectJson(traceBefore, srcRect);
        appendTraceBitmapSummary(traceBefore, "dest", bmp);
        if (srcRef != nullptr && srcRef->bitmap != nullptr) {
            appendTraceBitmapSummary(traceBefore, "src", *srcRef->bitmap);
        }
        traceBefore << '}';
        pushImageOperationTrace(traceBefore.str());
        notifyImageMutation(bmp);
        auto result = imageCopyPixels(bmp, args);
        if (image.mutationCallback) {
            image.mutationCallback(bmp);
        }
        std::ostringstream traceAfter;
        traceAfter << "{\"id\":" << imageOperationTraceNextId++
                   << ",\"op\":\"copyPixels.after\",\"ink\":" << traceInk
                   << ",\"destRect\":";
        appendTraceRectJson(traceAfter, destRect);
        traceAfter << ",\"srcRect\":";
        appendTraceRectJson(traceAfter, srcRect);
        appendTraceBitmapSummary(traceAfter, "dest", bmp);
        traceAfter << '}';
        pushImageOperationTrace(traceAfter.str());
        return result;
    }
    if (equalsIgnoreCase(methodName, "duplicate")) {
        return Datum::imageRef(std::make_shared<bitmap::Bitmap>(bmp.copy()));
    }
    if (equalsIgnoreCase(methodName, "crop")) {
        if (args.empty()) return Datum::voidValue();
        const auto* rect = args[0].asIntRect();
        if (rect == nullptr) return Datum::voidValue();
        const int width = rect->right - rect->left;
        const int height = rect->bottom - rect->top;
        if (width <= 0 || height <= 0) return Datum::voidValue();
        return Datum::imageRef(std::make_shared<bitmap::Bitmap>(bmp.getRegion(rect->left, rect->top, width, height)));
    }
    if (equalsIgnoreCase(methodName, "trimWhiteSpace")) {
        const auto bounds = bmp.trimWhiteSpace();
        std::ostringstream trace;
        trace << "{\"id\":" << imageOperationTraceNextId++
              << ",\"op\":\"trimWhiteSpace\",\"bounds\":";
        const Datum::IntRect boundsRect{bounds.left, bounds.top, bounds.right, bounds.bottom};
        appendTraceRectJson(trace, &boundsRect);
        appendTraceBitmapSummary(trace, "src", bmp);
        trace << '}';
        pushImageOperationTrace(trace.str());
        if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
            auto empty = std::make_shared<bitmap::Bitmap>(1, 1, bmp.bitDepth());
            empty->fill(0xFFFFFFFFU);
            return Datum::imageRef(std::move(empty));
        }
        return Datum::imageRef(std::make_shared<bitmap::Bitmap>(
            bmp.getRegion(bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top)));
    }
    if (equalsIgnoreCase(methodName, "getAt")) {
        if (args.empty()) return Datum::voidValue();
        const int index = toIntLikeJava(args[0]);
        if (index == 1) return Datum::of(bmp.width());
        if (index == 2) return Datum::of(bmp.height());
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "getPixel")) {
        if (args.size() < 2) return Datum::voidValue();
        const int x = toIntLikeJava(args[0]);
        const int y = toIntLikeJava(args[1]);
        if (x < 0 || x >= bmp.width() || y < 0 || y >= bmp.height()) {
            return Datum::voidValue();
        }
        if (const auto index = bmp.paletteIndex(x, y)) {
            return Datum::paletteIndexColor(*index);
        }
        const auto pixel = bmp.getPixel(x, y);
        return Datum::colorRef(static_cast<int>((pixel >> 16) & 0xFF),
                               static_cast<int>((pixel >> 8) & 0xFF),
                               static_cast<int>(pixel & 0xFF));
    }
    if (equalsIgnoreCase(methodName, "setPixel")) {
        if (args.size() >= 3) {
            const int x = toIntLikeJava(args[0]);
            const int y = toIntLikeJava(args[1]);
            if (x >= 0 && x < bmp.width() && y >= 0 && y < bmp.height()) {
                bmp.setPixel(x, y, imageColorArgb(args[2]));
                notifyImageMutation(image);
            }
        }
        return Datum::voidValue();
    }
    return Datum::voidValue();
}

Datum castLibObjectMethod(ExecutionContext& context,
                          const Datum::CastLibRef& castLib,
                          std::string_view methodName,
                          std::span<const Datum> args) {
    std::string propNameStorage;
    if ((!equalsIgnoreCase(methodName, "getProp") && !equalsIgnoreCase(methodName, "getPropRef")) || args.size() < 2 ||
        !equalsIgnoreCase(keyNameLikeJavaView(args[0], propNameStorage), "member")) {
        return Datum::voidValue();
    }

    auto* builtinContext = context.builtinContext();
    if (builtinContext == nullptr) {
        return Datum::voidValue();
    }

    const Datum& key = args[1];
    if (key.isInt() || key.isFloat()) {
        return builtinContext->castMemberResolver ? builtinContext->castMemberResolver(castLib.castLib, toIntLikeJava(key))
                                                  : Datum::voidValue();
    }
    return builtinContext->castMemberNameResolver ? builtinContext->castMemberNameResolver(castLib.castLib, toStringLikeJava(key))
                                                  : Datum::voidValue();
}

Datum castLibMemberAccessorObjectMethod(ExecutionContext& context,
                                        const Datum::CastLibMemberAccessor& accessor,
                                        std::string_view methodName,
                                        std::span<const Datum> args) {
    if (args.empty() ||
        (!equalsIgnoreCase(methodName, "getAt") && !equalsIgnoreCase(methodName, "getProp") &&
         !equalsIgnoreCase(methodName, "getPropRef"))) {
        return Datum::voidValue();
    }
    return getCastLibMemberAccessorValue(context, accessor, args[0]);
}

Datum castMemberObjectMethod(ExecutionContext& context,
                             const Datum::CastMemberRef& member,
                             std::string_view methodName,
                             const std::vector<Datum>& args) {
    auto* builtinContext = context.builtinContext();
    if (builtinContext == nullptr || !builtinContext->castMemberMethodHandler) {
        return Datum::voidValue();
    }
    return builtinContext->castMemberMethodHandler(member.castLib, member.memberNum(), std::string(methodName), args);
}

Datum spriteObjectMethod(ExecutionContext& context,
                         const Datum::SpriteRef& sprite,
                         std::string_view methodName,
                         const std::vector<Datum>& args) {
    auto* builtinContext = context.builtinContext();
    if (builtinContext == nullptr) {
        return Datum::voidValue();
    }

    if (builtinContext->spriteProperties != nullptr) {
        auto* scripts = builtinContext->spriteProperties->mutableScriptInstanceList(sprite.channel);
        if (scripts != nullptr) {
            for (auto& scriptInstance : *scripts) {
                if (scriptInstance.type() != DatumType::ScriptInstanceRef) {
                    continue;
                }
                Datum result = dispatch::ScriptInstanceMethodDispatcher::dispatch(
                    context,
                    scriptInstance,
                    methodName,
                    args);
                if (!result.isVoid()) {
                    return result;
                }
            }
        }
    }

    if (!builtinContext->spriteMethodHandler) {
        return Datum::voidValue();
    }
    return builtinContext->spriteMethodHandler(sprite.channel, std::string(methodName), args);
}

Datum getContextVar(ExecutionContext& context,
                    id::VarType varType,
                    const Datum& idDatum,
                    const std::optional<Datum>& fieldCastIdDatum = std::nullopt);
void setContextVar(ExecutionContext& context,
                   id::VarType varType,
                   const Datum& idDatum,
                   Datum value,
                   const std::optional<Datum>& fieldCastIdDatum = std::nullopt);

Datum chunkRefObjectMethod(ExecutionContext& context,
                           const Datum::ChunkRef& chunkRef,
                           std::string_view methodName,
                           const std::vector<Datum>&) {
    if (!equalsIgnoreCase(methodName, "delete")) {
        return Datum::voidValue();
    }

    const Datum current = getContextVar(context, chunkRef.varType, Datum::of(chunkRef.rawIndex));
    std::string currentStorage;
    const std::string_view currentString = stringViewLikeJava(current, currentStorage);
    std::string newValue;

    if (chunkRef.chunkType == StringChunkType::Char) {
        newValue = deleteCharChunkRefValue(currentString, chunkRef.start, chunkRef.end);
    } else {
        int first = chunkRef.start;
        int last = chunkRef.end;
        const char itemDelimiter = currentItemDelimiter(context);
        if (first < 0 || last < 0) {
            const int count = countChunks(currentString, chunkRef.chunkType, itemDelimiter);
            if (first < 0) {
                first = count;
            }
            if (last < 0) {
                last = count;
            }
        }
        newValue = deleteChunkValue(currentString, chunkRef.chunkType, first, last, itemDelimiter);
    }

    setContextVar(context,
                  chunkRef.varType,
                  Datum::of(chunkRef.rawIndex),
                  Datum::of(std::move(newValue)));
    return Datum::voidValue();
}

Datum scriptRefObjectMethod(ExecutionContext& context,
                            const Datum::ScriptRef& scriptRef,
                            std::string_view methodName,
                            const std::vector<Datum>& args) {
    if (!equalsIgnoreCase(methodName, "new")) {
        return Datum::voidValue();
    }

    std::vector<Datum> fullArgs;
    fullArgs.reserve(args.size() + 1);
    fullArgs.push_back(Datum::scriptRef(scriptRef.memberRef));
    fullArgs.insert(fullArgs.end(), args.begin(), args.end());
    if (const auto result = context.invokeBuiltinIfPresent("new", fullArgs)) {
        return *result;
    }
    return Datum::voidValue();
}

Datum varRefObjectMethod(ExecutionContext& context,
                         const Datum::VarRef& varRef,
                         std::string_view methodName,
                         std::span<const Datum> args) {
    Datum value = getContextVar(context, varRef.varType, Datum::of(varRef.rawIndex));
    if (equalsIgnoreCase(methodName, "getProp")) {
        if (args.size() < 2) {
            return Datum::of(std::string());
        }
        StringChunkType chunkType = StringChunkType::Char;
        std::string chunkNameStorage;
        const auto chunkTypeValue = stringChunkTypeByNameNoThrow(keyNameLikeJavaView(args[0], chunkNameStorage));
        if (!chunkTypeValue.has_value()) {
            chunkType = StringChunkType::Char;
        } else {
            chunkType = *chunkTypeValue;
        }
        const int start = toIntLikeJava(args[1]);
        const int end = args.size() >= 3 ? toIntLikeJava(args[2]) : start;
        std::string valueStorage;
        const std::string_view valueText = stringViewLikeJava(value, valueStorage);
        if (chunkType == StringChunkType::Char) {
            return Datum::of(getCharChunkRefValue(valueText, start, end));
        }
        return Datum::of(resolveChunkRange(valueText, chunkType, start, end, currentItemDelimiter(context)));
    }
    if (equalsIgnoreCase(methodName, "getPropRef")) {
        if (args.size() < 2) {
            return Datum::voidValue();
        }
        StringChunkType chunkType = StringChunkType::Char;
        std::string chunkNameStorage;
        const auto chunkTypeValue = stringChunkTypeByNameNoThrow(keyNameLikeJavaView(args[0], chunkNameStorage));
        if (!chunkTypeValue.has_value()) {
            chunkType = StringChunkType::Char;
        } else {
            chunkType = *chunkTypeValue;
        }
        const int start = toIntLikeJava(args[1]);
        const int end = args.size() >= 3 ? toIntLikeJava(args[2]) : start;
        return Datum::chunkRef(varRef.varType, varRef.rawIndex, chunkType, start, end);
    }
    if (value.isList()) {
        return dispatch::ListMethodDispatcher::dispatch(value.listValue(), methodName, args);
    }
    if (value.isPropList()) {
        return dispatch::PropListMethodDispatcher::dispatch(value.propListValue(), methodName, args);
    }
    if (value.isString()) {
        std::string valueStorage;
        return dispatch::StringMethodDispatcher::dispatch(stringViewLikeJava(value, valueStorage),
                                                          methodName,
                                                          args,
                                                          currentItemDelimiter(context));
    }
    if (auto* point = value.asIntPoint()) {
        return pointObjectMethod(*point, methodName, args);
    }
    if (auto* rect = value.asIntRect()) {
        return rectObjectMethod(*rect, methodName, args);
    }
    if (value.type() == DatumType::ScriptInstanceRef) {
        return scriptInstanceObjectMethod(context, value, methodName, args);
    }
    return Datum::voidValue();
}

Datum dispatchObjectMethod(ExecutionContext& context, Datum target, std::string_view methodName, const std::vector<Datum>& args) {
    if (const auto* varRef = target.asVarRef()) {
        return varRefObjectMethod(context, *varRef, methodName, args);
    }
    if (const auto* chunkRef = target.asChunkRef()) {
        return chunkRefObjectMethod(context, *chunkRef, methodName, args);
    }
    if (const auto* scriptRef = target.asScriptRef()) {
        return scriptRefObjectMethod(context, *scriptRef, methodName, args);
    }
    auto* builtinContext = context.builtinContext();
    if (const auto* timeout = target.asTimeoutRef()) {
        return builtinContext != nullptr ? builtin::TimeoutBuiltins::handleMethod(*builtinContext, *timeout, methodName, args)
                                         : Datum::voidValue();
    }
    if (const auto* soundChannel = target.asSoundChannel()) {
        return dispatch::SoundChannelMethodDispatcher::dispatch(builtinContext, *soundChannel, methodName, args);
    }
    if (const auto* xtraInstance = target.asXtraInstance()) {
        return builtinContext != nullptr ? builtin::XtraBuiltins::callHandler(*builtinContext, *xtraInstance, methodName, args)
                                         : Datum::voidValue();
    }
    if (const auto* sprite = target.asSpriteRef()) {
        return spriteObjectMethod(context, *sprite, methodName, args);
    }
    if (const auto* castLib = target.asCastLibRef()) {
        return castLibObjectMethod(context, *castLib, methodName, args);
    }
    if (const auto* accessor = target.asCastLibMemberAccessor()) {
        return castLibMemberAccessorObjectMethod(context, *accessor, methodName, args);
    }
    if (target.isList()) {
        return dispatch::ListMethodDispatcher::dispatch(target.listValue(), methodName, args);
    }
    if (target.isPropList()) {
        return dispatch::PropListMethodDispatcher::dispatch(target.propListValue(), methodName, args);
    }
    if (target.isString()) {
        std::string targetStorage;
        return dispatch::StringMethodDispatcher::dispatch(stringViewLikeJava(target, targetStorage),
                                                          methodName,
                                                          args,
                                                          currentItemDelimiter(context));
    }
    if (auto* point = target.asIntPoint()) {
        return pointObjectMethod(*point, methodName, args);
    }
    if (auto* rect = target.asIntRect()) {
        return rectObjectMethod(*rect, methodName, args);
    }
    if (const auto* image = target.asImageRef()) {
        return dispatch::ImageMethodDispatcher::dispatch(*image, methodName, args);
    }
    if (const auto* member = target.asCastMemberRef()) {
        return castMemberObjectMethod(context, *member, methodName, args);
    }
    if (target.type() == DatumType::ScriptInstanceRef) {
        return dispatch::ScriptInstanceMethodDispatcher::dispatch(context, target, methodName, args);
    }
    if (target.type() == DatumType::MovieRef) {
        if (auto builtinResult = context.invokeBuiltinIfPresent(methodName, args)) {
            return std::move(*builtinResult);
        }
        return Datum::voidValue();
    }

    std::vector<Datum> fullArgs;
    fullArgs.reserve(args.size() + 1);
    fullArgs.push_back(target);
    fullArgs.insert(fullArgs.end(), args.begin(), args.end());
    if (auto builtinResult = context.invokeBuiltinIfPresent(methodName, fullArgs)) {
        return std::move(*builtinResult);
    }
    return Datum::voidValue();
}

Datum dispatchObjectMethodSpan(ExecutionContext& context,
                               Datum target,
                               std::string_view methodName,
                               std::span<const Datum> args) {
    if (const auto* varRef = target.asVarRef()) {
        return varRefObjectMethod(context, *varRef, methodName, args);
    }
    if (target.isList()) {
        return dispatch::ListMethodDispatcher::dispatch(target.listValue(), methodName, args);
    }
    if (target.isPropList()) {
        return dispatch::PropListMethodDispatcher::dispatch(target.propListValue(), methodName, args);
    }
    if (target.isString()) {
        std::string targetStorage;
        return dispatch::StringMethodDispatcher::dispatch(stringViewLikeJava(target, targetStorage),
                                                          methodName,
                                                          args,
                                                          currentItemDelimiter(context));
    }
    if (auto* point = target.asIntPoint()) {
        return pointObjectMethod(*point, methodName, args);
    }
    if (auto* rect = target.asIntRect()) {
        return rectObjectMethod(*rect, methodName, args);
    }
    if (const auto* castLib = target.asCastLibRef()) {
        return castLibObjectMethod(context, *castLib, methodName, args);
    }
    if (const auto* accessor = target.asCastLibMemberAccessor()) {
        return castLibMemberAccessorObjectMethod(context, *accessor, methodName, args);
    }
    if (target.type() == DatumType::ScriptInstanceRef) {
        return scriptInstanceObjectMethod(context, target, methodName, args);
    }

    if (args.empty()) {
        return dispatchObjectMethod(context, std::move(target), methodName, emptyDatumArgs());
    }
    const std::vector<Datum> argVector(args.begin(), args.end());
    return dispatchObjectMethod(context, std::move(target), methodName, argVector);
}

const std::vector<Datum>& emptyDatumArgs() {
    static const std::vector<Datum> empty;
    return empty;
}

bool pushZero(ExecutionContext& context) {
    context.push(Datum::of(0));
    return true;
}

bool pushInt(ExecutionContext& context) {
    context.push(Datum::of(context.argument()));
    return true;
}

bool pushFloat(ExecutionContext& context) {
    context.push(Datum::of(std::bit_cast<float>(context.argument())));
    return true;
}

bool pushCons(ExecutionContext& context) {
    const auto& literals = context.literals();
    const int index = context.scaledArgument();
    if (index >= 0 && index < static_cast<int>(literals.size())) {
        context.push(literalToDatum(literals[static_cast<std::size_t>(index)]));
    } else {
        context.push(Datum::voidValue());
    }
    return true;
}

bool pushSymb(ExecutionContext& context) {
    context.push(Datum::symbol(context.resolveNameRef(context.argument())));
    return true;
}

bool pushChunkVarRef(ExecutionContext& context) {
    const int rawIndex = toIntLikeJava(context.pop());
    context.push(Datum::varRef(id::varTypeFromCode(context.argument()), rawIndex));
    return true;
}

bool swap(ExecutionContext& context) {
    context.swap();
    return true;
}

bool pop(ExecutionContext& context) {
    const int count = context.argument();
    if (count <= 1) {
        (void)context.pop();
    } else {
        for (int index = 0; index < count; ++index) {
            (void)context.pop();
        }
    }
    return true;
}

bool peek(ExecutionContext& context) {
    context.push(context.peek(context.argument()));
    return true;
}

Datum fallbackScriptInstance(const Datum& scriptArg) {
    if (const auto* scriptRef = scriptArg.asScriptRef()) {
        return Datum::scriptInstance("script", scriptRef->memberRef);
    }
    if (const auto* memberRef = scriptArg.asCastMemberRef()) {
        return Datum::scriptInstance("script", *memberRef);
    }
    if (const auto* symbol = scriptArg.asSymbol()) {
        return Datum::scriptInstance(symbol->name);
    }
    if (scriptArg.isString()) {
        return Datum::scriptInstance(scriptArg.stringValue());
    }
    return Datum::scriptInstance(toStringLikeJava(scriptArg));
}

struct ScriptConstructorTarget {
    Datum target{Datum::voidValue()};
    std::size_t consumedArgs{1};
};

std::optional<int> resolveScriptConstructorScope(builtin::BuiltinContext& builtinContext, const Datum& scopeArg) {
    if (const auto* castLib = scopeArg.asCastLibRef()) {
        return castLib->castLib >= 1 ? std::optional<int>(castLib->castLib) : std::nullopt;
    }
    if ((scopeArg.isString() || scopeArg.asSymbol() != nullptr) && builtinContext.castLibNameResolver) {
        const int castLib = builtinContext.castLibNameResolver(keyNameLikeJava(scopeArg));
        return castLib >= 1 ? std::optional<int>(castLib) : std::nullopt;
    }
    return std::nullopt;
}

Datum resolveNamedScriptConstructorTarget(builtin::BuiltinContext& builtinContext,
                                          const Datum& identifier,
                                          std::optional<int> scopedCastLib) {
    if (!builtinContext.castMemberNameResolver || (!identifier.isString() && identifier.asSymbol() == nullptr)) {
        return Datum::voidValue();
    }
    const int castLib = scopedCastLib.value_or(0);
    const Datum memberRef = builtinContext.castMemberNameResolver(castLib, keyNameLikeJava(identifier));
    if (const auto* member = memberRef.asCastMemberRef()) {
        return Datum::scriptRef(*member);
    }
    return Datum::voidValue();
}

ScriptConstructorTarget resolveScriptConstructorTarget(ExecutionContext& context, const std::vector<Datum>& args) {
    const Datum& scriptArg = args.front();
    if (scriptArg.asScriptRef() != nullptr || scriptArg.asCastMemberRef() != nullptr) {
        return ScriptConstructorTarget{scriptArg, 1};
    }
    auto* builtinContext = context.builtinContext();
    if (builtinContext != nullptr && args.size() >= 2) {
        if (const auto scopedCastLib = resolveScriptConstructorScope(*builtinContext, args[1])) {
            if (builtinContext->castMemberNameResolver) {
                Datum scopedResolved = resolveNamedScriptConstructorTarget(*builtinContext, scriptArg, scopedCastLib);
                if (scopedResolved.asScriptRef() != nullptr || scopedResolved.asCastMemberRef() != nullptr) {
                    return ScriptConstructorTarget{std::move(scopedResolved), 2};
                }
            }
            if ((args[1].asCastLibRef() != nullptr || args[1].isString() || args[1].asSymbol() != nullptr) &&
                builtinContext->scriptResolver) {
                Datum resolved = builtinContext->scriptResolver(scriptArg, args[1]);
                if (resolved.asScriptRef() != nullptr || resolved.asCastMemberRef() != nullptr) {
                    return ScriptConstructorTarget{std::move(resolved), 2};
                }
            }
        }
    }
    if (builtinContext != nullptr) {
        Datum resolved = resolveNamedScriptConstructorTarget(*builtinContext, scriptArg, std::nullopt);
        if (resolved.asScriptRef() != nullptr || resolved.asCastMemberRef() != nullptr) {
            return ScriptConstructorTarget{std::move(resolved), 1};
        }
    }
    if (builtinContext != nullptr && builtinContext->scriptResolver) {
        Datum resolved = builtinContext->scriptResolver(scriptArg, std::nullopt);
        if (resolved.asScriptRef() != nullptr || resolved.asCastMemberRef() != nullptr) {
            return ScriptConstructorTarget{std::move(resolved), 1};
        }
    }
    return ScriptConstructorTarget{scriptArg, 1};
}

std::optional<Datum::CastMemberRef> scriptConstructorMemberRef(const Datum& scriptArg) {
    if (const auto* scriptRef = scriptArg.asScriptRef()) {
        return scriptRef->memberRef;
    }
    if (const auto* memberRef = scriptArg.asCastMemberRef()) {
        return *memberRef;
    }
    return std::nullopt;
}

bool shouldInvokeBuiltinNewForScriptConstructor(ExecutionContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return false;
    }
    auto* builtinContext = context.builtinContext();
    if (builtinContext != nullptr && builtinContext->newInstanceHandler) {
        return true;
    }
    if (args.front().asXtra() != nullptr) {
        return true;
    }
    return args.front().asSymbol() != nullptr && args.size() > 1 && args[1].asCastLibRef() != nullptr;
}

std::uint64_t scriptPropertyCacheKey(int castLib, int memberNum) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(castLib)) << 32U) |
           static_cast<std::uint32_t>(memberNum);
}

const std::vector<std::string>& declaredScriptPropertyNames(builtin::BuiltinContext& context,
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

void initializeDeclaredScriptProperties(ExecutionContext& context, Datum& instance, const Datum::CastMemberRef& memberRef) {
    auto* builtinContext = context.builtinContext();
    if (builtinContext == nullptr || !builtinContext->scriptPropertyNamesResolver ||
        instance.type() != DatumType::ScriptInstanceRef) {
        return;
    }
    const auto& propertyNames = declaredScriptPropertyNames(*builtinContext, memberRef);
    auto& scriptInstance = instance.scriptInstanceValue();
    scriptInstance.reserveLocalProperties(propertyNames.size());
    for (const auto& propertyName : propertyNames) {
        scriptInstance.appendLocalProperty(propertyName, Datum::voidValue());
    }
}

Datum executeScriptNewHandler(ExecutionContext& context, const std::vector<Datum>& constructorArgs, Datum& receiver) {
    if (receiver.type() == DatumType::ScriptInstanceRef) {
        auto& instance = receiver.scriptInstanceValue();
        if (const auto handler = findScriptInstanceScriptHandler(context, instance, "new")) {
            return safeExecuteHandler(context, handlerRefFromLocation(*handler), constructorArgs, receiver);
        }
    }

    const auto handler = context.findHandler("new");
    if (!handler) {
        return Datum::voidValue();
    }
    return safeExecuteHandler(context, *handler, constructorArgs, receiver);
}

bool newObj(ExecutionContext& context) {
    const std::string& objectType = context.resolveNameRef(context.argument());
    if (!equalsIgnoreCase(objectType, "script")) {
        context.push(Datum::voidValue());
        return true;
    }

    const Datum argListDatum = context.pop();
    std::vector<Datum> argStorage;
    const std::vector<Datum>& args = argListItemsRef(argListDatum, argStorage);
    if (args.empty()) {
        context.push(Datum::voidValue());
        return true;
    }

    if (shouldInvokeBuiltinNewForScriptConstructor(context, args)) {
        if (const auto result = context.invokeBuiltinIfPresent("new", args); result && !result->isVoid()) {
            context.push(*result);
            return true;
        }
    }

    const ScriptConstructorTarget constructorTarget = resolveScriptConstructorTarget(context, args);
    Datum instance = fallbackScriptInstance(constructorTarget.target);
    if (const auto memberRef = scriptConstructorMemberRef(constructorTarget.target)) {
        initializeDeclaredScriptProperties(context, instance, *memberRef);
        std::vector<Datum> constructorArgs;
        if (constructorTarget.consumedArgs < args.size()) {
            constructorArgs.assign(args.begin() +
                                       static_cast<std::vector<Datum>::difference_type>(constructorTarget.consumedArgs),
                                   args.end());
        }
        (void)executeScriptNewHandler(context, constructorArgs, instance);
    }
    context.push(std::move(instance));
    return true;
}

bool ret(ExecutionContext& context) {
    context.setReturnValue(context.pop());
    return true;
}

bool retFactory(ExecutionContext& context) {
    context.setReturnValue(Datum::voidValue());
    return true;
}

bool jmp(ExecutionContext& context) {
    context.jumpTo(context.instructionOffset() + context.argument());
    return false;
}

bool jmpIfZero(ExecutionContext& context) {
    const bool condition = truthy(context.peekRef());
    context.scope().drop(1);
    if (!condition) {
        context.scope().clearIndexedCollectionSnapshots(context.scope().bytecodeIndex());
        context.jumpTo(context.instructionOffset() + context.argument());
        return false;
    }
    return true;
}

bool endRepeat(ExecutionContext& context) {
    context.jumpTo(context.instructionOffset() - context.argument());
    return false;
}

bool add(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const auto* bi = context.peekRef(0).asInt();
        const auto* ai = context.peekRef(1).asInt();
        if (ai != nullptr && bi != nullptr) {
            context.scope().replaceTopTwo(Datum::of(static_cast<int>(static_cast<std::int64_t>(ai->value) + bi->value)));
            return true;
        }
    }

    const Datum b = context.pop();
    const Datum a = context.pop();

    if (const auto* point = a.asIntPoint()) {
        int dx = toIntLikeJava(b);
        int dy = dx;
        if (const auto* other = b.asIntPoint()) {
            dx = other->x;
            dy = other->y;
        } else if (b.isList()) {
            const auto delta = listAsPointDelta(b.listValue());
            dx = delta.x;
            dy = delta.y;
        }
        context.push(Datum::intPoint(point->x + dx, point->y + dy));
        return true;
    }
    if (const auto* point = b.asIntPoint(); point != nullptr && a.isList()) {
        const auto delta = listAsPointDelta(a.listValue());
        context.push(Datum::intPoint(delta.x + point->x, delta.y + point->y));
        return true;
    }
    if (const auto* rect = a.asIntRect()) {
        int dl = toIntLikeJava(b);
        int dt = dl;
        int dr = dl;
        int db = dl;
        if (const auto* other = b.asIntRect()) {
            dl = other->left;
            dt = other->top;
            dr = other->right;
            db = other->bottom;
        } else if (b.isList()) {
            const auto delta = listAsRectDelta(b.listValue());
            dl = delta.left;
            dt = delta.top;
            dr = delta.right;
            db = delta.bottom;
        }
        context.push(Datum::intRect(rect->left + dl, rect->top + dt, rect->right + dr, rect->bottom + db));
        return true;
    }
    if (a.isList() && b.isList()) {
        const auto& lhs = a.listValue().items();
        const auto& rhs = b.listValue().items();
        std::vector<Datum> result;
        const auto count = std::min(lhs.size(), rhs.size());
        result.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const double value = toDoubleLikeJava(lhs[index]) + toDoubleLikeJava(rhs[index]);
            result.push_back(numericResult(lhs[index], rhs[index], value));
        }
        context.push(Datum::list(std::move(result)));
        return true;
    }
    if (a.isList()) {
        context.push(addScalarToList(a.listValue(), b));
        return true;
    }
    if (b.isList()) {
        context.push(addScalarToList(b.listValue(), a));
        return true;
    }
    if (const auto* lhs = a.asColorRef()) {
        if (const auto* rhs = b.asColorRef()) {
            context.push(Datum::colorRef(std::min(255, lhs->r + rhs->r),
                                         std::min(255, lhs->g + rhs->g),
                                         std::min(255, lhs->b + rhs->b)));
            return true;
        }
    }

    context.push(numericResult(a, b, toDoubleLikeJava(a) + toDoubleLikeJava(b)));
    return true;
}

bool sub(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const auto* bi = context.peekRef(0).asInt();
        const auto* ai = context.peekRef(1).asInt();
        if (ai != nullptr && bi != nullptr) {
            context.scope().replaceTopTwo(Datum::of(static_cast<int>(static_cast<std::int64_t>(ai->value) - bi->value)));
            return true;
        }
    }

    const Datum b = context.pop();
    const Datum a = context.pop();

    if (const auto* point = a.asIntPoint()) {
        int dx = toIntLikeJava(b);
        int dy = dx;
        if (const auto* other = b.asIntPoint()) {
            dx = other->x;
            dy = other->y;
        } else if (b.isList()) {
            const auto delta = listAsPointDelta(b.listValue());
            dx = delta.x;
            dy = delta.y;
        }
        context.push(Datum::intPoint(point->x - dx, point->y - dy));
        return true;
    }
    if (const auto* rect = a.asIntRect()) {
        int dl = toIntLikeJava(b);
        int dt = dl;
        int dr = dl;
        int db = dl;
        if (const auto* other = b.asIntRect()) {
            dl = other->left;
            dt = other->top;
            dr = other->right;
            db = other->bottom;
        } else if (b.isList()) {
            const auto delta = listAsRectDelta(b.listValue());
            dl = delta.left;
            dt = delta.top;
            dr = delta.right;
            db = delta.bottom;
        }
        context.push(Datum::intRect(rect->left - dl, rect->top - dt, rect->right - dr, rect->bottom - db));
        return true;
    }
    if (a.isList() && b.isList()) {
        const auto& lhs = a.listValue().items();
        const auto& rhs = b.listValue().items();
        std::vector<Datum> result;
        const auto count = std::min(lhs.size(), rhs.size());
        result.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const double value = toDoubleLikeJava(lhs[index]) - toDoubleLikeJava(rhs[index]);
            result.push_back(numericResult(lhs[index], rhs[index], value));
        }
        context.push(Datum::list(std::move(result)));
        return true;
    }
    if (a.isList()) {
        context.push(subtractScalarFromList(a.listValue(), b));
        return true;
    }
    if (b.isList()) {
        context.push(subtractListFromScalar(a, b.listValue()));
        return true;
    }
    if (const auto* lhs = a.asColorRef()) {
        if (const auto* rhs = b.asColorRef()) {
            context.push(Datum::colorRef(std::max(0, lhs->r - rhs->r),
                                         std::max(0, lhs->g - rhs->g),
                                         std::max(0, lhs->b - rhs->b)));
            return true;
        }
    }

    context.push(numericResult(a, b, toDoubleLikeJava(a) - toDoubleLikeJava(b)));
    return true;
}

bool mul(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const auto* bi = context.peekRef(0).asInt();
        const auto* ai = context.peekRef(1).asInt();
        if (ai != nullptr && bi != nullptr) {
            context.scope().replaceTopTwo(Datum::of(static_cast<int>(static_cast<std::int64_t>(ai->value) * bi->value)));
            return true;
        }
    }

    const Datum b = context.pop();
    const Datum a = context.pop();

    if (const auto* point = a.asIntPoint()) {
        const double scalar = toDoubleLikeJava(b);
        context.push(Datum::intPoint(static_cast<int>(point->x * scalar), static_cast<int>(point->y * scalar)));
        return true;
    }
    if (const auto* point = b.asIntPoint()) {
        const double scalar = toDoubleLikeJava(a);
        context.push(Datum::intPoint(static_cast<int>(point->x * scalar), static_cast<int>(point->y * scalar)));
        return true;
    }
    if (const auto* rect = a.asIntRect()) {
        const double scalar = toDoubleLikeJava(b);
        context.push(Datum::intRect(static_cast<int>(rect->left * scalar),
                                    static_cast<int>(rect->top * scalar),
                                    static_cast<int>(rect->right * scalar),
                                    static_cast<int>(rect->bottom * scalar)));
        return true;
    }
    if (const auto* rect = b.asIntRect()) {
        const double scalar = toDoubleLikeJava(a);
        context.push(Datum::intRect(static_cast<int>(rect->left * scalar),
                                    static_cast<int>(rect->top * scalar),
                                    static_cast<int>(rect->right * scalar),
                                    static_cast<int>(rect->bottom * scalar)));
        return true;
    }
    if (a.isList() && !b.isList()) {
        context.push(scaleList(a.listValue(), toDoubleLikeJava(b), isFloatLike(b)));
        return true;
    }
    if (b.isList() && !a.isList()) {
        context.push(scaleList(b.listValue(), toDoubleLikeJava(a), isFloatLike(a)));
        return true;
    }

    context.push(numericResult(a, b, toDoubleLikeJava(a) * toDoubleLikeJava(b)));
    return true;
}

bool div(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const auto* bi = context.peekRef(0).asInt();
        const auto* ai = context.peekRef(1).asInt();
        if (ai != nullptr && bi != nullptr) {
            if (bi->value == 0) {
                throw context.error("Division by zero");
            }
            context.scope().replaceTopTwo(Datum::of(ai->value / bi->value));
            return true;
        }
    }

    const Datum b = context.pop();
    const Datum a = context.pop();
    const double divisor = toDoubleLikeJava(b);
    if (divisor == 0.0) {
        throw context.error("Division by zero");
    }

    if (const auto* point = a.asIntPoint()) {
        context.push(Datum::intPoint(static_cast<int>(point->x / divisor), static_cast<int>(point->y / divisor)));
        return true;
    }
    if (const auto* rect = a.asIntRect()) {
        context.push(Datum::intRect(static_cast<int>(rect->left / divisor),
                                    static_cast<int>(rect->top / divisor),
                                    static_cast<int>(rect->right / divisor),
                                    static_cast<int>(rect->bottom / divisor)));
        return true;
    }
    if (a.isList()) {
        context.push(divideList(a.listValue(), b));
        return true;
    }

    context.push(numericResult(a, b, toDoubleLikeJava(a) / divisor));
    return true;
}

bool mod(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const auto* bi = context.peekRef(0).asInt();
        const auto* ai = context.peekRef(1).asInt();
        if (ai != nullptr && bi != nullptr) {
            if (bi->value == 0) {
                throw context.error("Modulo by zero");
            }
            context.scope().replaceTopTwo(Datum::of(ai->value % bi->value));
            return true;
        }
    }

    const Datum b = context.pop();
    const Datum a = context.pop();
    const int divisor = toIntLikeJava(b);
    if (divisor == 0) {
        throw context.error("Modulo by zero");
    }
    if (a.isList()) {
        context.push(modList(a.listValue(), divisor));
        return true;
    }
    context.push(Datum::of(toIntLikeJava(a) % divisor));
    return true;
}

bool inv(ExecutionContext& context) {
    if (const auto* intValue = context.peekRef().asInt()) {
        context.scope().replaceTop(Datum::of(-intValue->value));
        return true;
    }

    const Datum value = context.pop();
    if (const auto* floatValue = value.asFloat()) {
        context.push(Datum::of(-floatValue->value));
    } else if (const auto* point = value.asIntPoint()) {
        context.push(Datum::intPoint(-point->x, -point->y));
    } else if (const auto* rect = value.asIntRect()) {
        context.push(Datum::intRect(-rect->left, -rect->top, -rect->right, -rect->bottom));
    } else {
        context.push(Datum::of(-toIntLikeJava(value)));
    }
    return true;
}

bool lt(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const auto* bi = context.peekRef(0).asInt();
        const auto* ai = context.peekRef(1).asInt();
        if (ai != nullptr && bi != nullptr) {
            context.scope().replaceTopTwo(ai->value < bi->value ? Datum::TRUE : Datum::FALSE);
            return true;
        }
    }

    const Datum b = context.pop();
    const Datum a = context.pop();
    context.push(toDoubleLikeJava(a) < toDoubleLikeJava(b) ? Datum::TRUE : Datum::FALSE);
    return true;
}

bool ltEq(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const auto* bi = context.peekRef(0).asInt();
        const auto* ai = context.peekRef(1).asInt();
        if (ai != nullptr && bi != nullptr) {
            context.scope().replaceTopTwo(ai->value <= bi->value ? Datum::TRUE : Datum::FALSE);
            return true;
        }
    }

    const Datum b = context.pop();
    const Datum a = context.pop();
    context.push(toDoubleLikeJava(a) <= toDoubleLikeJava(b) ? Datum::TRUE : Datum::FALSE);
    return true;
}

bool gt(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const auto* bi = context.peekRef(0).asInt();
        const auto* ai = context.peekRef(1).asInt();
        if (ai != nullptr && bi != nullptr) {
            context.scope().replaceTopTwo(ai->value > bi->value ? Datum::TRUE : Datum::FALSE);
            return true;
        }
    }

    const Datum b = context.pop();
    const Datum a = context.pop();
    context.push(toDoubleLikeJava(a) > toDoubleLikeJava(b) ? Datum::TRUE : Datum::FALSE);
    return true;
}

bool gtEq(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const auto* bi = context.peekRef(0).asInt();
        const auto* ai = context.peekRef(1).asInt();
        if (ai != nullptr && bi != nullptr) {
            context.scope().replaceTopTwo(ai->value >= bi->value ? Datum::TRUE : Datum::FALSE);
            return true;
        }
    }

    const Datum b = context.pop();
    const Datum a = context.pop();
    context.push(toDoubleLikeJava(a) >= toDoubleLikeJava(b) ? Datum::TRUE : Datum::FALSE);
    return true;
}

bool eq(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const auto* bi = context.peekRef(0).asInt();
        const auto* ai = context.peekRef(1).asInt();
        if (ai != nullptr && bi != nullptr) {
            context.scope().replaceTopTwo(ai->value == bi->value ? Datum::TRUE : Datum::FALSE);
            return true;
        }
    }

    const Datum b = context.pop();
    const Datum a = context.pop();
    context.push(lingoEquals(a, b) ? Datum::TRUE : Datum::FALSE);
    return true;
}

bool notEq(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const auto* bi = context.peekRef(0).asInt();
        const auto* ai = context.peekRef(1).asInt();
        if (ai != nullptr && bi != nullptr) {
            context.scope().replaceTopTwo(ai->value != bi->value ? Datum::TRUE : Datum::FALSE);
            return true;
        }
    }

    const Datum b = context.pop();
    const Datum a = context.pop();
    context.push(!lingoEquals(a, b) ? Datum::TRUE : Datum::FALSE);
    return true;
}

std::optional<int> spriteChannelFromDatum(const Datum& datum) {
    if (const auto* spriteRef = datum.asSpriteRef()) {
        return spriteRef->spriteNum();
    }
    if (datum.asInt() != nullptr || datum.asFloat() != nullptr) {
        return toIntLikeJava(datum);
    }
    return std::nullopt;
}

bool spriteCollision(ExecutionContext& context, std::string_view methodName) {
    const Datum second = context.pop();
    const Datum first = context.pop();
    const auto firstChannel = spriteChannelFromDatum(first);
    auto* builtinContext = context.builtinContext();
    if (!firstChannel.has_value() || *firstChannel <= 0 || builtinContext == nullptr) {
        context.push(Datum::FALSE);
        return true;
    }

    Datum result = Datum::voidValue();
    if (builtinContext->spriteMethodHandler) {
        result = builtinContext->spriteMethodHandler(*firstChannel, std::string(methodName), {second});
    } else if (builtinContext->spriteProperties != nullptr) {
        result = builtinContext->spriteProperties->callSpriteMethod(*firstChannel, methodName, {second});
    }
    context.push(!result.isVoid() && truthy(result) ? Datum::TRUE : Datum::FALSE);
    return true;
}

bool spriteIntersects(ExecutionContext& context) {
    return spriteCollision(context, "intersects");
}

bool spriteWithin(ExecutionContext& context) {
    return spriteCollision(context, "within");
}

bool logicalAnd(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const bool result = truthy(context.peekRef(1)) && truthy(context.peekRef(0));
        context.scope().replaceTopTwo(result ? Datum::TRUE : Datum::FALSE);
    } else {
        context.scope().drop(context.scope().stackSize());
        context.push(Datum::FALSE);
    }
    return true;
}

bool logicalOr(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const bool result = truthy(context.peekRef(1)) || truthy(context.peekRef(0));
        context.scope().replaceTopTwo(result ? Datum::TRUE : Datum::FALSE);
    } else {
        context.scope().drop(context.scope().stackSize());
        context.push(Datum::FALSE);
    }
    return true;
}

bool logicalNot(ExecutionContext& context) {
    const bool result = truthy(context.peekRef());
    context.scope().replaceTop(result ? Datum::FALSE : Datum::TRUE);
    return true;
}

bool joinStr(ExecutionContext& context) {
    Datum b = context.pop();
    Datum a = context.pop();
    std::string aStorage;
    std::string bStorage;
    const std::string_view aString = stringViewLikeJava(a, aStorage);
    const std::string_view bString = stringViewLikeJava(b, bStorage);
    if (aString.empty()) {
        context.push(b.asString() != nullptr ? b : Datum::of(std::string(bString)));
        return true;
    }
    if (bString.empty()) {
        context.push(a.asString() != nullptr ? a : Datum::of(std::string(aString)));
        return true;
    }
    std::string result;
    result.reserve(aString.size() + bString.size());
    result.append(aString);
    result.append(bString);
    context.push(Datum::of(std::move(result)));
    return true;
}

bool joinPadStr(ExecutionContext& context) {
    Datum b = context.pop();
    Datum a = context.pop();
    std::string aStorage;
    std::string bStorage;
    const std::string_view aString = stringViewLikeJava(a, aStorage);
    const std::string_view bString = stringViewLikeJava(b, bStorage);
    if (aString.empty()) {
        context.push(b.asString() != nullptr ? b : Datum::of(std::string(bString)));
        return true;
    }
    if (bString.empty()) {
        context.push(a.asString() != nullptr ? a : Datum::of(std::string(aString)));
        return true;
    }
    std::string result;
    result.reserve(aString.size() + 1 + bString.size());
    result.append(aString);
    result.push_back(' ');
    result.append(bString);
    context.push(Datum::of(std::move(result)));
    return true;
}

bool containsStr(ExecutionContext& context) {
    const Datum& needle = context.peekRef(0);
    const Datum& haystack = context.peekRef(1);
    std::string needleStorage;
    std::string haystackStorage;
    const bool result = containsIgnoreCase(stringViewLikeJava(haystack, haystackStorage),
                                           stringViewLikeJava(needle, needleStorage));
    context.scope().drop(2);
    context.push(result ? Datum::TRUE : Datum::FALSE);
    return true;
}

bool contains0Str(ExecutionContext& context) {
    const Datum& needle = context.peekRef(0);
    const Datum& haystack = context.peekRef(1);
    if (haystack.isVoid()) {
        context.scope().drop(2);
        context.push(Datum::FALSE);
        return true;
    }
    std::string needleStorage;
    std::string haystackStorage;
    const bool result = startsWithIgnoreCase(stringViewLikeJava(haystack, haystackStorage),
                                             stringViewLikeJava(needle, needleStorage));
    context.scope().drop(2);
    context.push(result ? Datum::TRUE : Datum::FALSE);
    return true;
}

bool getChunk(ExecutionContext& context) {
    const int lastLine = toIntLikeJava(context.peekRef(1));
    const int firstLine = toIntLikeJava(context.peekRef(2));
    const int lastItem = toIntLikeJava(context.peekRef(3));
    const int firstItem = toIntLikeJava(context.peekRef(4));
    const int lastWord = toIntLikeJava(context.peekRef(5));
    const int firstWord = toIntLikeJava(context.peekRef(6));
    const int lastChar = toIntLikeJava(context.peekRef(7));
    const int firstChar = toIntLikeJava(context.peekRef(8));

    std::string valueStorage;
    const std::string_view value = stringViewLikeJava(context.peekRef(0), valueStorage);
    Datum resultDatum;
    if (firstChar != 0 && lastChar == 0 &&
        firstWord == 0 && lastWord == 0 &&
        firstItem == 0 && lastItem == 0 &&
        firstLine == 0 && lastLine == 0) {
        const int resolvedChar = firstChar < 0 ? static_cast<int>(value.size()) : firstChar;
        const int index = resolvedChar - 1;
        if (index >= 0 && index < static_cast<int>(value.size())) {
            resultDatum = Datum::of(std::string(1, value[static_cast<std::size_t>(index)]));
        } else {
            resultDatum = Datum::of(std::string());
        }
        context.scope().drop(9);
        context.push(std::move(resultDatum));
        return true;
    }

    if (firstChar != 0 && lastChar != 0 &&
        firstWord == 0 && lastWord == 0 &&
        firstItem == 0 && lastItem == 0 &&
        firstLine == 0 && lastLine == 0) {
        const int resolvedFirst = firstChar < 0 ? static_cast<int>(value.size()) : firstChar;
        const int resolvedLast = lastChar < 0 ? static_cast<int>(value.size()) : lastChar;
        const int start = resolvedFirst - 1;
        const int end = std::min(resolvedLast, static_cast<int>(value.size()));
        if (start >= 0 && start < static_cast<int>(value.size()) && end > start) {
            resultDatum = Datum::of(std::string(value.substr(static_cast<std::size_t>(start),
                                                             static_cast<std::size_t>(end - start))));
        } else {
            resultDatum = Datum::of(std::string());
        }
        context.scope().drop(9);
        context.push(std::move(resultDatum));
        return true;
    }

    const char itemDelimiter = currentItemDelimiter(context);
    std::string resultStorage;
    std::string_view result = value;
    if (firstLine != 0 || lastLine != 0) {
        resultStorage = resolveChunkRange(result, StringChunkType::Line, firstLine, lastLine, itemDelimiter);
        result = resultStorage;
    }
    if (firstItem != 0 || lastItem != 0) {
        resultStorage = resolveChunkRange(result, StringChunkType::Item, firstItem, lastItem, itemDelimiter);
        result = resultStorage;
    }
    if (firstWord != 0 || lastWord != 0) {
        resultStorage = resolveChunkRange(result, StringChunkType::Word, firstWord, lastWord, itemDelimiter);
        result = resultStorage;
    }
    if (firstChar != 0 || lastChar != 0) {
        resultStorage = resolveChunkRange(result, StringChunkType::Char, firstChar, lastChar, itemDelimiter);
        result = resultStorage;
    }
    resultDatum = Datum::of(std::string(result));
    context.scope().drop(9);
    context.push(std::move(resultDatum));
    return true;
}

Datum normalizedFieldIdentifier(const Datum& idDatum) {
    if (idDatum.isString() || idDatum.isInt()) {
        return idDatum;
    }
    return Datum::of(toStringLikeJava(idDatum));
}

Datum getContextVar(ExecutionContext& context,
                    id::VarType varType,
                    const Datum& idDatum,
                    const std::optional<Datum>& fieldCastIdDatum) {
    switch (varType) {
        case id::VarType::LOCAL:
            return context.getLocal(toIntLikeJava(idDatum) / context.variableMultiplier());
        case id::VarType::PARAM:
            return context.getParam(toIntLikeJava(idDatum) / context.variableMultiplier());
        case id::VarType::GLOBAL:
        case id::VarType::GLOBAL2:
            return context.getGlobal(context.resolveNameRef(toIntLikeJava(idDatum)));
        case id::VarType::PROPERTY: {
            const Datum receiver = context.scope().receiver();
            if (receiver.type() == DatumType::ScriptInstanceRef) {
                return util::getProperty(receiver.scriptInstanceValue(), context.resolveNameRef(toIntLikeJava(idDatum)));
            }
            return Datum::voidValue();
        }
        case id::VarType::FIELD: {
            auto* builtinContext = context.builtinContext();
            if (builtinContext != nullptr && builtinContext->fieldResolver) {
                const int castId = fieldCastIdDatum ? toIntLikeJava(*fieldCastIdDatum) : 0;
                return builtinContext->fieldResolver(normalizedFieldIdentifier(idDatum), castId);
            }
            return Datum::of(std::string());
        }
    }
    return Datum::voidValue();
}

void setContextVar(ExecutionContext& context,
                   id::VarType varType,
                   const Datum& idDatum,
                   Datum value,
                   const std::optional<Datum>& fieldCastIdDatum) {
    switch (varType) {
        case id::VarType::LOCAL:
            context.setLocal(toIntLikeJava(idDatum) / context.variableMultiplier(), std::move(value));
            return;
        case id::VarType::PARAM:
            context.setParam(toIntLikeJava(idDatum) / context.variableMultiplier(), std::move(value));
            return;
        case id::VarType::GLOBAL:
        case id::VarType::GLOBAL2:
            context.setGlobal(context.resolveNameRef(toIntLikeJava(idDatum)), std::move(value));
            return;
        case id::VarType::PROPERTY: {
            Datum receiver = context.scope().receiver();
            if (receiver.type() == DatumType::ScriptInstanceRef) {
                const std::string& propName = context.resolveNameRef(toIntLikeJava(idDatum));
                if (!context.hasVariableSetListener()) {
                    util::setProperty(receiver.scriptInstanceValue(), propName, std::move(value));
                    return;
                }
                const Datum tracedValue = value;
                util::setProperty(receiver.scriptInstanceValue(), propName, std::move(value));
                context.tracePropertySet(propName, tracedValue);
            }
            return;
        }
        case id::VarType::FIELD: {
            auto* builtinContext = context.builtinContext();
            if (builtinContext != nullptr && builtinContext->fieldSetter) {
                const int castId = fieldCastIdDatum ? toIntLikeJava(*fieldCastIdDatum) : 0;
                builtinContext->fieldSetter(normalizedFieldIdentifier(idDatum), castId, toStringLikeJava(value));
            }
            return;
        }
    }
}

bool put(ExecutionContext& context) {
    const int encoded = context.argument();
    const int putType = (encoded >> 4) & 0xF;
    const auto varType = id::varTypeFromCode(encoded & 0xF);

    std::optional<Datum> fieldCastIdDatum;
    if (varType == id::VarType::FIELD) {
        fieldCastIdDatum = context.pop();
    }
    const Datum idDatum = context.pop();
    Datum value = context.pop();

    switch (putType) {
        case 1:
            setContextVar(context, varType, idDatum, std::move(value), fieldCastIdDatum);
            break;
        case 3: {
            const Datum current = getContextVar(context, varType, idDatum, fieldCastIdDatum);
            const std::string valueString = toStringLikeJava(value);
            const std::string currentString = toStringLikeJava(current);
            const std::string newValue = currentString.empty()
                                             ? valueString
                                             : valueString.empty() ? currentString : valueString + currentString;
            setContextVar(context, varType, idDatum, Datum::of(newValue), fieldCastIdDatum);
            break;
        }
        case 2: {
            const Datum current = getContextVar(context, varType, idDatum, fieldCastIdDatum);
            const std::string currentString = toStringLikeJava(current);
            const std::string valueString = toStringLikeJava(value);
            const std::string newValue = currentString.empty()
                                             ? valueString
                                             : valueString.empty() ? currentString : currentString + valueString;
            setContextVar(context, varType, idDatum, Datum::of(newValue), fieldCastIdDatum);
            break;
        }
        default:
            break;
    }
    return true;
}

bool deleteChunk(ExecutionContext& context) {
    const auto varType = id::varTypeFromCode(context.argument());
    std::optional<Datum> fieldCastIdDatum;
    if (varType == id::VarType::FIELD) {
        fieldCastIdDatum = context.pop();
    }
    const Datum idDatum = context.pop();

    const int lastLine = toIntLikeJava(context.pop());
    const int firstLine = toIntLikeJava(context.pop());
    const int lastItem = toIntLikeJava(context.pop());
    const int firstItem = toIntLikeJava(context.pop());
    const int lastWord = toIntLikeJava(context.pop());
    const int firstWord = toIntLikeJava(context.pop());
    const int lastChar = toIntLikeJava(context.pop());
    const int firstChar = toIntLikeJava(context.pop());

    ChunkSpec chunk = chooseSingleChunkSpec(firstChar,
                                            lastChar,
                                            firstWord,
                                            lastWord,
                                            firstItem,
                                            lastItem,
                                            firstLine,
                                            lastLine);

    const Datum current = getContextVar(context, varType, idDatum, fieldCastIdDatum);
    const std::string currentString = toStringLikeJava(current);
    const char itemDelimiter = currentItemDelimiter(context);

    if (chunk.type == StringChunkType::Char) {
        if (chunk.first < 0) {
            chunk.first = static_cast<int>(currentString.size());
        }
        if (chunk.last < 0) {
            chunk.last = static_cast<int>(currentString.size());
        }
    } else if (chunk.first < 0 || chunk.last < 0) {
        const int count = countChunks(currentString, chunk.type, itemDelimiter);
        if (chunk.first < 0) {
            chunk.first = count;
        }
        if (chunk.last < 0) {
            chunk.last = count;
        }
    }

    setContextVar(context,
                  varType,
                  idDatum,
                  Datum::of(deleteChunkValue(currentString, chunk.type, chunk.first, chunk.last, itemDelimiter)),
                  fieldCastIdDatum);
    return true;
}

bool putChunk(ExecutionContext& context) {
    const int encoded = context.argument();
    const int putType = (encoded >> 4) & 0xF;
    const auto varType = id::varTypeFromCode(encoded & 0xF);

    std::optional<Datum> fieldCastIdDatum;
    if (varType == id::VarType::FIELD) {
        fieldCastIdDatum = context.pop();
    }
    const Datum idDatum = context.pop();
    const Datum value = context.pop();

    const int lastLine = toIntLikeJava(context.pop());
    const int firstLine = toIntLikeJava(context.pop());
    const int lastItem = toIntLikeJava(context.pop());
    const int firstItem = toIntLikeJava(context.pop());
    const int lastWord = toIntLikeJava(context.pop());
    const int firstWord = toIntLikeJava(context.pop());
    const int lastChar = toIntLikeJava(context.pop());
    const int firstChar = toIntLikeJava(context.pop());

    ChunkSpec chunk = chooseSingleChunkSpec(firstChar,
                                            lastChar,
                                            firstWord,
                                            lastWord,
                                            firstItem,
                                            lastItem,
                                            firstLine,
                                            lastLine);

    const std::string currentString = toStringLikeJava(getContextVar(context, varType, idDatum, fieldCastIdDatum));
    const std::string valueString = toStringLikeJava(value);
    const char itemDelimiter = currentItemDelimiter(context);
    std::string newString = currentString;

    if (chunk.type == StringChunkType::Char) {
        switch (putType) {
            case 1:
                if (const auto range = charChunkReplaceRange(currentString, chunk.first, chunk.last)) {
                    newString = std::string(currentString.substr(0, static_cast<std::size_t>(range->first))) +
                                valueString +
                                std::string(currentString.substr(static_cast<std::size_t>(range->second)));
                }
                break;
            case 3:
                newString = putBeforeChunkValue(currentString, chunk.type, chunk.first, valueString, itemDelimiter);
                break;
            case 2:
                newString = putAfterChunkValue(currentString,
                                               chunk.type,
                                               chunk.last == 0 ? chunk.first : chunk.last,
                                               valueString,
                                               itemDelimiter);
                break;
            default:
                break;
        }
        setContextVar(context, varType, idDatum, Datum::of(std::move(newString)), fieldCastIdDatum);
        return true;
    }

    if (chunk.first < 0 || chunk.last < 0) {
        const int count = countChunks(currentString, chunk.type, itemDelimiter);
        if (chunk.first < 0) {
            chunk.first = count;
        }
        if (chunk.last < 0) {
            chunk.last = count;
        }
    }

    switch (putType) {
        case 1:
            newString = putIntoChunkValue(currentString, chunk.type, chunk.first, chunk.last, valueString, itemDelimiter);
            break;
        case 3:
            newString = putBeforeChunkValue(currentString, chunk.type, chunk.first, valueString, itemDelimiter);
            break;
        case 2:
            newString = putAfterChunkValue(currentString,
                                           chunk.type,
                                           chunk.last == 0 ? chunk.first : chunk.last,
                                           valueString,
                                           itemDelimiter);
            break;
        default:
            break;
    }

    setContextVar(context, varType, idDatum, Datum::of(std::move(newString)), fieldCastIdDatum);
    return true;
}

bool getLocal(ExecutionContext& context) {
    context.scope().pushLocal(context.scaledArgument());
    return true;
}

bool setLocal(ExecutionContext& context) {
    context.setLocal(context.scaledArgument(), context.pop());
    return true;
}

bool getParam(ExecutionContext& context) {
    context.scope().pushParam(context.scaledArgument());
    return true;
}

bool setParam(ExecutionContext& context) {
    context.setParam(context.scaledArgument(), context.pop());
    return true;
}

bool getGlobal(ExecutionContext& context) {
    const std::string& name = context.resolveNameRef(context.argument());
    context.push(context.getGlobal(name));
    return true;
}

bool setGlobal(ExecutionContext& context) {
    const std::string& name = context.resolveNameRef(context.argument());
    context.setGlobal(name, context.pop());
    return true;
}

bool pushList(ExecutionContext& context) {
    Datum argListDatum = context.pop();
    std::vector<Datum> items = argListItems(argListDatum);
    if (items.empty() && argListDatum.type() != DatumType::ArgList && argListDatum.type() != DatumType::ArgListNoRet &&
        !argListDatum.isVoid()) {
        items.push_back(std::move(argListDatum));
    }
    context.push(Datum::list(std::move(items)));
    return true;
}

bool pushPropList(ExecutionContext& context) {
    const Datum argListDatum = context.pop();
    std::vector<Datum> itemStorage;
    const std::vector<Datum>& items = argListItemsRef(argListDatum, itemStorage);
    Datum propList = Datum::propList();
    auto& properties = propList.propListValue().properties();
    properties.reserve(items.size() / 2);
    for (std::size_t index = 0; index + 1 < items.size(); index += 2) {
        properties.emplace_back(items[index], items[index + 1]);
    }
    context.push(std::move(propList));
    return true;
}

bool tryImmediateObjCall(ExecutionContext& context, bool noReturn);
bool tryImmediateExtCall(ExecutionContext& context, bool noReturn);

bool pushArgList(ExecutionContext& context) {
    if (tryImmediateObjCall(context, false)) {
        return true;
    }
    if (tryImmediateExtCall(context, false)) {
        return true;
    }
    context.push(Datum::argList(context.popArgs(context.argument())));
    return true;
}

bool pushArgListNoRet(ExecutionContext& context) {
    if (tryImmediateObjCall(context, true)) {
        return true;
    }
    if (tryImmediateExtCall(context, true)) {
        return true;
    }
    context.push(Datum::argListNoRet(context.popArgs(context.argument())));
    return true;
}

bool getProp(ExecutionContext& context) {
    const std::string& propName = context.resolveNameRef(context.argument());
    const Datum receiver = context.scope().receiver();
    if (receiver.type() == DatumType::ScriptInstanceRef) {
        context.push(util::getProperty(receiver.scriptInstanceValue(), propName));
    } else {
        context.push(Datum::voidValue());
    }
    return true;
}

bool setProp(ExecutionContext& context) {
    const std::string& propName = context.resolveNameRef(context.argument());
    Datum receiver = context.scope().receiver();
    Datum value = context.pop();
    if (receiver.type() == DatumType::ScriptInstanceRef) {
        if (!context.hasVariableSetListener()) {
            util::setProperty(receiver.scriptInstanceValue(), propName, std::move(value));
            return true;
        }
        const Datum tracedValue = value;
        util::setProperty(receiver.scriptInstanceValue(), propName, std::move(value));
        context.tracePropertySet(propName, tracedValue);
    }
    return true;
}

bool getMovieProp(ExecutionContext& context) {
    const std::string& propName = context.resolveNameRef(context.argument());
    if (auto* builtinContext = context.builtinContext(); builtinContext != nullptr && builtinContext->movieProperties != nullptr) {
        context.push(builtinContext->movieProperties->getMovieProp(propName));
    } else {
        context.push(builtinConstant(propName).value_or(Datum::voidValue()));
    }
    return true;
}

bool setMovieProp(ExecutionContext& context) {
    const std::string& propName = context.resolveNameRef(context.argument());
    const Datum value = context.pop();
    if (auto* builtinContext = context.builtinContext(); builtinContext != nullptr && builtinContext->movieProperties != nullptr) {
        (void)builtinContext->movieProperties->setMovieProp(propName, value);
    }
    return true;
}

std::optional<Datum> indexedCollectionSnapshotCount(ExecutionContext& context,
                                                    std::string_view propName,
                                                    const Datum& collection);

bool getObjProp(ExecutionContext& context) {
    const std::string& propName = context.resolveNameRef(context.argument());
    const Datum& objectRef = context.peekRef();
    if (const auto snapshotCount = indexedCollectionSnapshotCount(context, propName, objectRef)) {
        context.scope().drop(1);
        context.push(*snapshotCount);
        return true;
    }
    if (objectRef.type() == DatumType::ScriptInstanceRef) {
        Datum result = equalsIgnoreCase(propName, "ilk")
            ? Datum::symbol("instance")
            : util::getProperty(objectRef.scriptInstanceValue(), propName);
        context.scope().drop(1);
        context.push(std::move(result));
        return true;
    }
    if (objectRef.isList()) {
        Datum result = getListProp(objectRef.listValue(), propName);
        context.scope().drop(1);
        context.push(std::move(result));
        return true;
    }
    if (objectRef.isPropList()) {
        Datum result = getPropListProp(objectRef.propListValue(), propName);
        context.scope().drop(1);
        context.push(std::move(result));
        return true;
    }
    if (objectRef.isString()) {
        std::string storage;
        Datum result = getStringProp(stringViewLikeJava(objectRef, storage), propName);
        context.scope().drop(1);
        context.push(std::move(result));
        return true;
    }

    const Datum object = context.pop();
    if (const auto snapshotCount = indexedCollectionSnapshotCount(context, propName, object)) {
        context.push(*snapshotCount);
        return true;
    }
    if (object.type() == DatumType::ScriptInstanceRef) {
        context.push(equalsIgnoreCase(propName, "ilk")
                         ? Datum::symbol("instance")
                         : util::getProperty(object.scriptInstanceValue(), propName));
        return true;
    }
    if (object.isList()) {
        context.push(getListProp(object.listValue(), propName));
        return true;
    }
    if (object.isPropList()) {
        context.push(getPropListProp(object.propListValue(), propName));
        return true;
    }
    if (object.isString()) {
        std::string storage;
        context.push(getStringProp(stringViewLikeJava(object, storage), propName));
        return true;
    }
    if (const auto* image = object.asImageRef()) {
        context.push(dispatch::ImageMethodDispatcher::getProperty(*image, propName));
        return true;
    }
    if (const auto* member = object.asCastMemberRef()) {
        context.push(getCastMemberProp(context, *member, propName));
        return true;
    }
    context.push(getObjectProperty(context, object, propName));
    return true;
}

bool getChainedProp(ExecutionContext& context) {
    const std::string& propName = context.resolveNameRef(context.argument());
    Datum result = getChainedObjectProperty(context, context.peekRef(), propName);
    context.scope().drop(1);
    context.push(std::move(result));
    return true;
}

bool getTopLevelProp(ExecutionContext& context) {
    const std::string& propName = context.resolveNameRef(context.argument());
    if (equalsIgnoreCase(propName, "_player")) {
        context.push(Datum::playerRef());
    } else if (equalsIgnoreCase(propName, "_movie")) {
        context.push(Datum::movieRef());
    } else if (equalsIgnoreCase(propName, "_sound")) {
        context.push(Datum::soundRef());
    } else {
        context.push(Datum::voidValue());
    }
    return true;
}

bool setObjProp(ExecutionContext& context) {
    const std::string& propName = context.resolveNameRef(context.argument());
    Datum value = context.pop();
    Datum object = context.pop();
    setObjectProperty(context, object, propName, std::move(value));
    return true;
}

bool getLegacyProperty(ExecutionContext& context) {
    const int propertyId = toIntLikeJava(context.pop());
    const int propertyType = context.argument();
    const char itemDelimiter = currentItemDelimiter(context);
    auto* builtinContext = context.builtinContext();

    if (propertyType == 0x00) {
        if (propertyId <= 0x0B) {
            if (const auto propName = PropertyIdMappings::getMoviePropName(propertyId);
                propName && builtinContext != nullptr && builtinContext->movieProperties != nullptr) {
                context.push(builtinContext->movieProperties->getMovieProp(*propName));
            } else {
                context.push(Datum::voidValue());
            }
            return true;
        }
        if (const auto chunkType = legacyChunkPropertyTypeByCode(propertyId - 0x0B)) {
            const std::string value = toStringLikeJava(context.pop());
            context.push(Datum::of(getLastChunkValue(value, *chunkType, itemDelimiter)));
            return true;
        }
        context.push(Datum::voidValue());
        return true;
    }

    if (propertyType == 0x01) {
        const std::string value = toStringLikeJava(context.pop());
        if (const auto chunkType = legacyChunkPropertyTypeByCode(propertyId)) {
            context.push(Datum::of(countChunks(value, *chunkType, itemDelimiter)));
        } else {
            context.push(Datum::voidValue());
        }
        return true;
    }

    if (propertyType == 0x06) {
        const int spriteNum = toIntLikeJava(context.pop());
        if (const auto propName = PropertyIdMappings::getSpritePropName(propertyId);
            propName && builtinContext != nullptr && builtinContext->spriteProperties != nullptr) {
            context.push(builtinContext->spriteProperties->getSpriteProp(spriteNum, *propName));
        } else {
            context.push(Datum::voidValue());
        }
        return true;
    }

    if (propertyType == 0x07 || propertyType == 0x09) {
        if (const auto propName = PropertyIdMappings::getAnimPropName(propertyId);
            propName && builtinContext != nullptr && builtinContext->movieProperties != nullptr) {
            context.push(builtinContext->movieProperties->getMovieProp(*propName));
        } else {
            context.push(Datum::voidValue());
        }
        return true;
    }

    if (propertyType == 0x08) {
        if (propertyId == 0x02) {
            const int castLibNum = toIntLikeJava(context.pop());
            if (builtinContext != nullptr && builtinContext->castMemberCountSupplier) {
                context.push(Datum::of(builtinContext->castMemberCountSupplier(castLibNum)));
            } else {
                context.push(Datum::of(0));
            }
            return true;
        }
        if (const auto propName = PropertyIdMappings::getAnim2PropName(propertyId);
            propName && builtinContext != nullptr && builtinContext->movieProperties != nullptr) {
            context.push(builtinContext->movieProperties->getMovieProp(*propName));
        } else {
            context.push(Datum::voidValue());
        }
        return true;
    }

    if (propertyType == 0x0B) {
        const int channelNum = toIntLikeJava(context.pop());
        const std::string propName = PropertyIdMappings::getSoundPropName(propertyId);
        if (channelNum >= 1 && channelNum <= 8) {
            context.push(dispatch::SoundChannelMethodDispatcher::getProperty(builtinContext, Datum::SoundChannel{channelNum}, propName));
        } else {
            context.push(Datum::voidValue());
        }
        return true;
    }

    context.push(Datum::voidValue());
    return true;
}

bool getField(ExecutionContext& context) {
    const Datum castIdDatum = context.pop();
    const Datum fieldNameOrNum = context.pop();
    const int castId = toIntLikeJava(castIdDatum);

    std::vector<Datum> args;
    if (fieldNameOrNum.asString() != nullptr || fieldNameOrNum.asInt() != nullptr) {
        args.push_back(fieldNameOrNum);
    } else {
        args.push_back(Datum::of(toStringLikeJava(fieldNameOrNum)));
    }
    if (castId > 0) {
        args.push_back(Datum::castLibRef(id::CastLibId(castId)));
    }

    if (const auto result = context.invokeBuiltinIfPresent("field", args)) {
        context.push(*result);
    } else {
        context.push(Datum::of(std::string()));
    }
    return true;
}

bool setLegacyProperty(ExecutionContext& context) {
    const int propertyId = toIntLikeJava(context.pop());
    const Datum value = context.pop();
    const int propertyType = context.argument();
    auto* builtinContext = context.builtinContext();

    if (propertyType == 0x00) {
        if (const auto propName = PropertyIdMappings::getMoviePropName(propertyId);
            propertyId <= 0x0B && propName && builtinContext != nullptr && builtinContext->movieProperties != nullptr) {
            (void)builtinContext->movieProperties->setMovieProp(*propName, value);
        }
        return true;
    }

    if (propertyType == 0x04) {
        const int channelNum = toIntLikeJava(context.pop());
        const std::string propName = PropertyIdMappings::getSoundPropName(propertyId);
        if (channelNum >= 1 && channelNum <= 8) {
            (void)dispatch::SoundChannelMethodDispatcher::setProperty(builtinContext, Datum::SoundChannel{channelNum}, propName, value);
        }
        return true;
    }

    if (propertyType == 0x06) {
        const int spriteNum = toIntLikeJava(context.pop());
        if (const auto propName = PropertyIdMappings::getSpritePropName(propertyId);
            propName && builtinContext != nullptr && builtinContext->spriteProperties != nullptr) {
            (void)builtinContext->spriteProperties->setSpriteProp(spriteNum, *propName, value);
        }
        return true;
    }

    if (propertyType == 0x07) {
        if (const auto propName = PropertyIdMappings::getAnimPropName(propertyId);
            propName && builtinContext != nullptr && builtinContext->movieProperties != nullptr) {
            (void)builtinContext->movieProperties->setMovieProp(*propName, value);
        }
        return true;
    }

    return true;
}

bool theBuiltin(ExecutionContext& context) {
    (void)context.pop();
    const std::string& propName = context.resolveNameRef(context.argument());
    if (equalsIgnoreCase(propName, "paramcount")) {
        context.push(Datum::of(static_cast<int>(context.scope().arguments().size())));
    } else if (equalsIgnoreCase(propName, "result")) {
        context.push(context.scope().returnValue());
    } else if (auto* builtinContext = context.builtinContext(); builtinContext != nullptr && builtinContext->movieProperties != nullptr) {
        Datum value = builtinContext->movieProperties->getMovieProp(propName);
        if (value.isVoid()) {
            value = builtinConstant(propName).value_or(Datum::voidValue());
        }
        context.push(std::move(value));
    } else {
        context.push(builtinConstant(propName).value_or(Datum::voidValue()));
    }
    return true;
}

bool localCall(ExecutionContext& context) {
    const auto targetHandler = context.findLocalHandler(context.argument());
    const auto* script = context.scope().script();
    if (!targetHandler || script == nullptr) {
        return true;
    }

    const Datum argListDatum = context.pop();
    const bool noReturn = isNoReturnArgList(argListDatum);
    std::vector<Datum> argStorage;
    const std::vector<Datum>& argItems = argListItemsRef(argListDatum, argStorage);
    std::span<const Datum> args(argItems);
    Datum receiver = context.scope().receiver();
    if (!receiver.isVoid() && !receiver.isNull() && !args.empty() && args.front() == receiver) {
        bool handlerDeclaresMe = false;
        if (!targetHandler->argNameIds.empty()) {
            handlerDeclaresMe = equalsIgnoreCase(context.resolveNameRef(targetHandler->argNameIds.front()), "me");
        }
        if (!handlerDeclaresMe) {
            receiver = args.front();
        }
        args = args.subspan(1);
    }

    Datum result = safeExecuteHandler(
        context,
        HandlerRef{
            script,
            targetHandler,
            context.scope().scriptOwner(),
            context.scope().fileOwner(),
            context.scope().scriptNamesOwner()
        },
        args,
        receiver);
    if (!noReturn) {
        context.push(std::move(result));
    }
    return true;
}

Datum propListBuiltinGetAtValue(const Datum::PropList& propList, const Datum& keyOrIndex) {
    int index = -1;
    if (keyOrIndex.asSymbol() != nullptr || keyOrIndex.isString()) {
        index = propList.findTypedKey(keyOrIndex);
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

Datum propListObjectGetAtValue(const Datum::PropList& propList, const Datum& keyOrIndex) {
    Datum value = propListBuiltinGetAtValue(propList, keyOrIndex);
    if (!value.isVoid() || !keyOrIndex.isInt()) {
        return value;
    }
    const Datum stringKey = Datum::of(std::to_string(toIntLikeJava(keyOrIndex)));
    const int index = propList.findTypedKey(stringKey);
    return index >= 0 ? propList.properties()[static_cast<std::size_t>(index)].second : Datum::voidValue();
}

std::optional<Datum> fastListBuiltinCall(std::string_view handlerName, std::span<const Datum> args) {
    if (args.empty()) {
        return std::nullopt;
    }
    if (equalsIgnoreCase(handlerName, "count")) {
        if (args[0].isList()) {
            return Datum::of(args[0].listValue().count());
        }
        if (args[0].isPropList()) {
            return Datum::of(args[0].propListValue().count());
        }
        return std::nullopt;
    }
    if (!equalsIgnoreCase(handlerName, "getAt") || args.size() < 2) {
        return std::nullopt;
    }
    if (args[0].isList()) {
        const int index = toIntLikeJava(args[1]);
        const auto& items = args[0].listValue().items();
        if (index >= 1 && index <= static_cast<int>(items.size())) {
            return items[static_cast<std::size_t>(index - 1)];
        }
        return Datum::voidValue();
    }
    if (args[0].isPropList()) {
        return propListBuiltinGetAtValue(args[0].propListValue(), args[1]);
    }
    return std::nullopt;
}

std::optional<Datum> fastPrimitiveBuiltinCall(ExecutionContext& context,
                                              std::string_view handlerName,
                                              std::span<const Datum> args) {
    if (context.builtins() == nullptr) {
        return std::nullopt;
    }

    if (equalsIgnoreCase(handlerName, "length")) {
        if (args.empty()) {
            return Datum::of(0);
        }
        if (args[0].isList()) {
            return Datum::of(args[0].listValue().count());
        }
        if (args[0].isPropList()) {
            return Datum::of(args[0].propListValue().count());
        }
        if (const auto directValue = directStringViewLikeJava(args[0])) {
            return Datum::of(static_cast<int>(directValue->size()));
        }
        std::string storage;
        return Datum::of(static_cast<int>(stringViewLikeJava(args[0], storage).size()));
    }
    if (equalsIgnoreCase(handlerName, "count")) {
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
    if (equalsIgnoreCase(handlerName, "chars")) {
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
        return Datum::of(std::string(value.substr(static_cast<std::size_t>(start),
                                                  static_cast<std::size_t>(end - start))));
    }
    if (equalsIgnoreCase(handlerName, "bitAnd")) {
        return args.size() < 2 ? Datum::of(0) : Datum::of(toIntLikeJava(args[0]) & toIntLikeJava(args[1]));
    }
    if (equalsIgnoreCase(handlerName, "bitOr")) {
        return args.size() < 2 ? Datum::of(0) : Datum::of(toIntLikeJava(args[0]) | toIntLikeJava(args[1]));
    }
    if (equalsIgnoreCase(handlerName, "bitXor")) {
        return args.size() < 2 ? Datum::of(0) : Datum::of(toIntLikeJava(args[0]) ^ toIntLikeJava(args[1]));
    }
    if (equalsIgnoreCase(handlerName, "bitNot")) {
        return args.empty() ? Datum::of(0) : Datum::of(~toIntLikeJava(args[0]));
    }
    if (equalsIgnoreCase(handlerName, "abs")) {
        if (args.empty()) {
            return Datum::of(0);
        }
        if (args[0].isFloat()) {
            return Datum::of(std::fabs(toDoubleLikeJava(args[0])));
        }
        const int value = toIntLikeJava(args[0]);
        return value == std::numeric_limits<int>::min() ? Datum::of(value) : Datum::of(std::abs(value));
    }
    if (equalsIgnoreCase(handlerName, "listp")) {
        return !args.empty() && (args[0].isList() || args[0].isPropList()) ? Datum::TRUE : Datum::FALSE;
    }
    if (equalsIgnoreCase(handlerName, "voidp")) {
        return args.empty() || args[0].isVoid() ? Datum::TRUE : Datum::FALSE;
    }
    if (equalsIgnoreCase(handlerName, "charToNum")) {
        if (args.empty()) {
            return Datum::of(0);
        }
        if (const auto* value = args[0].asString()) {
            return value->value.empty()
                ? Datum::of(0)
                : Datum::of(static_cast<int>(static_cast<unsigned char>(value->value.front())));
        }
        if (const auto* value = args[0].asFieldText()) {
            return value->value.empty()
                ? Datum::of(0)
                : Datum::of(static_cast<int>(static_cast<unsigned char>(value->value.front())));
        }
        if (const auto* value = args[0].asStringChunk()) {
            return value->value.empty()
                ? Datum::of(0)
                : Datum::of(static_cast<int>(static_cast<unsigned char>(value->value.front())));
        }
        if (const auto* value = args[0].asSymbol()) {
            return value->name.empty()
                ? Datum::of(0)
                : Datum::of(static_cast<int>(static_cast<unsigned char>(value->name.front())));
        }
        const std::string value = toStringLikeJava(args[0]);
        return value.empty()
            ? Datum::of(0)
            : Datum::of(static_cast<int>(static_cast<unsigned char>(value.front())));
    }
    if (equalsIgnoreCase(handlerName, "numToChar")) {
        if (args.empty()) {
            return Datum::of(std::string());
        }
        const int numericValue = args[0].asInt() != nullptr
            ? args[0].asInt()->value
            : toIntLikeJava(args[0]);
        return Datum::of(std::string(1, static_cast<char>(numericValue)));
    }
    if (equalsIgnoreCase(handlerName, "string")) {
        if (args.empty()) {
            return Datum::of(std::string());
        }
        if (args[0].isString()) {
            return args[0];
        }
        return Datum::of(toStringLikeJava(args[0]));
    }
    if (equalsIgnoreCase(handlerName, "random")) {
        if (args.empty()) {
            return Datum::of(1);
        }
        const int max = toIntLikeJava(args[0]);
        if (max <= 0) {
            return Datum::of(1);
        }
        auto* builtinContext = context.builtinContext();
        if (builtinContext != nullptr && builtinContext->randomIntHandler) {
            return Datum::of(builtinContext->randomIntHandler(max));
        }
        return Datum::of(1);
    }
    if (equalsIgnoreCase(handlerName, "add") && args.size() >= 2 && args[0].isList()) {
        Datum mutableTarget = args[0];
        mutableTarget.listValue().items().push_back(args[1]);
        return Datum::voidValue();
    }

    return std::nullopt;
}

Datum fastCharToNumValue(const Datum& valueDatum) {
    if (const auto* value = valueDatum.asString()) {
        return value->value.empty()
            ? Datum::of(0)
            : Datum::of(static_cast<int>(static_cast<unsigned char>(value->value.front())));
    }
    if (const auto* value = valueDatum.asFieldText()) {
        return value->value.empty()
            ? Datum::of(0)
            : Datum::of(static_cast<int>(static_cast<unsigned char>(value->value.front())));
    }
    if (const auto* value = valueDatum.asStringChunk()) {
        return value->value.empty()
            ? Datum::of(0)
            : Datum::of(static_cast<int>(static_cast<unsigned char>(value->value.front())));
    }
    if (const auto* value = valueDatum.asSymbol()) {
        return value->name.empty()
            ? Datum::of(0)
            : Datum::of(static_cast<int>(static_cast<unsigned char>(value->name.front())));
    }
    const std::string value = toStringLikeJava(valueDatum);
    return value.empty()
        ? Datum::of(0)
        : Datum::of(static_cast<int>(static_cast<unsigned char>(value.front())));
}

bool canUseImmediatePrimitiveExtCall(ExecutionContext& context, std::string_view handlerName) {
    if (context.builtins() == nullptr) {
        return false;
    }
    const auto handler = context.findHandler(handlerName);
    if (!handler) {
        return true;
    }
    return handler->script != nullptr &&
           handler->scriptType == chunks::ScriptChunkType::Parent &&
           context.isBuiltin(handlerName);
}

bool isImmediatePrimitiveExtCallCandidate(std::string_view handlerName, int argCount) {
    if (argCount == 1) {
        return equalsIgnoreCase(handlerName, "charToNum") ||
               equalsIgnoreCase(handlerName, "numToChar") ||
               equalsIgnoreCase(handlerName, "length") ||
               equalsIgnoreCase(handlerName, "count") ||
               equalsIgnoreCase(handlerName, "string") ||
               equalsIgnoreCase(handlerName, "integer") ||
               equalsIgnoreCase(handlerName, "abs") ||
               equalsIgnoreCase(handlerName, "listp") ||
               equalsIgnoreCase(handlerName, "voidp") ||
               equalsIgnoreCase(handlerName, "new") ||
               equalsIgnoreCase(handlerName, "min") ||
               equalsIgnoreCase(handlerName, "max");
    }
    if (argCount == 2) {
        return equalsIgnoreCase(handlerName, "bitAnd") ||
               equalsIgnoreCase(handlerName, "bitOr") ||
               equalsIgnoreCase(handlerName, "bitXor") ||
               equalsIgnoreCase(handlerName, "min") ||
               equalsIgnoreCase(handlerName, "max");
    }
    if (argCount == 3) {
        return equalsIgnoreCase(handlerName, "chars");
    }
    return false;
}

Datum fastIntegerValue(const Datum& value) {
    if (value.isString()) {
        std::string storage;
        const std::string_view trimmed = trimView(stringViewLikeJava(value, storage));
        if (trimmed.empty()) {
            return Datum::of(0);
        }
        if (trimmed.front() == '*' && trimmed.size() > 1) {
            if (const auto parsed = parseLongStrictView(trimmed.substr(1), 16)) {
                return Datum::of(static_cast<int>(*parsed));
            }
        }
        if (const auto parsedInt = parseIntStrictView(trimmed)) {
            return Datum::of(*parsedInt);
        }
        if (const auto parsedDouble = parseDoubleStrict(trimmed)) {
            return Datum::of(javaRoundToInt(*parsedDouble));
        }
        return Datum::voidValue();
    }
    return Datum::of(javaRoundToInt(toDoubleLikeJava(value)));
}

Datum fastMinValue(const Datum& value) {
    if (!value.isList()) {
        return value;
    }
    const auto& items = value.listValue().items();
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

Datum fastMaxValue(const Datum& value) {
    if (!value.isList()) {
        return value;
    }
    const auto& items = value.listValue().items();
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

bool tryImmediatePrimitiveExtCall(ExecutionContext& context,
                                  std::string_view handlerName,
                                  int argCount,
                                  bool noReturn) {
    if (!isImmediatePrimitiveExtCallCandidate(handlerName, argCount)) {
        return false;
    }
    if (!canUseImmediatePrimitiveExtCall(context, handlerName)) {
        return false;
    }

    if (equalsIgnoreCase(handlerName, "charToNum") && argCount == 1) {
        Datum result = fastCharToNumValue(context.peekRef());
        context.scope().drop(1);
        if (!noReturn) {
            context.push(std::move(result));
        }
        return true;
    }

    if (equalsIgnoreCase(handlerName, "numToChar") && argCount == 1) {
        const Datum& value = context.peekRef();
        const int numericValue = value.asInt() != nullptr ? value.asInt()->value : toIntLikeJava(value);
        context.scope().drop(1);
        if (!noReturn) {
            context.push(Datum::of(std::string(1, static_cast<char>(numericValue))));
        }
        return true;
    }

    if (equalsIgnoreCase(handlerName, "chars") && argCount == 3) {
        Datum result = Datum::voidValue();
        if (!noReturn) {
            std::string valueStorage;
            const std::string_view value = stringViewLikeJava(context.peekRef(2), valueStorage);
            int start = toIntLikeJava(context.peekRef(1)) - 1;
            int end = toIntLikeJava(context.peekRef(0));
            if (start < 0) {
                start = 0;
            }
            if (end > static_cast<int>(value.size())) {
                end = static_cast<int>(value.size());
            }
            if (start >= end) {
                result = Datum::of(std::string());
            } else {
                result = Datum::of(std::string(value.substr(static_cast<std::size_t>(start),
                                                            static_cast<std::size_t>(end - start))));
            }
        }
        context.scope().drop(3);
        if (!noReturn) {
            context.push(std::move(result));
        }
        return true;
    }

    if (equalsIgnoreCase(handlerName, "length") && argCount == 1) {
        const Datum& value = context.peekRef();
        Datum result = Datum::of(0);
        if (value.isList()) {
            result = Datum::of(value.listValue().count());
        } else if (value.isPropList()) {
            result = Datum::of(value.propListValue().count());
        } else if (const auto directValue = directStringViewLikeJava(value)) {
            result = Datum::of(static_cast<int>(directValue->size()));
        } else {
            std::string storage;
            result = Datum::of(static_cast<int>(stringViewLikeJava(value, storage).size()));
        }
        context.scope().drop(1);
        if (!noReturn) {
            context.push(std::move(result));
        }
        return true;
    }

    if (equalsIgnoreCase(handlerName, "count") && argCount == 1) {
        const Datum& value = context.peekRef();
        Datum result = Datum::of(0);
        if (value.isList()) {
            result = Datum::of(value.listValue().count());
        } else if (value.isPropList()) {
            result = Datum::of(value.propListValue().count());
        }
        context.scope().drop(1);
        if (!noReturn) {
            context.push(std::move(result));
        }
        return true;
    }

    if ((equalsIgnoreCase(handlerName, "bitAnd") ||
         equalsIgnoreCase(handlerName, "bitOr") ||
         equalsIgnoreCase(handlerName, "bitXor")) && argCount == 2) {
        const Datum& right = context.peekRef(0);
        const Datum& left = context.peekRef(1);
        Datum result = Datum::voidValue();
        if (!noReturn) {
            if (equalsIgnoreCase(handlerName, "bitAnd")) {
                result = Datum::of(toIntLikeJava(left) & toIntLikeJava(right));
            } else if (equalsIgnoreCase(handlerName, "bitOr")) {
                result = Datum::of(toIntLikeJava(left) | toIntLikeJava(right));
            } else {
                result = Datum::of(toIntLikeJava(left) ^ toIntLikeJava(right));
            }
        }
        context.scope().drop(2);
        if (!noReturn) {
            context.push(std::move(result));
        }
        return true;
    }

    if (equalsIgnoreCase(handlerName, "string") && argCount == 1) {
        const Datum& value = context.peekRef();
        Datum result = Datum::voidValue();
        if (!noReturn) {
            result = Datum::of(toStringLikeJava(value));
        }
        context.scope().drop(1);
        if (!noReturn) {
            context.push(std::move(result));
        }
        return true;
    }

    if (equalsIgnoreCase(handlerName, "integer") && argCount == 1) {
        const Datum& value = context.peekRef();
        Datum result = Datum::voidValue();
        if (!noReturn) {
            result = fastIntegerValue(value);
        }
        context.scope().drop(1);
        if (!noReturn) {
            context.push(std::move(result));
        }
        return true;
    }

    if (equalsIgnoreCase(handlerName, "abs") && argCount == 1) {
        const Datum& value = context.peekRef();
        Datum result = Datum::voidValue();
        if (!noReturn) {
            if (value.isFloat()) {
                result = Datum::of(std::fabs(toDoubleLikeJava(value)));
            } else {
                const int numericValue = toIntLikeJava(value);
                result = numericValue == std::numeric_limits<int>::min()
                    ? Datum::of(numericValue)
                    : Datum::of(std::abs(numericValue));
            }
        }
        context.scope().drop(1);
        if (!noReturn) {
            context.push(std::move(result));
        }
        return true;
    }

    if (equalsIgnoreCase(handlerName, "listp") && argCount == 1) {
        const Datum& value = context.peekRef();
        const bool result = value.isList() || value.isPropList();
        context.scope().drop(1);
        if (!noReturn) {
            context.push(result ? Datum::TRUE : Datum::FALSE);
        }
        return true;
    }

    if (equalsIgnoreCase(handlerName, "voidp") && argCount == 1) {
        const bool result = context.peekRef().isVoid();
        context.scope().drop(1);
        if (!noReturn) {
            context.push(result ? Datum::TRUE : Datum::FALSE);
        }
        return true;
    }

    if (equalsIgnoreCase(handlerName, "new") && argCount == 1) {
        const Datum& targetRef = context.peekRef(0);
        const auto* scriptRef = targetRef.asScriptRef();
        auto* builtinContext = context.builtinContext();
        if (scriptRef == nullptr && (builtinContext == nullptr || !builtinContext->newInstanceHandler)) {
            return false;
        }

        Datum target = context.pop();
        const std::vector<Datum> emptyArgs;
        Datum result = Datum::voidValue();
        if (builtinContext != nullptr && builtinContext->newInstanceHandler) {
            result = builtinContext->newInstanceHandler(target, emptyArgs);
        } else {
            result = Datum::scriptInstance("script", scriptRef->memberRef);
            initializeDeclaredScriptProperties(context, result, scriptRef->memberRef);
            if (builtinContext != nullptr && builtinContext->callTargetHandler) {
                const Datum handlerResult = builtinContext->callTargetHandler(result, "new", emptyArgs);
                if (!handlerResult.isVoid()) {
                    result = handlerResult;
                }
            }
        }
        if (!noReturn) {
            context.push(std::move(result));
        }
        return true;
    }

    if ((equalsIgnoreCase(handlerName, "min") || equalsIgnoreCase(handlerName, "max")) &&
        (argCount == 1 || argCount == 2)) {
        if (argCount == 1) {
            const Datum& value = context.peekRef();
            Datum result = Datum::voidValue();
            if (!noReturn) {
                result = equalsIgnoreCase(handlerName, "min") ? fastMinValue(value) : fastMaxValue(value);
            }
            context.scope().drop(1);
            if (!noReturn) {
                context.push(std::move(result));
            }
            return true;
        }

        const Datum& right = context.peekRef(0);
        const Datum& left = context.peekRef(1);
        Datum result = Datum::voidValue();
        if (!noReturn) {
            const bool isMin = equalsIgnoreCase(handlerName, "min");
            if (left.isFloat() || right.isFloat()) {
                result = Datum::of(isMin
                    ? std::min(toDoubleLikeJava(left), toDoubleLikeJava(right))
                    : std::max(toDoubleLikeJava(left), toDoubleLikeJava(right)));
            } else {
                result = Datum::of(isMin
                    ? std::min(toIntLikeJava(left), toIntLikeJava(right))
                    : std::max(toIntLikeJava(left), toIntLikeJava(right)));
            }
        }
        context.scope().drop(2);
        if (!noReturn) {
            context.push(std::move(result));
        }
        return true;
    }

    return false;
}

std::optional<Opcode> setOpcodeForGetOpcode(Opcode opcode) {
    switch (opcode) {
        case Opcode::GET_GLOBAL: return Opcode::SET_GLOBAL;
        case Opcode::GET_PROP: return Opcode::SET_PROP;
        case Opcode::GET_PARAM: return Opcode::SET_PARAM;
        case Opcode::GET_LOCAL: return Opcode::SET_LOCAL;
        default: return std::nullopt;
    }
}

bool sameVariableInstruction(const chunks::ScriptChunk::Instruction& lhs,
                             const chunks::ScriptChunk::Instruction& rhs) {
    return lhs.opcode == rhs.opcode && lhs.argument == rhs.argument;
}

bool isIndexedCollectionLoadOpcode(Opcode opcode) {
    return opcode == Opcode::GET_GLOBAL ||
           opcode == Opcode::GET_PROP ||
           opcode == Opcode::GET_PARAM ||
           opcode == Opcode::GET_LOCAL;
}

bool indexedCollectionLoopHeaderMatches(const ExecutionContext& context,
                                        int headerIndex,
                                        const chunks::ScriptChunk::Instruction& collectionLoad,
                                        const chunks::ScriptChunk::Instruction& indexLoad) {
    const auto& handler = context.scope().handler();
    const auto& instructions = handler.instructions;
    if (headerIndex < 4 || headerIndex >= static_cast<int>(instructions.size())) {
        return false;
    }
    const auto& jmpIfZero = instructions[static_cast<std::size_t>(headerIndex)];
    if (jmpIfZero.opcode != Opcode::JMP_IF_Z) {
        return false;
    }

    const int loopEndOffset = jmpIfZero.offset + jmpIfZero.argument;
    const int endIndex = handler.getInstructionIndex(loopEndOffset);
    if (endIndex < 1 || endIndex > static_cast<int>(instructions.size())) {
        return false;
    }
    const auto& endRepeat = instructions[static_cast<std::size_t>(endIndex - 1)];
    if (endRepeat.opcode != Opcode::END_REPEAT) {
        return false;
    }

    const int condStart = handler.getInstructionIndex(endRepeat.offset - endRepeat.argument);
    if (condStart < 1 || condStart + 4 != headerIndex) {
        return false;
    }
    if (!sameVariableInstruction(instructions[static_cast<std::size_t>(condStart)], indexLoad) ||
        !sameVariableInstruction(instructions[static_cast<std::size_t>(condStart + 1)], collectionLoad)) {
        return false;
    }
    const auto& countProp = instructions[static_cast<std::size_t>(condStart + 2)];
    if (countProp.opcode != Opcode::GET_OBJ_PROP || !equalsIgnoreCase(context.resolveNameRef(countProp.argument), "count")) {
        return false;
    }
    const auto comparison = instructions[static_cast<std::size_t>(condStart + 3)].opcode;
    if (comparison != Opcode::LT_EQ && comparison != Opcode::GT_EQ) {
        return false;
    }

    const auto setOpcode = setOpcodeForGetOpcode(indexLoad.opcode);
    if (!setOpcode.has_value()) {
        return false;
    }
    const auto& initialSet = instructions[static_cast<std::size_t>(condStart - 1)];
    if (initialSet.opcode != *setOpcode || initialSet.argument != indexLoad.argument) {
        return false;
    }
    if (endIndex < 5) {
        return false;
    }
    const int expectedIncrement = comparison == Opcode::LT_EQ ? 1 : -1;
    return instructions[static_cast<std::size_t>(endIndex - 5)].opcode == Opcode::PUSH_INT8 &&
           instructions[static_cast<std::size_t>(endIndex - 5)].argument == expectedIncrement &&
           sameVariableInstruction(instructions[static_cast<std::size_t>(endIndex - 4)], indexLoad) &&
           instructions[static_cast<std::size_t>(endIndex - 3)].opcode == Opcode::ADD &&
           instructions[static_cast<std::size_t>(endIndex - 2)].opcode == *setOpcode &&
           instructions[static_cast<std::size_t>(endIndex - 2)].argument == indexLoad.argument;
}

bool isLoopHeaderForIndexedCollection(const ExecutionContext& context,
                                      int headerIndex,
                                      int currentIndex,
                                      const chunks::ScriptChunk::Instruction& collectionLoad,
                                      const chunks::ScriptChunk::Instruction& indexLoad) {
    if (currentIndex <= headerIndex ||
        !indexedCollectionLoopHeaderMatches(context, headerIndex, collectionLoad, indexLoad)) {
        return false;
    }
    const auto& jmpIfZero = context.scope().handler().instructions[static_cast<std::size_t>(headerIndex)];
    return jmpIfZero.offset + jmpIfZero.argument > context.instructionOffset();
}

bool loopBodyHasIndexedCollectionGetAt(const ExecutionContext& context,
                                       int headerIndex,
                                       const chunks::ScriptChunk::Instruction& collectionLoad,
                                       const chunks::ScriptChunk::Instruction& indexLoad) {
    const auto& handler = context.scope().handler();
    const auto& instructions = handler.instructions;
    const auto& jmpIfZero = instructions[static_cast<std::size_t>(headerIndex)];
    const int endIndex = handler.getInstructionIndex(jmpIfZero.offset + jmpIfZero.argument);
    if (endIndex < 1) {
        return false;
    }

    for (int index = headerIndex + 1; index < endIndex - 1; ++index) {
        const auto& instruction = instructions[static_cast<std::size_t>(index)];
        if (instruction.opcode != Opcode::OBJ_CALL || !equalsIgnoreCase(context.resolveNameRef(instruction.argument), "getAt") ||
            index < 3) {
            continue;
        }
        const auto& pushArgs = instructions[static_cast<std::size_t>(index - 1)];
        if (pushArgs.opcode == Opcode::PUSH_ARG_LIST && pushArgs.argument == 2 &&
            sameVariableInstruction(instructions[static_cast<std::size_t>(index - 2)], indexLoad) &&
            sameVariableInstruction(instructions[static_cast<std::size_t>(index - 3)], collectionLoad)) {
            return true;
        }
    }
    return false;
}

std::string indexedCollectionLoopHeaderCacheKey(const ExecutionContext& context, char kind) {
    std::string key;
    key.reserve(32);
    key.push_back(kind);
    key.push_back(':');
    key.append(std::to_string(reinterpret_cast<std::uintptr_t>(&context.scope().handler())));
    key.push_back(':');
    appendInt(key, context.scope().bytecodeIndex());
    return key;
}

std::optional<int> cachedIndexedCollectionLoopHeader(const ExecutionContext& context,
                                                     char kind,
                                                     std::optional<int> (*compute)(const ExecutionContext&));

std::optional<int> indexedCollectionLoopHeaderUncached(const ExecutionContext& context) {
    const auto& handler = context.scope().handler();
    const auto& instructions = handler.instructions;
    const int currentIndex = context.scope().bytecodeIndex();
    if (currentIndex < 3 || currentIndex >= static_cast<int>(instructions.size())) {
        return std::nullopt;
    }
    const auto& pushArgs = instructions[static_cast<std::size_t>(currentIndex - 1)];
    const auto& indexLoad = instructions[static_cast<std::size_t>(currentIndex - 2)];
    const auto& collectionLoad = instructions[static_cast<std::size_t>(currentIndex - 3)];
    if (pushArgs.opcode != Opcode::PUSH_ARG_LIST || pushArgs.argument != 2 ||
        !isIndexedCollectionLoadOpcode(collectionLoad.opcode) ||
        !isIndexedCollectionLoadOpcode(indexLoad.opcode)) {
        return std::nullopt;
    }

    for (int headerIndex = currentIndex - 1; headerIndex >= 0; --headerIndex) {
        if (isLoopHeaderForIndexedCollection(context, headerIndex, currentIndex, collectionLoad, indexLoad)) {
            return headerIndex;
        }
    }
    return std::nullopt;
}

std::optional<int> indexedCollectionCountLoopHeaderUncached(const ExecutionContext& context) {
    const auto& handler = context.scope().handler();
    const auto& instructions = handler.instructions;
    const int currentIndex = context.scope().bytecodeIndex();
    if (currentIndex < 2 || currentIndex + 2 >= static_cast<int>(instructions.size())) {
        return std::nullopt;
    }
    const auto& indexLoad = instructions[static_cast<std::size_t>(currentIndex - 2)];
    const auto& collectionLoad = instructions[static_cast<std::size_t>(currentIndex - 1)];
    if (!isIndexedCollectionLoadOpcode(collectionLoad.opcode) ||
        !isIndexedCollectionLoadOpcode(indexLoad.opcode)) {
        return std::nullopt;
    }
    const int headerIndex = currentIndex + 2;
    if (indexedCollectionLoopHeaderMatches(context, headerIndex, collectionLoad, indexLoad) &&
        loopBodyHasIndexedCollectionGetAt(context, headerIndex, collectionLoad, indexLoad)) {
        return headerIndex;
    }
    return std::nullopt;
}

std::optional<int> cachedIndexedCollectionLoopHeader(const ExecutionContext& context,
                                                     char kind,
                                                     std::optional<int> (*compute)(const ExecutionContext&)) {
    auto* builtinContext = context.builtinContext();
    if (builtinContext == nullptr) {
        return compute(context);
    }

    const std::string key = indexedCollectionLoopHeaderCacheKey(context, kind);
    const auto cached = builtinContext->indexedCollectionLoopHeaderCache.find(key);
    if (cached != builtinContext->indexedCollectionLoopHeaderCache.end()) {
        return cached->second >= 0 ? std::optional<int>{cached->second} : std::nullopt;
    }

    const auto result = compute(context);
    builtinContext->indexedCollectionLoopHeaderCache.emplace(key, result.value_or(-1));
    return result;
}

std::optional<int> indexedCollectionLoopHeader(const ExecutionContext& context) {
    return cachedIndexedCollectionLoopHeader(context, 'g', indexedCollectionLoopHeaderUncached);
}

std::optional<int> indexedCollectionCountLoopHeader(const ExecutionContext& context) {
    return cachedIndexedCollectionLoopHeader(context, 'c', indexedCollectionCountLoopHeaderUncached);
}

const void* collectionIdentity(const Datum& value) {
    if (value.isList()) {
        return &value.listValue();
    }
    if (value.isPropList()) {
        return &value.propListValue();
    }
    return nullptr;
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

std::optional<Datum> indexedCollectionSnapshotGetAt(ExecutionContext& context,
                                                    std::string_view methodName,
                                                    std::span<const Datum> args) {
    if (!equalsIgnoreCase(methodName, "getAt") || args.size() < 2 ||
        (!args[0].isList() && !args[0].isPropList()) ||
        args[1].isString() || args[1].isSymbol()) {
        return std::nullopt;
    }
    const auto loopHeader = indexedCollectionLoopHeader(context);
    if (!loopHeader.has_value()) {
        return std::nullopt;
    }
    return context.scope().indexedCollectionSnapshotValue(*loopHeader,
                                                          collectionIdentity(args[0]),
                                                          args[0],
                                                          toIntLikeJava(args[1]));
}

std::optional<Datum> indexedCollectionSnapshotGetAtDirect(ExecutionContext& context,
                                                          std::string_view methodName,
                                                          const Datum& collection,
                                                          const Datum& index) {
    if (!equalsIgnoreCase(methodName, "getAt") ||
        (!collection.isList() && !collection.isPropList()) ||
        index.isString() || index.isSymbol()) {
        return std::nullopt;
    }
    const auto loopHeader = indexedCollectionLoopHeader(context);
    if (!loopHeader.has_value()) {
        return std::nullopt;
    }
    return context.scope().indexedCollectionSnapshotValue(*loopHeader,
                                                          collectionIdentity(collection),
                                                          collection,
                                                          toIntLikeJava(index));
}

std::optional<Datum> indexedCollectionSnapshotCount(ExecutionContext& context,
                                                    std::string_view propName,
                                                    const Datum& collection) {
    if (!equalsIgnoreCase(propName, "count") || (!collection.isList() && !collection.isPropList())) {
        return std::nullopt;
    }
    const auto loopHeader = indexedCollectionCountLoopHeader(context);
    if (!loopHeader.has_value()) {
        return std::nullopt;
    }
    return Datum::of(context.scope().indexedCollectionSnapshotCount(*loopHeader,
                                                                    collectionIdentity(collection),
                                                                    collection));
}

std::optional<Datum> fastListObjectCall(std::string_view methodName,
                                        std::span<const Datum> args) {
    if (args.empty()) {
        return std::nullopt;
    }
    const Datum& target = args[0];
    if (target.isList()) {
        const auto& readList = target.listValue();
        const auto& readItems = readList.items();
        if (equalsIgnoreCase(methodName, "count")) {
            return Datum::of(static_cast<int>(readItems.size()));
        }
        if (equalsIgnoreCase(methodName, "getAt")) {
            if (args.size() < 2) {
                return Datum::voidValue();
            }
            if (args[1].isString() || args[1].isSymbol()) {
                if (const auto* propList = singlePropListWrapper(readList)) {
                    const int propIndex = propList->findTypedKey(args[1]);
                    return propIndex >= 0 ? propList->properties()[static_cast<std::size_t>(propIndex)].second
                                          : Datum::voidValue();
                }
            }
            const int index = toIntLikeJava(args[1]);
            if (index < 1 || index > static_cast<int>(readItems.size())) {
                throw LingoException("getAt: index " + std::to_string(index) +
                                     " out of range (list size: " + std::to_string(readItems.size()) + ")");
            }
            return readItems[static_cast<std::size_t>(index - 1)];
        }
        Datum mutableTarget = target;
        auto& list = mutableTarget.listValue();
        auto& items = list.items();
        if (equalsIgnoreCase(methodName, "setAt")) {
            if (args.size() >= 3) {
                if ((args[1].isString() || args[1].isSymbol()) && singlePropListWrapper(list) != nullptr) {
                    singlePropListWrapper(list)->putTyped(args[1], args[2]);
                } else {
                    const int index = toIntLikeJava(args[1]);
                    if (index >= 1) {
                        const auto zeroIndex = static_cast<std::size_t>(index - 1);
                        if (zeroIndex < items.size()) {
                            items[zeroIndex] = args[2];
                        } else {
                            while (items.size() < zeroIndex) {
                                items.push_back(Datum::voidValue());
                            }
                            items.push_back(args[2]);
                        }
                    }
                }
            }
            return Datum::voidValue();
        }
        if (equalsIgnoreCase(methodName, "append") || equalsIgnoreCase(methodName, "add")) {
            if (args.size() >= 2) {
                items.push_back(args[1]);
            }
            return Datum::voidValue();
        }
        if (equalsIgnoreCase(methodName, "addAt")) {
            if (args.size() >= 3) {
                int index = toIntLikeJava(args[1]) - 1;
                if (index < 0) {
                    index = 0;
                }
                const auto insertIndex = static_cast<std::size_t>(index);
                if (insertIndex >= items.size()) {
                    items.push_back(args[2]);
                } else {
                    items.insert(items.begin() + static_cast<std::ptrdiff_t>(insertIndex), args[2]);
                }
            }
            return Datum::voidValue();
        }
        if (equalsIgnoreCase(methodName, "deleteAt")) {
            if (args.size() >= 2) {
                const int index = toIntLikeJava(args[1]) - 1;
                if (index >= 0 && index < static_cast<int>(items.size())) {
                    items.erase(items.begin() + index);
                }
            }
            return Datum::voidValue();
        }
        if (equalsIgnoreCase(methodName, "duplicate")) {
            return list.deepCopyDatum();
        }
        if (equalsIgnoreCase(methodName, "getLast")) {
            return items.empty() ? Datum::voidValue() : items.back();
        }
        if (equalsIgnoreCase(methodName, "getFirst")) {
            return items.empty() ? Datum::voidValue() : items.front();
        }
        return std::nullopt;
    }
    if (!target.isPropList()) {
        return std::nullopt;
    }

    const auto& propList = target.propListValue();
    if (equalsIgnoreCase(methodName, "count")) {
        if (args.size() >= 2) {
            std::string keyNameStorage;
            const int index = propList.findUntypedKeyName(keyNameLikeJavaView(args[1], keyNameStorage));
            if (index < 0) {
                return Datum::of(0);
            }
            const Datum& value = propList.properties()[static_cast<std::size_t>(index)].second;
            if (value.isList()) {
                return Datum::of(value.listValue().count());
            }
            if (value.isPropList()) {
                return Datum::of(value.propListValue().count());
            }
            return Datum::of(0);
        }
        return Datum::of(propList.count());
    }
    if (equalsIgnoreCase(methodName, "getAt")) {
        if (args.size() < 2) {
            return Datum::voidValue();
        }
        return propListObjectGetAtValue(propList, args[1]);
    }
    if (equalsIgnoreCase(methodName, "getAProp") || equalsIgnoreCase(methodName, "getProp") ||
        equalsIgnoreCase(methodName, "getProperty") || equalsIgnoreCase(methodName, "getPropRef")) {
        if (args.size() < 2) {
            return Datum::voidValue();
        }
        std::string keyNameStorage;
        const int index = propList.findUntypedKeyName(keyNameLikeJavaView(args[1], keyNameStorage));
        if (index < 0) {
            return Datum::voidValue();
        }
        const Datum& value = propList.properties()[static_cast<std::size_t>(index)].second;
        if (args.size() >= 3 && value.isList()) {
            const int itemIndex = toIntLikeJava(args[2]);
            if (itemIndex >= 1 && itemIndex <= value.listValue().count()) {
                return value.listValue().getAt(itemIndex);
            }
            return Datum::voidValue();
        }
        return value;
    }
    if (equalsIgnoreCase(methodName, "setAt")) {
        if (args.size() >= 3) {
            Datum mutableTarget = target;
            auto& mutablePropList = mutableTarget.propListValue();
            const int position = toIntLikeJava(args[1]) - 1;
            if (args[1].isInt() && position >= 0 && position < mutablePropList.count()) {
                mutablePropList.properties()[static_cast<std::size_t>(position)].second = args[2];
            } else if (args[1].isInt()) {
                mutablePropList.putTyped(Datum::of(std::to_string(toIntLikeJava(args[1]))), args[2]);
            } else {
                mutablePropList.putTyped(args[1], args[2]);
            }
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "setProp") || equalsIgnoreCase(methodName, "setAProp")) {
        if (args.size() >= 3) {
            Datum mutableTarget = target;
            mutableTarget.propListValue().putTyped(args[1], args[2]);
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(methodName, "findPos")) {
        if (args.size() < 2) {
            return Datum::voidValue();
        }
        const int index = propList.findUntypedKey(args[1]);
        return index >= 0 ? Datum::of(index + 1) : Datum::voidValue();
    }
    return std::nullopt;
}

bool isFastScriptInstanceObjectMethod(ImmediateObjectMethod method) {
    return method == ImmediateObjectMethod::Count ||
           method == ImmediateObjectMethod::GetProp ||
           method == ImmediateObjectMethod::GetPropRef ||
           method == ImmediateObjectMethod::SetProp;
}

std::optional<Datum> fastScriptInstanceObjectCall(ImmediateObjectMethod method, std::span<const Datum> args) {
    if (args.size() < 2 || args.front().type() != DatumType::ScriptInstanceRef) {
        return std::nullopt;
    }

    const bool isCount = method == ImmediateObjectMethod::Count;
    const bool isGetProp = method == ImmediateObjectMethod::GetProp || method == ImmediateObjectMethod::GetPropRef;
    const bool isSetProp = method == ImmediateObjectMethod::SetProp;
    if (!isCount && !isGetProp && !isSetProp) {
        return std::nullopt;
    }

    std::string propNameStorage;
    const std::string_view propName = keyNameLikeJavaView(args[1], propNameStorage);

    if (isCount) {
        const auto& instance = args.front().scriptInstanceValue();
        return scriptInstancePropertyCountValue(instance, propName);
    }

    if (isGetProp) {
        const auto& instance = args.front().scriptInstanceValue();
        if (args.size() >= 3) {
            return scriptInstanceNestedPropertyValue(instance, propName, args[2]);
        }
        return scriptInstancePropertyValue(instance, propName);
    }

    if (isSetProp) {
        auto instance = args.front().scriptInstancePtr();
        if (args.size() == 3) {
            util::setProperty(*instance, propName, args[2]);
        } else if (args.size() >= 4) {
            Datum localProp = util::getProperty(*instance, propName);
            if (localProp.isVoid()) {
                localProp = Datum::propList();
                util::setProperty(*instance, propName, localProp);
            }
            scriptInstanceSetNestedProperty(localProp, args[2], args[3]);
            util::setProperty(*instance, propName, std::move(localProp));
        }
        return Datum::voidValue();
    }

    return std::nullopt;
}

std::optional<Datum> fastScriptInstanceObjectCall(std::string_view methodName, std::span<const Datum> args) {
    return fastScriptInstanceObjectCall(classifyImmediateObjectMethod(methodName), args);
}

std::optional<Datum> fastScriptInstanceObjectCall(std::string_view methodName, const std::vector<Datum>& args) {
    return fastScriptInstanceObjectCall(methodName, std::span<const Datum>(args));
}

bool executeObjCallWithArgs(ExecutionContext& context,
                            std::string_view methodName,
                            ImmediateObjectMethod method,
                            std::span<const Datum> args,
                            bool noReturn) {
    if (auto snapshotResult = indexedCollectionSnapshotGetAt(context, methodName, args)) {
        if (!noReturn) {
            context.push(std::move(*snapshotResult));
        }
        return true;
    }
    if (auto fastResult = fastListObjectCall(methodName, args)) {
        if (!noReturn) {
            context.push(std::move(*fastResult));
        }
        return true;
    }
    if (auto fastResult = fastScriptInstanceObjectCall(method, args)) {
        if (!noReturn) {
            context.push(std::move(*fastResult));
        }
        return true;
    }
    Datum target = args.empty() ? Datum::voidValue() : args.front();
    Datum result = Datum::voidValue();
    if (target.type() == DatumType::ScriptInstanceRef) {
        const std::span<const Datum> methodArgs(args.data() + 1, args.size() - 1);
        result = scriptInstanceObjectMethod(context, target, methodName, method, methodArgs);
    } else {
        const std::span<const Datum> methodArgs = args.size() <= 1
                                                      ? std::span<const Datum>()
                                                      : std::span<const Datum>(args.data() + 1, args.size() - 1);
        result = dispatchObjectMethodSpan(context, std::move(target), methodName, methodArgs);
    }
    if (!noReturn) {
        context.push(std::move(result));
    }
    return true;
}

bool executeObjCallWithArgs(ExecutionContext& context,
                            std::string_view methodName,
                            std::span<const Datum> args,
                            bool noReturn) {
    return executeObjCallWithArgs(context, methodName, classifyImmediateObjectMethod(methodName), args, noReturn);
}

bool executeObjCallWithArgs(ExecutionContext& context,
                            std::string_view methodName,
                            const std::vector<Datum>& args,
                            bool noReturn) {
    return executeObjCallWithArgs(context, methodName, std::span<const Datum>(args), noReturn);
}

bool tryImmediateFastObjCall(ExecutionContext& context,
                             std::string_view methodName,
                             ImmediateObjectMethod method,
                             int argCount,
                             bool noReturn) {
    if (argCount <= 0 || context.scope().stackSize() < argCount) {
        return false;
    }

    const Datum& target = context.peekRef(argCount - 1);
    if (target.isList()) {
        if (argCount == 1 && method == ImmediateObjectMethod::Count) {
            const int count = target.listValue().count();
            context.scope().drop(argCount);
            if (!noReturn) {
                context.push(Datum::of(count));
            }
            return true;
        }

        if (argCount == 2 && method == ImmediateObjectMethod::GetAt) {
            const Datum& indexDatum = context.peekRef(0);
            if (auto snapshotResult =
                    indexedCollectionSnapshotGetAtDirect(context, methodName, target, indexDatum)) {
                context.scope().drop(argCount);
                if (!noReturn) {
                    context.push(std::move(*snapshotResult));
                }
                return true;
            }

            if (indexDatum.isString() || indexDatum.isSymbol()) {
                if (const auto* propList = singlePropListWrapper(target.listValue())) {
                    const int propIndex = propList->findTypedKey(indexDatum);
                    Datum result = propIndex >= 0
                        ? propList->properties()[static_cast<std::size_t>(propIndex)].second
                        : Datum::voidValue();
                    context.scope().drop(argCount);
                    if (!noReturn) {
                        context.push(std::move(result));
                    }
                    return true;
                }
            }

            const int index = toIntLikeJava(indexDatum);
            const auto& items = target.listValue().items();
            if (index < 1 || index > static_cast<int>(items.size())) {
                throw LingoException("getAt: index " + std::to_string(index) +
                                     " out of range (list size: " + std::to_string(items.size()) + ")");
            }
            Datum result = items[static_cast<std::size_t>(index - 1)];
            context.scope().drop(argCount);
            if (!noReturn) {
                context.push(std::move(result));
            }
            return true;
        }

        if (argCount == 1 && method == ImmediateObjectMethod::GetLast) {
            const auto& items = target.listValue().items();
            Datum result = items.empty() ? Datum::voidValue() : items.back();
            context.scope().drop(argCount);
            if (!noReturn) {
                context.push(std::move(result));
            }
            return true;
        }

        if (argCount == 1 && method == ImmediateObjectMethod::GetFirst) {
            const auto& items = target.listValue().items();
            Datum result = items.empty() ? Datum::voidValue() : items.front();
            context.scope().drop(argCount);
            if (!noReturn) {
                context.push(std::move(result));
            }
            return true;
        }

        if (argCount == 3 && method == ImmediateObjectMethod::SetAt) {
            Datum value = context.pop();
            Datum indexDatum = context.pop();
            Datum mutableTarget = context.pop();
            if ((indexDatum.isString() || indexDatum.isSymbol()) &&
                singlePropListWrapper(mutableTarget.listValue()) != nullptr) {
                singlePropListWrapper(mutableTarget.listValue())->putTyped(std::move(indexDatum), std::move(value));
            } else {
                const int index = toIntLikeJava(indexDatum);
                if (index >= 1) {
                    auto& items = mutableTarget.listValue().items();
                    const auto zeroIndex = static_cast<std::size_t>(index - 1);
                    if (zeroIndex < items.size()) {
                        items[zeroIndex] = std::move(value);
                    } else {
                        while (items.size() < zeroIndex) {
                            items.push_back(Datum::voidValue());
                        }
                        items.push_back(std::move(value));
                    }
                }
            }
            if (!noReturn) {
                context.push(Datum::voidValue());
            }
            return true;
        }

        if (argCount == 3 && method == ImmediateObjectMethod::AddAt) {
            Datum value = context.pop();
            const int rawIndex = toIntLikeJava(context.pop()) - 1;
            Datum mutableTarget = context.pop();
            auto& items = mutableTarget.listValue().items();
            const int clampedIndex = std::max(0, rawIndex);
            const auto insertIndex = static_cast<std::size_t>(clampedIndex);
            if (insertIndex >= items.size()) {
                items.push_back(std::move(value));
            } else {
                items.insert(items.begin() + static_cast<std::ptrdiff_t>(insertIndex), std::move(value));
            }
            if (!noReturn) {
                context.push(Datum::voidValue());
            }
            return true;
        }

        if (argCount == 2 && method == ImmediateObjectMethod::DeleteAt) {
            const int rawIndex = toIntLikeJava(context.pop()) - 1;
            Datum mutableTarget = context.pop();
            auto& items = mutableTarget.listValue().items();
            if (rawIndex >= 0 && rawIndex < static_cast<int>(items.size())) {
                items.erase(items.begin() + rawIndex);
            }
            if (!noReturn) {
                context.push(Datum::voidValue());
            }
            return true;
        }

        if (argCount == 2 && (method == ImmediateObjectMethod::Append || method == ImmediateObjectMethod::Add)) {
            Datum value = context.pop();
            Datum mutableTarget = context.pop();
            mutableTarget.listValue().items().push_back(std::move(value));
            if (!noReturn) {
                context.push(Datum::voidValue());
            }
            return true;
        }

        if (argCount == 1 && method == ImmediateObjectMethod::Duplicate) {
            const auto& list = target.listValue();
            Datum result = list.deepCopyDatum();
            context.scope().drop(argCount);
            if (!noReturn) {
                context.push(std::move(result));
            }
            return true;
        }
    }

    if (target.isPropList()) {
        if (argCount == 1 && method == ImmediateObjectMethod::Count) {
            const int count = target.propListValue().count();
            context.scope().drop(argCount);
            if (!noReturn) {
                context.push(Datum::of(count));
            }
            return true;
        }

        if (argCount == 2 &&
            (method == ImmediateObjectMethod::GetProp || method == ImmediateObjectMethod::GetPropRef ||
             method == ImmediateObjectMethod::GetAProp || method == ImmediateObjectMethod::GetProperty)) {
            const Datum& key = context.peekRef(0);
            std::string keyNameStorage;
            const int propIndex = target.propListValue().findUntypedKeyName(keyNameLikeJavaView(key, keyNameStorage));
            Datum result = propIndex >= 0
                ? target.propListValue().properties()[static_cast<std::size_t>(propIndex)].second
                : Datum::voidValue();
            context.scope().drop(argCount);
            if (!noReturn) {
                context.push(std::move(result));
            }
            return true;
        }

        if (argCount == 2 && method == ImmediateObjectMethod::GetAt) {
            const Datum& keyOrIndex = context.peekRef(0);
            if (auto snapshotResult =
                    indexedCollectionSnapshotGetAtDirect(context, methodName, target, keyOrIndex)) {
                context.scope().drop(argCount);
                if (!noReturn) {
                    context.push(std::move(*snapshotResult));
                }
                return true;
            }
            Datum result = propListObjectGetAtValue(target.propListValue(), keyOrIndex);
            context.scope().drop(argCount);
            if (!noReturn) {
                context.push(std::move(result));
            }
            return true;
        }

        if (argCount == 3 && method == ImmediateObjectMethod::SetAt) {
            Datum value = context.pop();
            Datum keyOrIndex = context.pop();
            Datum mutableTarget = context.pop();
            auto& propList = mutableTarget.propListValue();
            if (keyOrIndex.isString() || keyOrIndex.isSymbol()) {
                propList.putSameType(std::move(keyOrIndex), std::move(value));
            } else {
                const int position = toIntLikeJava(keyOrIndex) - 1;
                if (position >= 0 && position < propList.count()) {
                    propList.properties()[static_cast<std::size_t>(position)].second = std::move(value);
                }
            }
            if (!noReturn) {
                context.push(Datum::voidValue());
            }
            return true;
        }

        if (argCount == 3 && (method == ImmediateObjectMethod::SetProp || method == ImmediateObjectMethod::SetAProp)) {
            Datum value = context.pop();
            Datum key = context.pop();
            Datum mutableTarget = context.pop();
            mutableTarget.propListValue().putTyped(std::move(key), std::move(value));
            if (!noReturn) {
                context.push(Datum::voidValue());
            }
            return true;
        }
    }

    if (target.type() == DatumType::ScriptInstanceRef) {
        if (argCount == 1 && method == ImmediateObjectMethod::Count) {
            const auto& instance = target.scriptInstanceValue();
            const int count = static_cast<int>(instance.properties().size()) +
                              (instance.ancestor() ? 1 : 0);
            context.scope().drop(argCount);
            if (!noReturn) {
                context.push(Datum::of(count));
            }
            return true;
        }

        if (argCount == 2 && method == ImmediateObjectMethod::Count) {
            const auto& instance = target.scriptInstanceValue();
            std::string propNameStorage;
            const std::string_view propName = keyNameLikeJavaView(context.peekRef(0), propNameStorage);
            Datum result = scriptInstancePropertyCountValue(instance, propName);
            context.scope().drop(argCount);
            if (!noReturn) {
                context.push(std::move(result));
            }
            return true;
        }

        if ((argCount == 2 || argCount == 3) && method == ImmediateObjectMethod::GetAt) {
            const auto& instance = target.scriptInstanceValue();
            std::string propNameStorage;
            const std::string_view propName = keyNameLikeJavaView(context.peekRef(argCount - 2), propNameStorage);
            Datum result = argCount == 3
                ? scriptInstanceNestedPropertyValue(instance, propName, context.peekRef(0))
                : scriptInstancePropertyValue(instance, propName);
            if (argCount == 2 && equalsIgnoreCase(propName, "ancestor") && result.isVoid()) {
                result = Datum::of(0);
            }
            context.scope().drop(argCount);
            if (!noReturn) {
                context.push(std::move(result));
            }
            return true;
        }

        if (argCount == 2 && method == ImmediateObjectMethod::GetAProp) {
            const auto& instance = target.scriptInstanceValue();
            std::string propNameStorage;
            const std::string_view propName = keyNameLikeJavaView(context.peekRef(0), propNameStorage);
            Datum result = scriptInstancePropertyValue(instance, propName);
            context.scope().drop(argCount);
            if (!noReturn) {
                context.push(std::move(result));
            }
            return true;
        }

        if ((argCount == 2 || argCount == 3) &&
            (method == ImmediateObjectMethod::GetProp || method == ImmediateObjectMethod::GetPropRef)) {
            const auto& instance = target.scriptInstanceValue();
            std::string propNameStorage;
            const std::string_view propName = keyNameLikeJavaView(context.peekRef(argCount - 2), propNameStorage);
            Datum result = argCount == 3
                ? scriptInstanceNestedPropertyValue(instance, propName, context.peekRef(0))
                : scriptInstancePropertyValue(instance, propName);
            context.scope().drop(argCount);
            if (!noReturn) {
                context.push(std::move(result));
            }
            return true;
        }

        if ((argCount == 3 || argCount == 4) &&
            (method == ImmediateObjectMethod::SetAt || method == ImmediateObjectMethod::SetAProp)) {
            auto instance = target.scriptInstancePtr();
            Datum value = context.pop();
            if (argCount == 3) {
                std::string propNameStorage;
                const std::string_view propName = keyNameLikeJavaView(context.peekRef(0), propNameStorage);
                util::setProperty(*instance, propName, std::move(value));
                context.scope().drop(2);
            } else {
                Datum subKey = context.pop();
                std::string propNameStorage;
                const std::string_view propName = keyNameLikeJavaView(context.peekRef(0), propNameStorage);
                Datum localProp = util::getProperty(*instance, propName);
                if (localProp.isVoid()) {
                    localProp = Datum::propList();
                    util::setProperty(*instance, propName, localProp);
                }
                scriptInstanceSetNestedProperty(localProp, subKey, std::move(value));
                util::setProperty(*instance, propName, std::move(localProp));
                context.scope().drop(2);
            }
            if (!noReturn) {
                context.push(Datum::voidValue());
            }
            return true;
        }

        if ((argCount == 3 || argCount == 4) && method == ImmediateObjectMethod::SetProp) {
            auto instance = target.scriptInstancePtr();
            Datum value = context.pop();
            if (argCount == 3) {
                std::string propNameStorage;
                const std::string_view propName = keyNameLikeJavaView(context.peekRef(0), propNameStorage);
                util::setProperty(*instance, propName, std::move(value));
                context.scope().drop(2);
            } else {
                Datum subKey = context.pop();
                std::string propNameStorage;
                const std::string_view propName = keyNameLikeJavaView(context.peekRef(0), propNameStorage);
                Datum localProp = util::getProperty(*instance, propName);
                if (localProp.isVoid()) {
                    localProp = Datum::propList();
                    util::setProperty(*instance, propName, localProp);
                }
                scriptInstanceSetNestedProperty(localProp, subKey, std::move(value));
                util::setProperty(*instance, propName, std::move(localProp));
                context.scope().drop(2);
            }
            if (!noReturn) {
                context.push(Datum::voidValue());
            }
            return true;
        }

        if (argCount == 3 && method == ImmediateObjectMethod::AddProp) {
            auto instance = target.scriptInstancePtr();
            Datum value = context.pop();
            std::string propNameStorage;
            const std::string_view propName = keyNameLikeJavaView(context.peekRef(0), propNameStorage);
            scriptInstancePutLocalProperty(*instance, propName, std::move(value));
            context.scope().drop(2);
            if (!noReturn) {
                context.push(Datum::voidValue());
            }
            return true;
        }

        if (argCount == 2 && method == ImmediateObjectMethod::DeleteProp) {
            auto instance = target.scriptInstancePtr();
            std::string propNameStorage;
            const std::string_view propName = keyNameLikeJavaView(context.peekRef(0), propNameStorage);
            scriptInstanceDeleteLocalProperty(*instance, propName);
            context.scope().drop(argCount);
            if (!noReturn) {
                context.push(Datum::voidValue());
            }
            return true;
        }

        if (argCount == 1 && method == ImmediateObjectMethod::Ilk) {
            context.scope().drop(argCount);
            if (!noReturn) {
                context.push(Datum::symbol("instance"));
            }
            return true;
        }

        if (argCount == 1 && method == ImmediateObjectMethod::AddAt) {
            context.scope().drop(argCount);
            if (!noReturn) {
                context.push(Datum::voidValue());
            }
            return true;
        }
    }

    if (target.isString() &&
        (argCount == 3 || argCount == 4) &&
        (method == ImmediateObjectMethod::GetProp || method == ImmediateObjectMethod::GetPropRef)) {
        const bool getPropRef = method == ImmediateObjectMethod::GetPropRef;
        std::string chunkNameStorage;
        const std::string_view chunkName = keyNameLikeJavaView(context.peekRef(argCount - 2), chunkNameStorage);
        const auto chunkType = stringChunkTypeByNameNoThrow(chunkName);
        if (!chunkType.has_value()) {
            return false;
        }

        std::string valueStorage;
        const std::string_view value = stringViewLikeJava(target, valueStorage);
        const int start = toIntLikeJava(context.peekRef(argCount - 3));
        const int end = !getPropRef && argCount >= 4 ? toIntLikeJava(context.peekRef(argCount - 4)) : start;
        Datum result = Datum::voidValue();
        if (*chunkType == StringChunkType::Char && start == end) {
            result = (start >= 1 && start <= static_cast<int>(value.size()))
                ? Datum::of(std::string(1, value[static_cast<std::size_t>(start - 1)]))
                : Datum::of(std::string());
        }
        if (*chunkType == StringChunkType::Line) {
            if (const auto* lineIndex = cachedFieldLineIndex(target, context.builtinContext())) {
                result = Datum::of(util::getLineRange(value, *lineIndex, start, end));
            }
        }
        if (result.isVoid()) {
            result = Datum::of(util::getChunkRange(value, *chunkType, start, end, currentItemDelimiter(context)));
        }
        context.scope().drop(argCount);
        if (!noReturn) {
            context.push(std::move(result));
        }
        return true;
    }

    if (target.isString() && argCount == 2 && method == ImmediateObjectMethod::Count) {
        std::string chunkNameStorage;
        const std::string_view chunkName = keyNameLikeJavaView(context.peekRef(0), chunkNameStorage);
        const auto chunkType = stringChunkTypeByNameNoThrow(chunkName);
        if (!chunkType.has_value()) {
            return false;
        }

        std::string valueStorage;
        const std::string_view value = stringViewLikeJava(target, valueStorage);
        Datum result = Datum::voidValue();
        if (*chunkType == StringChunkType::Line) {
            if (const auto* lineIndex = cachedFieldLineIndex(target, context.builtinContext())) {
                result = Datum::of(util::lineCount(*lineIndex));
            }
        }
        if (result.isVoid()) {
            result = Datum::of(util::countChunks(value, *chunkType, currentItemDelimiter(context)));
        }
        context.scope().drop(argCount);
        if (!noReturn) {
            context.push(std::move(result));
        }
        return true;
    }

    return false;
}

bool tryImmediateObjCall(ExecutionContext& context, bool noReturn) {
    if (context.instructionTraceEnabled()) {
        return false;
    }

    auto& scope = context.scope();
    const int currentIndex = scope.bytecodeIndex();
    const int nextIndex = currentIndex + 1;
    const auto& instructions = scope.handler().instructions;
    if (nextIndex < 0 || nextIndex >= static_cast<int>(instructions.size())) {
        return false;
    }

    const auto& next = instructions[static_cast<std::size_t>(nextIndex)];
    if (next.opcode != Opcode::OBJ_CALL) {
        return false;
    }

    const int argCount = context.argument();
    scope.setBytecodeIndex(nextIndex);
    context.setInstruction(next);
    const std::string& methodName = context.resolveNameRef(next.argument);
    const ImmediateObjectMethod method = classifyImmediateObjectMethod(methodName);
    if (tryImmediateFastObjCall(context, methodName, method, argCount, noReturn)) {
        return true;
    }
    if (argCount >= 2 && argCount <= 4 &&
        isFastScriptInstanceObjectMethod(method) &&
        context.peekRef(argCount - 1).type() == DatumType::ScriptInstanceRef) {
        std::array<Datum, 4> argsStorage{};
        for (int index = argCount - 1; index >= 0; --index) {
            argsStorage[static_cast<std::size_t>(index)] = context.pop();
        }
        const std::span<const Datum> args(argsStorage.data(), static_cast<std::size_t>(argCount));
        if (auto fastResult = fastScriptInstanceObjectCall(method, args)) {
            if (!noReturn) {
                context.push(std::move(*fastResult));
            }
            return true;
        }
        return executeObjCallWithArgs(context, methodName, method, args, noReturn);
    }
    if (argCount >= 0 && argCount <= 8) {
        std::array<Datum, 8> argsStorage{};
        for (int index = argCount - 1; index >= 0; --index) {
            argsStorage[static_cast<std::size_t>(index)] = context.pop();
        }
        return executeObjCallWithArgs(context,
                                      methodName,
                                      method,
                                      std::span<const Datum>(argsStorage.data(), static_cast<std::size_t>(argCount)),
                                      noReturn);
    }

    std::vector<Datum> args = context.popArgs(argCount);
    return executeObjCallWithArgs(context, methodName, method, args, noReturn);
}

bool executeExtCallWithArgsImpl(ExecutionContext& context,
                                std::string_view handlerName,
                                std::span<const Datum> args,
                                bool noReturn,
                                const std::vector<Datum>* existingArgs) {
    std::vector<Datum> materializedArgs;
    const std::vector<Datum>* materializedArgsPtr = existingArgs;
    auto vectorArgs = [&]() -> const std::vector<Datum>& {
        if (materializedArgsPtr == nullptr) {
            materializedArgs.assign(args.begin(), args.end());
            materializedArgsPtr = &materializedArgs;
        }
        return *materializedArgsPtr;
    };

    Datum result = Datum::voidValue();
    if (equalsIgnoreCase(handlerName, "return")) {
        context.setReturnValue(args.empty() ? Datum::voidValue() : args[0]);
    } else if (equalsIgnoreCase(handlerName, "voidp")) {
        result = (args.empty() || args[0].isVoid()) ? Datum::TRUE : Datum::FALSE;
    } else if (equalsIgnoreCase(handlerName, "new")) {
        if (auto builtinResult = context.invokeBuiltinIfPresent(handlerName, vectorArgs())) {
            result = std::move(*builtinResult);
        } else if (const auto handler = context.findHandler(handlerName)) {
            result = safeExecuteHandler(context, *handler, args, Datum::voidValue());
        }
    } else if (const auto handler = context.findHandler(handlerName)) {
        if (handler->script != nullptr && handler->scriptType == chunks::ScriptChunkType::Parent) {
            if (auto builtinResult = context.invokeBuiltinIfPresent(handlerName, vectorArgs())) {
                result = std::move(*builtinResult);
            } else {
                result = safeExecuteHandler(context, *handler, args, Datum::voidValue());
            }
        } else {
            result = safeExecuteHandler(context, *handler, args, Datum::voidValue());
        }
    } else if (auto fastPrimitiveResult = fastPrimitiveBuiltinCall(context, handlerName, args)) {
        result = std::move(*fastPrimitiveResult);
    } else if (auto fastBuiltinResult = fastListBuiltinCall(handlerName, args)) {
        result = std::move(*fastBuiltinResult);
    } else if (auto builtinResult = context.invokeBuiltinIfPresent(handlerName, vectorArgs())) {
        result = std::move(*builtinResult);
    } else if (!args.empty()) {
        Datum target = args.front();
        const std::span<const Datum> methodArgs = args.size() <= 1
                                                      ? std::span<const Datum>()
                                                      : std::span<const Datum>(args.data() + 1, args.size() - 1);
        result = dispatchObjectMethodSpan(context, std::move(target), handlerName, methodArgs);
    } else if (args.empty()) {
        result = builtinConstant(handlerName).value_or(Datum::voidValue());
    }

    if (!noReturn) {
        context.push(std::move(result));
    }
    return true;
}

bool executeExtCallWithArgs(ExecutionContext& context,
                            std::string_view handlerName,
                            std::span<const Datum> args,
                            bool noReturn) {
    return executeExtCallWithArgsImpl(context, handlerName, args, noReturn, nullptr);
}

bool executeExtCallWithArgs(ExecutionContext& context,
                            std::string_view handlerName,
                            const std::vector<Datum>& args,
                            bool noReturn) {
    return executeExtCallWithArgsImpl(context, handlerName, std::span<const Datum>(args), noReturn, &args);
}

bool tryImmediateCallBuiltin(ExecutionContext& context,
                             std::string_view handlerName,
                             int argCount,
                             int nextIndex,
                             const chunks::ScriptChunk::Instruction& nextInstruction,
                             bool noReturn) {
    if (!equalsIgnoreCase(handlerName, "call") || argCount < 2) {
        return false;
    }
    auto* builtinContext = context.builtinContext();
    if (builtinContext == nullptr || !builtinContext->callTargetHandler) {
        return false;
    }

    const Datum& handlerDatum = context.peekRef(argCount - 1);
    const std::string targetHandlerName = handlerDatum.asSymbol() != nullptr ? handlerDatum.asSymbol()->name
                                                                             : toStringLikeJava(handlerDatum);
    const Datum& targetRef = context.peekRef(argCount - 2);
    std::vector<Datum> callArgs = snapshotCallArgsFromStack(context, argCount);
    Datum lastResult = Datum::voidValue();
    context.scope().setBytecodeIndex(nextIndex);
    context.setInstruction(nextInstruction);

    if (targetRef.isList()) {
        std::vector<Datum> snapshot = targetRef.listValue().items();
        context.scope().drop(argCount);
        for (const auto& item : snapshot) {
            lastResult = builtinContext->callTargetHandler(item, targetHandlerName, callArgs);
        }
    } else if (targetRef.isPropList()) {
        std::vector<Datum> snapshot;
        snapshot.reserve(targetRef.propListValue().properties().size());
        for (const auto& entry : targetRef.propListValue().properties()) {
            snapshot.push_back(entry.second);
        }
        context.scope().drop(argCount);
        for (const auto& item : snapshot) {
            lastResult = builtinContext->callTargetHandler(item, targetHandlerName, callArgs);
        }
    } else {
        Datum target = targetRef;
        context.scope().drop(argCount);
        lastResult = builtinContext->callTargetHandler(target, targetHandlerName, callArgs);
    }

    if (!noReturn) {
        context.push(std::move(lastResult));
    }
    return true;
}

bool tryImmediateExtCall(ExecutionContext& context, bool noReturn) {
    if (context.instructionTraceEnabled()) {
        return false;
    }

    auto& scope = context.scope();
    const int currentIndex = scope.bytecodeIndex();
    const int nextIndex = currentIndex + 1;
    const auto& instructions = scope.handler().instructions;
    if (nextIndex < 0 || nextIndex >= static_cast<int>(instructions.size())) {
        return false;
    }

    const auto& next = instructions[static_cast<std::size_t>(nextIndex)];
    if (next.opcode != Opcode::EXT_CALL) {
        return false;
    }

    const std::string& handlerName = context.resolveNameRef(next.argument);
    const int argCount = context.argument();
    if (tryImmediateCallBuiltin(context, handlerName, argCount, nextIndex, next, noReturn)) {
        return true;
    }
    if (tryImmediatePrimitiveExtCall(context, handlerName, context.argument(), noReturn)) {
        scope.setBytecodeIndex(nextIndex);
        context.setInstruction(next);
        return true;
    }

    if (argCount >= 0 && argCount <= 8 &&
        !equalsIgnoreCase(handlerName, "return") &&
        !equalsIgnoreCase(handlerName, "voidp") &&
        !equalsIgnoreCase(handlerName, "new")) {
        if (const auto handler = context.findHandler(handlerName);
            handler.has_value() && handler->script != nullptr &&
            handler->scriptType != chunks::ScriptChunkType::Parent) {
            std::array<Datum, 8> argsStorage{};
            for (int index = argCount - 1; index >= 0; --index) {
                argsStorage[static_cast<std::size_t>(index)] = context.pop();
            }
            scope.setBytecodeIndex(nextIndex);
            context.setInstruction(next);
            Datum result = safeExecuteHandler(context,
                                              *handler,
                                              std::span<const Datum>(argsStorage.data(),
                                                                     static_cast<std::size_t>(argCount)),
                                              Datum::voidValue());
            if (!noReturn) {
                context.push(std::move(result));
            }
            return true;
        }
    }

    if (argCount >= 0 && argCount <= 8) {
        std::array<Datum, 8> argsStorage{};
        for (int index = argCount - 1; index >= 0; --index) {
            argsStorage[static_cast<std::size_t>(index)] = context.pop();
        }
        scope.setBytecodeIndex(nextIndex);
        context.setInstruction(next);
        return executeExtCallWithArgs(context,
                                      handlerName,
                                      std::span<const Datum>(argsStorage.data(), static_cast<std::size_t>(argCount)),
                                      noReturn);
    }

    std::vector<Datum> args = context.popArgs(argCount);
    scope.setBytecodeIndex(nextIndex);
    context.setInstruction(next);
    return executeExtCallWithArgs(context, handlerName, args, noReturn);
}

bool extCall(ExecutionContext& context) {
    const std::string& handlerName = context.resolveNameRef(context.argument());
    const Datum argListDatum = context.pop();
    const bool noReturn = isNoReturnArgList(argListDatum);
    std::vector<Datum> argStorage;
    const std::vector<Datum>& args = argListItemsRef(argListDatum, argStorage);
    return executeExtCallWithArgs(context, handlerName, args, noReturn);
}

bool objCall(ExecutionContext& context) {
    const std::string& methodName = context.resolveNameRef(context.argument());
    const Datum argListDatum = context.pop();
    const bool noReturn = isNoReturnArgList(argListDatum);
    std::vector<Datum> argStorage;
    const std::vector<Datum>& args = argListItemsRef(argListDatum, argStorage);
    return executeObjCallWithArgs(context, methodName, args, noReturn);
}

} // namespace

std::string imageOperationTraceJson() {
    std::ostringstream out;
    out << "{\"events\":[";
    for (std::size_t index = 0; index < imageOperationTrace.size(); ++index) {
        if (index > 0) {
            out << ',';
        }
        out << imageOperationTrace[index];
    }
    out << "]}";
    return out.str();
}

void clearImageOperationTrace() {
    imageOperationTrace.clear();
}

namespace dispatch {

void ImageMethodDispatcher::setImageMutationCallback(std::function<void()> callback) {
    imageMutationCallbackOwner = nullptr;
    imageMutationCallback = std::move(callback);
}

void ImageMethodDispatcher::setImageMutationCallback(void* owner, std::function<void()> callback) {
    imageMutationCallbackOwner = owner;
    imageMutationCallback = std::move(callback);
}

void ImageMethodDispatcher::clearImageMutationCallback(void* owner) {
    if (imageMutationCallbackOwner == owner) {
        imageMutationCallbackOwner = nullptr;
        imageMutationCallback = {};
    }
}

Datum ScriptInstanceMethodDispatcher::dispatch(ExecutionContext& context,
                                               Datum& receiver,
                                               std::string_view methodName,
                                               const std::vector<Datum>& args) {
    return scriptInstanceObjectMethod(context, receiver, methodName, args);
}

Datum ImageMethodDispatcher::dispatch(const Datum::ImageRef& imageRef,
                                      std::string_view methodName,
                                      const std::vector<Datum>& args) {
    return imageObjectMethod(imageRef, methodName, args);
}

Datum ImageMethodDispatcher::getProperty(const Datum::ImageRef& imageRef, std::string_view propName) {
    return getImageProp(imageRef, propName);
}

void ImageMethodDispatcher::setProperty(builtin::BuiltinContext* context,
                                        const Datum::ImageRef& imageRef,
                                        std::string_view propName,
                                        const Datum& value) {
    setImageProp(context, imageRef, propName, value);
}

} // namespace dispatch

OpcodeRegistry::OpcodeRegistry() {
    StackOpcodes::registerHandlers(*this);
    ArithmeticOpcodes::registerHandlers(*this);
    ComparisonOpcodes::registerHandlers(*this);
    LogicalOpcodes::registerHandlers(*this);
    StringOpcodes::registerHandlers(*this);
    VariableOpcodes::registerHandlers(*this);
    ControlFlowOpcodes::registerHandlers(*this);
    ListOpcodes::registerHandlers(*this);
    CallOpcodes::registerHandlers(*this);
    PropertyOpcodes::registerHandlers(*this);
    defaultRawHandlers_ = rawHandlers_;
}

const OpcodeHandler* OpcodeRegistry::get(Opcode opcode) const {
    const auto index = static_cast<std::size_t>(code(opcode));
    if (index >= handlers_.size() || handlers_[index] == nullptr) {
        return nullptr;
    }
    return &handlers_[index];
}

OpcodeHandlerFn OpcodeRegistry::getRaw(Opcode opcode) const {
    const auto index = static_cast<std::size_t>(code(opcode));
    return index < rawHandlers_.size() ? rawHandlers_[index] : nullptr;
}

bool OpcodeRegistry::isDefaultRawHandler(Opcode opcode) const {
    const auto index = static_cast<std::size_t>(code(opcode));
    return index < rawHandlers_.size() &&
           rawHandlers_[index] != nullptr &&
           rawHandlers_[index] == defaultRawHandlers_[index];
}

bool OpcodeRegistry::hasHandler(Opcode opcode) const {
    return getRaw(opcode) != nullptr || get(opcode) != nullptr;
}

bool OpcodeRegistry::execute(Opcode opcode, ExecutionContext& context) const {
    if (const auto handler = getRaw(opcode)) {
        return handler(context);
    }
    const auto* handler = get(opcode);
    return handler != nullptr && (*handler)(context);
}

void OpcodeRegistry::registerHandler(Opcode opcode, OpcodeHandlerFn handler) {
    const auto index = static_cast<std::size_t>(code(opcode));
    if (index < handlers_.size()) {
        rawHandlers_[index] = handler;
        handlers_[index] = handler;
    }
}

void OpcodeRegistry::registerFunctionHandler(Opcode opcode, OpcodeHandler handler) {
    const auto index = static_cast<std::size_t>(code(opcode));
    if (index < handlers_.size()) {
        rawHandlers_[index] = nullptr;
        handlers_[index] = handler;
    }
}

void StackOpcodes::registerHandlers(OpcodeRegistry& registry) {
    registry.registerHandler(Opcode::PUSH_ZERO, pushZero);
    registry.registerHandler(Opcode::PUSH_INT8, pushInt);
    registry.registerHandler(Opcode::PUSH_INT16, pushInt);
    registry.registerHandler(Opcode::PUSH_INT32, pushInt);
    registry.registerHandler(Opcode::PUSH_FLOAT32, pushFloat);
    registry.registerHandler(Opcode::PUSH_CONS, pushCons);
    registry.registerHandler(Opcode::PUSH_SYMB, pushSymb);
    registry.registerHandler(Opcode::PUSH_CHUNK_VAR_REF, pushChunkVarRef);
    registry.registerHandler(Opcode::SWAP, swap);
    registry.registerHandler(Opcode::POP, pop);
    registry.registerHandler(Opcode::PEEK, peek);
    registry.registerHandler(Opcode::NEW_OBJ, newObj);
}

void ControlFlowOpcodes::registerHandlers(OpcodeRegistry& registry) {
    registry.registerHandler(Opcode::RET, ret);
    registry.registerHandler(Opcode::RET_FACTORY, retFactory);
    registry.registerHandler(Opcode::JMP, jmp);
    registry.registerHandler(Opcode::JMP_IF_Z, jmpIfZero);
    registry.registerHandler(Opcode::END_REPEAT, endRepeat);
}

void ArithmeticOpcodes::registerHandlers(OpcodeRegistry& registry) {
    registry.registerHandler(Opcode::ADD, add);
    registry.registerHandler(Opcode::SUB, sub);
    registry.registerHandler(Opcode::MUL, mul);
    registry.registerHandler(Opcode::DIV, div);
    registry.registerHandler(Opcode::MOD, mod);
    registry.registerHandler(Opcode::INV, inv);
}

void ComparisonOpcodes::registerHandlers(OpcodeRegistry& registry) {
    registry.registerHandler(Opcode::LT, lt);
    registry.registerHandler(Opcode::LT_EQ, ltEq);
    registry.registerHandler(Opcode::GT, gt);
    registry.registerHandler(Opcode::GT_EQ, gtEq);
    registry.registerHandler(Opcode::EQ, eq);
    registry.registerHandler(Opcode::NT_EQ, notEq);
    registry.registerHandler(Opcode::ONTO_SPR, spriteIntersects);
    registry.registerHandler(Opcode::INTO_SPR, spriteWithin);
}

void LogicalOpcodes::registerHandlers(OpcodeRegistry& registry) {
    registry.registerHandler(Opcode::AND, logicalAnd);
    registry.registerHandler(Opcode::OR, logicalOr);
    registry.registerHandler(Opcode::NOT, logicalNot);
}

void StringOpcodes::registerHandlers(OpcodeRegistry& registry) {
    registry.registerHandler(Opcode::GET_CHUNK, getChunk);
    registry.registerHandler(Opcode::JOIN_STR, joinStr);
    registry.registerHandler(Opcode::JOIN_PAD_STR, joinPadStr);
    registry.registerHandler(Opcode::CONTAINS_STR, containsStr);
    registry.registerHandler(Opcode::CONTAINS_0_STR, contains0Str);
    registry.registerHandler(Opcode::PUT, put);
    registry.registerHandler(Opcode::DELETE_CHUNK, deleteChunk);
    registry.registerHandler(Opcode::PUT_CHUNK, putChunk);
}

void VariableOpcodes::registerHandlers(OpcodeRegistry& registry) {
    registry.registerHandler(Opcode::GET_LOCAL, getLocal);
    registry.registerHandler(Opcode::SET_LOCAL, setLocal);
    registry.registerHandler(Opcode::GET_PARAM, getParam);
    registry.registerHandler(Opcode::SET_PARAM, setParam);
    registry.registerHandler(Opcode::GET_GLOBAL, getGlobal);
    registry.registerHandler(Opcode::GET_GLOBAL2, getGlobal);
    registry.registerHandler(Opcode::SET_GLOBAL, setGlobal);
    registry.registerHandler(Opcode::SET_GLOBAL2, setGlobal);
}

void ListOpcodes::registerHandlers(OpcodeRegistry& registry) {
    registry.registerHandler(Opcode::PUSH_LIST, pushList);
    registry.registerHandler(Opcode::PUSH_PROP_LIST, pushPropList);
    registry.registerHandler(Opcode::PUSH_ARG_LIST, pushArgList);
    registry.registerHandler(Opcode::PUSH_ARG_LIST_NO_RET, pushArgListNoRet);
}

void PropertyOpcodes::registerHandlers(OpcodeRegistry& registry) {
    registry.registerHandler(Opcode::GET_PROP, getProp);
    registry.registerHandler(Opcode::SET_PROP, setProp);
    registry.registerHandler(Opcode::GET_MOVIE_PROP, getMovieProp);
    registry.registerHandler(Opcode::SET_MOVIE_PROP, setMovieProp);
    registry.registerHandler(Opcode::GET_OBJ_PROP, getObjProp);
    registry.registerHandler(Opcode::SET_OBJ_PROP, setObjProp);
    registry.registerHandler(Opcode::THE_BUILTIN, theBuiltin);
    registry.registerHandler(Opcode::GET_CHAINED_PROP, getChainedProp);
    registry.registerHandler(Opcode::GET_TOP_LEVEL_PROP, getTopLevelProp);
    registry.registerHandler(Opcode::GET, getLegacyProperty);
    registry.registerHandler(Opcode::SET, setLegacyProperty);
    registry.registerHandler(Opcode::GET_FIELD, getField);
}

void CallOpcodes::registerHandlers(OpcodeRegistry& registry) {
    registry.registerHandler(Opcode::LOCAL_CALL, localCall);
    registry.registerHandler(Opcode::EXT_CALL, extCall);
    registry.registerHandler(Opcode::OBJ_CALL, objCall);
}

} // namespace libreshockwave::lingo::vm
