#include "libreshockwave/lingo/vm/opcode_registry.hpp"

#include "libreshockwave/bitmap/bitmap.hpp"
#include "libreshockwave/bitmap/color_ref.hpp"
#include "libreshockwave/bitmap/drawing.hpp"
#include "libreshockwave/bitmap/palette.hpp"
#include "../../bitmap/bitmap_processing.hpp"
#include "libreshockwave/lingo/vm/dispatch/image_method_dispatcher.hpp"
#include "libreshockwave/lingo/vm/dispatch/list_method_dispatcher.hpp"
#include "libreshockwave/lingo/vm/dispatch/prop_list_method_dispatcher.hpp"
#include "libreshockwave/lingo/vm/dispatch/script_instance_method_dispatcher.hpp"
#include "libreshockwave/lingo/vm/dispatch/sound_channel_method_dispatcher.hpp"
#include "libreshockwave/lingo/vm/dispatch/string_method_dispatcher.hpp"
#include "libreshockwave/lingo/vm/property_id_mappings.hpp"
#include "libreshockwave/lingo/vm/util/ancestor_chain_walker.hpp"
#include "libreshockwave/lingo/vm/util/string_chunk_utils.hpp"

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

std::function<void()> image_mutation_callback;
void* image_mutation_callback_owner = nullptr;
std::deque<std::string> image_operation_trace;
int image_operation_trace_next_id = 1;
constexpr std::size_t k_max_image_operation_trace_entries = 512;

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
    int min_x = bitmap.width();
    int min_y = bitmap.height();
    int max_x = -1;
    int max_y = -1;
    int opaque_pixels = 0;
    int transparent_pixels = 0;
    for (int y = 0; y < bitmap.height(); ++y) {
        for (int x = 0; x < bitmap.width(); ++x) {
            const auto pixel = bitmap.getPixel(x, y);
            const int alpha = static_cast<int>((pixel >> 24U) & 0xFFU);
            if (alpha == 0) {
                ++transparent_pixels;
                continue;
            }
            ++opaque_pixels;
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
        }
    }
    out << ",\"" << key << "\":{\"width\":" << bitmap.width()
        << ",\"height\":" << bitmap.height()
        << ",\"bitDepth\":" << bitmap.bitDepth()
        << ",\"nativeAlpha\":" << (bitmap.isNativeAlpha() ? "true" : "false")
        << ",\"scriptModified\":" << (bitmap.isScriptModified() ? "true" : "false")
        << ",\"scriptFillBacking\":" << (bitmap.hasScriptFillBacking() ? "true" : "false")
        << ",\"preserveScriptFillBacking\":" << (bitmap.preservesScriptFillBacking() ? "true" : "false")
        << ",\"rectangularMedia\":" << (bitmap.isRectangularMedia() ? "true" : "false")
        << ",\"textRendered\":" << (bitmap.isTextRendered() ? "true" : "false")
        << ",\"palette_indices\":" << (bitmap.paletteIndices().has_value() ? "true" : "false")
        << ",\"opaquePixels\":" << opaque_pixels
        << ",\"transparentPixels\":" << transparent_pixels;
    if (max_x >= 0) {
        out << ",\"alphaBounds\":{\"left\":" << min_x
            << ",\"top\":" << min_y
            << ",\"right\":" << (max_x + 1)
            << ",\"bottom\":" << (max_y + 1)
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
    if (image_operation_trace.size() >= k_max_image_operation_trace_entries) {
        image_operation_trace.pop_front();
    }
    image_operation_trace.push_back(std::move(entry));
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
            return Datum::of(literal.numeric_value);
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
    if (const auto direct_value = directStringViewLikeJava(datum)) {
        return *direct_value;
    }
    storage = toStringLikeJava(datum);
    return storage;
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
        color != nullptr && color->palette_index.has_value() && target != nullptr) {
        return 0xFF000000U | (target->resolvePaletteIndex(*color->palette_index, nullptr) & 0x00FFFFFFU);
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

std::optional<int> imageFillPaletteIndex(const Datum& color_datum, const bitmap::Bitmap& target) {
    if (target.bitDepth() > 8 || target.imagePalette() == nullptr) {
        return std::nullopt;
    }
    if (const auto* color = color_datum.asColorRef(); color != nullptr && color->palette_index.has_value()) {
        return *color->palette_index & 0xFF;
    }
    if (const auto* value = color_datum.asInt(); value != nullptr && value->value >= 0 && value->value <= 255) {
        return value->value;
    }
    return std::nullopt;
}

std::uint32_t imageCopyPixelsRemapColorArgb(const Datum& datum,
                                            const bitmap::Bitmap& src,
                                            const bitmap::Bitmap& dest) {
    const auto* target = &dest;
    if (const auto* color = datum.asColorRef();
        color != nullptr && color->palette_index.has_value() && src.imagePalette() != nullptr) {
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
    if (const auto* field = datum.asFieldText()) {
        return parseDoubleStrict(field->value).value_or(0.0);
    }
    if (const auto* chunk = datum.asStringChunk()) {
        return parseDoubleStrict(chunk->value).value_or(0.0);
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

bool isSpecialArithmeticDatum(const Datum& datum) {
    return datum.isList() || datum.asIntPoint() != nullptr || datum.asIntRect() != nullptr || datum.asColorRef() != nullptr;
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

Datum scaleList(const Datum::List& list, double scalar, bool scalar_is_float) {
    std::vector<Datum> result;
    result.reserve(list.items().size());
    for (const auto& item : list.items()) {
        if (isFloatLike(item) || scalar_is_float) {
            result.emplace_back(Datum::of(toDoubleLikeJava(item) * scalar));
        } else {
            result.emplace_back(Datum::of(static_cast<int>(toIntLikeJava(item) * scalar)));
        }
    }
    return Datum::list(std::move(result));
}

Datum addScalarToList(const Datum::List& list, const Datum& scalar_datum) {
    const double scalar = toDoubleLikeJava(scalar_datum);
    std::vector<Datum> result;
    result.reserve(list.items().size());
    for (const auto& item : list.items()) {
        result.emplace_back(numericResult(item, scalar_datum, toDoubleLikeJava(item) + scalar));
    }
    return Datum::list(std::move(result));
}

Datum subtractScalarFromList(const Datum::List& list, const Datum& scalar_datum) {
    const double scalar = toDoubleLikeJava(scalar_datum);
    std::vector<Datum> result;
    result.reserve(list.items().size());
    for (const auto& item : list.items()) {
        result.emplace_back(numericResult(item, scalar_datum, toDoubleLikeJava(item) - scalar));
    }
    return Datum::list(std::move(result));
}

Datum subtractListFromScalar(const Datum& scalar_datum, const Datum::List& list) {
    const double scalar = toDoubleLikeJava(scalar_datum);
    std::vector<Datum> result;
    result.reserve(list.items().size());
    for (const auto& item : list.items()) {
        result.emplace_back(numericResult(scalar_datum, item, scalar - toDoubleLikeJava(item)));
    }
    return Datum::list(std::move(result));
}

Datum divideList(const Datum::List& list, const Datum& divisor) {
    const double scalar = toDoubleLikeJava(divisor);
    const bool divisor_is_float = isFloatLike(divisor);
    const int int_divisor = divisor_is_float ? 0 : toIntLikeJava(divisor);
    std::vector<Datum> result;
    result.reserve(list.items().size());
    for (const auto& item : list.items()) {
        if (isFloatLike(item) || divisor_is_float) {
            result.emplace_back(Datum::of(toDoubleLikeJava(item) / scalar));
        } else {
            result.emplace_back(Datum::of(toIntLikeJava(item) / int_divisor));
        }
    }
    return Datum::list(std::move(result));
}

Datum modList(const Datum::List& list, int divisor) {
    std::vector<Datum> result;
    result.reserve(list.items().size());
    for (const auto& item : list.items()) {
        result.emplace_back(Datum::of(toIntLikeJava(item) % divisor));
    }
    return Datum::list(std::move(result));
}

bool lingoEquals(const Datum& a, const Datum& b) {
    if ((a.isVoid() && b.isNumber()) || (a.isNumber() && b.isVoid()) || (a.isNumber() && b.isNumber())) {
        return toDoubleLikeJava(a) == toDoubleLikeJava(b);
    }
    if (a.isNumber()) {
        if (const auto string_value = directStringViewLikeJava(b)) {
            if (const auto numeric_value = parseDoubleStrict(*string_value)) {
                return toDoubleLikeJava(a) == *numeric_value;
            }
        }
    }
    if (b.isNumber()) {
        if (const auto string_value = directStringViewLikeJava(a)) {
            if (const auto numeric_value = parseDoubleStrict(*string_value)) {
                return toDoubleLikeJava(b) == *numeric_value;
            }
        }
    }
    if ((a.isString() || a.isSymbol()) && (b.isString() || b.isSymbol())) {
        const auto lhs = directStringViewLikeJava(a);
        const auto rhs = directStringViewLikeJava(b);
        return lhs.has_value() && rhs.has_value() &&
               (equalsIgnoreCase(*lhs, *rhs) ||
                ((equalsIgnoreCase(*lhs, "field") && equalsIgnoreCase(*rhs, "text")) ||
                 (equalsIgnoreCase(*lhs, "text") && equalsIgnoreCase(*rhs, "field"))));
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
    storage.clear();
    return storage;
}

std::string keyNameLikeJava(const Datum& datum) {
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
    if (const auto* timeout = datum.asTimeoutRef()) {
        return timeout->name;
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

Datum getPropListProp(const Datum::PropList& prop_list, std::string_view prop_name) {
    if (equalsIgnoreCase(prop_name, "count") || equalsIgnoreCase(prop_name, "length")) {
        return Datum::of(prop_list.count());
    }
    const int index = prop_list.findUntypedKeyName(prop_name);
    if (index >= 0) {
        return prop_list.properties()[static_cast<std::size_t>(index)].second;
    }
    if (equalsIgnoreCase(prop_name, "ilk")) {
        return Datum::symbol("propList");
    }
    return Datum::voidValue();
}

Datum getPropListKey(const Datum::PropList& prop_list, std::string_view key_name) {
    const int index = prop_list.findUntypedKeyName(key_name);
    return index >= 0 ? prop_list.properties()[static_cast<std::size_t>(index)].second : Datum::voidValue();
}

bool isCallMessageStruct(const Datum::PropList& prop_list) {
    if (getPropListKey(prop_list, "connection").type() != DatumType::ScriptInstanceRef) {
        return false;
    }

    const Datum ilk = getPropListKey(prop_list, "ilk");
    if (const auto* symbol = ilk.asSymbol(); symbol != nullptr && equalsIgnoreCase(symbol->name, "struct")) {
        return true;
    }

    return !getPropListKey(prop_list, "subject").isVoid() &&
           !getPropListKey(prop_list, "content").isVoid();
}

Datum snapshotCallArg(const Datum& arg) {
    if (!arg.isPropList() || !isCallMessageStruct(arg.propListValue())) {
        return arg;
    }
    return arg.deepCopy();
}

std::vector<Datum> snapshotCallArgsFromStack(ExecutionContext& context, int arg_count) {
    std::vector<Datum> call_args;
    if (arg_count <= 2) {
        return call_args;
    }
    call_args.reserve(static_cast<std::size_t>(arg_count - 2));
    for (int index = 2; index < arg_count; ++index) {
        call_args.emplace_back(snapshotCallArg(context.peekRef(arg_count - 1 - index)));
    }
    return call_args;
}

void putPropListProp(Datum::PropList& prop_list, std::string_view prop_name, Datum value) {
    const int index = prop_list.findUntypedKeyName(prop_name);
    if (index >= 0) {
        prop_list.properties()[static_cast<std::size_t>(index)].second = std::move(value);
        return;
    }
    prop_list.appendProperty(Datum::symbol(std::string(prop_name)), std::move(value));
}

std::vector<std::string> splitLines(std::string_view value);
int countLines(std::string_view value);

Datum getStringProp(std::string_view value, std::string_view prop_name) {
    if (equalsIgnoreCase(prop_name, "length")) {
        return Datum::of(static_cast<int>(value.size()));
    }
    if (equalsIgnoreCase(prop_name, "linecount")) {
        return Datum::of(countLines(value));
    }
    if (equalsIgnoreCase(prop_name, "line")) {
        auto chunks = splitLines(value);
        std::vector<Datum> lines;
        lines.reserve(chunks.size());
        for (auto& line : chunks) {
            lines.emplace_back(Datum::of(std::move(line)));
        }
        return Datum::list(std::move(lines));
    }
    if (equalsIgnoreCase(prop_name, "ilk")) {
        return Datum::symbol("string");
    }
    return Datum::voidValue();
}

Datum getListProp(const Datum::List& list, std::string_view prop_name) {
    if (equalsIgnoreCase(prop_name, "count") || equalsIgnoreCase(prop_name, "length")) {
        return Datum::of(list.count());
    }
    if (equalsIgnoreCase(prop_name, "ilk")) {
        return Datum::symbol("list");
    }
    if (const auto index = parseIntStrict(prop_name)) {
        if (*index >= 1 && *index <= list.count()) {
            return list.getAt(*index);
        }
    }
    return Datum::voidValue();
}

Datum getPointProp(const Datum::IntPoint& point, std::string_view prop_name) {
    if (equalsIgnoreCase(prop_name, "loch") || equalsIgnoreCase(prop_name, "x")) return Datum::of(point.x);
    if (equalsIgnoreCase(prop_name, "locv") || equalsIgnoreCase(prop_name, "y")) return Datum::of(point.y);
    if (equalsIgnoreCase(prop_name, "ilk")) return Datum::symbol("point");
    return Datum::voidValue();
}

void setPointProp(Datum::IntPoint& point, std::string_view prop_name, const Datum& value) {
    const int new_value = toIntLikeJava(value);
    if (equalsIgnoreCase(prop_name, "loch") || equalsIgnoreCase(prop_name, "x")) {
        point.x = new_value;
    } else if (equalsIgnoreCase(prop_name, "locv") || equalsIgnoreCase(prop_name, "y")) {
        point.y = new_value;
    }
}

Datum getRectProp(const Datum::IntRect& rect, std::string_view prop_name) {
    if (equalsIgnoreCase(prop_name, "left")) return Datum::of(rect.left);
    if (equalsIgnoreCase(prop_name, "top")) return Datum::of(rect.top);
    if (equalsIgnoreCase(prop_name, "right")) return Datum::of(rect.right);
    if (equalsIgnoreCase(prop_name, "bottom")) return Datum::of(rect.bottom);
    if (equalsIgnoreCase(prop_name, "width")) return Datum::of(rect.width());
    if (equalsIgnoreCase(prop_name, "height")) return Datum::of(rect.height());
    if (equalsIgnoreCase(prop_name, "ilk")) return Datum::symbol("rect");
    return Datum::voidValue();
}

void setRectProp(Datum::IntRect& rect, std::string_view prop_name, const Datum& value) {
    const int new_value = toIntLikeJava(value);
    if (equalsIgnoreCase(prop_name, "left")) {
        rect.left = new_value;
    } else if (equalsIgnoreCase(prop_name, "top")) {
        rect.top = new_value;
    } else if (equalsIgnoreCase(prop_name, "right")) {
        rect.right = new_value;
    } else if (equalsIgnoreCase(prop_name, "bottom")) {
        rect.bottom = new_value;
    }
}

Datum getColorProp(const Datum::ColorRef& color, std::string_view prop_name) {
    if (color.palette_index.has_value()) {
        if (equalsIgnoreCase(prop_name, "paletteindex")) return Datum::of(*color.palette_index);
        if (equalsIgnoreCase(prop_name, "ilk")) return Datum::symbol("color");
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(prop_name, "red")) return Datum::of(color.r);
    if (equalsIgnoreCase(prop_name, "green")) return Datum::of(color.g);
    if (equalsIgnoreCase(prop_name, "blue")) return Datum::of(color.b);
    if (equalsIgnoreCase(prop_name, "ilk")) return Datum::symbol("color");
    return Datum::voidValue();
}

Datum getImageProp(const Datum::ImageRef& image, std::string_view prop_name) {
    const auto& bitmap = image.bitmap;
    if (equalsIgnoreCase(prop_name, "rect")) {
        return bitmap != nullptr ? Datum::intRect(0, 0, bitmap->width(), bitmap->height()) : Datum::intRect(0, 0, 0, 0);
    }
    if (equalsIgnoreCase(prop_name, "width")) return Datum::of(bitmap != nullptr ? bitmap->width() : 0);
    if (equalsIgnoreCase(prop_name, "height")) return Datum::of(bitmap != nullptr ? bitmap->height() : 0);
    if (equalsIgnoreCase(prop_name, "depth")) return Datum::of(bitmap != nullptr ? bitmap->bitDepth() : 0);
    if (equalsIgnoreCase(prop_name, "usealpha")) return bitmap != nullptr && bitmap->isNativeAlpha() ? Datum::TRUE : Datum::FALSE;
    if (equalsIgnoreCase(prop_name, "ilk")) return Datum::symbol("image");
    if (equalsIgnoreCase(prop_name, "image")) return Datum::imageRef(bitmap, image.mutation_callback);
    if (equalsIgnoreCase(prop_name, "paletteref") && bitmap != nullptr) {
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
    if (image_mutation_callback) {
        image_mutation_callback();
    }
}

void notifyImageMutation(const Datum::ImageRef& image) {
    if (image.bitmap == nullptr) {
        return;
    }
    notifyImageMutation(*image.bitmap);
    if (image.mutation_callback) {
        image.mutation_callback(*image.bitmap);
    }
}

bool applyImagePaletteProperty(bitmap::Bitmap& bmp, const Datum& value, builtin::BuiltinContext* builtin_context) {
    bool resolved = false;
    if (value.isString() || value.isSymbol()) {
        std::string name_storage;
        const std::string_view name = stringViewLikeJava(value, name_storage);
        const auto normalized_name = bitmap::Palette::normalizeBuiltInSymbolName(name);
        const bitmap::Palette* palette = bitmap::Palette::builtInBySymbolName(name);
        if (palette != nullptr && normalized_name) {
            if (bmp.bitDepth() <= 8) {
                (void)bmp.remapImagePalette(palette);
            } else {
                bmp.setImagePalette(palette);
            }
            bmp.setPaletteRefSystemName(*normalized_name);
            resolved = true;
        }
    }

    if (!resolved && builtin_context != nullptr && builtin_context->image_palette_resolver) {
        if (auto palette = builtin_context->image_palette_resolver(value); palette && palette->palette) {
            if (bmp.bitDepth() <= 8) {
                (void)bmp.remapImagePalette(palette->palette);
            } else {
                bmp.setImagePalette(palette->palette);
            }
            if (palette->member_ref) {
                bmp.setPaletteRefCastMember(palette->member_ref->cast_lib, palette->member_ref->memberNum());
            } else if (palette->system_name) {
                bmp.setPaletteRefSystemName(*palette->system_name);
            }
            resolved = true;
        }
    }

    return resolved;
}

void setImageProp(builtin::BuiltinContext* builtin_context, const Datum::ImageRef& image, std::string_view prop_name, const Datum& value) {
    if (image.bitmap == nullptr) {
        return;
    }
    auto& bitmap = *image.bitmap;
    if (equalsIgnoreCase(prop_name, "usealpha")) {
        bitmap.setNativeAlpha(truthy(value));
        notifyImageMutation(image);
        return;
    }
    if (equalsIgnoreCase(prop_name, "paletteref")) {
        if (applyImagePaletteProperty(bitmap, value, builtin_context)) {
            notifyImageMutation(image);
        }
    }
}

Datum getCastMemberProp(ExecutionContext& context, const Datum::CastMemberRef& member, std::string_view prop_name) {
    auto* builtin_context = context.builtinContext();
    auto invalid_ref = [&]() {
        return member.cast_member <= 0 ||
               (builtin_context != nullptr && builtin_context->cast_member_exists_resolver &&
                !builtin_context->cast_member_exists_resolver(member.cast_lib, member.memberNum()));
    };
    if (equalsIgnoreCase(prop_name, "number")) {
        return invalid_ref() ? Datum::of(0) : Datum::of((member.cast_lib << 16) | (member.cast_member & 0xFFFF));
    }
    if (equalsIgnoreCase(prop_name, "membernum")) {
        return invalid_ref() ? Datum::of(0) : Datum::of(member.memberNum());
    }
    if (equalsIgnoreCase(prop_name, "castlibnum")) {
        return Datum::of(member.cast_lib);
    }
    if (equalsIgnoreCase(prop_name, "castlib")) {
        return member.cast_lib >= 1 ? Datum::castLibRef(id::CastLibId(member.cast_lib)) : Datum::voidValue();
    }
    if (builtin_context != nullptr && builtin_context->cast_member_property_getter) {
        return builtin_context->cast_member_property_getter(member.cast_lib, member.memberNum(), std::string(prop_name));
    }
    return Datum::voidValue();
}

Datum getCastLibMemberAccessorValue(ExecutionContext& context,
                                    const Datum::CastLibMemberAccessor& accessor,
                                    const Datum& key_or_index) {
    auto* builtin_context = context.builtinContext();
    if (builtin_context == nullptr) {
        return Datum::voidValue();
    }
    if (key_or_index.isInt() || key_or_index.isFloat()) {
        return builtin_context->cast_member_resolver ? builtin_context->cast_member_resolver(accessor.cast_lib,
                                                                                       toIntLikeJava(key_or_index))
                                                  : Datum::voidValue();
    }
    return builtin_context->cast_member_name_resolver
               ? builtin_context->cast_member_name_resolver(accessor.cast_lib, toStringLikeJava(key_or_index))
               : Datum::voidValue();
}

Datum getObjectProperty(ExecutionContext& context, const Datum& object, std::string_view prop_name);

Datum getChainedObjectProperty(ExecutionContext& context, const Datum& object, std::string_view prop_name) {
    auto* builtin_context = context.builtinContext();
    if (object.type() == DatumType::ScriptInstanceRef) {
        if (couldBeStrictInteger(prop_name) && parseIntStrictView(prop_name).has_value()) {
            return Datum::voidValue();
        }
        return util::getProperty(object.scriptInstanceValue(), prop_name);
    }
    if (object.isList()) {
        const auto numeric_index = parseIntStrictView(prop_name);
        if (numeric_index.has_value() && *numeric_index >= 1 && *numeric_index <= object.listValue().count()) {
            return object.listValue().getAt(*numeric_index);
        }
        return numeric_index.has_value() ? Datum::voidValue() : getListProp(object.listValue(), prop_name);
    }
    if (object.isPropList()) {
        return getPropListProp(object.propListValue(), prop_name);
    }
    if (const auto* str = object.asString()) {
        return prop_name == "length" ? Datum::of(static_cast<int>(str->value.size())) : Datum::voidValue();
    }
    if (object.asFieldText() != nullptr) {
        return Datum::voidValue();
    }
    if (const auto* point = object.asIntPoint()) {
        return equalsIgnoreCase(prop_name, "ilk") ? Datum::voidValue() : getPointProp(*point, prop_name);
    }
    if (const auto* rect = object.asIntRect()) {
        return equalsIgnoreCase(prop_name, "ilk") ? Datum::voidValue() : getRectProp(*rect, prop_name);
    }
    if (const auto* color = object.asColorRef()) {
        if (color->palette_index.has_value()) {
            return Datum::voidValue();
        }
        return equalsIgnoreCase(prop_name, "ilk") ? Datum::voidValue() : getColorProp(*color, prop_name);
    }
    if (const auto* accessor = object.asCastLibMemberAccessor()) {
        const auto numeric_index = parseIntStrictView(prop_name);
        return numeric_index.has_value()
                   ? getCastLibMemberAccessorValue(context, *accessor, Datum::of(*numeric_index))
                   : getCastLibMemberAccessorValue(context, *accessor, Datum::of(std::string(prop_name)));
    }
    if (const auto* sprite = object.asSpriteRef()) {
        return builtin_context != nullptr && builtin_context->sprite_properties != nullptr
                   ? builtin_context->sprite_properties->getSpriteProp(sprite->channel, prop_name)
                   : Datum::voidValue();
    }
    if (const auto* value = object.asInt(); value != nullptr) {
        if (!equalsIgnoreCase(prop_name, "ilk") && builtin_context != nullptr && builtin_context->sprite_properties != nullptr) {
            return builtin_context->sprite_properties->getSpriteProp(value->value, prop_name);
        }
        return equalsIgnoreCase(prop_name, "ilk") ? Datum::symbol(ilkTypeName(object)) : Datum::voidValue();
    }
    return getObjectProperty(context, object, prop_name);
}

Datum getObjectProperty(ExecutionContext& context, const Datum& object, std::string_view prop_name) {
    if (object.isVoid()) {
        return Datum::voidValue();
    }
    auto* builtin_context = context.builtinContext();
    if (object.type() == DatumType::MovieRef) {
        return builtin_context != nullptr && builtin_context->movie_properties != nullptr
                   ? builtin_context->movie_properties->getMovieProp(prop_name)
                   : Datum::voidValue();
    }
    if (object.type() == DatumType::PlayerRef) {
        if (const auto constant = builtinConstant(prop_name)) {
            return *constant;
        }
        return builtin_context != nullptr && builtin_context->movie_properties != nullptr
                   ? builtin_context->movie_properties->getMovieProp(prop_name)
                   : Datum::voidValue();
    }
    if (object.type() == DatumType::StageRef) {
        return builtin_context != nullptr && builtin_context->movie_properties != nullptr
                   ? builtin_context->movie_properties->getStageProp(prop_name)
                   : Datum::voidValue();
    }
    if (const auto* cast_lib = object.asCastLibRef()) {
        if (equalsIgnoreCase(prop_name, "member")) {
            return Datum::castLibMemberAccessor(id::CastLibId(cast_lib->cast_lib));
        }
        return builtin_context != nullptr && builtin_context->cast_lib_property_getter
                   ? builtin_context->cast_lib_property_getter(cast_lib->cast_lib, std::string(prop_name))
                   : Datum::voidValue();
    }
    if (const auto* accessor = object.asCastLibMemberAccessor()) {
        return getCastLibMemberAccessorValue(context, *accessor, Datum::of(std::string(prop_name)));
    }
    if (const auto* sprite = object.asSpriteRef()) {
        return builtin_context != nullptr && builtin_context->sprite_properties != nullptr
                   ? builtin_context->sprite_properties->getSpriteProp(sprite->channel, prop_name)
                   : Datum::voidValue();
    }
    if (const auto* value = object.asInt(); value != nullptr && !equalsIgnoreCase(prop_name, "ilk")) {
        return builtin_context != nullptr && builtin_context->sprite_properties != nullptr
                   ? builtin_context->sprite_properties->getSpriteProp(value->value, prop_name)
                   : Datum::voidValue();
    }
    if (const auto* timeout = object.asTimeoutRef()) {
        return builtin_context != nullptr ? builtin::TimeoutBuiltins::getProperty(*builtin_context, *timeout, prop_name)
                                         : Datum::voidValue();
    }
    if (object.asSoundRef() != nullptr) {
        if (equalsIgnoreCase(prop_name, "ilk")) {
            return Datum::symbol("sound");
        }
        const bool is_sound_property = equalsIgnoreCase(prop_name, "soundEnabled") ||
                                     equalsIgnoreCase(prop_name, "soundLevel") ||
                                     equalsIgnoreCase(prop_name, "soundKeepDevice") ||
                                     equalsIgnoreCase(prop_name, "soundMixMedia");
        if (!is_sound_property) {
            return Datum::voidValue();
        }
        if (builtin_context != nullptr && builtin_context->movie_properties != nullptr) {
            return builtin_context->movie_properties->getMovieProp(prop_name);
        }
        if (builtin_context != nullptr && builtin_context->sound_manager != nullptr) {
            if (equalsIgnoreCase(prop_name, "soundEnabled")) {
                return builtin_context->sound_manager->isEnabled() ? Datum::TRUE : Datum::FALSE;
            }
            if (equalsIgnoreCase(prop_name, "soundLevel")) {
                return Datum::of(builtin_context->sound_manager->getSoundLevel());
            }
            if (equalsIgnoreCase(prop_name, "soundKeepDevice")) {
                return builtin_context->sound_manager->soundKeepDevice() ? Datum::TRUE : Datum::FALSE;
            }
            return builtin_context->sound_manager->soundMixMedia() ? Datum::TRUE : Datum::FALSE;
        }
        if (equalsIgnoreCase(prop_name, "soundLevel")) {
            return Datum::of(7);
        }
        if (equalsIgnoreCase(prop_name, "soundEnabled") ||
            equalsIgnoreCase(prop_name, "soundKeepDevice") ||
            equalsIgnoreCase(prop_name, "soundMixMedia")) {
            return Datum::TRUE;
        }
        return Datum::voidValue();
    }
    if (const auto* sound_channel = object.asSoundChannel()) {
        return dispatch::SoundChannelMethodDispatcher::getProperty(builtin_context, *sound_channel, prop_name);
    }
    if (const auto* xtra_instance = object.asXtraInstance()) {
        return builtin_context != nullptr ? builtin::XtraBuiltins::getProperty(*builtin_context, *xtra_instance, prop_name)
                                         : Datum::voidValue();
    }
    if (object.type() == DatumType::ScriptInstanceRef) {
        if (equalsIgnoreCase(prop_name, "ilk")) {
            return Datum::symbol("instance");
        }
        return util::getProperty(object.scriptInstanceValue(), prop_name);
    }
    if (object.isPropList()) {
        return getPropListProp(object.propListValue(), prop_name);
    }
    if (object.isList()) {
        return getListProp(object.listValue(), prop_name);
    }
    if (object.isString()) {
        std::string storage;
        return getStringProp(stringViewLikeJava(object, storage), prop_name);
    }
    if (const auto* point = object.asIntPoint()) {
        return getPointProp(*point, prop_name);
    }
    if (const auto* rect = object.asIntRect()) {
        return getRectProp(*rect, prop_name);
    }
    if (const auto* color = object.asColorRef()) {
        return getColorProp(*color, prop_name);
    }
    if (const auto* image = object.asImageRef()) {
        return dispatch::ImageMethodDispatcher::getProperty(*image, prop_name);
    }
    if (const auto* member = object.asCastMemberRef()) {
        return getCastMemberProp(context, *member, prop_name);
    }
    if (equalsIgnoreCase(prop_name, "ilk")) {
        return Datum::symbol(ilkTypeName(object));
    }
    return Datum::voidValue();
}

void setObjectProperty(ExecutionContext& context, Datum& object, std::string_view prop_name, Datum value) {
    auto* builtin_context = context.builtinContext();
    if (object.type() == DatumType::MovieRef || object.type() == DatumType::PlayerRef) {
        if (builtin_context != nullptr && builtin_context->movie_properties != nullptr) {
            (void)builtin_context->movie_properties->setMovieProp(prop_name, value);
        }
        return;
    }
    if (object.type() == DatumType::StageRef) {
        if (builtin_context != nullptr && builtin_context->movie_properties != nullptr) {
            (void)builtin_context->movie_properties->setStageProp(prop_name, value);
        }
        return;
    }
    if (const auto* cast_lib = object.asCastLibRef()) {
        if (builtin_context != nullptr && builtin_context->cast_lib_property_setter) {
            (void)builtin_context->cast_lib_property_setter(cast_lib->cast_lib, std::string(prop_name), value);
        }
        return;
    }
    if (const auto* sprite = object.asSpriteRef()) {
        if (builtin_context != nullptr && builtin_context->sprite_properties != nullptr) {
            (void)builtin_context->sprite_properties->setSpriteProp(sprite->channel, prop_name, value);
        }
        return;
    }
    if (const auto* int_value = object.asInt()) {
        if (builtin_context != nullptr && builtin_context->sprite_properties != nullptr) {
            (void)builtin_context->sprite_properties->setSpriteProp(int_value->value, prop_name, value);
        }
        return;
    }
    if (const auto* member = object.asCastMemberRef()) {
        if (builtin_context != nullptr && builtin_context->cast_member_property_setter) {
            (void)builtin_context->cast_member_property_setter(member->cast_lib, member->memberNum(), std::string(prop_name), value);
        }
        return;
    }
    if (const auto* timeout = object.asTimeoutRef()) {
        if (builtin_context != nullptr) {
            (void)builtin::TimeoutBuiltins::setProperty(*builtin_context, *timeout, prop_name, std::move(value));
        }
        return;
    }
    if (object.asSoundRef() != nullptr) {
        const bool is_sound_property = equalsIgnoreCase(prop_name, "soundEnabled") ||
                                     equalsIgnoreCase(prop_name, "soundLevel") ||
                                     equalsIgnoreCase(prop_name, "soundKeepDevice") ||
                                     equalsIgnoreCase(prop_name, "soundMixMedia");
        if (is_sound_property && builtin_context != nullptr && builtin_context->movie_properties != nullptr) {
            (void)builtin_context->movie_properties->setMovieProp(prop_name, value);
        } else if (is_sound_property && builtin_context != nullptr && builtin_context->sound_manager != nullptr) {
            if (equalsIgnoreCase(prop_name, "soundEnabled")) {
                builtin_context->sound_manager->setEnabled(value.boolValue());
            } else if (equalsIgnoreCase(prop_name, "soundLevel")) {
                builtin_context->sound_manager->setSoundLevel(value.intValue());
            } else if (equalsIgnoreCase(prop_name, "soundKeepDevice")) {
                builtin_context->sound_manager->setSoundKeepDevice(value.boolValue());
            } else {
                builtin_context->sound_manager->setSoundMixMedia(value.boolValue());
            }
        }
        return;
    }
    if (const auto* sound_channel = object.asSoundChannel()) {
        (void)dispatch::SoundChannelMethodDispatcher::setProperty(builtin_context, *sound_channel, prop_name, std::move(value));
        return;
    }
    if (const auto* xtra_instance = object.asXtraInstance()) {
        if (builtin_context != nullptr) {
            builtin::XtraBuiltins::setProperty(*builtin_context, *xtra_instance, prop_name, value);
        }
        return;
    }
    if (object.type() == DatumType::ScriptInstanceRef) {
        if (!context.hasVariableSetListener()) {
            util::setProperty(object.scriptInstanceValue(), prop_name, std::move(value));
            return;
        }
        const Datum traced_value = value;
        util::setProperty(object.scriptInstanceValue(), prop_name, std::move(value));
        context.tracePropertySet(prop_name, traced_value);
        return;
    }
    if (object.isPropList()) {
        putPropListProp(object.propListValue(), prop_name, std::move(value));
        return;
    }
    if (auto* point = object.asIntPoint()) {
        setPointProp(*point, prop_name, value);
        return;
    }
    if (auto* rect = object.asIntRect()) {
        setRectProp(*rect, prop_name, value);
        return;
    }
    if (const auto* image = object.asImageRef()) {
        dispatch::ImageMethodDispatcher::setProperty(context.builtinContext(), *image, prop_name, value);
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
        location.script_owner,
        location.file_owner,
        location.script_names_owner,
        location.script_type
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

std::vector<std::string> splitChunks(std::string_view value, StringChunkType type, char item_delimiter = ',') {
    return util::splitIntoChunks(value, type, item_delimiter);
}

std::string chunkDelimiter(StringChunkType type, char item_delimiter = ',') {
    return util::chunkDelimiter(type, item_delimiter);
}

std::string sourceChunkDelimiter(std::string_view value, StringChunkType type, char item_delimiter = ',') {
    return type == StringChunkType::Line ? pickLineDelimiter(value) : chunkDelimiter(type, item_delimiter);
}

int countChunks(std::string_view value, StringChunkType type, char item_delimiter = ',') {
    return util::countChunks(value, type, item_delimiter);
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
    appendInt(key, field.cast_lib);
    key.push_back(':');
    appendInt(key, field.member_num);
    key.push_back(':');
    appendInt(key, field.revision);
    return key;
}

bool lineIndexMatches(std::string_view value, const util::LineIndex& index) {
    return index.source_size == value.size() &&
           index.source_hash == std::hash<std::string_view>{}(value);
}

const util::LineIndex* cachedFieldLineIndex(const Datum& target, builtin::BuiltinContext* builtin_context) {
    const auto* field = target.asFieldText();
    if (field == nullptr || field->revision == 0 || builtin_context == nullptr) {
        return nullptr;
    }

    if (builtin_context->field_line_index_cache.size() > 128) {
        builtin_context->field_line_index_cache.clear();
    }

    const std::string key = fieldLineIndexCacheKey(*field);
    auto found = builtin_context->field_line_index_cache.find(key);
    if (found == builtin_context->field_line_index_cache.end()) {
        found = builtin_context->field_line_index_cache
            .emplace(key, util::buildLineIndex(field->value))
            .first;
    } else if (!lineIndexMatches(field->value, found->second)) {
        found->second = util::buildLineIndex(field->value);
    }
    return &found->second;
}

std::string getChunkValue(std::string_view value, StringChunkType type, int index, char item_delimiter = ',') {
    return util::getChunk(value, type, index, item_delimiter);
}

std::string getChunkRangeValue(std::string_view value,
                               StringChunkType type,
                               int start,
                               int end,
                               char item_delimiter = ',') {
    return util::getChunkRange(value, type, start, end, item_delimiter);
}

std::string resolveChunkRange(std::string_view value,
                              StringChunkType type,
                              int first,
                              int last,
                              char item_delimiter = ',') {
    if (first == 0 && last == 0) {
        return std::string(value);
    }
    if (first < 0 || last < 0) {
        const int count = countChunks(value, type, item_delimiter);
        if (first < 0) {
            first = count;
        }
        if (last < 0) {
            last = count;
        }
    }

    const int effective_last = last == 0 ? first : last;
    return first == effective_last
        ? util::getChunk(value, type, first, item_delimiter)
        : util::getChunkRange(value, type, first, effective_last, item_delimiter);
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
    if (auto* builtin_context = context.builtinContext(); builtin_context != nullptr && builtin_context->movie_properties != nullptr) {
        return builtin_context->movie_properties->getItemDelimiter();
    }
    return ',';
}

std::string getLastChunkValue(std::string_view value, StringChunkType type, char item_delimiter = ',') {
    return util::getLastChunk(value, type, item_delimiter);
}

std::optional<std::pair<int, int>> itemByteRange(std::string_view value, int first, int last, char delimiter) {
    int chunk_num = 1;
    int chunk_start = 0;
    int range_start = -1;
    for (int index = 0; index <= static_cast<int>(value.size()); ++index) {
        if (index == static_cast<int>(value.size()) || value[static_cast<std::size_t>(index)] == delimiter) {
            if (chunk_num == first) {
                range_start = chunk_start;
            }
            if (chunk_num == last) {
                if (range_start < 0) {
                    return std::nullopt;
                }
                return std::pair{range_start, index};
            }
            if (chunk_num > last) {
                break;
            }
            ++chunk_num;
            chunk_start = index + 1;
        }
    }
    if (range_start >= 0) {
        return std::pair{range_start, static_cast<int>(value.size())};
    }
    return std::nullopt;
}

std::optional<std::pair<int, int>> chunkByteRange(std::string_view value,
                                                  StringChunkType type,
                                                  int first,
                                                  int last,
                                                  char item_delimiter = ',') {
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
        return itemByteRange(value, first, last, item_delimiter);
    }

    const auto chunks = splitChunks(value, type, item_delimiter);
    if (chunks.empty() || first > static_cast<int>(chunks.size())) {
        return std::nullopt;
    }
    last = std::min(last, static_cast<int>(chunks.size()));
    const std::string delimiter = sourceChunkDelimiter(value, type, item_delimiter);

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
                             char item_delimiter = ',') {
    auto range = chunkByteRange(value, type, first, last, item_delimiter);
    if (!range) {
        return std::string(value);
    }

    auto [delete_start, delete_end] = *range;
    if (type != StringChunkType::Char) {
        const std::string delimiter = sourceChunkDelimiter(value, type, item_delimiter);
        if (delete_end < static_cast<int>(value.size()) && startsWithAt(value, delimiter, delete_end)) {
            delete_end += static_cast<int>(delimiter.size());
        } else if (delete_start > 0 && prefixEndsWith(value, delete_start, delimiter)) {
            delete_start -= static_cast<int>(delimiter.size());
        }
    }

    if (delete_start == 0) {
        return std::string(value.substr(static_cast<std::size_t>(delete_end)));
    }
    if (delete_end >= static_cast<int>(value.size())) {
        return std::string(value.substr(0, static_cast<std::size_t>(delete_start)));
    }
    std::string result;
    result.reserve(value.size() - static_cast<std::size_t>(delete_end - delete_start));
    result.append(value.substr(0, static_cast<std::size_t>(delete_start)));
    result.append(value.substr(static_cast<std::size_t>(delete_end)));
    return result;
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
    std::string result;
    result.reserve(value.size() - static_cast<std::size_t>(end - start));
    result.append(value.substr(0, static_cast<std::size_t>(start)));
    result.append(value.substr(static_cast<std::size_t>(end)));
    return result;
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
    const int normalized_first = normalizeCharChunkIndex(first, length, true);
    const int normalized_last = normalizeCharChunkIndex(last == 0 ? first : last, length, true);
    if (normalized_first < 1 || normalized_first > length || normalized_last < normalized_first) {
        return std::nullopt;
    }
    const int range_start = normalized_first - 1;
    const int range_end = std::min(normalized_last, length);
    if (range_end < range_start) {
        return std::nullopt;
    }
    return std::pair{range_start, range_end};
}

int chunkInsertionIndex(std::string_view value,
                        StringChunkType type,
                        int target_index,
                        bool before,
                        char item_delimiter = ',') {
    const int length = static_cast<int>(value.size());
    if (type == StringChunkType::Char) {
        const int normalized = normalizeCharChunkIndex(target_index, length, before);
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

    if (const auto range = chunkByteRange(value, type, target_index, target_index, item_delimiter)) {
        return before ? range->first : range->second;
    }
    if (before) {
        return target_index <= 1 ? 0 : length;
    }
    return target_index <= 0 ? 0 : length;
}

std::string putIntoChunkValue(std::string_view value,
                              StringChunkType type,
                              int first,
                              int last,
                              std::string_view replacement,
                              char item_delimiter = ',') {
    auto range = chunkByteRange(value, type, first, last, item_delimiter);
    if (!range) {
        return std::string(value);
    }
    std::string result;
    result.reserve(value.size() - static_cast<std::size_t>(range->second - range->first) + replacement.size());
    result.append(value.substr(0, static_cast<std::size_t>(range->first)));
    result.append(replacement);
    result.append(value.substr(static_cast<std::size_t>(range->second)));
    return result;
}

std::string putBeforeChunkValue(std::string_view value,
                                StringChunkType type,
                                int first,
                                std::string_view inserted,
                                char item_delimiter = ',') {
    const int insert_at = chunkInsertionIndex(value, type, first, true, item_delimiter);
    std::string result;
    result.reserve(value.size() + inserted.size());
    result.append(value.substr(0, static_cast<std::size_t>(insert_at)));
    result.append(inserted);
    result.append(value.substr(static_cast<std::size_t>(insert_at)));
    return result;
}

std::string putAfterChunkValue(std::string_view value,
                               StringChunkType type,
                               int target_index,
                               std::string_view inserted,
                               char item_delimiter = ',') {
    const int insert_at = chunkInsertionIndex(value, type, target_index, false, item_delimiter);
    std::string result;
    result.reserve(value.size() + inserted.size());
    result.append(value.substr(0, static_cast<std::size_t>(insert_at)));
    result.append(inserted);
    result.append(value.substr(static_cast<std::size_t>(insert_at)));
    return result;
}

struct ChunkSpec {
    StringChunkType type;
    int first;
    int last;
};

ChunkSpec chooseSingleChunkSpec(int first_char,
                                int last_char,
                                int first_word,
                                int last_word,
                                int first_item,
                                int last_item,
                                int first_line,
                                int last_line) {
    if (first_line != 0 || last_line != 0) {
        return {StringChunkType::Line, first_line, last_line};
    }
    if (first_item != 0 || last_item != 0) {
        return {StringChunkType::Item, first_item, last_item};
    }
    if (first_word != 0 || last_word != 0) {
        return {StringChunkType::Word, first_word, last_word};
    }
    if (first_char != 0 || last_char != 0) {
        return {StringChunkType::Char, first_char, last_char};
    }
    return {StringChunkType::Char, 1, 1};
}

Datum pointObjectMethod(Datum::IntPoint& point, std::string_view method_name, std::span<const Datum> args) {
    if (equalsIgnoreCase(method_name, "getAt")) {
        if (args.empty()) return Datum::voidValue();
        const int index = toIntLikeJava(args[0]);
        if (index == 1) return Datum::of(point.x);
        if (index == 2) return Datum::of(point.y);
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "duplicate")) {
        return Datum::intPoint(point.x, point.y);
    }
    if (equalsIgnoreCase(method_name, "setAt")) {
        if (args.size() < 2) return Datum::voidValue();
        const int component = toIntLikeJava(args[0]);
        const int new_value = toIntLikeJava(args[1]);
        if (component == 1) {
            point.x = new_value;
        } else if (component == 2) {
            point.y = new_value;
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "inside")) {
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

Datum rectObjectMethod(Datum::IntRect& rect, std::string_view method_name, std::span<const Datum> args) {
    if (equalsIgnoreCase(method_name, "getAt")) {
        if (args.empty()) return Datum::voidValue();
        switch (toIntLikeJava(args[0])) {
            case 1: return Datum::of(rect.left);
            case 2: return Datum::of(rect.top);
            case 3: return Datum::of(rect.right);
            case 4: return Datum::of(rect.bottom);
            default: return Datum::voidValue();
        }
    }
    if (equalsIgnoreCase(method_name, "duplicate")) {
        return Datum::intRect(rect.left, rect.top, rect.right, rect.bottom);
    }
    if (equalsIgnoreCase(method_name, "setAt")) {
        if (args.size() < 2) return Datum::voidValue();
        const int component = toIntLikeJava(args[0]);
        const int new_value = toIntLikeJava(args[1]);
        switch (component) {
            case 1: rect.left = new_value; break;
            case 2: rect.top = new_value; break;
            case 3: rect.right = new_value; break;
            case 4: rect.bottom = new_value; break;
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

Datum scriptInstanceNestedProperty(const Datum& container, const Datum& sub_key) {
    if (container.isList()) {
        const int index = toIntLikeJava(sub_key);
        const auto& items = container.listValue().items();
        if (index >= 1 && index <= static_cast<int>(items.size())) {
            return items[static_cast<std::size_t>(index - 1)];
        }
        return Datum::voidValue();
    }
    if (container.isPropList()) {
        const auto& properties = container.propListValue().properties();
        if (sub_key.isInt() || sub_key.isFloat()) {
            const int index = toIntLikeJava(sub_key);
            if (index >= 1 && index <= static_cast<int>(properties.size())) {
                return properties[static_cast<std::size_t>(index - 1)].second;
            }
            return Datum::voidValue();
        }
        std::string key_name_storage;
        return getPropListKey(container.propListValue(), keyNameLikeJavaView(sub_key, key_name_storage));
    }
    return Datum::voidValue();
}

Datum scriptInstancePropertyValue(const Datum::ScriptInstanceRef& instance, std::string_view prop_name) {
    if (const auto* value = util::findPropertyValue(instance, prop_name)) {
        return *value;
    }
    if (equalsIgnoreCase(prop_name, "ancestor")) {
        return util::getProperty(instance, prop_name);
    }
    return Datum::voidValue();
}

Datum scriptInstancePropertyCountValue(const Datum::ScriptInstanceRef& instance, std::string_view prop_name) {
    if (const auto* value = util::findPropertyValue(instance, prop_name)) {
        return scriptInstanceCountValue(*value);
    }
    return Datum::of(0);
}

Datum scriptInstanceNestedPropertyValue(const Datum::ScriptInstanceRef& instance,
                                        std::string_view prop_name,
                                        const Datum& sub_key) {
    if (const auto* value = util::findPropertyValue(instance, prop_name)) {
        return scriptInstanceNestedProperty(*value, sub_key);
    }
    if (equalsIgnoreCase(prop_name, "ancestor")) {
        return scriptInstanceNestedProperty(util::getProperty(instance, prop_name), sub_key);
    }
    return Datum::voidValue();
}

void scriptInstanceSetNestedProperty(Datum& container, const Datum& sub_key, Datum value) {
    if (container.isList()) {
        const int index = toIntLikeJava(sub_key);
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
        if (sub_key.isInt() || sub_key.isFloat()) {
            const int index = toIntLikeJava(sub_key);
            if (index >= 1 && index <= static_cast<int>(properties.size())) {
                properties[static_cast<std::size_t>(index - 1)].second = std::move(value);
            }
            return;
        }
        container.propListValue().put(sub_key, std::move(value));
    }
}

void scriptInstancePutLocalProperty(Datum::ScriptInstanceRef& instance, std::string_view prop_name, Datum value) {
    if (prop_name == "ancestor") {
        instance.setProperty(std::string(prop_name), std::move(value));
        return;
    }
    instance.putLocalPropertyExactView(prop_name, std::move(value));
}

void scriptInstanceDeleteLocalProperty(Datum::ScriptInstanceRef& instance, std::string_view prop_name) {
    if (prop_name == "ancestor") {
        instance.setProperty(std::string(prop_name), Datum::voidValue());
        return;
    }
    (void)instance.eraseLocalPropertyExact(prop_name);
}

bool shouldDeferNumericCloseThread(ExecutionContext& context,
                                   const Datum& receiver,
                                   std::string_view method_name,
                                   std::span<const Datum> args) {
    if (!equalsIgnoreCase(method_name, "closeThread") || args.size() != 1 ||
        (!args.front().isInt() && !args.front().isFloat())) {
        return false;
    }
    auto* builtin_context = context.builtinContext();
    if (builtin_context == nullptr || !builtin_context->script_instance_method_deferrer) {
        return false;
    }
    const std::vector<Datum> arg_vector(args.begin(), args.end());
    return builtin_context->script_instance_method_deferrer(receiver, std::string(method_name), arg_vector);
}

std::optional<builtin::BuiltinContext::ScriptHandlerLocation> findScriptInstanceScriptHandler(
    ExecutionContext& context,
    Datum::ScriptInstanceRef& instance,
    std::string_view method_name) {
    auto* builtin_context = context.builtinContext();
    if (builtin_context == nullptr || !builtin_context->script_handler_finder) {
        return std::nullopt;
    }

    if (instance.ancestorRaw() == nullptr) {
        if (const auto& script_ref = instance.scriptRef(); script_ref.has_value()) {
            const int cast_lib = script_ref->cast_lib > 0 ? script_ref->cast_lib : 1;
            const int member_num = script_ref->memberNum();
            const auto script_key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cast_lib)) << 32U) |
                                   static_cast<std::uint32_t>(member_num);
            auto& script_cache = builtin_context->direct_script_instance_handler_cache[script_key];
            if (const auto cached = script_cache.find(method_name); cached != script_cache.end()) {
                return cached->second;
            }
            std::string method_name_string(method_name);
            if (auto handler = builtin_context->script_handler_finder(cast_lib, member_num, method_name_string);
                handler.has_value() && handler->script != nullptr) {
                script_cache.emplace(std::move(method_name_string), handler);
                return handler;
            }
            script_cache.emplace(std::move(method_name_string), std::nullopt);
        }
        return std::nullopt;
    }

    std::string cache_key;
    cache_key.reserve(static_cast<std::size_t>(util::MAX_ANCESTOR_DEPTH * 4) + method_name.size());
    const auto* current = &instance;
    for (int depth = 0; current != nullptr && depth < util::MAX_ANCESTOR_DEPTH; ++depth) {
        appendInt(cache_key, current->identityId());
        cache_key.push_back('/');
        current = current->ancestorRaw();
    }
    appendLowerAscii(cache_key, method_name);
    if (const auto cached = builtin_context->script_instance_handler_cache.find(cache_key);
        cached != builtin_context->script_instance_handler_cache.end()) {
        return cached->second;
    }

    const std::string method_name_string(method_name);
    current = &instance;
    for (int depth = 0; current != nullptr && depth < util::MAX_ANCESTOR_DEPTH; ++depth) {
        if (const auto& script_ref = current->scriptRef(); script_ref.has_value()) {
            const int cast_lib = script_ref->cast_lib > 0 ? script_ref->cast_lib : 1;
            const int member_num = script_ref->memberNum();
            if (auto handler = builtin_context->script_handler_finder(cast_lib, member_num, method_name_string);
                handler.has_value() && handler->script != nullptr) {
                builtin_context->script_instance_handler_cache[cache_key] = handler;
                return handler;
            }
        }

        current = current->ancestorRaw();
    }
    builtin_context->script_instance_handler_cache[cache_key] = std::nullopt;
    return std::nullopt;
}

enum class ImmediateObjectMethod {
    Unknown,
    Add,
    AddAt,
    AddProp,
    Append,
    Char,
    Count,
    DeleteProp,
    DeleteAt,
    Duplicate,
    FindPos,
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

ImmediateObjectMethod classifyImmediateObjectMethod(std::string_view method_name) {
    switch (method_name.size()) {
        case 3:
            if (equalsIgnoreCase(method_name, "add")) return ImmediateObjectMethod::Add;
            if (equalsIgnoreCase(method_name, "ilk")) return ImmediateObjectMethod::Ilk;
            break;
        case 5:
            if (equalsIgnoreCase(method_name, "addAt")) return ImmediateObjectMethod::AddAt;
            if (equalsIgnoreCase(method_name, "count")) return ImmediateObjectMethod::Count;
            if (equalsIgnoreCase(method_name, "getAt")) return ImmediateObjectMethod::GetAt;
            if (equalsIgnoreCase(method_name, "setAt")) return ImmediateObjectMethod::SetAt;
            break;
        case 4:
            if (equalsIgnoreCase(method_name, "char")) return ImmediateObjectMethod::Char;
            break;
        case 6:
            if (equalsIgnoreCase(method_name, "append")) return ImmediateObjectMethod::Append;
            break;
        case 7:
            if (equalsIgnoreCase(method_name, "addProp")) return ImmediateObjectMethod::AddProp;
            if (equalsIgnoreCase(method_name, "findPos")) return ImmediateObjectMethod::FindPos;
            if (equalsIgnoreCase(method_name, "getLast")) return ImmediateObjectMethod::GetLast;
            if (equalsIgnoreCase(method_name, "getProp")) return ImmediateObjectMethod::GetProp;
            if (equalsIgnoreCase(method_name, "handler")) return ImmediateObjectMethod::Handler;
            if (equalsIgnoreCase(method_name, "setProp")) return ImmediateObjectMethod::SetProp;
            break;
        case 8:
            if (equalsIgnoreCase(method_name, "deleteAt")) return ImmediateObjectMethod::DeleteAt;
            if (equalsIgnoreCase(method_name, "getAProp")) return ImmediateObjectMethod::GetAProp;
            if (equalsIgnoreCase(method_name, "getFirst")) return ImmediateObjectMethod::GetFirst;
            if (equalsIgnoreCase(method_name, "setAProp")) return ImmediateObjectMethod::SetAProp;
            break;
        case 9:
            if (equalsIgnoreCase(method_name, "duplicate")) return ImmediateObjectMethod::Duplicate;
            break;
        case 10:
            if (equalsIgnoreCase(method_name, "deleteProp")) return ImmediateObjectMethod::DeleteProp;
            if (equalsIgnoreCase(method_name, "getPropRef")) return ImmediateObjectMethod::GetPropRef;
            break;
        case 11:
            if (equalsIgnoreCase(method_name, "getProperty")) return ImmediateObjectMethod::GetProperty;
            break;
        default:
            break;
    }
    return ImmediateObjectMethod::Unknown;
}

Datum scriptInstanceObjectMethod(ExecutionContext& context,
                                 Datum& receiver,
                                 std::string_view method_name,
                                 ImmediateObjectMethod method,
                                 std::span<const Datum> args) {
    auto& instance = receiver.scriptInstanceValue();
    if (shouldDeferNumericCloseThread(context, receiver, method_name, args)) {
        return Datum::TRUE;
    }
    if (method == ImmediateObjectMethod::SetAt || method == ImmediateObjectMethod::SetAProp) {
        if (args.size() >= 2) {
            std::string prop_name_storage;
            util::setProperty(instance, keyNameLikeJavaView(args[0], prop_name_storage), args[1]);
        }
        return Datum::voidValue();
    }
    if (method == ImmediateObjectMethod::SetProp) {
        if (args.size() == 2) {
            std::string prop_name_storage;
            util::setProperty(instance, keyNameLikeJavaView(args[0], prop_name_storage), args[1]);
        } else if (args.size() >= 3) {
            std::string prop_name_storage;
            const std::string_view prop_name = keyNameLikeJavaView(args[0], prop_name_storage);
            Datum local_prop = util::getProperty(instance, prop_name);
            if (local_prop.isVoid()) {
                local_prop = Datum::propList();
                util::setProperty(instance, prop_name, local_prop);
            }
            scriptInstanceSetNestedProperty(local_prop, args[1], args[2]);
            util::setProperty(instance, prop_name, std::move(local_prop));
        }
        return Datum::voidValue();
    }
    if (method == ImmediateObjectMethod::GetAt) {
        if (args.empty()) return Datum::voidValue();
        std::string key_storage;
        const std::string_view key = keyNameLikeJavaView(args[0], key_storage);
        if (equalsIgnoreCase(key, "ancestor")) {
            const Datum ancestor = util::getProperty(instance, key);
            return ancestor.isVoid() ? Datum::of(0) : ancestor;
        }
        return util::getProperty(instance, key);
    }
    if (method == ImmediateObjectMethod::GetAProp) {
        if (args.empty()) return Datum::voidValue();
        std::string prop_name_storage;
        return scriptInstancePropertyValue(instance, keyNameLikeJavaView(args[0], prop_name_storage));
    }
    if (method == ImmediateObjectMethod::GetProp || method == ImmediateObjectMethod::GetPropRef) {
        if (args.empty()) return Datum::voidValue();
        std::string prop_name_storage;
        const std::string_view prop_name = keyNameLikeJavaView(args[0], prop_name_storage);
        if (args.size() >= 2) {
            return scriptInstanceNestedPropertyValue(instance, prop_name, args[1]);
        }
        return scriptInstancePropertyValue(instance, prop_name);
    }
    if (method == ImmediateObjectMethod::AddProp) {
        if (args.size() >= 2) {
            std::string prop_name_storage;
            scriptInstancePutLocalProperty(instance, keyNameLikeJavaView(args[0], prop_name_storage), args[1]);
        }
        return Datum::voidValue();
    }
    if (method == ImmediateObjectMethod::DeleteProp) {
        if (!args.empty()) {
            std::string prop_name_storage;
            scriptInstanceDeleteLocalProperty(instance, keyNameLikeJavaView(args[0], prop_name_storage));
        }
        return Datum::voidValue();
    }
    if (method == ImmediateObjectMethod::Count) {
        if (!args.empty()) {
            std::string prop_name_storage;
            return scriptInstancePropertyCountValue(instance, keyNameLikeJavaView(args[0], prop_name_storage));
        }
        return Datum::of(static_cast<int>(instance.properties().size()) + (instance.ancestorRaw() != nullptr ? 1 : 0));
    }
    if (method == ImmediateObjectMethod::Ilk) {
        return Datum::symbol("instance");
    }
    if (method == ImmediateObjectMethod::AddAt) {
        return Datum::voidValue();
    }
    if (method == ImmediateObjectMethod::Handler) {
        if (args.empty()) return Datum::FALSE;
        std::string handler_name_storage;
        const std::string_view handler_name = keyNameLikeJavaView(args[0], handler_name_storage);
        return findScriptInstanceScriptHandler(context, instance, handler_name).has_value() ? Datum::TRUE : Datum::FALSE;
    }
    const auto handler = findScriptInstanceScriptHandler(context, instance, method_name);
    if (handler) {
        return safeExecuteHandler(context, handlerRefFromLocation(*handler), args, receiver);
    }

    Datum property = util::getProperty(instance, method_name);
    return property.isVoid() ? Datum::voidValue() : std::move(property);
}

Datum scriptInstanceObjectMethod(ExecutionContext& context,
                                 Datum& receiver,
                                 std::string_view method_name,
                                 std::span<const Datum> args) {
    return scriptInstanceObjectMethod(context,
                                      receiver,
                                      method_name,
                                      classifyImmediateObjectMethod(method_name),
                                      args);
}

bool imageFill(bitmap::Bitmap& bmp, const std::vector<Datum>& args) {
    if (args.size() < 2) return false;

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    Datum color_datum = Datum::voidValue();

    if (const auto* rect = args[0].asIntRect()) {
        left = rect->left;
        top = rect->top;
        right = rect->right;
        bottom = rect->bottom;
        color_datum = args[1];
    } else if (args.size() >= 5) {
        left = toIntLikeJava(args[0]);
        top = toIntLikeJava(args[1]);
        right = toIntLikeJava(args[2]);
        bottom = toIntLikeJava(args[3]);
        color_datum = args[4];
    } else {
        return false;
    }

    if (color_datum.isPropList()) {
        const Datum prop_color = getPropListKey(color_datum.propListValue(), "color");
        if (!prop_color.isVoid()) {
            color_datum = prop_color;
        }
    }
    if (color_datum.isVoid()) {
        return false;
    }

    const int width = right - left;
    const int height = bottom - top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    const auto color_argb = imageColorArgb(color_datum, &bmp);
    if (const auto palette_index = imageFillPaletteIndex(color_datum, bmp)) {
        bmp.fillRectPaletteIndex(left, top, width, height, *palette_index, color_argb);
    } else {
        bmp.fillRect(left, top, width, height, color_argb);
    }
    bmp.clearPreserveScriptFillBacking();
    if (((color_argb >> 24U) & 0xFFU) == 0xFFU) {
        bmp.markScriptFillBacking(color_argb);
    }
    return true;
}

bool imageHasOpaqueNonWhiteContent(const bitmap::Bitmap& bitmap) {
    for (const auto pixel : bitmap.pixels()) {
        const auto alpha = (pixel >> 24U) & 0xFFU;
        if (alpha != 0 && (pixel & 0x00FFFFFFU) != 0x00FFFFFFU) {
            return true;
        }
    }
    return false;
}

bool imageCopyLooksLikeSparseViewportContent(const bitmap::Bitmap& src,
                                             const Datum::IntRect& dest_rect,
                                             const Datum::IntRect& src_rect) {
    if (src.width() <= 0 || src.height() <= 0) {
        return false;
    }
    const int dest_width = dest_rect.right - dest_rect.left;
    const int dest_height = dest_rect.bottom - dest_rect.top;
    if (dest_width <= 0 || dest_height <= 0) {
        return false;
    }
    if (!imageHasOpaqueNonWhiteContent(src)) {
        return false;
    }

    const int visible_src_width = std::max(0, std::min(src_rect.right, src.width()) - std::max(src_rect.left, 0));
    const int visible_src_height = std::max(0, std::min(src_rect.bottom, src.height()) - std::max(src_rect.top, 0));
    // Friend-style list buffers feed a short content strip into a viewport.
    // Navigator text/link feeds either contain only matte white or occupy most of the target height.
    return visible_src_width * 2 >= dest_width &&
           visible_src_height * 4 <= dest_height;
}

Datum imageDraw(bitmap::Bitmap& bmp, const std::vector<Datum>& args) {
    if (args.size() < 2) return Datum::voidValue();

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    Datum props_arg = Datum::voidValue();

    if (const auto* rect = args[0].asIntRect()) {
        left = rect->left;
        top = rect->top;
        right = rect->right;
        bottom = rect->bottom;
        props_arg = args[1];
    } else if (args.size() >= 5) {
        left = toIntLikeJava(args[0]);
        top = toIntLikeJava(args[1]);
        right = toIntLikeJava(args[2]);
        bottom = toIntLikeJava(args[3]);
        props_arg = args[4];
    } else {
        return Datum::voidValue();
    }

    std::uint32_t color_argb = 0xFF000000U;
    std::string shape_type = "rect";
    if (props_arg.isPropList()) {
        const Datum color_datum = getPropListKey(props_arg.propListValue(), "color");
        if (!color_datum.isVoid()) {
            color_argb = imageColorArgb(color_datum, &bmp);
        }
        const Datum shape_datum = getPropListKey(props_arg.propListValue(), "shapeType");
        if (const auto* shape = shape_datum.asSymbol()) {
            shape_type = shape->name;
        }
    } else {
        color_argb = imageColorArgb(props_arg, &bmp);
    }

    const int width = right - left;
    const int height = bottom - top;
    if (width <= 0 || height <= 0) {
        return Datum::voidValue();
    }

    if (equalsIgnoreCase(shape_type, "oval") || equalsIgnoreCase(shape_type, "ellipse")) {
        bitmap::drawing::drawEllipse(bmp, left + width / 2, top + height / 2, width / 2, height / 2, color_argb);
    } else if (equalsIgnoreCase(shape_type, "line")) {
        bitmap::drawing::drawLine(bmp, left, top, right, bottom, color_argb);
    } else {
        bitmap::drawing::drawRect(bmp, left, top, width, height, color_argb);
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

    int edge_pixels = 0;
    int white_edge_pixels = 0;
    bool has_dark_pixel = false;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int luma = imageMaskAlphaFromPixel(alpha.getPixel(x, y));
            if (luma < 250) {
                has_dark_pixel = true;
            }
            if (x == 0 || y == 0 || x == width - 1 || y == height - 1) {
                ++edge_pixels;
                if (luma >= 250) {
                    ++white_edge_pixels;
                }
            }
        }
    }
    return has_dark_pixel && edge_pixels > 0 && white_edge_pixels * 4 >= edge_pixels * 3;
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
    int white_corners = 0;
    for (const auto& [x, y] : corners) {
        if (imageMaskAlphaFromPixel(alpha.getPixel(x, y)) >= 250) {
            ++white_corners;
        }
    }
    if (white_corners < static_cast<int>(corners.size())) {
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

    if (const auto* alpha_ref = args[0].asImageRef()) {
        const auto& alpha = alpha_ref->bitmap;
        if (alpha == nullptr ||
            alpha->bitDepth() != 8 ||
            alpha->width() != bmp.width() ||
            alpha->height() != bmp.height()) {
            return Datum::FALSE;
        }

        const bool matte_polarity = imageHasMattePolarity(*alpha);
        for (int y = 0; y < bmp.height(); ++y) {
            for (int x = 0; x < bmp.width(); ++x) {
                int alpha_level = imageMaskAlphaFromPixel(alpha->getPixel(x, y));
                if (matte_polarity) {
                    alpha_level = 255 - alpha_level;
                }
                const auto pixel = bmp.getPixel(x, y);
                bmp.setPixel(x, y, (static_cast<std::uint32_t>(alpha_level & 0xFF) << 24) | (pixel & 0x00FFFFFFU));
            }
        }
        bmp.setNativeAlpha(true);
        return Datum::TRUE;
    }

    const int alpha_level = std::clamp(toIntLikeJava(args[0]), 0, 255);
    for (int y = 0; y < bmp.height(); ++y) {
        for (int x = 0; x < bmp.width(); ++x) {
            const auto pixel = bmp.getPixel(x, y);
            bmp.setPixel(x, y, (static_cast<std::uint32_t>(alpha_level & 0xFF) << 24) | (pixel & 0x00FFFFFFU));
        }
    }
    bmp.setNativeAlpha(true);
    return Datum::TRUE;
}

struct ImageFloodFillMatte {
    std::optional<int> palette_index;
    int color_rgb = 0xFFFFFF;
    int tolerance = 0;

    [[nodiscard]] bool usesPaletteIndex() const { return palette_index.has_value(); }
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

bool imageHasPaletteIndices(const std::optional<std::vector<std::uint8_t>>& palette_indices, int width, int height) {
    return palette_indices.has_value() && palette_indices->size() >= static_cast<std::size_t>(width * height);
}

bool imageIsUniformPaletteIndex(const std::vector<std::uint8_t>& palette_indices, int palette_index) {
    return std::all_of(palette_indices.begin(), palette_indices.end(), [&](std::uint8_t entry) {
        return static_cast<int>(entry) == palette_index;
    });
}

int imageResolvePaletteIndexRgb(const std::vector<std::uint32_t>& pixels,
                                const std::vector<std::uint8_t>& palette_indices,
                                int palette_index) {
    for (std::size_t index = 0; index < pixels.size() && index < palette_indices.size(); ++index) {
        if (static_cast<int>(palette_indices[index]) == palette_index) {
            return static_cast<int>(pixels[index] & 0x00FFFFFFU);
        }
    }
    return 0xFFFFFF;
}

bool imageCornerContainsPaletteIndex(const std::vector<std::uint8_t>& palette_indices,
                                     int width,
                                     int height,
                                     int palette_index) {
    for (const int index : imageCornerIndices(width, height)) {
        if (index >= 0 && static_cast<std::size_t>(index) < palette_indices.size() &&
            static_cast<int>(palette_indices[static_cast<std::size_t>(index)]) == palette_index) {
            return true;
        }
    }
    return false;
}

bool imageDefaultIndexedMatteRgb(int rgb) {
    return rgb == 0x000000 || rgb == 0xFFFFFF;
}

bool imageIsNearWhiteGrayscale(int rgb, int min_channel, int max_delta) {
    const int r = (rgb >> 16) & 0xFF;
    const int g = (rgb >> 8) & 0xFF;
    const int b = rgb & 0xFF;
    return r >= min_channel && g >= min_channel && b >= min_channel &&
           std::abs(r - g) <= max_delta &&
           std::abs(g - b) <= max_delta &&
           std::abs(r - b) <= max_delta;
}

std::optional<int> imageInferDominantEdgePaletteIndex(const std::vector<std::uint32_t>& pixels,
                                                      const std::vector<std::uint8_t>& palette_indices,
                                                      int width,
                                                      int height) {
    if (width <= 0 || height <= 0) return std::nullopt;

    std::array<int, 256> counts{};
    int opaque_edge_count = 0;
    int dominant_index = -1;
    int dominant_count = 0;
    for (const int index : imageEdgeIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        const int palette_index = static_cast<int>(palette_indices[static_cast<std::size_t>(index)]);
        const int count = ++counts[static_cast<std::size_t>(palette_index)];
        ++opaque_edge_count;
        if (count > dominant_count) {
            dominant_count = count;
            dominant_index = palette_index;
        }
    }
    if (opaque_edge_count == 0 || dominant_index < 0 || imageIsUniformPaletteIndex(palette_indices, dominant_index)) {
        return std::nullopt;
    }

    int opaque_corner_count = 0;
    for (const int index : imageCornerIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        ++opaque_corner_count;
        if (static_cast<int>(palette_indices[static_cast<std::size_t>(index)]) != dominant_index) {
            return std::nullopt;
        }
    }
    if (opaque_corner_count == 0 || dominant_count * 4 < opaque_edge_count * 3) {
        return std::nullopt;
    }
    return dominant_index;
}

std::optional<int> imageInferWhiteEdgePaletteIndex(const std::vector<std::uint32_t>& pixels,
                                                   const std::vector<std::uint8_t>& palette_indices,
                                                   int width,
                                                   int height) {
    if (width <= 0 || height <= 0) return std::nullopt;

    std::array<int, 256> counts{};
    int best_index = -1;
    int best_count = 0;
    for (const int index : imageEdgeIndices(width, height)) {
        const auto offset = static_cast<std::size_t>(index);
        if (offset >= pixels.size() || offset >= palette_indices.size() ||
            ((pixels[offset] >> 24) & 0xFFU) == 0) {
            continue;
        }
        const int palette_index = static_cast<int>(palette_indices[offset]);
        const int rgb = imageResolvePaletteIndexRgb(pixels, palette_indices, palette_index);
        if (rgb != 0xFFFFFF) {
            continue;
        }
        const int count = ++counts[static_cast<std::size_t>(palette_index)];
        if (count > best_count) {
            best_count = count;
            best_index = palette_index;
        }
    }
    return best_index >= 0 && !imageIsUniformPaletteIndex(palette_indices, best_index)
        ? std::optional<int>(best_index)
        : std::nullopt;
}

std::optional<ImageFloodFillMatte> imageResolveIndexedFloodFillMatte(
    const std::vector<std::uint32_t>& pixels,
    const std::vector<std::uint8_t>& palette_indices,
    int width,
    int height) {
    if (const auto white_edge = imageInferWhiteEdgePaletteIndex(pixels, palette_indices, width, height)) {
        return ImageFloodFillMatte{
            *white_edge, imageResolvePaletteIndexRgb(pixels, palette_indices, *white_edge), 0};
    }
    if (const auto dominant = imageInferDominantEdgePaletteIndex(pixels, palette_indices, width, height)) {
        const int matte_rgb = imageResolvePaletteIndexRgb(pixels, palette_indices, *dominant);
        if (matte_rgb == 0xFFFFFF ||
            (*dominant == 0 && imageDefaultIndexedMatteRgb(matte_rgb))) {
            return ImageFloodFillMatte{*dominant, matte_rgb, 0};
        }
    }
    if (imageCornerContainsPaletteIndex(palette_indices, width, height, 0)) {
        const int index_zero_rgb = imageResolvePaletteIndexRgb(pixels, palette_indices, 0);
        if (imageDefaultIndexedMatteRgb(index_zero_rgb)) {
            return ImageFloodFillMatte{0, index_zero_rgb, 0};
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
    int opaque_edge_count = 0;
    int dominant_rgb = -1;
    int dominant_count = 0;
    for (const int index : imageEdgeIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        const int rgb = static_cast<int>(pixel & 0x00FFFFFFU);
        const int count = ++counts[rgb];
        ++opaque_edge_count;
        if (count > dominant_count) {
            dominant_count = count;
            dominant_rgb = rgb;
        }
    }
    if (opaque_edge_count == 0 || dominant_rgb < 0 || imageIsUniformRgb(pixels, dominant_rgb)) {
        return std::nullopt;
    }

    int opaque_corner_count = 0;
    for (const int index : imageCornerIndices(width, height)) {
        const auto pixel = pixels[static_cast<std::size_t>(index)];
        if (((pixel >> 24) & 0xFFU) == 0) {
            continue;
        }
        ++opaque_corner_count;
        if (static_cast<int>(pixel & 0x00FFFFFFU) != dominant_rgb) {
            return std::nullopt;
        }
    }
    if (opaque_corner_count == 0 || dominant_count * 4 < opaque_edge_count * 3) {
        return std::nullopt;
    }
    return dominant_rgb;
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
    const std::optional<std::vector<std::uint8_t>>& palette_indices,
    int width,
    int height) {
    if (imageHasPaletteIndices(palette_indices, width, height) &&
        bitmap::detail::paletteIndicesMatchPixels(pixels, *palette_indices)) {
        return imageResolveIndexedFloodFillMatte(pixels, *palette_indices, width, height);
    }
    return imageResolveRgbFloodFillMatte(pixels, width, height);
}

bool imageEdgeContainsOpaquePaletteIndex(const std::vector<std::uint32_t>& pixels,
                                         const std::vector<std::uint8_t>& palette_indices,
                                         int width,
                                         int height,
                                         int palette_index) {
    for (const int index : imageEdgeIndices(width, height)) {
        const auto offset = static_cast<std::size_t>(index);
        if (((pixels[offset] >> 24) & 0xFFU) != 0 &&
            offset < palette_indices.size() &&
            static_cast<int>(palette_indices[offset]) == palette_index) {
            return true;
        }
    }
    return false;
}

bool imageHasOpaqueNonPaletteIndexContent(const std::vector<std::uint32_t>& pixels,
                                          const std::vector<std::uint8_t>& palette_indices,
                                          int palette_index) {
    for (std::size_t index = 0; index < pixels.size() && index < palette_indices.size(); ++index) {
        if (((pixels[index] >> 24) & 0xFFU) != 0 &&
            static_cast<int>(palette_indices[index]) != palette_index) {
            return true;
        }
    }
    return false;
}

bool imageMatchesRgb(std::uint32_t pixel, int matte_rgb, int tolerance) {
    const int pr = static_cast<int>((pixel >> 16) & 0xFFU);
    const int pg = static_cast<int>((pixel >> 8) & 0xFFU);
    const int pb = static_cast<int>(pixel & 0xFFU);
    const int mr = (matte_rgb >> 16) & 0xFF;
    const int mg = (matte_rgb >> 8) & 0xFF;
    const int mb = matte_rgb & 0xFF;
    return std::abs(pr - mr) <= tolerance && std::abs(pg - mg) <= tolerance && std::abs(pb - mb) <= tolerance;
}

bool imageIsTransparentOrMatte(const std::vector<std::uint32_t>& pixels,
                               const std::optional<std::vector<std::uint8_t>>& palette_indices,
                               int index,
                               const ImageFloodFillMatte& matte) {
    const auto pixel = pixels[static_cast<std::size_t>(index)];
    if (((pixel >> 24) & 0xFFU) == 0) {
        return true;
    }
    if (matte.usesPaletteIndex() && palette_indices.has_value() &&
        static_cast<std::size_t>(index) < palette_indices->size()) {
        return static_cast<int>((*palette_indices)[static_cast<std::size_t>(index)]) == *matte.palette_index;
    }
    return imageMatchesRgb(pixel, matte.color_rgb, matte.tolerance);
}

std::vector<bool> imageComputeFloodFillTransparency(
    const std::vector<std::uint32_t>& pixels,
    const std::optional<std::vector<std::uint8_t>>& palette_indices,
    int width,
    int height,
    const ImageFloodFillMatte& matte) {
    std::vector<bool> transparent(static_cast<std::size_t>(width * height), false);
    std::queue<int> queue;
    const auto seed = [&](int x, int y) {
        const int index = y * width + x;
        if (!transparent[static_cast<std::size_t>(index)] &&
            imageIsTransparentOrMatte(pixels, palette_indices, index, matte)) {
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
    int opaque_pixels = 0;
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
        ++opaque_pixels;
    }
    return opaque_pixels > 0 && opaque_pixels * 4 <= static_cast<int>(pixels.size()) * 3;
}

bool imageIsWhiteBackedMaskSource(const std::vector<std::uint32_t>& pixels,
                                  const std::vector<bool>& transparent,
                                  const ImageFloodFillMatte& matte) {
    if (imageMaskAlphaFromPixel(0xFF000000U | static_cast<std::uint32_t>(matte.color_rgb & 0x00FFFFFF)) < 250) {
        return false;
    }

    bool has_transparent_matte = false;
    bool has_opaque_ink = false;
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        if (((pixels[index] >> 24) & 0xFFU) == 0) {
            continue;
        }
        if (transparent[index]) {
            has_transparent_matte = true;
        } else {
            has_opaque_ink = true;
        }
        if (has_transparent_matte && has_opaque_ink) {
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
            const int pixel_rgb = static_cast<int>(pixel & 0x00FFFFFFU);
            if (!rgb.has_value()) {
                rgb = pixel_rgb;
            } else if (*rgb != pixel_rgb) {
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
            const int pixel_rgb = static_cast<int>(pixel & 0x00FFFFFFU);
            if (!rgb.has_value()) {
                rgb = pixel_rgb;
            } else if (*rgb != pixel_rgb) {
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
                                         int source_rgb,
                                         const bitmap::Palette& palette) {
    const auto target_rgb = palette.getColor(palette.nearestIndex(static_cast<std::uint32_t>(source_rgb)));
    for (auto& pixel : bitmap.pixels()) {
        if ((pixel & 0x00FFFFFFU) == (static_cast<std::uint32_t>(source_rgb) & 0x00FFFFFFU)) {
            pixel = (pixel & 0xFF000000U) | (target_rgb & 0x00FFFFFFU);
        }
    }
}

bool imageCopyMatteToMaskImage(bitmap::Bitmap& dest,
                               const bitmap::Bitmap& src,
                               const Datum::IntRect& dest_rect,
                               const Datum::IntRect& src_rect) {
    const int dest_width = dest_rect.right - dest_rect.left;
    const int dest_height = dest_rect.bottom - dest_rect.top;
    const int src_width = src_rect.right - src_rect.left;
    const int src_height = src_rect.bottom - src_rect.top;
    if (src.width() <= 0 || src.height() <= 0 ||
        src_width <= 0 || src_height <= 0 ||
        dest_width <= 0 || dest_height <= 0 ||
        dest.imagePalette() != nullptr ||
        !imageIsMostlyWhiteRegion(dest, dest_rect)) {
        return false;
    }

    const auto pixels = src.pixels();
    const auto palette_indices = src.paletteIndices();
    const auto matte = imageResolveFloodFillMatte(pixels, palette_indices, src.width(), src.height());
    if (!matte.has_value()) {
        return false;
    }
    const auto transparent =
        imageComputeFloodFillTransparency(pixels, palette_indices, src.width(), src.height(), *matte);
    if (!imageIsMaskSource(pixels, transparent, *matte)) {
        return false;
    }

    const int matte_luma =
        imageMaskAlphaFromPixel(0xFF000000U | static_cast<std::uint32_t>(matte->color_rgb & 0x00FFFFFF));
    const bool light_matte = matte_luma >= 128;
    for (int dy = 0; dy < dest_height; ++dy) {
        const int sy = src_rect.top + (dy * src_height / dest_height);
        const int py = dest_rect.top + dy;
        if (sy < 0 || sy >= src.height() || py < 0 || py >= dest.height()) {
            continue;
        }
        for (int dx = 0; dx < dest_width; ++dx) {
            const int sx = src_rect.left + (dx * src_width / dest_width);
            const int px = dest_rect.left + dx;
            if (sx < 0 || sx >= src.width() || px < 0 || px >= dest.width()) {
                continue;
            }
            const auto src_offset = static_cast<std::size_t>(sy * src.width() + sx);
            if (src_offset >= transparent.size() || transparent[src_offset]) {
                continue;
            }
            int mask_luma = imageMaskAlphaFromPixel(pixels[src_offset]);
            if (!light_matte) {
                mask_luma = 255 - mask_luma;
            }
            const auto luma = static_cast<std::uint32_t>(mask_luma & 0xFF);
            dest.setPixelPreservePaletteIndex(px, py, 0xFF000000U | (luma << 16) | (luma << 8) | luma);
        }
    }
    return true;
}

int imagePercentToBlendAlpha(const Datum& blend_datum) {
    return std::clamp(static_cast<int>(std::lround(toDoubleLikeJava(blend_datum) * 255.0 / 100.0)), 0, 255);
}

bool imageIsOpaqueKeyPixel(std::uint32_t pixel, int key_rgb) {
    return ((pixel >> 24) & 0xFFU) == 255 && static_cast<int>(pixel & 0x00FFFFFFU) == (key_rgb & 0x00FFFFFF);
}

bool imageHasOpaqueBackgroundKeyBorder(const bitmap::Bitmap& src, const Datum::IntRect& rect, int key_rgb) {
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
        if (imageIsOpaqueKeyPixel(src.getPixel(x, top), key_rgb) ||
            imageIsOpaqueKeyPixel(src.getPixel(x, bottom), key_rgb)) {
            return true;
        }
    }
    for (int y = top + 1; y < bottom; ++y) {
        if (imageIsOpaqueKeyPixel(src.getPixel(left, y), key_rgb) ||
            imageIsOpaqueKeyPixel(src.getPixel(right, y), key_rgb)) {
            return true;
        }
    }
    return false;
}

bool imageIsInverseWhiteAlphaMask(const bitmap::Bitmap& src, const Datum::IntRect& rect) {
    bool has_opaque_white = false;
    bool has_transparent_ink = false;
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
                has_transparent_ink = true;
                continue;
            }
            if ((pixel & 0x00FFFFFFU) != 0x00FFFFFFU) {
                return false;
            }
            has_opaque_white = true;
        }
    }
    return has_opaque_white && has_transparent_ink;
}

int imageInverseWhiteAlphaMaskInkRgb(std::optional<int> bg_color_remap) {
    return bg_color_remap.has_value() ? 0x000000 : 0x7B9498;
}

bool imageInvertedWhiteAlphaMaskHasOpaqueKeyBorder(const bitmap::Bitmap& src,
                                                   const Datum::IntRect& rect,
                                                   int key_rgb,
                                                   int ink_rgb) {
    if ((ink_rgb & 0x00FFFFFF) != (key_rgb & 0x00FFFFFF)) {
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
    const auto transparent_hole = [&src](int x, int y) {
        return ((src.getPixel(x, y) >> 24) & 0xFFU) == 0;
    };

    for (int x = left; x <= right; ++x) {
        if (transparent_hole(x, top) || transparent_hole(x, bottom)) {
            return true;
        }
    }
    for (int y = top + 1; y < bottom; ++y) {
        if (transparent_hole(left, y) || transparent_hole(right, y)) {
            return true;
        }
    }
    return false;
}

std::uint32_t imageInvertWhiteAlphaMaskPixel(std::uint32_t pixel, int ink_rgb) {
    return ((pixel >> 24) & 0xFFU) == 0
        ? (0xFF000000U | static_cast<std::uint32_t>(ink_rgb & 0x00FFFFFF))
        : 0x00000000U;
}

bool imageShouldTreatWhiteTextBackgroundAsTransparent(const bitmap::Bitmap& src,
                                                      const Datum::IntRect& rect,
                                                      id::InkMode ink,
                                                      int blend,
                                                      const std::shared_ptr<bitmap::Bitmap>& mask,
                                                      std::optional<int> color_remap,
                                                      std::optional<int> bg_color_remap) {
    if (src.bitDepth() < 32 ||
        src.hasNativeMatteAlpha() ||
        ink != id::InkMode::COPY ||
        blend < 255 ||
        mask != nullptr ||
        color_remap.has_value() ||
        bg_color_remap.has_value()) {
        return false;
    }

    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    int white = 0;
    int non_white = 0;
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
                ++non_white;
                if (std::abs(r - g) > 2 || std::abs(g - b) > 2) {
                    ++colored;
                }
            }
        }
    }
    return white > 0 && non_white > 0 && colored == 0 && white >= non_white;
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

bool imageShouldKeyNearWhiteMatte(const bitmap::Bitmap& src, const Datum::IntRect& rect, int background_key_rgb) {
    if (src.bitDepth() < 32 ||
        !src.hasTransparentPixels() ||
        (background_key_rgb & 0x00FFFFFF) != 0xFFFFFF ||
        rect.right <= rect.left ||
        rect.bottom <= rect.top ||
        src.width() <= 0 ||
        src.height() <= 0) {
        return false;
    }

    const int max_x = src.width() - 1;
    const int max_y = src.height() - 1;
    const int left = std::clamp(rect.left, 0, max_x);
    const int top = std::clamp(rect.top, 0, max_y);
    const int right = std::clamp(rect.right - 1, 0, max_x);
    const int bottom = std::clamp(rect.bottom - 1, 0, max_y);
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
                                       int background_key_rgb,
                                       const std::optional<std::vector<std::uint8_t>>& palette_indices) {
    if (src.bitDepth() > 8 ||
        (background_key_rgb & 0x00FFFFFF) != 0xFFFFFF ||
        !imageHasPaletteIndices(palette_indices, src.width(), src.height()) ||
        imageIsUniformPaletteIndex(*palette_indices, 0)) {
        return false;
    }

    const auto& pixels = src.pixels();
    if (!imageEdgeContainsOpaquePaletteIndex(pixels, *palette_indices, src.width(), src.height(), 0)) {
        return false;
    }

    const int index_zero_rgb = imageResolvePaletteIndexRgb(pixels, *palette_indices, 0);
    return imageIsNearWhiteGrayscale(index_zero_rgb, 232, 16) &&
           imageHasOpaqueNonPaletteIndexContent(pixels, *palette_indices, 0);
}

bool imagePalettesCompatibleForIndexPreserve(const bitmap::Bitmap& dest, const bitmap::Bitmap& src) {
    const auto dest_palette = dest.imagePalette();
    const auto src_palette = src.imagePalette();
    // Palette indices without an attached source palette are not portable
    // into a paletted destination.  Refresh them from the source RGB values
    // instead of treating stale index zero as the destination's white matte.
    return dest_palette == nullptr ||
           (src_palette != nullptr && dest_palette.get() == src_palette.get());
}

bool imageCanPreservePaletteIndices(const bitmap::Bitmap& dest,
                                    const bitmap::Bitmap& src,
                                    const std::optional<std::vector<std::uint8_t>>& src_palette_indices,
                                    id::InkMode ink,
                                    int blend,
                                    const std::shared_ptr<bitmap::Bitmap>& mask,
                                    std::optional<int> color_remap,
                                    std::optional<int> bg_color_remap) {
    return src_palette_indices.has_value() &&
           src_palette_indices->size() == src.pixels().size() &&
           src.bitDepth() <= 8 &&
           dest.bitDepth() <= 8 &&
           dest.bitDepth() >= src.bitDepth() &&
           imagePalettesCompatibleForIndexPreserve(dest, src) &&
           (ink == id::InkMode::COPY ||
            ink == id::InkMode::MATTE ||
            ink == id::InkMode::BACKGROUND_TRANSPARENT) &&
           blend >= 255 &&
           mask == nullptr &&
           !color_remap.has_value() &&
           !bg_color_remap.has_value();
}

bool imageCanRefreshDestinationPaletteIndices(const bitmap::Bitmap& dest) {
    return dest.bitDepth() <= 8 &&
           dest.imagePalette() != nullptr;
}

bool imageShouldSkipPaletteIndexPreserve(std::uint32_t source_pixel, id::InkMode ink, int background_key_rgb) {
    if (((source_pixel >> 24) & 0xFFU) == 0) {
        return true;
    }
    if (ink == id::InkMode::BACKGROUND_TRANSPARENT) {
        return static_cast<int>(source_pixel & 0x00FFFFFFU) == (background_key_rgb & 0x00FFFFFF);
    }
    return false;
}

bool imageCopiedWhiteCanKeepMatteIndex(const bitmap::Palette& palette, std::uint32_t pixel, int previous_index) {
    return previous_index == 0 &&
           palette.size() > 0 &&
           (palette.getColor(0) & 0x00FFFFFFU) == 0x00FFFFFFU &&
           (pixel & 0x00FFFFFFU) == 0x00FFFFFFU;
}

int imageNearestCopiedRgbPaletteIndex(const bitmap::Palette& palette, std::uint32_t pixel) {
    if (equalsIgnoreCase(palette.name(), "Grayscale")) {
        const int nearest = palette.nearestIndex(pixel);
        if (nearest == 0 || nearest == 255) {
            return nearest;
        }
        return std::min(248, ((nearest + 3) / 8) * 8);
    }

    const auto rgb = pixel & 0x00FFFFFFU;
    if (rgb != 0x00FFFFFFU || palette.size() <= 1 ||
        (palette.getColor(0) & 0x00FFFFFFU) != 0x00FFFFFFU) {
        return palette.nearestIndex(pixel);
    }

    int best_index = 1;
    int best_distance = std::numeric_limits<int>::max();
    for (int index = 1; index < palette.size(); ++index) {
        const auto color = palette.getColor(index) & 0x00FFFFFFU;
        const int dr = 0xFF - static_cast<int>((color >> 16) & 0xFFU);
        const int dg = 0xFF - static_cast<int>((color >> 8) & 0xFFU);
        const int db = 0xFF - static_cast<int>(color & 0xFFU);
        const int distance = (dr * dr) + (dg * dg) + (db * db);
        if (distance < best_distance) {
            best_distance = distance;
            best_index = index;
            if (distance == 0) {
                break;
            }
        }
    }
    return best_index;
}

int imageConsistentSourcePaletteIndex(const bitmap::Bitmap& src,
                                      const std::vector<std::uint8_t>& palette_indices,
                                      std::size_t offset,
                                      std::uint32_t pixel) {
    if (offset >= palette_indices.size()) {
        return 0;
    }
    const int source_index = static_cast<int>(palette_indices[offset]);
    const auto palette = src.imagePalette();
    if (palette == nullptr || source_index < 0 || source_index >= palette->size() ||
        (palette->getColor(source_index) & 0x00FFFFFFU) == (pixel & 0x00FFFFFFU)) {
        return source_index;
    }
    // Some authored indexed members carry stale indices after a palette
    // override.  Preserve the actual pixel colour when copying into another
    // indexed surface; otherwise a later matte operation can mistake real
    // icon/text pixels for palette index zero (the white matte slot).
    return imageNearestCopiedRgbPaletteIndex(*palette, pixel);
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

bool imageIsWhiteBackedAlreadyColorized(const bitmap::Bitmap& src, const Datum::IntRect& rect, int color_rgb) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    constexpr int key_rgb = 0xFFFFFF;
    const int target_rgb = color_rgb & 0x00FFFFFF;
    bool has_key = false;
    bool has_colored = false;
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
            if (rgb == key_rgb) {
                has_key = true;
            } else if (rgb == target_rgb) {
                has_colored = true;
            } else {
                return false;
            }
        }
    }
    return has_key && has_colored;
}

std::optional<std::uint32_t> imageMatchingPalettePixel(
    const bitmap::Bitmap& src,
    const std::optional<std::vector<std::uint8_t>>& palette_indices,
    int x,
    int y) {
    if (!palette_indices.has_value() || x < 0 || x >= src.width() || y < 0 || y >= src.height()) {
        return std::nullopt;
    }
    const auto offset = static_cast<std::size_t>(y * src.width() + x);
    if (offset >= palette_indices->size() || src.imagePalette() == nullptr) {
        return std::nullopt;
    }
    const int palette_index = static_cast<int>((*palette_indices)[offset]);
    if (palette_index < 0 || palette_index >= src.imagePalette()->size()) {
        return std::nullopt;
    }
    const auto pixel = src.getPixel(x, y) & 0x00FFFFFFU;
    return (src.imagePalette()->getColor(palette_index) & 0x00FFFFFFU) == pixel
        ? std::optional<std::uint32_t>(pixel)
        : std::nullopt;
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
                        const std::optional<std::vector<std::uint8_t>>& palette_indices,
                        int x,
                        int y,
                        int r,
                        bool indexed_shade) {
    if (indexed_shade && palette_indices.has_value() &&
        x >= 0 && x < src.width() && y >= 0 && y < src.height()) {
        const auto offset = static_cast<std::size_t>(y * src.width() + x);
        if (offset < palette_indices->size()) {
            const int palette_index = static_cast<int>((*palette_indices)[offset]);
            if (const auto pixel = imageMatchingPalettePixel(src, palette_indices, x, y);
                pixel.has_value() &&
                (((*pixel >> 16) & 0xFFU) == ((*pixel >> 8) & 0xFFU)) &&
                (((*pixel >> 8) & 0xFFU) == (*pixel & 0xFFU))) {
                return static_cast<int>((*pixel >> 16) & 0xFFU);
            }
            return 255 - palette_index;
        }
    }
    return r;
}

int imageDarkenFixedChannel(int source, int tint, bool preserve_full_tint) {
    return preserve_full_tint && tint == 0xFF ? source : source * tint / 256;
}

std::uint32_t imageDarkenBgTintPixel(std::uint32_t pixel,
                                     int tint_rgb,
                                     std::optional<int> indexed_shade,
                                     bool preserve_source_channels) {
    const int alpha = static_cast<int>((pixel >> 24) & 0xFFU);
    const int tint_r = (tint_rgb >> 16) & 0xFF;
    const int tint_g = (tint_rgb >> 8) & 0xFF;
    const int tint_b = tint_rgb & 0xFF;
    const int source_r = preserve_source_channels
        ? static_cast<int>((pixel >> 16) & 0xFFU)
        : indexed_shade.value_or(static_cast<int>((pixel >> 16) & 0xFFU));
    const int source_g = preserve_source_channels
        ? static_cast<int>((pixel >> 8) & 0xFFU)
        : indexed_shade.value_or(static_cast<int>((pixel >> 8) & 0xFFU));
    const int source_b = preserve_source_channels
        ? static_cast<int>(pixel & 0xFFU)
        : indexed_shade.value_or(static_cast<int>(pixel & 0xFFU));
    const int r = source_r * tint_r / 256;
    const int g = source_g * tint_g / 256;
    const int b = source_b * tint_b / 256;
    return (static_cast<std::uint32_t>(alpha & 0xFF) << 24) |
           (static_cast<std::uint32_t>(r & 0xFF) << 16) |
           (static_cast<std::uint32_t>(g & 0xFF) << 8) |
           static_cast<std::uint32_t>(b & 0xFF);
}

std::uint32_t imageMultiplyDarkenPixel(std::uint32_t pixel,
                                       int tint_rgb,
                                       const bitmap::Bitmap& src,
                                       const std::optional<std::vector<std::uint8_t>>& palette_indices,
                                       int x,
                                       int y,
                                       bool indexed_shade) {
    if (tint_rgb == 0xFFFFFF) {
        return pixel;
    }
    const int alpha = static_cast<int>((pixel >> 24) & 0xFFU);
    if (alpha == 0) {
        return 0;
    }

    const int tint_r = (tint_rgb >> 16) & 0xFF;
    const int tint_g = (tint_rgb >> 8) & 0xFF;
    const int tint_b = tint_rgb & 0xFF;
    const int src_r = static_cast<int>((pixel >> 16) & 0xFFU);
    const int src_g = static_cast<int>((pixel >> 8) & 0xFFU);
    const int src_b = static_cast<int>(pixel & 0xFFU);
    int r = src_r * tint_r / 255;
    int g = src_g * tint_g / 255;
    int b = src_b * tint_b / 255;

    if (palette_indices.has_value() && src.bitDepth() <= 8) {
        const bool custom_palette_color_shade = !indexed_shade && src.imagePalette() != nullptr;
        const bool matching_palette_color = imageMatchingPalettePixel(src, palette_indices, x, y).has_value();
        if (custom_palette_color_shade || matching_palette_color) {
            r = imageDarkenFixedChannel(src_r, tint_r, true);
            g = imageDarkenFixedChannel(src_g, tint_g, true);
            b = imageDarkenFixedChannel(src_b, tint_b, true);
        } else {
            const int shade = imageShadeForDarken(src, palette_indices, x, y, src_r, indexed_shade);
            r = imageDarkenFixedChannel(shade, tint_r, indexed_shade);
            g = imageDarkenFixedChannel(shade, tint_g, indexed_shade);
            b = imageDarkenFixedChannel(shade, tint_b, indexed_shade);
        }
    }

    return (static_cast<std::uint32_t>(alpha & 0xFF) << 24) |
           (static_cast<std::uint32_t>(r & 0xFF) << 16) |
           (static_cast<std::uint32_t>(g & 0xFF) << 8) |
           static_cast<std::uint32_t>(b & 0xFF);
}

std::uint32_t imageRemapGrayscalePixel(std::uint32_t pixel,
                                       std::optional<int> color_remap,
                                       std::optional<int> bg_color_remap) {
    if (!color_remap.has_value() && !bg_color_remap.has_value()) {
        return pixel;
    }
    const int alpha = static_cast<int>((pixel >> 24) & 0xFFU);
    const int gray = static_cast<int>(((pixel >> 16) & 0xFFU));
    const int fg = color_remap.value_or(0);
    const int bg = bg_color_remap.value_or(0xFFFFFF);
    const int fg_r = (fg >> 16) & 0xFF;
    const int fg_g = (fg >> 8) & 0xFF;
    const int fg_b = fg & 0xFF;
    const int bg_r = (bg >> 16) & 0xFF;
    const int bg_g = (bg >> 8) & 0xFF;
    const int bg_b = bg & 0xFF;

    if (color_remap.has_value() && !bg_color_remap.has_value()) {
        const int mask_alpha = (255 - gray) * alpha / 255;
        return (static_cast<std::uint32_t>(mask_alpha & 0xFF) << 24) |
               (static_cast<std::uint32_t>(fg_r & 0xFF) << 16) |
               (static_cast<std::uint32_t>(fg_g & 0xFF) << 8) |
               static_cast<std::uint32_t>(fg_b & 0xFF);
    }

    const float t = static_cast<float>(gray) / 255.0F;
    const int r = static_cast<int>((1.0F - t) * static_cast<float>(fg_r) + t * static_cast<float>(bg_r) + 0.5F);
    const int g = static_cast<int>((1.0F - t) * static_cast<float>(fg_g) + t * static_cast<float>(bg_g) + 0.5F);
    const int b = static_cast<int>((1.0F - t) * static_cast<float>(fg_b) + t * static_cast<float>(bg_b) + 0.5F);
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

Datum imagePropListWithMaskImage(const Datum::PropList& prop_list, std::shared_ptr<bitmap::Bitmap> mask) {
    auto copy = Datum::propList(prop_list.sorted());
    copy.propListValue().properties().reserve(prop_list.properties().size() + 1);
    bool replaced = false;
    for (const auto& [key, value] : prop_list.properties()) {
        std::string key_storage;
        if (equalsIgnoreCase(keyNameLikeJavaView(key, key_storage), "maskImage")) {
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
    const auto* src_ref = args[0].asImageRef();
    if (src_ref == nullptr || src_ref->bitmap == nullptr) return Datum::voidValue();
    const auto* dest_rect = args[1].asIntRect();
    const auto* src_rect = args[2].asIntRect();
    if (src_rect == nullptr) return Datum::voidValue();

    const auto& src = *src_ref->bitmap;
    const int src_width = src_rect->right - src_rect->left;
    const int src_height = src_rect->bottom - src_rect->top;
    if (dest_rect == nullptr) {
        if (!args[1].isList()) {
            return Datum::voidValue();
        }
        const auto quad_points = imageQuadPoints(args[1].listValue());
        if (!quad_points.has_value() || src_width <= 0 || src_height <= 0) {
            return Datum::voidValue();
        }

        const auto& [px, py] = *quad_points;
        const int min_x = std::min({px[0], px[1], px[2], px[3]});
        const int min_y = std::min({py[0], py[1], py[2], py[3]});
        const int max_x = std::max({px[0], px[1], px[2], px[3]});
        const int max_y = std::max({py[0], py[1], py[2], py[3]});
        const int dest_width = max_x - min_x;
        const int dest_height = max_y - min_y;
        if (dest_width <= 0 || dest_height <= 0) {
            return Datum::voidValue();
        }

        const auto transformed = bitmap::drawing::transformQuadBitmap(
            src, src_rect->left, src_rect->top, src_rect->right, src_rect->bottom, px, py, min_x, min_y, max_x, max_y);
        std::vector<Datum> transformed_args{
            Datum::imageRef(transformed),
            Datum::intRect(min_x, min_y, max_x, max_y),
            Datum::intRect(0, 0, dest_width, dest_height),
        };
        if (args.size() >= 4 && args[3].isPropList()) {
            const Datum mask_datum = getPropListKey(args[3].propListValue(), "maskImage");
            if (const auto* mask_ref = mask_datum.asImageRef(); mask_ref != nullptr && mask_ref->bitmap != nullptr) {
                const auto transformed_mask = bitmap::drawing::transformQuadBitmap(
                    *mask_ref->bitmap,
                    src_rect->left,
                    src_rect->top,
                    src_rect->right,
                    src_rect->bottom,
                    px,
                    py,
                    min_x,
                    min_y,
                    max_x,
                    max_y);
                transformed_args.push_back(imagePropListWithMaskImage(args[3].propListValue(), transformed_mask));
            } else {
                transformed_args.push_back(args[3]);
            }
        }
        return imageCopyPixels(dest, transformed_args);
    }

    const int dest_width = dest_rect->right - dest_rect->left;
    const int dest_height = dest_rect->bottom - dest_rect->top;
    if (src_width <= 0 || src_height <= 0 || dest_width <= 0 || dest_height <= 0) {
        return Datum::voidValue();
    }

    int blend = 255;
    id::InkMode ink = id::InkMode::COPY;
    int background_key_rgb = 0xFFFFFF;
    std::optional<int> color_remap;
    std::optional<int> bg_color_remap;
    std::shared_ptr<bitmap::Bitmap> mask;
    if (args.size() >= 4 && args[3].isPropList()) {
        const Datum blend_datum = getPropListKey(args[3].propListValue(), "blend");
        if (!blend_datum.isVoid()) {
            blend = imagePercentToBlendAlpha(blend_datum);
        }
        const Datum ink_datum = getPropListKey(args[3].propListValue(), "ink");
        if (!ink_datum.isVoid()) {
            if (ink_datum.isInt() || ink_datum.isFloat()) {
                ink = id::inkModeFromCode(toIntLikeJava(ink_datum));
            } else if (const auto parsed = id::inkModeFromName(keyNameLikeJava(ink_datum))) {
                ink = *parsed;
            }
        }
        const Datum bg_color_datum = getPropListKey(args[3].propListValue(), "bgColor");
        if (!bg_color_datum.isVoid()) {
            bg_color_remap = static_cast<int>(imageCopyPixelsRemapColorArgb(bg_color_datum, src, dest) & 0x00FFFFFFU);
            background_key_rgb = *bg_color_remap;
        }
        const Datum color_datum = getPropListKey(args[3].propListValue(), "color");
        if (!color_datum.isVoid()) {
            color_remap = static_cast<int>(imageCopyPixelsRemapColorArgb(color_datum, src, dest) & 0x00FFFFFFU);
        }
        const Datum mask_datum = getPropListKey(args[3].propListValue(), "maskImage");
        if (const auto* mask_ref = mask_datum.asImageRef()) {
            mask = mask_ref->bitmap;
        }
    }

    if (dest.imagePalette() == nullptr && src.imagePalette() != nullptr) {
        dest.copyPaletteStateFrom(&src);
        if (dest.bitDepth() > 8) {
            dest.clearPaletteIndices();
        }
    }
    if (!dest.hasAnchorPoint() && src.hasAnchorPoint()) {
        dest.setAnchorPoint(dest_rect->left + src.anchorX() - src_rect->left,
                            dest_rect->top + src.anchorY() - src_rect->top);
    }

    if (ink == id::InkMode::MATTE &&
        dest.bitDepth() <= 8 &&
        imageCopyMatteToMaskImage(dest, src, *dest_rect, *src_rect)) {
        const bool destination_is_fully_replaced =
            dest_rect->left <= 0 &&
            dest_rect->top <= 0 &&
            dest_rect->right >= dest.width() &&
            dest_rect->bottom >= dest.height();
        const bool source_is_fully_consumed =
            src_rect->left <= 0 &&
            src_rect->top <= 0 &&
            src_rect->right >= src.width() &&
            src_rect->bottom >= src.height();
        if (src.isTextRendered() &&
            ((destination_is_fully_replaced && source_is_fully_consumed) || !dest.hasScriptFillBacking())) {
            dest.markTextRendered();
        }
        return Datum::voidValue();
    }

    std::shared_ptr<bitmap::Bitmap> matte_source;
    std::shared_ptr<bitmap::Bitmap> background_transparent_source;
    std::shared_ptr<bitmap::Bitmap> degenerate_alpha_source;
    const bitmap::Bitmap* effective_source = &src;
    if (ink == id::InkMode::MATTE) {
        matte_source = std::make_shared<bitmap::Bitmap>(bitmap::drawing::applyFloodFillTransparency(src));
        effective_source = matte_source.get();
    } else if (ink == id::InkMode::BACKGROUND_TRANSPARENT) {
        background_transparent_source = bitmap::drawing::preprocessBackgroundTransparent(src, background_key_rgb);
        if (background_transparent_source != nullptr) {
            effective_source = background_transparent_source.get();
        }
    }
    if (ink == id::InkMode::COPY && effective_source == &src && src.hasDegenerateAlphaWithRgbContent()) {
        const auto source_backing_rgb = imageRegionSolidDegenerateRgb(src, *src_rect);
        const auto dest_backing_rgb = imageRegionSolidOpaqueRgb(dest, *dest_rect);
        const bool palette_backed_dynamic_buffer =
            source_backing_rgb.has_value() &&
            dest_backing_rgb.has_value() &&
            imageIsDefaultWhiteRgb(*dest_backing_rgb) &&
            src.imagePalette() != nullptr;
        degenerate_alpha_source = std::make_shared<bitmap::Bitmap>(src.copyWithDegenerateAlphaOpaque());
        if (palette_backed_dynamic_buffer) {
            imageNormalizeBackingThroughPalette(*degenerate_alpha_source, *source_backing_rgb, *src.imagePalette());
        }
        effective_source = degenerate_alpha_source.get();
    }
    const auto& copy_src = *effective_source;
    const auto src_palette_indices = copy_src.paletteIndices();

    const bool transparent_background_remap = color_remap.has_value() && !bg_color_remap.has_value();
    const bool already_colorized_white_backed =
        transparent_background_remap && imageIsWhiteBackedAlreadyColorized(copy_src, *src_rect, *color_remap);
    const bool apply_grayscale_remap =
        (color_remap.has_value() || bg_color_remap.has_value()) &&
        !copy_src.hasNativeMatteAlpha() &&
        imageRegionIsMostlyGrayscale(copy_src, *src_rect) &&
        !already_colorized_white_backed;
    const bool darken_bg_tint =
        ink == id::InkMode::DARKEN && bg_color_remap.has_value() && !color_remap.has_value();
    const bool indexed_shade_for_darken = imageUsesIndexedShadeForDarken(copy_src);
    const bool inverse_white_alpha_mask =
        ink == id::InkMode::BACKGROUND_TRANSPARENT &&
        src.hasNativeMatteAlpha() &&
        imageIsInverseWhiteAlphaMask(src, *src_rect);
    const int inverse_white_alpha_mask_ink_rgb = imageInverseWhiteAlphaMaskInkRgb(bg_color_remap);
    id::InkMode effective_ink = ink;
    if (inverse_white_alpha_mask) {
        if (!imageInvertedWhiteAlphaMaskHasOpaqueKeyBorder(
                src, *src_rect, background_key_rgb, inverse_white_alpha_mask_ink_rgb)) {
            effective_ink = id::InkMode::COPY;
        }
    } else if (effective_ink == id::InkMode::BACKGROUND_TRANSPARENT &&
        src.hasNativeMatteAlpha() &&
        !imageHasOpaqueBackgroundKeyBorder(src, *src_rect, background_key_rgb)) {
        effective_ink = id::InkMode::COPY;
    }
    if (blend < 255 && effective_ink == id::InkMode::COPY) {
        effective_ink = id::InkMode::BLEND;
    }
    const bool white_text_background_transparent = imageShouldTreatWhiteTextBackgroundAsTransparent(
        copy_src, *src_rect, effective_ink, blend, mask, color_remap, bg_color_remap);
    const bool key_near_white_matte =
        effective_ink == id::InkMode::BACKGROUND_TRANSPARENT &&
        imageShouldKeyNearWhiteMatte(copy_src, *src_rect, background_key_rgb);
    const bool key_default_indexed_matte =
        effective_ink == id::InkMode::BACKGROUND_TRANSPARENT &&
        imageShouldKeyDefaultIndexedMatte(copy_src, background_key_rgb, src_palette_indices);

    const bool destination_is_fully_replaced =
        dest_rect->left <= 0 &&
        dest_rect->top <= 0 &&
        dest_rect->right >= dest.width() &&
        dest_rect->bottom >= dest.height();
    const bool source_is_fully_consumed =
        src_rect->left <= 0 &&
        src_rect->top <= 0 &&
        src_rect->right >= src.width() &&
        src_rect->bottom >= src.height();
    const bool source_rect_extends_past_source =
        src_rect->left < 0 ||
        src_rect->top < 0 ||
        src_rect->right > src.width() ||
        src_rect->bottom > src.height();
    if (destination_is_fully_replaced &&
        source_rect_extends_past_source &&
        !src.isTextRendered() &&
        src.hasScriptFillBacking() &&
        src.scriptFillBackingColor().has_value()) {
        // Director's image copy leaves the source's authored backing in the
        // uncovered part of a full destination when the source rect extends
        // beyond the source image. Preserve that backing before compositing.
        dest.fill(*src.scriptFillBackingColor());
    }

    const bool preserve_palette_indices = imageCanPreservePaletteIndices(
        dest, copy_src, src_palette_indices, effective_ink, blend, mask, color_remap, bg_color_remap);
    const bool refresh_preserved_background_transparent_indices =
        preserve_palette_indices &&
        effective_ink == id::InkMode::BACKGROUND_TRANSPARENT &&
        imageCanRefreshDestinationPaletteIndices(dest);
    const bool refresh_dest_palette_indices =
        !preserve_palette_indices && imageCanRefreshDestinationPaletteIndices(dest);
    std::optional<std::vector<std::uint8_t>> dest_palette_indices;
    if (preserve_palette_indices || refresh_dest_palette_indices) {
        dest_palette_indices = dest.paletteIndices().value_or(
            std::vector<std::uint8_t>(dest.pixels().size(), 0));
    } else {
        dest.clearPaletteIndices();
    }

    for (int dy = 0; dy < dest_height; ++dy) {
        const int sy = src_rect->top + (dy * src_height / dest_height);
        const int py = dest_rect->top + dy;
        if (sy < 0 || sy >= copy_src.height() || py < 0 || py >= dest.height()) {
            continue;
        }
        for (int dx = 0; dx < dest_width; ++dx) {
            const int sx = src_rect->left + (dx * src_width / dest_width);
            const int px = dest_rect->left + dx;
            if (sx < 0 || sx >= copy_src.width() || px < 0 || px >= dest.width()) {
                continue;
            }
            if (mask != nullptr && !bitmap::drawing::maskAllowsPixel(*mask, sx, sy)) {
                continue;
            }
            const auto raw_source_pixel = copy_src.getPixel(sx, sy);
            const auto src_offset = static_cast<std::size_t>(sy * copy_src.width() + sx);
            const bool skip_source =
                (key_near_white_matte && imageIsNearWhiteMattePixel(raw_source_pixel)) ||
                (key_default_indexed_matte &&
                 src_palette_indices.has_value() &&
                 src_offset < src_palette_indices->size() &&
                 static_cast<int>((*src_palette_indices)[src_offset]) == 0);
            std::uint32_t source_pixel = raw_source_pixel;
            if (!skip_source) {
                if (inverse_white_alpha_mask) {
                    source_pixel = imageInvertWhiteAlphaMaskPixel(raw_source_pixel, inverse_white_alpha_mask_ink_rgb);
                } else if (white_text_background_transparent) {
                    source_pixel = imageMakeWhiteTextBackgroundTransparentPixel(raw_source_pixel);
                } else if (apply_grayscale_remap) {
                    if (darken_bg_tint) {
                        std::optional<int> indexed_shade;
                        if (indexed_shade_for_darken) {
                            const int r = static_cast<int>((raw_source_pixel >> 16) & 0xFFU);
                            indexed_shade = imageShadeForDarken(copy_src, src_palette_indices, sx, sy, r, true);
                        }
                        const bool preserve_source_channels = indexed_shade_for_darken &&
                            imageMatchingPalettePixel(copy_src, src_palette_indices, sx, sy).has_value();
                        source_pixel = imageDarkenBgTintPixel(
                            raw_source_pixel, *bg_color_remap, indexed_shade, preserve_source_channels);
                    } else {
                        source_pixel = imageRemapGrayscalePixel(raw_source_pixel, color_remap, bg_color_remap);
                    }
                } else if (ink == id::InkMode::DARKEN) {
                    source_pixel = imageMultiplyDarkenPixel(
                        raw_source_pixel,
                        bg_color_remap.value_or(0xFFFFFF),
                        copy_src,
                        src_palette_indices,
                        sx,
                        sy,
                        indexed_shade_for_darken);
                }
            }
            const auto current_dest_pixel = dest.getPixel(px, py);
            const auto dest_pixel = skip_source
                ? current_dest_pixel
                : bitmap::drawing::applyInk(source_pixel, current_dest_pixel, effective_ink, blend, background_key_rgb);
            dest.setPixelPreservePaletteIndex(px, py, dest_pixel);
            const auto dest_offset = static_cast<std::size_t>(py * dest.width() + px);
            if (preserve_palette_indices && dest_palette_indices.has_value() && src_palette_indices.has_value()) {
                if (!skip_source && !imageShouldSkipPaletteIndexPreserve(source_pixel, effective_ink, background_key_rgb)) {
                    if (src_offset < src_palette_indices->size() && dest_offset < dest_palette_indices->size()) {
                        const int palette_index = imageConsistentSourcePaletteIndex(
                            copy_src, *src_palette_indices, src_offset, source_pixel);
                        (*dest_palette_indices)[dest_offset] = static_cast<std::uint8_t>(palette_index & 0xFF);
                    }
                }
                if (refresh_preserved_background_transparent_indices && dest_offset < dest_palette_indices->size()) {
                    const auto alpha = (dest_pixel >> 24) & 0xFFU;
                    const auto palette = dest.imagePalette();
                    if (alpha != 0 && palette != nullptr) {
                        const int previous_index = static_cast<int>((*dest_palette_indices)[dest_offset]);
                        const int palette_index = imageCopiedWhiteCanKeepMatteIndex(*palette, dest_pixel, previous_index)
                            ? previous_index
                            : imageNearestCopiedRgbPaletteIndex(*palette, dest_pixel);
                        (*dest_palette_indices)[dest_offset] = static_cast<std::uint8_t>(palette_index & 0xFF);
                        dest.setPixelPreservePaletteIndex(
                            px,
                            py,
                            (dest_pixel & 0xFF000000U) | (palette->getColor(palette_index) & 0x00FFFFFFU));
                    }
                }
            } else if (refresh_dest_palette_indices && dest_palette_indices.has_value() &&
                       dest_offset < dest_palette_indices->size()) {
                const auto alpha = (dest_pixel >> 24) & 0xFFU;
                const auto palette = dest.imagePalette();
                if (alpha != 0 && palette != nullptr) {
                    const int previous_index = static_cast<int>((*dest_palette_indices)[dest_offset]);
                    const int palette_index = imageCopiedWhiteCanKeepMatteIndex(*palette, dest_pixel, previous_index)
                        ? previous_index
                        : imageNearestCopiedRgbPaletteIndex(*palette, dest_pixel);
                    (*dest_palette_indices)[dest_offset] = static_cast<std::uint8_t>(palette_index & 0xFF);
                    dest.setPixelPreservePaletteIndex(
                        px,
                        py,
                        (dest_pixel & 0xFF000000U) | (palette->getColor(palette_index) & 0x00FFFFFFU));
                }
            }
        }
    }

    if (dest_palette_indices.has_value()) {
        dest.setPaletteIndices(std::move(*dest_palette_indices));
    }
    const bool text_rendering_source =
        src.isTextRendered() || (mask != nullptr && mask->isTextRendered());
    if (text_rendering_source &&
        ((destination_is_fully_replaced && source_is_fully_consumed) || !dest.hasScriptFillBacking())) {
        // Text wrappers repeatedly copy a rendered text member through
        // intermediate images before the field reaches its window buffer.
        // Keep the provenance while the full destination is being replaced so
        // the final background-transparent copy can key the text matte.
        dest.markTextRendered();
    }
    const bool composited_bitmap_source =
        !src.isTextRendered() && (src.hasNativeMatteAlpha() || src.isScriptModified());
    if (ink == id::InkMode::BACKGROUND_TRANSPARENT &&
        dest.hasScriptFillBacking() &&
        (composited_bitmap_source ||
         (src.isRectangularMedia() &&
          !src.isTextRendered() &&
          source_rect_extends_past_source &&
          imageCopyLooksLikeSparseViewportContent(src, *dest_rect, *src_rect))) &&
        mask == nullptr &&
        !color_remap.has_value() &&
        !bg_color_remap.has_value()) {
        dest.markPreserveScriptFillBacking();
    }
    return Datum::voidValue();
}

Datum imageObjectMethod(const Datum::ImageRef& image, std::string_view method_name, const std::vector<Datum>& args) {
    if (image.bitmap == nullptr) {
        if (equalsIgnoreCase(method_name, "duplicate")) {
            return Datum::imageRef(nullptr);
        }
        if (equalsIgnoreCase(method_name, "getAt")) {
            if (args.empty()) return Datum::voidValue();
            const int index = toIntLikeJava(args[0]);
            return index == 1 || index == 2 ? Datum::of(0) : Datum::voidValue();
        }
        return Datum::voidValue();
    }

    auto& bmp = *image.bitmap;
    if (equalsIgnoreCase(method_name, "getAt") && args.size() >= 2) {
        std::string property_storage;
        const auto property_name = keyNameLikeJavaView(args[0], property_storage);
        Datum property = getImageProp(image, property_name);
        if (auto* rect = property.asIntRect()) {
            return rectObjectMethod(*rect, "getAt", std::span<const Datum>(args.data() + 1, 1));
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "getProp") ||
        equalsIgnoreCase(method_name, "getPropRef") ||
        equalsIgnoreCase(method_name, "getAProp")) {
        if (args.empty()) {
            return Datum::voidValue();
        }
        std::string property_storage;
        const auto property_name = keyNameLikeJavaView(args[0], property_storage);
        Datum property = getImageProp(image, property_name);
        if (args.size() == 1) {
            return property;
        }
        if (auto* rect = property.asIntRect()) {
            return rectObjectMethod(*rect, "getAt", std::span<const Datum>(args.data() + 1, 1));
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "fill")) {
        if (imageFill(bmp, args)) {
            notifyImageMutation(image);
        }
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "setAlpha")) {
        notifyImageMutation(image);
        return imageSetAlpha(bmp, args);
    }
    if (equalsIgnoreCase(method_name, "draw")) {
        notifyImageMutation(image);
        return imageDraw(bmp, args);
    }
    if (equalsIgnoreCase(method_name, "createMatte")) {
        const int alpha_threshold = !args.empty() && !args[0].isVoid() ? toIntLikeJava(args[0]) : 0;
        return Datum::imageRef(bitmap::drawing::createMatte(bmp, alpha_threshold));
    }
    if (equalsIgnoreCase(method_name, "createMask")) {
        const int alpha_threshold = !args.empty() && !args[0].isVoid() ? toIntLikeJava(args[0]) : 0;
        return Datum::imageRef(bitmap::drawing::createMask(bmp, alpha_threshold));
    }
    if (equalsIgnoreCase(method_name, "copyPixels")) {
        const auto* src_ref = args.size() >= 1 ? args[0].asImageRef() : nullptr;
        const auto* dest_rect = args.size() >= 2 ? args[1].asIntRect() : nullptr;
        const auto* src_rect = args.size() >= 3 ? args[2].asIntRect() : nullptr;
        int trace_ink = 0;
        if (args.size() >= 4 && args[3].isPropList()) {
            const Datum ink_datum = getPropListKey(args[3].propListValue(), "ink");
            if (!ink_datum.isVoid()) {
                trace_ink = toIntLikeJava(ink_datum);
            }
        }
        std::ostringstream trace_before;
        trace_before << "{\"id\":" << image_operation_trace_next_id++
                    << ",\"op\":\"copyPixels.before\",\"ink\":" << trace_ink
                    << ",\"destRect\":";
        appendTraceRectJson(trace_before, dest_rect);
        trace_before << ",\"srcRect\":";
        appendTraceRectJson(trace_before, src_rect);
        appendTraceBitmapSummary(trace_before, "dest", bmp);
        if (src_ref != nullptr && src_ref->bitmap != nullptr) {
            appendTraceBitmapSummary(trace_before, "src", *src_ref->bitmap);
        }
        trace_before << '}';
        pushImageOperationTrace(trace_before.str());
        notifyImageMutation(bmp);
        auto result = imageCopyPixels(bmp, args);
        if (image.mutation_callback) {
            image.mutation_callback(bmp);
        }
        std::ostringstream trace_after;
        trace_after << "{\"id\":" << image_operation_trace_next_id++
                   << ",\"op\":\"copyPixels.after\",\"ink\":" << trace_ink
                   << ",\"destRect\":";
        appendTraceRectJson(trace_after, dest_rect);
        trace_after << ",\"srcRect\":";
        appendTraceRectJson(trace_after, src_rect);
        appendTraceBitmapSummary(trace_after, "dest", bmp);
        trace_after << '}';
        pushImageOperationTrace(trace_after.str());
        return result;
    }
    if (equalsIgnoreCase(method_name, "duplicate")) {
        return Datum::imageRef(std::make_shared<bitmap::Bitmap>(bmp.copy()));
    }
    if (equalsIgnoreCase(method_name, "crop")) {
        if (args.empty()) return Datum::voidValue();
        const auto* rect = args[0].asIntRect();
        if (rect == nullptr) return Datum::voidValue();
        const int width = rect->right - rect->left;
        const int height = rect->bottom - rect->top;
        if (width <= 0 || height <= 0) return Datum::voidValue();
        return Datum::imageRef(std::make_shared<bitmap::Bitmap>(bmp.getRegion(rect->left, rect->top, width, height)));
    }
    if (equalsIgnoreCase(method_name, "trimWhiteSpace")) {
        const auto bounds = bmp.trimWhiteSpace();
        std::ostringstream trace;
        trace << "{\"id\":" << image_operation_trace_next_id++
              << ",\"op\":\"trimWhiteSpace\",\"bounds\":";
        const Datum::IntRect bounds_rect{bounds.left, bounds.top, bounds.right, bounds.bottom};
        appendTraceRectJson(trace, &bounds_rect);
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
    if (equalsIgnoreCase(method_name, "getAt")) {
        if (args.empty()) return Datum::voidValue();
        const int index = toIntLikeJava(args[0]);
        if (index == 1) return Datum::of(bmp.width());
        if (index == 2) return Datum::of(bmp.height());
        return Datum::voidValue();
    }
    if (equalsIgnoreCase(method_name, "getPixel")) {
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
    if (equalsIgnoreCase(method_name, "setPixel")) {
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
                          const Datum::CastLibRef& cast_lib,
                          std::string_view method_name,
                          std::span<const Datum> args) {
    std::string prop_name_storage;
    if ((!equalsIgnoreCase(method_name, "getProp") && !equalsIgnoreCase(method_name, "getPropRef")) || args.size() < 2 ||
        !equalsIgnoreCase(keyNameLikeJavaView(args[0], prop_name_storage), "member")) {
        return Datum::voidValue();
    }

    auto* builtin_context = context.builtinContext();
    if (builtin_context == nullptr) {
        return Datum::voidValue();
    }

    const Datum& key = args[1];
    if (key.isInt() || key.isFloat()) {
        return builtin_context->cast_member_resolver ? builtin_context->cast_member_resolver(cast_lib.cast_lib, toIntLikeJava(key))
                                                  : Datum::voidValue();
    }
    return builtin_context->cast_member_name_resolver ? builtin_context->cast_member_name_resolver(cast_lib.cast_lib, toStringLikeJava(key))
                                                  : Datum::voidValue();
}

Datum castLibMemberAccessorObjectMethod(ExecutionContext& context,
                                        const Datum::CastLibMemberAccessor& accessor,
                                        std::string_view method_name,
                                        std::span<const Datum> args) {
    if (args.empty() ||
        (!equalsIgnoreCase(method_name, "getAt") && !equalsIgnoreCase(method_name, "getProp") &&
         !equalsIgnoreCase(method_name, "getPropRef"))) {
        return Datum::voidValue();
    }
    return getCastLibMemberAccessorValue(context, accessor, args[0]);
}

Datum castMemberObjectMethod(ExecutionContext& context,
                             const Datum::CastMemberRef& member,
                             std::string_view method_name,
                             const std::vector<Datum>& args) {
    auto* builtin_context = context.builtinContext();
    if (builtin_context == nullptr || !builtin_context->cast_member_method_handler) {
        return Datum::voidValue();
    }
    return builtin_context->cast_member_method_handler(member.cast_lib, member.memberNum(), std::string(method_name), args);
}

Datum spriteObjectMethod(ExecutionContext& context,
                         const Datum::SpriteRef& sprite,
                         std::string_view method_name,
                         const std::vector<Datum>& args) {
    auto* builtin_context = context.builtinContext();
    if (builtin_context == nullptr) {
        return Datum::voidValue();
    }

    if (builtin_context->sprite_properties != nullptr) {
        auto* scripts = builtin_context->sprite_properties->mutableScriptInstanceList(sprite.channel);
        if (scripts != nullptr) {
            for (auto& script_instance : *scripts) {
                if (script_instance.type() != DatumType::ScriptInstanceRef) {
                    continue;
                }
                Datum result = dispatch::ScriptInstanceMethodDispatcher::dispatch(
                    context,
                    script_instance,
                    method_name,
                    args);
                if (!result.isVoid()) {
                    return result;
                }
            }
        }
    }

    if (!builtin_context->sprite_method_handler) {
        return Datum::voidValue();
    }
    return builtin_context->sprite_method_handler(sprite.channel, std::string(method_name), args);
}

Datum getContextVar(ExecutionContext& context,
                    id::VarType var_type,
                    const Datum& id_datum,
                    const std::optional<Datum>& field_cast_id_datum = std::nullopt);
void setContextVar(ExecutionContext& context,
                   id::VarType var_type,
                   const Datum& id_datum,
                   Datum value,
                   const std::optional<Datum>& field_cast_id_datum = std::nullopt);

Datum chunkRefObjectMethod(ExecutionContext& context,
                           const Datum::ChunkRef& chunk_ref,
                           std::string_view method_name,
                           const std::vector<Datum>&) {
    if (!equalsIgnoreCase(method_name, "delete")) {
        return Datum::voidValue();
    }

    const Datum current = getContextVar(context, chunk_ref.var_type, Datum::of(chunk_ref.raw_index));
    std::string current_storage;
    const std::string_view current_string = stringViewLikeJava(current, current_storage);
    std::string new_value;

    if (chunk_ref.chunk_type == StringChunkType::Char) {
        new_value = deleteCharChunkRefValue(current_string, chunk_ref.start, chunk_ref.end);
    } else {
        int first = chunk_ref.start;
        int last = chunk_ref.end;
        const char item_delimiter = currentItemDelimiter(context);
        if (first < 0 || last < 0) {
            const int count = countChunks(current_string, chunk_ref.chunk_type, item_delimiter);
            if (first < 0) {
                first = count;
            }
            if (last < 0) {
                last = count;
            }
        }
        new_value = deleteChunkValue(current_string, chunk_ref.chunk_type, first, last, item_delimiter);
    }

    setContextVar(context,
                  chunk_ref.var_type,
                  Datum::of(chunk_ref.raw_index),
                  Datum::of(std::move(new_value)));
    return Datum::voidValue();
}

Datum scriptRefObjectMethod(ExecutionContext& context,
                            const Datum::ScriptRef& script_ref,
                            std::string_view method_name,
                            const std::vector<Datum>& args) {
    if (!equalsIgnoreCase(method_name, "new")) {
        return Datum::voidValue();
    }

    std::vector<Datum> full_args;
    full_args.reserve(args.size() + 1);
    full_args.push_back(Datum::scriptRef(script_ref.member_ref));
    full_args.insert(full_args.end(), args.begin(), args.end());
    if (auto result = context.invokeBuiltinIfPresent("new", full_args)) {
        return std::move(*result);
    }
    return Datum::voidValue();
}

Datum varRefObjectMethod(ExecutionContext& context,
                         const Datum::VarRef& var_ref,
                         std::string_view method_name,
                         std::span<const Datum> args) {
    Datum value = getContextVar(context, var_ref.var_type, Datum::of(var_ref.raw_index));
    if (equalsIgnoreCase(method_name, "getProp")) {
        if (args.size() < 2) {
            return Datum::of(std::string());
        }
        StringChunkType chunk_type = StringChunkType::Char;
        std::string chunk_name_storage;
        const auto chunk_type_value = stringChunkTypeByNameNoThrow(keyNameLikeJavaView(args[0], chunk_name_storage));
        if (!chunk_type_value.has_value()) {
            chunk_type = StringChunkType::Char;
        } else {
            chunk_type = *chunk_type_value;
        }
        const int start = toIntLikeJava(args[1]);
        const int end = args.size() >= 3 ? toIntLikeJava(args[2]) : start;
        std::string value_storage;
        const std::string_view value_text = stringViewLikeJava(value, value_storage);
        if (chunk_type == StringChunkType::Char) {
            return Datum::of(getCharChunkRefValue(value_text, start, end));
        }
        return Datum::of(resolveChunkRange(value_text, chunk_type, start, end, currentItemDelimiter(context)));
    }
    if (equalsIgnoreCase(method_name, "getPropRef")) {
        if (args.size() < 2) {
            return Datum::voidValue();
        }
        StringChunkType chunk_type = StringChunkType::Char;
        std::string chunk_name_storage;
        const auto chunk_type_value = stringChunkTypeByNameNoThrow(keyNameLikeJavaView(args[0], chunk_name_storage));
        if (!chunk_type_value.has_value()) {
            chunk_type = StringChunkType::Char;
        } else {
            chunk_type = *chunk_type_value;
        }
        const int start = toIntLikeJava(args[1]);
        const int end = args.size() >= 3 ? toIntLikeJava(args[2]) : start;
        return Datum::chunkRef(var_ref.var_type, var_ref.raw_index, chunk_type, start, end);
    }
    if (value.isList()) {
        return dispatch::ListMethodDispatcher::dispatch(value.listValue(), method_name, args);
    }
    if (value.isPropList()) {
        return dispatch::PropListMethodDispatcher::dispatch(value.propListValue(), method_name, args);
    }
    if (value.isString()) {
        std::string value_storage;
        return dispatch::StringMethodDispatcher::dispatch(stringViewLikeJava(value, value_storage),
                                                          method_name,
                                                          args,
                                                          currentItemDelimiter(context));
    }
    if (auto* point = value.asIntPoint()) {
        return pointObjectMethod(*point, method_name, args);
    }
    if (auto* rect = value.asIntRect()) {
        return rectObjectMethod(*rect, method_name, args);
    }
    if (value.type() == DatumType::ScriptInstanceRef) {
        return scriptInstanceObjectMethod(context, value, method_name, args);
    }
    return Datum::voidValue();
}

Datum dispatchObjectMethod(ExecutionContext& context, Datum target, std::string_view method_name, const std::vector<Datum>& args) {
    if (const auto* var_ref = target.asVarRef()) {
        return varRefObjectMethod(context, *var_ref, method_name, args);
    }
    if (const auto* chunk_ref = target.asChunkRef()) {
        return chunkRefObjectMethod(context, *chunk_ref, method_name, args);
    }
    if (const auto* script_ref = target.asScriptRef()) {
        return scriptRefObjectMethod(context, *script_ref, method_name, args);
    }
    auto* builtin_context = context.builtinContext();
    if (const auto* timeout = target.asTimeoutRef()) {
        return builtin_context != nullptr ? builtin::TimeoutBuiltins::handleMethod(*builtin_context, *timeout, method_name, args)
                                         : Datum::voidValue();
    }
    if (const auto* sound_channel = target.asSoundChannel()) {
        return dispatch::SoundChannelMethodDispatcher::dispatch(builtin_context, *sound_channel, method_name, args);
    }
    if (const auto* xtra_instance = target.asXtraInstance()) {
        return builtin_context != nullptr ? builtin::XtraBuiltins::callHandler(*builtin_context, *xtra_instance, method_name, args)
                                         : Datum::voidValue();
    }
    if (const auto* sprite = target.asSpriteRef()) {
        return spriteObjectMethod(context, *sprite, method_name, args);
    }
    if (const auto* cast_lib = target.asCastLibRef()) {
        return castLibObjectMethod(context, *cast_lib, method_name, args);
    }
    if (const auto* accessor = target.asCastLibMemberAccessor()) {
        return castLibMemberAccessorObjectMethod(context, *accessor, method_name, args);
    }
    if (target.isList()) {
        return dispatch::ListMethodDispatcher::dispatch(target.listValue(), method_name, args);
    }
    if (target.isPropList()) {
        return dispatch::PropListMethodDispatcher::dispatch(target.propListValue(), method_name, args);
    }
    if (target.isString()) {
        std::string target_storage;
        return dispatch::StringMethodDispatcher::dispatch(stringViewLikeJava(target, target_storage),
                                                          method_name,
                                                          args,
                                                          currentItemDelimiter(context));
    }
    if (auto* point = target.asIntPoint()) {
        return pointObjectMethod(*point, method_name, args);
    }
    if (auto* rect = target.asIntRect()) {
        return rectObjectMethod(*rect, method_name, args);
    }
    if (const auto* image = target.asImageRef()) {
        return dispatch::ImageMethodDispatcher::dispatch(*image, method_name, args);
    }
    if (const auto* member = target.asCastMemberRef()) {
        return castMemberObjectMethod(context, *member, method_name, args);
    }
    if (target.type() == DatumType::ScriptInstanceRef) {
        return dispatch::ScriptInstanceMethodDispatcher::dispatch(context, target, method_name, args);
    }
    if (target.type() == DatumType::MovieRef) {
        if (auto builtin_result = context.invokeBuiltinIfPresent(method_name, args)) {
            return std::move(*builtin_result);
        }
        return Datum::voidValue();
    }

    std::vector<Datum> full_args;
    full_args.reserve(args.size() + 1);
    full_args.push_back(std::move(target));
    full_args.insert(full_args.end(), args.begin(), args.end());
    if (auto builtin_result = context.invokeBuiltinIfPresent(method_name, full_args)) {
        return std::move(*builtin_result);
    }
    return Datum::voidValue();
}

Datum dispatchObjectMethodSpan(ExecutionContext& context,
                               Datum target,
                               std::string_view method_name,
                               std::span<const Datum> args) {
    if (const auto* var_ref = target.asVarRef()) {
        return varRefObjectMethod(context, *var_ref, method_name, args);
    }
    if (const auto* chunk_ref = target.asChunkRef()) {
        return chunkRefObjectMethod(context, *chunk_ref, method_name, emptyDatumArgs());
    }
    if (const auto* script_ref = target.asScriptRef()) {
        if (!equalsIgnoreCase(method_name, "new")) {
            return Datum::voidValue();
        }
        std::vector<Datum> full_args;
        full_args.reserve(args.size() + 1);
        full_args.push_back(Datum::scriptRef(script_ref->member_ref));
        full_args.insert(full_args.end(), args.begin(), args.end());
        if (auto result = context.invokeBuiltinIfPresent("new", full_args)) {
            return std::move(*result);
        }
        return Datum::voidValue();
    }
    if (target.isList()) {
        return dispatch::ListMethodDispatcher::dispatch(target.listValue(), method_name, args);
    }
    if (target.isPropList()) {
        return dispatch::PropListMethodDispatcher::dispatch(target.propListValue(), method_name, args);
    }
    if (target.isString()) {
        std::string target_storage;
        return dispatch::StringMethodDispatcher::dispatch(stringViewLikeJava(target, target_storage),
                                                          method_name,
                                                          args,
                                                          currentItemDelimiter(context));
    }
    if (auto* point = target.asIntPoint()) {
        return pointObjectMethod(*point, method_name, args);
    }
    if (auto* rect = target.asIntRect()) {
        return rectObjectMethod(*rect, method_name, args);
    }
    if (const auto* cast_lib = target.asCastLibRef()) {
        return castLibObjectMethod(context, *cast_lib, method_name, args);
    }
    if (const auto* accessor = target.asCastLibMemberAccessor()) {
        return castLibMemberAccessorObjectMethod(context, *accessor, method_name, args);
    }
    if (target.type() == DatumType::ScriptInstanceRef) {
        return scriptInstanceObjectMethod(context, target, method_name, args);
    }

    if (args.empty()) {
        return dispatchObjectMethod(context, std::move(target), method_name, emptyDatumArgs());
    }
    const std::vector<Datum> arg_vector(args.begin(), args.end());
    return dispatchObjectMethod(context, std::move(target), method_name, arg_vector);
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
    const int raw_index = toIntLikeJava(context.peekRef());
    context.scope().replaceTop(Datum::varRef(id::varTypeFromCode(context.argument()), raw_index));
    return true;
}

bool swap(ExecutionContext& context) {
    context.swap();
    return true;
}

bool pop(ExecutionContext& context) {
    context.scope().drop(context.argument() <= 0 ? 1 : context.argument());
    return true;
}

bool peek(ExecutionContext& context) {
    const Datum& value = context.peekRef(context.argument());
    if (const auto* int_value = value.asInt()) {
        context.push(Datum::of(int_value->value));
    } else if (const auto* float_value = value.asFloat()) {
        context.push(Datum::of(float_value->value));
    } else if (value.isVoid()) {
        context.push(Datum::voidValue());
    } else if (value.isNull()) {
        context.push(Datum::nullValue());
    } else {
        context.push(value);
    }
    return true;
}

Datum fallbackScriptInstance(const Datum& script_arg) {
    if (const auto* script_ref = script_arg.asScriptRef()) {
        return Datum::scriptInstance("script", script_ref->member_ref);
    }
    if (const auto* member_ref = script_arg.asCastMemberRef()) {
        return Datum::scriptInstance("script", *member_ref);
    }
    if (const auto* symbol = script_arg.asSymbol()) {
        return Datum::scriptInstance(symbol->name);
    }
    if (script_arg.isString()) {
        return Datum::scriptInstance(script_arg.stringValue());
    }
    return Datum::scriptInstance(toStringLikeJava(script_arg));
}

struct ScriptConstructorTarget {
    Datum target{Datum::voidValue()};
    std::size_t consumed_args{1};
};

std::optional<int> resolveScriptConstructorScope(builtin::BuiltinContext& builtin_context, const Datum& scope_arg) {
    if (const auto* cast_lib = scope_arg.asCastLibRef()) {
        return cast_lib->cast_lib >= 1 ? std::optional<int>(cast_lib->cast_lib) : std::nullopt;
    }
    if ((scope_arg.isString() || scope_arg.asSymbol() != nullptr) && builtin_context.cast_lib_name_resolver) {
        const int cast_lib = builtin_context.cast_lib_name_resolver(keyNameLikeJava(scope_arg));
        return cast_lib >= 1 ? std::optional<int>(cast_lib) : std::nullopt;
    }
    return std::nullopt;
}

Datum resolveNamedScriptConstructorTarget(builtin::BuiltinContext& builtin_context,
                                          const Datum& identifier,
                                          std::optional<int> scoped_cast_lib) {
    if (!builtin_context.cast_member_name_resolver || (!identifier.isString() && identifier.asSymbol() == nullptr)) {
        return Datum::voidValue();
    }
    const int cast_lib = scoped_cast_lib.value_or(0);
    const Datum member_ref = builtin_context.cast_member_name_resolver(cast_lib, keyNameLikeJava(identifier));
    if (const auto* member = member_ref.asCastMemberRef()) {
        return Datum::scriptRef(*member);
    }
    return Datum::voidValue();
}

ScriptConstructorTarget resolveScriptConstructorTarget(ExecutionContext& context, const std::vector<Datum>& args) {
    const Datum& script_arg = args.front();
    if (script_arg.asScriptRef() != nullptr || script_arg.asCastMemberRef() != nullptr) {
        return ScriptConstructorTarget{script_arg, 1};
    }
    auto* builtin_context = context.builtinContext();
    if (builtin_context != nullptr && args.size() >= 2) {
        if (const auto scoped_cast_lib = resolveScriptConstructorScope(*builtin_context, args[1])) {
            if (builtin_context->cast_member_name_resolver) {
                Datum scoped_resolved = resolveNamedScriptConstructorTarget(*builtin_context, script_arg, scoped_cast_lib);
                if (scoped_resolved.asScriptRef() != nullptr || scoped_resolved.asCastMemberRef() != nullptr) {
                    return ScriptConstructorTarget{std::move(scoped_resolved), 2};
                }
            }
            if ((args[1].asCastLibRef() != nullptr || args[1].isString() || args[1].asSymbol() != nullptr) &&
                builtin_context->script_resolver) {
                Datum resolved = builtin_context->script_resolver(script_arg, args[1]);
                if (resolved.asScriptRef() != nullptr || resolved.asCastMemberRef() != nullptr) {
                    return ScriptConstructorTarget{std::move(resolved), 2};
                }
            }
        }
    }
    if (builtin_context != nullptr) {
        Datum resolved = resolveNamedScriptConstructorTarget(*builtin_context, script_arg, std::nullopt);
        if (resolved.asScriptRef() != nullptr || resolved.asCastMemberRef() != nullptr) {
            return ScriptConstructorTarget{std::move(resolved), 1};
        }
    }
    if (builtin_context != nullptr && builtin_context->script_resolver) {
        Datum resolved = builtin_context->script_resolver(script_arg, std::nullopt);
        if (resolved.asScriptRef() != nullptr || resolved.asCastMemberRef() != nullptr) {
            return ScriptConstructorTarget{std::move(resolved), 1};
        }
    }
    return ScriptConstructorTarget{script_arg, 1};
}

std::optional<Datum::CastMemberRef> scriptConstructorMemberRef(const Datum& script_arg) {
    if (const auto* script_ref = script_arg.asScriptRef()) {
        return script_ref->member_ref;
    }
    if (const auto* member_ref = script_arg.asCastMemberRef()) {
        return *member_ref;
    }
    return std::nullopt;
}

bool shouldInvokeBuiltinNewForScriptConstructor(ExecutionContext& context, const std::vector<Datum>& args) {
    if (args.empty()) {
        return false;
    }
    auto* builtin_context = context.builtinContext();
    if (builtin_context != nullptr && builtin_context->new_instance_handler) {
        return true;
    }
    if (args.front().asXtra() != nullptr) {
        return true;
    }
    return args.front().asSymbol() != nullptr && args.size() > 1 && args[1].asCastLibRef() != nullptr;
}

std::uint64_t scriptPropertyCacheKey(int cast_lib, int member_num) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cast_lib)) << 32U) |
           static_cast<std::uint32_t>(member_num);
}

const std::vector<std::string>& declaredScriptPropertyNames(builtin::BuiltinContext& context,
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

void initializeDeclaredScriptProperties(ExecutionContext& context, Datum& instance, const Datum::CastMemberRef& member_ref) {
    auto* builtin_context = context.builtinContext();
    if (builtin_context == nullptr || !builtin_context->script_property_names_resolver ||
        instance.type() != DatumType::ScriptInstanceRef) {
        return;
    }
    const auto& property_names = declaredScriptPropertyNames(*builtin_context, member_ref);
    auto& script_instance = instance.scriptInstanceValue();
    script_instance.reserveLocalProperties(property_names.size());
    for (const auto& property_name : property_names) {
        script_instance.appendLocalProperty(property_name, Datum::voidValue());
    }
}

Datum executeScriptNewHandler(ExecutionContext& context, std::span<const Datum> constructor_args, Datum& receiver) {
    if (receiver.type() == DatumType::ScriptInstanceRef) {
        auto& instance = receiver.scriptInstanceValue();
        if (const auto handler = findScriptInstanceScriptHandler(context, instance, "new")) {
            return safeExecuteHandler(context, handlerRefFromLocation(*handler), constructor_args, receiver);
        }
    }

    const auto handler = context.findHandler("new");
    if (!handler) {
        return Datum::voidValue();
    }
    return safeExecuteHandler(context, *handler, constructor_args, receiver);
}

bool newObj(ExecutionContext& context) {
    const std::string& object_type = context.resolveNameRef(context.argument());
    if (!equalsIgnoreCase(object_type, "script")) {
        context.push(Datum::voidValue());
        return true;
    }

    const Datum arg_list_datum = context.pop();
    std::vector<Datum> arg_storage;
    const std::vector<Datum>& args = argListItemsRef(arg_list_datum, arg_storage);
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

    const ScriptConstructorTarget constructor_target = resolveScriptConstructorTarget(context, args);
    Datum instance = fallbackScriptInstance(constructor_target.target);
    if (const auto member_ref = scriptConstructorMemberRef(constructor_target.target)) {
        initializeDeclaredScriptProperties(context, instance, *member_ref);
        const auto constructor_offset = std::min(constructor_target.consumed_args, args.size());
        const std::span<const Datum> constructor_args(args.data() + constructor_offset, args.size() - constructor_offset);
        (void)executeScriptNewHandler(context, constructor_args, instance);
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

        const Datum& b = context.peekRef(0);
        const Datum& a = context.peekRef(1);
        if (!isSpecialArithmeticDatum(a) && !isSpecialArithmeticDatum(b)) {
            context.scope().replaceTopTwo(numericResult(a, b, toDoubleLikeJava(a) + toDoubleLikeJava(b)));
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
            result.emplace_back(numericResult(lhs[index], rhs[index], value));
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

        const Datum& b = context.peekRef(0);
        const Datum& a = context.peekRef(1);
        if (!isSpecialArithmeticDatum(a) && !isSpecialArithmeticDatum(b)) {
            context.scope().replaceTopTwo(numericResult(a, b, toDoubleLikeJava(a) - toDoubleLikeJava(b)));
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
            result.emplace_back(numericResult(lhs[index], rhs[index], value));
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

        const Datum& b = context.peekRef(0);
        const Datum& a = context.peekRef(1);
        if (!isSpecialArithmeticDatum(a) && !isSpecialArithmeticDatum(b)) {
            context.scope().replaceTopTwo(numericResult(a, b, toDoubleLikeJava(a) * toDoubleLikeJava(b)));
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
        const Datum& b = context.peekRef(0);
        const Datum& a = context.peekRef(1);
        const auto* bi = b.asInt();
        const auto* ai = a.asInt();
        if (ai != nullptr && bi != nullptr) {
            if (bi->value == 0) {
                throw context.error("Division by zero");
            }
            context.scope().replaceTopTwo(Datum::of(ai->value / bi->value));
            return true;
        }

        if (!isSpecialArithmeticDatum(a) && !isSpecialArithmeticDatum(b)) {
            const double divisor = toDoubleLikeJava(b);
            if (divisor == 0.0) {
                throw context.error("Division by zero");
            }
            context.scope().replaceTopTwo(numericResult(a, b, toDoubleLikeJava(a) / divisor));
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
        const Datum& b = context.peekRef(0);
        const Datum& a = context.peekRef(1);
        const auto* bi = b.asInt();
        const auto* ai = a.asInt();
        if (ai != nullptr && bi != nullptr) {
            if (bi->value == 0) {
                throw context.error("Modulo by zero");
            }
            context.scope().replaceTopTwo(Datum::of(ai->value % bi->value));
            return true;
        }

        if (!isSpecialArithmeticDatum(a) && !isSpecialArithmeticDatum(b)) {
            const int divisor = toIntLikeJava(b);
            if (divisor == 0) {
                throw context.error("Modulo by zero");
            }
            context.scope().replaceTopTwo(Datum::of(toIntLikeJava(a) % divisor));
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
    const Datum& value = context.peekRef();
    if (const auto* int_value = value.asInt()) {
        context.scope().replaceTop(Datum::of(-int_value->value));
        return true;
    }

    if (const auto* float_value = value.asFloat()) {
        context.scope().replaceTop(Datum::of(-float_value->value));
    } else if (const auto* point = value.asIntPoint()) {
        context.scope().replaceTop(Datum::intPoint(-point->x, -point->y));
    } else if (const auto* rect = value.asIntRect()) {
        context.scope().replaceTop(Datum::intRect(-rect->left, -rect->top, -rect->right, -rect->bottom));
    } else {
        context.scope().replaceTop(Datum::of(-toIntLikeJava(value)));
    }
    return true;
}

bool lt(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const Datum& b = context.peekRef(0);
        const Datum& a = context.peekRef(1);
        const auto* bi = b.asInt();
        const auto* ai = a.asInt();
        if (ai != nullptr && bi != nullptr) {
            context.scope().replaceTopTwo(ai->value < bi->value ? Datum::TRUE : Datum::FALSE);
            return true;
        }
        context.scope().replaceTopTwo(toDoubleLikeJava(a) < toDoubleLikeJava(b) ? Datum::TRUE : Datum::FALSE);
        return true;
    }

    const Datum b = context.pop();
    const Datum a = context.pop();
    context.push(toDoubleLikeJava(a) < toDoubleLikeJava(b) ? Datum::TRUE : Datum::FALSE);
    return true;
}

bool ltEq(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const Datum& b = context.peekRef(0);
        const Datum& a = context.peekRef(1);
        const auto* bi = b.asInt();
        const auto* ai = a.asInt();
        if (ai != nullptr && bi != nullptr) {
            context.scope().replaceTopTwo(ai->value <= bi->value ? Datum::TRUE : Datum::FALSE);
            return true;
        }
        context.scope().replaceTopTwo(toDoubleLikeJava(a) <= toDoubleLikeJava(b) ? Datum::TRUE : Datum::FALSE);
        return true;
    }

    const Datum b = context.pop();
    const Datum a = context.pop();
    context.push(toDoubleLikeJava(a) <= toDoubleLikeJava(b) ? Datum::TRUE : Datum::FALSE);
    return true;
}

bool gt(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const Datum& b = context.peekRef(0);
        const Datum& a = context.peekRef(1);
        const auto* bi = b.asInt();
        const auto* ai = a.asInt();
        if (ai != nullptr && bi != nullptr) {
            context.scope().replaceTopTwo(ai->value > bi->value ? Datum::TRUE : Datum::FALSE);
            return true;
        }
        context.scope().replaceTopTwo(toDoubleLikeJava(a) > toDoubleLikeJava(b) ? Datum::TRUE : Datum::FALSE);
        return true;
    }

    const Datum b = context.pop();
    const Datum a = context.pop();
    context.push(toDoubleLikeJava(a) > toDoubleLikeJava(b) ? Datum::TRUE : Datum::FALSE);
    return true;
}

bool gtEq(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const Datum& b = context.peekRef(0);
        const Datum& a = context.peekRef(1);
        const auto* bi = b.asInt();
        const auto* ai = a.asInt();
        if (ai != nullptr && bi != nullptr) {
            context.scope().replaceTopTwo(ai->value >= bi->value ? Datum::TRUE : Datum::FALSE);
            return true;
        }
        context.scope().replaceTopTwo(toDoubleLikeJava(a) >= toDoubleLikeJava(b) ? Datum::TRUE : Datum::FALSE);
        return true;
    }

    const Datum b = context.pop();
    const Datum a = context.pop();
    context.push(toDoubleLikeJava(a) >= toDoubleLikeJava(b) ? Datum::TRUE : Datum::FALSE);
    return true;
}

bool eq(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const Datum& b = context.peekRef(0);
        const Datum& a = context.peekRef(1);
        const auto* bi = b.asInt();
        const auto* ai = a.asInt();
        if (ai != nullptr && bi != nullptr) {
            context.scope().replaceTopTwo(ai->value == bi->value ? Datum::TRUE : Datum::FALSE);
            return true;
        }
        const bool result = lingoEquals(a, b);
        context.scope().replaceTopTwo(result ? Datum::TRUE : Datum::FALSE);
        return true;
    }

    const Datum b = context.pop();
    const Datum a = context.pop();
    context.push(lingoEquals(a, b) ? Datum::TRUE : Datum::FALSE);
    return true;
}

bool notEq(ExecutionContext& context) {
    if (context.scope().stackSize() >= 2) {
        const Datum& b = context.peekRef(0);
        const Datum& a = context.peekRef(1);
        const auto* bi = b.asInt();
        const auto* ai = a.asInt();
        if (ai != nullptr && bi != nullptr) {
            context.scope().replaceTopTwo(ai->value != bi->value ? Datum::TRUE : Datum::FALSE);
            return true;
        }
        const bool result = !lingoEquals(a, b);
        context.scope().replaceTopTwo(result ? Datum::TRUE : Datum::FALSE);
        return true;
    }

    const Datum b = context.pop();
    const Datum a = context.pop();
    context.push(!lingoEquals(a, b) ? Datum::TRUE : Datum::FALSE);
    return true;
}

std::optional<int> spriteChannelFromDatum(const Datum& datum) {
    if (const auto* sprite_ref = datum.asSpriteRef()) {
        return sprite_ref->spriteNum();
    }
    if (datum.asInt() != nullptr || datum.asFloat() != nullptr) {
        return toIntLikeJava(datum);
    }
    return std::nullopt;
}

bool spriteCollision(ExecutionContext& context, std::string_view method_name) {
    const Datum second = context.pop();
    const Datum first = context.pop();
    const auto first_channel = spriteChannelFromDatum(first);
    auto* builtin_context = context.builtinContext();
    if (!first_channel.has_value() || *first_channel <= 0 || builtin_context == nullptr) {
        context.push(Datum::FALSE);
        return true;
    }

    Datum result = Datum::voidValue();
    if (builtin_context->sprite_method_handler) {
        result = builtin_context->sprite_method_handler(*first_channel, std::string(method_name), {second});
    } else if (builtin_context->sprite_properties != nullptr) {
        result = builtin_context->sprite_properties->callSpriteMethod(*first_channel, method_name, {second});
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
    std::string a_storage;
    std::string b_storage;
    const std::string_view a_string = stringViewLikeJava(a, a_storage);
    const std::string_view b_string = stringViewLikeJava(b, b_storage);
    if (a_string.empty()) {
        context.push(b.asString() != nullptr ? std::move(b) : Datum::of(std::string(b_string)));
        return true;
    }
    if (b_string.empty()) {
        context.push(a.asString() != nullptr ? std::move(a) : Datum::of(std::string(a_string)));
        return true;
    }
    std::string result;
    result.reserve(a_string.size() + b_string.size());
    result.append(a_string);
    result.append(b_string);
    context.push(Datum::of(std::move(result)));
    return true;
}

bool joinPadStr(ExecutionContext& context) {
    Datum b = context.pop();
    Datum a = context.pop();
    std::string a_storage;
    std::string b_storage;
    const std::string_view a_string = stringViewLikeJava(a, a_storage);
    const std::string_view b_string = stringViewLikeJava(b, b_storage);
    if (a_string.empty()) {
        context.push(b.asString() != nullptr ? std::move(b) : Datum::of(std::string(b_string)));
        return true;
    }
    if (b_string.empty()) {
        context.push(a.asString() != nullptr ? std::move(a) : Datum::of(std::string(a_string)));
        return true;
    }
    std::string result;
    result.reserve(a_string.size() + 1 + b_string.size());
    result.append(a_string);
    result.push_back(' ');
    result.append(b_string);
    context.push(Datum::of(std::move(result)));
    return true;
}

bool containsStr(ExecutionContext& context) {
    const Datum& needle = context.peekRef(0);
    const Datum& haystack = context.peekRef(1);
    std::string needle_storage;
    std::string haystack_storage;
    const bool result = containsIgnoreCase(stringViewLikeJava(haystack, haystack_storage),
                                           stringViewLikeJava(needle, needle_storage));
    context.scope().replaceTopTwo(result ? Datum::TRUE : Datum::FALSE);
    return true;
}

bool contains0Str(ExecutionContext& context) {
    const Datum& needle = context.peekRef(0);
    const Datum& haystack = context.peekRef(1);
    if (haystack.isVoid()) {
        context.scope().replaceTopTwo(Datum::FALSE);
        return true;
    }
    std::string needle_storage;
    std::string haystack_storage;
    const bool result = startsWithIgnoreCase(stringViewLikeJava(haystack, haystack_storage),
                                             stringViewLikeJava(needle, needle_storage));
    context.scope().replaceTopTwo(result ? Datum::TRUE : Datum::FALSE);
    return true;
}

bool getChunk(ExecutionContext& context) {
    const int last_line = toIntLikeJava(context.peekRef(1));
    const int first_line = toIntLikeJava(context.peekRef(2));
    const int last_item = toIntLikeJava(context.peekRef(3));
    const int first_item = toIntLikeJava(context.peekRef(4));
    const int last_word = toIntLikeJava(context.peekRef(5));
    const int first_word = toIntLikeJava(context.peekRef(6));
    const int last_char = toIntLikeJava(context.peekRef(7));
    const int first_char = toIntLikeJava(context.peekRef(8));

    std::string value_storage;
    const std::string_view value = stringViewLikeJava(context.peekRef(0), value_storage);
    Datum result_datum;
    if (first_char != 0 && last_char == 0 &&
        first_word == 0 && last_word == 0 &&
        first_item == 0 && last_item == 0 &&
        first_line == 0 && last_line == 0) {
        const int resolved_char = first_char < 0 ? static_cast<int>(value.size()) : first_char;
        const int index = resolved_char - 1;
        if (index >= 0 && index < static_cast<int>(value.size())) {
            result_datum = Datum::of(std::string(1, value[static_cast<std::size_t>(index)]));
        } else {
            result_datum = Datum::of(std::string());
        }
        context.scope().drop(9);
        context.push(std::move(result_datum));
        return true;
    }

    if (first_char != 0 && last_char != 0 &&
        first_word == 0 && last_word == 0 &&
        first_item == 0 && last_item == 0 &&
        first_line == 0 && last_line == 0) {
        const int resolved_first = first_char < 0 ? static_cast<int>(value.size()) : first_char;
        const int resolved_last = last_char < 0 ? static_cast<int>(value.size()) : last_char;
        const int start = resolved_first - 1;
        const int end = std::min(resolved_last, static_cast<int>(value.size()));
        if (start >= 0 && start < static_cast<int>(value.size()) && end > start) {
            result_datum = Datum::of(std::string(value.substr(static_cast<std::size_t>(start),
                                                             static_cast<std::size_t>(end - start))));
        } else {
            result_datum = Datum::of(std::string());
        }
        context.scope().drop(9);
        context.push(std::move(result_datum));
        return true;
    }

    const char item_delimiter = currentItemDelimiter(context);
    std::string result_storage;
    std::string_view result = value;
    if (first_line != 0 || last_line != 0) {
        result_storage = resolveChunkRange(result, StringChunkType::Line, first_line, last_line, item_delimiter);
        result = result_storage;
    }
    if (first_item != 0 || last_item != 0) {
        result_storage = resolveChunkRange(result, StringChunkType::Item, first_item, last_item, item_delimiter);
        result = result_storage;
    }
    if (first_word != 0 || last_word != 0) {
        result_storage = resolveChunkRange(result, StringChunkType::Word, first_word, last_word, item_delimiter);
        result = result_storage;
    }
    if (first_char != 0 || last_char != 0) {
        result_storage = resolveChunkRange(result, StringChunkType::Char, first_char, last_char, item_delimiter);
        result = result_storage;
    }
    result_datum = Datum::of(std::string(result));
    context.scope().drop(9);
    context.push(std::move(result_datum));
    return true;
}

Datum normalizedFieldIdentifier(const Datum& id_datum) {
    if (id_datum.isString() || id_datum.isInt()) {
        return id_datum;
    }
    return Datum::of(toStringLikeJava(id_datum));
}

Datum getContextVar(ExecutionContext& context,
                    id::VarType var_type,
                    const Datum& id_datum,
                    const std::optional<Datum>& field_cast_id_datum) {
    switch (var_type) {
        case id::VarType::LOCAL:
            return context.getLocal(toIntLikeJava(id_datum) / context.variableMultiplier());
        case id::VarType::PARAM:
            return context.getParam(toIntLikeJava(id_datum) / context.variableMultiplier());
        case id::VarType::GLOBAL:
        case id::VarType::GLOBAL2:
            return context.getGlobal(context.resolveNameRef(toIntLikeJava(id_datum)));
        case id::VarType::PROPERTY: {
            const Datum& receiver = context.scope().receiver();
            if (receiver.type() == DatumType::ScriptInstanceRef) {
                return util::getProperty(receiver.scriptInstanceValue(), context.resolveNameRef(toIntLikeJava(id_datum)));
            }
            return Datum::voidValue();
        }
        case id::VarType::FIELD: {
            auto* builtin_context = context.builtinContext();
            if (builtin_context != nullptr && builtin_context->field_resolver) {
                const int cast_id = field_cast_id_datum ? toIntLikeJava(*field_cast_id_datum) : 0;
                return builtin_context->field_resolver(normalizedFieldIdentifier(id_datum), cast_id);
            }
            return Datum::of(std::string());
        }
    }
    return Datum::voidValue();
}

void setContextVar(ExecutionContext& context,
                   id::VarType var_type,
                   const Datum& id_datum,
                   Datum value,
                   const std::optional<Datum>& field_cast_id_datum) {
    switch (var_type) {
        case id::VarType::LOCAL:
            context.setLocal(toIntLikeJava(id_datum) / context.variableMultiplier(), std::move(value));
            return;
        case id::VarType::PARAM:
            context.setParam(toIntLikeJava(id_datum) / context.variableMultiplier(), std::move(value));
            return;
        case id::VarType::GLOBAL:
        case id::VarType::GLOBAL2:
            context.setGlobal(context.resolveNameRef(toIntLikeJava(id_datum)), std::move(value));
            return;
        case id::VarType::PROPERTY: {
            const Datum& receiver = context.scope().receiver();
            if (receiver.type() == DatumType::ScriptInstanceRef) {
                const std::string& prop_name = context.resolveNameRef(toIntLikeJava(id_datum));
                auto instance = receiver.scriptInstancePtr();
                if (!context.hasVariableSetListener()) {
                    util::setProperty(*instance, prop_name, std::move(value));
                    return;
                }
                const Datum traced_value = value;
                util::setProperty(*instance, prop_name, std::move(value));
                context.tracePropertySet(prop_name, traced_value);
            }
            return;
        }
        case id::VarType::FIELD: {
            auto* builtin_context = context.builtinContext();
            if (builtin_context != nullptr && builtin_context->field_setter) {
                const int cast_id = field_cast_id_datum ? toIntLikeJava(*field_cast_id_datum) : 0;
                builtin_context->field_setter(normalizedFieldIdentifier(id_datum), cast_id, toStringLikeJava(value));
            }
            return;
        }
    }
}

bool put(ExecutionContext& context) {
    const int encoded = context.argument();
    const int put_type = (encoded >> 4) & 0xF;
    const auto var_type = id::varTypeFromCode(encoded & 0xF);

    std::optional<Datum> field_cast_id_datum;
    if (var_type == id::VarType::FIELD) {
        field_cast_id_datum = context.pop();
    }
    const Datum id_datum = context.pop();
    Datum value = context.pop();

    switch (put_type) {
        case 1:
            setContextVar(context, var_type, id_datum, std::move(value), field_cast_id_datum);
            break;
        case 3: {
            const Datum current = getContextVar(context, var_type, id_datum, field_cast_id_datum);
            std::string value_storage;
            std::string current_storage;
            const std::string_view value_string = stringViewLikeJava(value, value_storage);
            const std::string_view current_string = stringViewLikeJava(current, current_storage);
            std::string new_value;
            new_value.reserve(value_string.size() + current_string.size());
            new_value.append(value_string);
            new_value.append(current_string);
            setContextVar(context, var_type, id_datum, Datum::of(std::move(new_value)), field_cast_id_datum);
            break;
        }
        case 2: {
            const Datum current = getContextVar(context, var_type, id_datum, field_cast_id_datum);
            std::string current_storage;
            std::string value_storage;
            const std::string_view current_string = stringViewLikeJava(current, current_storage);
            const std::string_view value_string = stringViewLikeJava(value, value_storage);
            std::string new_value;
            new_value.reserve(current_string.size() + value_string.size());
            new_value.append(current_string);
            new_value.append(value_string);
            setContextVar(context, var_type, id_datum, Datum::of(std::move(new_value)), field_cast_id_datum);
            break;
        }
        default:
            break;
    }
    return true;
}

bool deleteChunk(ExecutionContext& context) {
    const auto var_type = id::varTypeFromCode(context.argument());
    std::optional<Datum> field_cast_id_datum;
    if (var_type == id::VarType::FIELD) {
        field_cast_id_datum = context.pop();
    }
    const Datum id_datum = context.pop();

    const int last_line = toIntLikeJava(context.pop());
    const int first_line = toIntLikeJava(context.pop());
    const int last_item = toIntLikeJava(context.pop());
    const int first_item = toIntLikeJava(context.pop());
    const int last_word = toIntLikeJava(context.pop());
    const int first_word = toIntLikeJava(context.pop());
    const int last_char = toIntLikeJava(context.pop());
    const int first_char = toIntLikeJava(context.pop());

    ChunkSpec chunk = chooseSingleChunkSpec(first_char,
                                            last_char,
                                            first_word,
                                            last_word,
                                            first_item,
                                            last_item,
                                            first_line,
                                            last_line);

    const Datum current = getContextVar(context, var_type, id_datum, field_cast_id_datum);
    std::string current_storage;
    const std::string_view current_string = stringViewLikeJava(current, current_storage);
    const char item_delimiter = currentItemDelimiter(context);

    if (chunk.type == StringChunkType::Char) {
        if (chunk.first < 0) {
            chunk.first = static_cast<int>(current_string.size());
        }
        if (chunk.last < 0) {
            chunk.last = static_cast<int>(current_string.size());
        }
    } else if (chunk.first < 0 || chunk.last < 0) {
        const int count = countChunks(current_string, chunk.type, item_delimiter);
        if (chunk.first < 0) {
            chunk.first = count;
        }
        if (chunk.last < 0) {
            chunk.last = count;
        }
    }

    setContextVar(context,
                  var_type,
                  id_datum,
                  Datum::of(deleteChunkValue(current_string, chunk.type, chunk.first, chunk.last, item_delimiter)),
                  field_cast_id_datum);
    return true;
}

bool putChunk(ExecutionContext& context) {
    const int encoded = context.argument();
    const int put_type = (encoded >> 4) & 0xF;
    const auto var_type = id::varTypeFromCode(encoded & 0xF);

    std::optional<Datum> field_cast_id_datum;
    if (var_type == id::VarType::FIELD) {
        field_cast_id_datum = context.pop();
    }
    const Datum id_datum = context.pop();
    const Datum value = context.pop();

    const int last_line = toIntLikeJava(context.pop());
    const int first_line = toIntLikeJava(context.pop());
    const int last_item = toIntLikeJava(context.pop());
    const int first_item = toIntLikeJava(context.pop());
    const int last_word = toIntLikeJava(context.pop());
    const int first_word = toIntLikeJava(context.pop());
    const int last_char = toIntLikeJava(context.pop());
    const int first_char = toIntLikeJava(context.pop());

    ChunkSpec chunk = chooseSingleChunkSpec(first_char,
                                            last_char,
                                            first_word,
                                            last_word,
                                            first_item,
                                            last_item,
                                            first_line,
                                            last_line);

    const Datum current = getContextVar(context, var_type, id_datum, field_cast_id_datum);
    std::string current_storage;
    std::string value_storage;
    const std::string_view current_string = stringViewLikeJava(current, current_storage);
    const std::string_view value_string = stringViewLikeJava(value, value_storage);
    const char item_delimiter = currentItemDelimiter(context);
    std::string new_string(current_string);

    if (chunk.type == StringChunkType::Char) {
        switch (put_type) {
            case 1:
                if (const auto range = charChunkReplaceRange(current_string, chunk.first, chunk.last)) {
                    new_string.clear();
                    new_string.reserve(current_string.size() - static_cast<std::size_t>(range->second - range->first) +
                                      value_string.size());
                    new_string.append(current_string.substr(0, static_cast<std::size_t>(range->first)));
                    new_string.append(value_string);
                    new_string.append(current_string.substr(static_cast<std::size_t>(range->second)));
                }
                break;
            case 3:
                new_string = putBeforeChunkValue(current_string, chunk.type, chunk.first, value_string, item_delimiter);
                break;
            case 2:
                new_string = putAfterChunkValue(current_string,
                                               chunk.type,
                                               chunk.last == 0 ? chunk.first : chunk.last,
                                               value_string,
                                               item_delimiter);
                break;
            default:
                break;
        }
        setContextVar(context, var_type, id_datum, Datum::of(std::move(new_string)), field_cast_id_datum);
        return true;
    }

    if (chunk.first < 0 || chunk.last < 0) {
        const int count = countChunks(current_string, chunk.type, item_delimiter);
        if (chunk.first < 0) {
            chunk.first = count;
        }
        if (chunk.last < 0) {
            chunk.last = count;
        }
    }

    switch (put_type) {
        case 1:
            new_string = putIntoChunkValue(current_string, chunk.type, chunk.first, chunk.last, value_string, item_delimiter);
            break;
        case 3:
            new_string = putBeforeChunkValue(current_string, chunk.type, chunk.first, value_string, item_delimiter);
            break;
        case 2:
            new_string = putAfterChunkValue(current_string,
                                           chunk.type,
                                           chunk.last == 0 ? chunk.first : chunk.last,
                                           value_string,
                                           item_delimiter);
            break;
        default:
            break;
    }

    setContextVar(context, var_type, id_datum, Datum::of(std::move(new_string)), field_cast_id_datum);
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
    Datum arg_list_datum = context.pop();
    std::vector<Datum> items = argListItems(arg_list_datum);
    if (items.empty() && arg_list_datum.type() != DatumType::ArgList && arg_list_datum.type() != DatumType::ArgListNoRet &&
        !arg_list_datum.isVoid()) {
        items.push_back(std::move(arg_list_datum));
    }
    context.push(Datum::list(std::move(items)));
    return true;
}

bool pushPropList(ExecutionContext& context) {
    const Datum arg_list_datum = context.pop();
    std::vector<Datum> item_storage;
    const std::vector<Datum>& items = argListItemsRef(arg_list_datum, item_storage);
    Datum prop_list = Datum::propList();
    auto& properties = prop_list.propListValue().properties();
    properties.reserve(items.size() / 2);
    for (std::size_t index = 0; index + 1 < items.size(); index += 2) {
        properties.emplace_back(items[index], items[index + 1]);
    }
    context.push(std::move(prop_list));
    return true;
}

bool tryImmediateObjCall(ExecutionContext& context, bool no_return);
bool tryImmediateExtCall(ExecutionContext& context, bool no_return);

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
    const std::string& prop_name = context.resolveNameRef(context.argument());
    const Datum& receiver = context.scope().receiver();
    if (receiver.type() == DatumType::ScriptInstanceRef) {
        context.push(util::getProperty(receiver.scriptInstanceValue(), prop_name));
    } else {
        context.push(Datum::voidValue());
    }
    return true;
}

bool setProp(ExecutionContext& context) {
    const std::string& prop_name = context.resolveNameRef(context.argument());
    const Datum& receiver = context.scope().receiver();
    Datum value = context.pop();
    if (receiver.type() == DatumType::ScriptInstanceRef) {
        auto instance = receiver.scriptInstancePtr();
        if (!context.hasVariableSetListener()) {
            util::setProperty(*instance, prop_name, std::move(value));
            return true;
        }
        const Datum traced_value = value;
        util::setProperty(*instance, prop_name, std::move(value));
        context.tracePropertySet(prop_name, traced_value);
    }
    return true;
}

bool getMovieProp(ExecutionContext& context) {
    const std::string& prop_name = context.resolveNameRef(context.argument());
    if (auto* builtin_context = context.builtinContext(); builtin_context != nullptr && builtin_context->movie_properties != nullptr) {
        context.push(builtin_context->movie_properties->getMovieProp(prop_name));
    } else {
        context.push(builtinConstant(prop_name).value_or(Datum::voidValue()));
    }
    return true;
}

bool setMovieProp(ExecutionContext& context) {
    const std::string& prop_name = context.resolveNameRef(context.argument());
    const Datum value = context.pop();
    if (auto* builtin_context = context.builtinContext(); builtin_context != nullptr && builtin_context->movie_properties != nullptr) {
        (void)builtin_context->movie_properties->setMovieProp(prop_name, value);
    }
    return true;
}

std::optional<Datum> indexedCollectionSnapshotCount(ExecutionContext& context,
                                                    std::string_view prop_name,
                                                    const Datum& collection);

bool getObjProp(ExecutionContext& context) {
    const std::string& prop_name = context.resolveNameRef(context.argument());
    const Datum& object_ref = context.peekRef();
    if (const auto snapshot_count = indexedCollectionSnapshotCount(context, prop_name, object_ref)) {
        context.scope().replaceTop(*snapshot_count);
        return true;
    }
    if (object_ref.type() == DatumType::ScriptInstanceRef) {
        Datum result = equalsIgnoreCase(prop_name, "ilk")
            ? Datum::symbol("instance")
            : util::getProperty(object_ref.scriptInstanceValue(), prop_name);
        context.scope().replaceTop(std::move(result));
        return true;
    }
    if (object_ref.isList()) {
        Datum result = getListProp(object_ref.listValue(), prop_name);
        context.scope().replaceTop(std::move(result));
        return true;
    }
    if (object_ref.isPropList()) {
        Datum result = getPropListProp(object_ref.propListValue(), prop_name);
        context.scope().replaceTop(std::move(result));
        return true;
    }
    if (object_ref.isString()) {
        std::string storage;
        Datum result = getStringProp(stringViewLikeJava(object_ref, storage), prop_name);
        context.scope().replaceTop(std::move(result));
        return true;
    }

    const Datum object = context.pop();
    if (const auto snapshot_count = indexedCollectionSnapshotCount(context, prop_name, object)) {
        context.push(*snapshot_count);
        return true;
    }
    if (object.type() == DatumType::ScriptInstanceRef) {
        context.push(equalsIgnoreCase(prop_name, "ilk")
                         ? Datum::symbol("instance")
                         : util::getProperty(object.scriptInstanceValue(), prop_name));
        return true;
    }
    if (object.isList()) {
        context.push(getListProp(object.listValue(), prop_name));
        return true;
    }
    if (object.isPropList()) {
        context.push(getPropListProp(object.propListValue(), prop_name));
        return true;
    }
    if (object.isString()) {
        std::string storage;
        context.push(getStringProp(stringViewLikeJava(object, storage), prop_name));
        return true;
    }
    if (const auto* image = object.asImageRef()) {
        context.push(dispatch::ImageMethodDispatcher::getProperty(*image, prop_name));
        return true;
    }
    if (const auto* member = object.asCastMemberRef()) {
        context.push(getCastMemberProp(context, *member, prop_name));
        return true;
    }
    context.push(getObjectProperty(context, object, prop_name));
    return true;
}

bool getChainedProp(ExecutionContext& context) {
    const std::string& prop_name = context.resolveNameRef(context.argument());
    Datum result = getChainedObjectProperty(context, context.peekRef(), prop_name);
    context.scope().replaceTop(std::move(result));
    return true;
}

bool getTopLevelProp(ExecutionContext& context) {
    const std::string& prop_name = context.resolveNameRef(context.argument());
    if (equalsIgnoreCase(prop_name, "_player")) {
        context.push(Datum::playerRef());
    } else if (equalsIgnoreCase(prop_name, "_movie")) {
        context.push(Datum::movieRef());
    } else if (equalsIgnoreCase(prop_name, "_sound")) {
        context.push(Datum::soundRef());
    } else {
        context.push(Datum::voidValue());
    }
    return true;
}

bool setObjProp(ExecutionContext& context) {
    const std::string& prop_name = context.resolveNameRef(context.argument());
    Datum value = context.pop();
    Datum object = context.pop();
    setObjectProperty(context, object, prop_name, std::move(value));
    return true;
}

bool getLegacyProperty(ExecutionContext& context) {
    const int property_id = toIntLikeJava(context.pop());
    const int property_type = context.argument();
    const char item_delimiter = currentItemDelimiter(context);
    auto* builtin_context = context.builtinContext();

    if (property_type == 0x00) {
        if (property_id <= 0x0B) {
            if (const auto prop_name = PropertyIdMappings::getMoviePropName(property_id);
                prop_name && builtin_context != nullptr && builtin_context->movie_properties != nullptr) {
                context.push(builtin_context->movie_properties->getMovieProp(*prop_name));
            } else {
                context.push(Datum::voidValue());
            }
            return true;
        }
        if (const auto chunk_type = legacyChunkPropertyTypeByCode(property_id - 0x0B)) {
            const Datum value = context.pop();
            std::string storage;
            context.push(Datum::of(getLastChunkValue(stringViewLikeJava(value, storage), *chunk_type, item_delimiter)));
            return true;
        }
        context.push(Datum::voidValue());
        return true;
    }

    if (property_type == 0x01) {
        const Datum value = context.pop();
        std::string storage;
        if (const auto chunk_type = legacyChunkPropertyTypeByCode(property_id)) {
            context.push(Datum::of(countChunks(stringViewLikeJava(value, storage), *chunk_type, item_delimiter)));
        } else {
            context.push(Datum::voidValue());
        }
        return true;
    }

    if (property_type == 0x06) {
        const int sprite_num = toIntLikeJava(context.pop());
        if (const auto prop_name = PropertyIdMappings::getSpritePropName(property_id);
            prop_name && builtin_context != nullptr && builtin_context->sprite_properties != nullptr) {
            context.push(builtin_context->sprite_properties->getSpriteProp(sprite_num, *prop_name));
        } else {
            context.push(Datum::voidValue());
        }
        return true;
    }

    if (property_type == 0x07 || property_type == 0x09) {
        if (const auto prop_name = PropertyIdMappings::getAnimPropName(property_id);
            prop_name && builtin_context != nullptr && builtin_context->movie_properties != nullptr) {
            context.push(builtin_context->movie_properties->getMovieProp(*prop_name));
        } else {
            context.push(Datum::voidValue());
        }
        return true;
    }

    if (property_type == 0x08) {
        if (property_id == 0x02) {
            const int cast_lib_num = toIntLikeJava(context.pop());
            if (builtin_context != nullptr && builtin_context->cast_member_count_supplier) {
                context.push(Datum::of(builtin_context->cast_member_count_supplier(cast_lib_num)));
            } else {
                context.push(Datum::of(0));
            }
            return true;
        }
        if (const auto prop_name = PropertyIdMappings::getAnim2PropName(property_id);
            prop_name && builtin_context != nullptr && builtin_context->movie_properties != nullptr) {
            context.push(builtin_context->movie_properties->getMovieProp(*prop_name));
        } else {
            context.push(Datum::voidValue());
        }
        return true;
    }

    if (property_type == 0x0B) {
        const int channel_num = toIntLikeJava(context.pop());
        const std::string prop_name = PropertyIdMappings::getSoundPropName(property_id);
        if (channel_num >= 1 && channel_num <= 8) {
            context.push(dispatch::SoundChannelMethodDispatcher::getProperty(builtin_context, Datum::SoundChannel{channel_num}, prop_name));
        } else {
            context.push(Datum::voidValue());
        }
        return true;
    }

    context.push(Datum::voidValue());
    return true;
}

bool getField(ExecutionContext& context) {
    const Datum cast_id_datum = context.pop();
    Datum field_name_or_num = context.pop();
    const int cast_id = toIntLikeJava(cast_id_datum);

    std::vector<Datum> args;
    args.reserve(2);
    if (field_name_or_num.asString() != nullptr || field_name_or_num.asInt() != nullptr) {
        args.push_back(std::move(field_name_or_num));
    } else {
        args.push_back(Datum::of(toStringLikeJava(field_name_or_num)));
    }
    if (cast_id > 0) {
        args.push_back(Datum::castLibRef(id::CastLibId(cast_id)));
    }

    if (const auto result = context.invokeBuiltinIfPresent("field", args)) {
        context.push(*result);
    } else {
        context.push(Datum::of(std::string()));
    }
    return true;
}

bool setLegacyProperty(ExecutionContext& context) {
    const int property_id = toIntLikeJava(context.pop());
    const Datum value = context.pop();
    const int property_type = context.argument();
    auto* builtin_context = context.builtinContext();

    if (property_type == 0x00) {
        if (const auto prop_name = PropertyIdMappings::getMoviePropName(property_id);
            property_id <= 0x0B && prop_name && builtin_context != nullptr && builtin_context->movie_properties != nullptr) {
            (void)builtin_context->movie_properties->setMovieProp(*prop_name, value);
        }
        return true;
    }

    if (property_type == 0x04) {
        const int channel_num = toIntLikeJava(context.pop());
        const std::string prop_name = PropertyIdMappings::getSoundPropName(property_id);
        if (channel_num >= 1 && channel_num <= 8) {
            (void)dispatch::SoundChannelMethodDispatcher::setProperty(builtin_context, Datum::SoundChannel{channel_num}, prop_name, value);
        }
        return true;
    }

    if (property_type == 0x06) {
        const int sprite_num = toIntLikeJava(context.pop());
        if (const auto prop_name = PropertyIdMappings::getSpritePropName(property_id);
            prop_name && builtin_context != nullptr && builtin_context->sprite_properties != nullptr) {
            (void)builtin_context->sprite_properties->setSpriteProp(sprite_num, *prop_name, value);
        }
        return true;
    }

    if (property_type == 0x07) {
        if (const auto prop_name = PropertyIdMappings::getAnimPropName(property_id);
            prop_name && builtin_context != nullptr && builtin_context->movie_properties != nullptr) {
            (void)builtin_context->movie_properties->setMovieProp(*prop_name, value);
        }
        return true;
    }

    return true;
}

bool theBuiltin(ExecutionContext& context) {
    const std::string& prop_name = context.resolveNameRef(context.argument());
    Datum result = Datum::voidValue();
    if (equalsIgnoreCase(prop_name, "paramcount")) {
        result = Datum::of(static_cast<int>(context.scope().arguments().size()));
    } else if (equalsIgnoreCase(prop_name, "result")) {
        result = context.scope().returnValue();
    } else if (auto* builtin_context = context.builtinContext(); builtin_context != nullptr && builtin_context->movie_properties != nullptr) {
        result = builtin_context->movie_properties->getMovieProp(prop_name);
        if (result.isVoid()) {
            result = builtinConstant(prop_name).value_or(Datum::voidValue());
        }
    } else {
        result = builtinConstant(prop_name).value_or(Datum::voidValue());
    }
    context.scope().replaceTop(std::move(result));
    return true;
}

bool localCall(ExecutionContext& context) {
    const auto target_handler = context.findLocalHandler(context.argument());
    const auto* script = context.scope().script();
    if (!target_handler || script == nullptr) {
        return true;
    }

    const Datum arg_list_datum = context.pop();
    const bool no_return = isNoReturnArgList(arg_list_datum);
    std::vector<Datum> arg_storage;
    const std::vector<Datum>& arg_items = argListItemsRef(arg_list_datum, arg_storage);
    std::span<const Datum> args(arg_items);
    const Datum& scope_receiver = context.scope().receiver();
    const Datum* receiver = &scope_receiver;
    if (!scope_receiver.isVoid() && !scope_receiver.isNull() && !args.empty() && args.front() == scope_receiver) {
        bool handler_declares_me = false;
        if (!target_handler->arg_name_ids.empty()) {
            handler_declares_me = equalsIgnoreCase(context.resolveNameRef(target_handler->arg_name_ids.front()), "me");
        }
        if (!handler_declares_me) {
            receiver = &args.front();
        }
        args = args.subspan(1);
    }

    Datum result = safeExecuteHandler(
        context,
        HandlerRef{
            script,
            target_handler,
            context.scope().scriptOwner(),
            context.scope().fileOwner(),
            context.scope().scriptNamesOwner()
        },
        args,
        *receiver);
    if (!no_return) {
        context.push(std::move(result));
    }
    return true;
}

Datum propListBuiltinGetAtValue(const Datum::PropList& prop_list, const Datum& key_or_index) {
    int index = -1;
    if (key_or_index.asSymbol() != nullptr || key_or_index.isString()) {
        index = prop_list.findTypedKey(key_or_index);
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

Datum propListObjectGetAtValue(const Datum::PropList& prop_list, const Datum& key_or_index) {
    Datum value = propListBuiltinGetAtValue(prop_list, key_or_index);
    if (!value.isVoid() || !key_or_index.isInt()) {
        return value;
    }
    const Datum string_key = Datum::of(std::to_string(toIntLikeJava(key_or_index)));
    const int index = prop_list.findTypedKey(string_key);
    return index >= 0 ? prop_list.properties()[static_cast<std::size_t>(index)].second : Datum::voidValue();
}

std::optional<Datum> fastListBuiltinCall(std::string_view handler_name, std::span<const Datum> args) {
    if (args.empty()) {
        return std::nullopt;
    }
    if (equalsIgnoreCase(handler_name, "count")) {
        if (args[0].isList()) {
            return Datum::of(args[0].listValue().count());
        }
        if (args[0].isPropList()) {
            return Datum::of(args[0].propListValue().count());
        }
        return std::nullopt;
    }
    if (!equalsIgnoreCase(handler_name, "getAt") || args.size() < 2) {
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

Datum fastCharToNumValue(const Datum& value_datum) {
    if (const auto* value = value_datum.asString()) {
        return value->value.empty()
            ? Datum::of(0)
            : Datum::of(static_cast<int>(static_cast<unsigned char>(value->value.front())));
    }
    if (const auto* value = value_datum.asFieldText()) {
        return value->value.empty()
            ? Datum::of(0)
            : Datum::of(static_cast<int>(static_cast<unsigned char>(value->value.front())));
    }
    if (const auto* value = value_datum.asStringChunk()) {
        return value->value.empty()
            ? Datum::of(0)
            : Datum::of(static_cast<int>(static_cast<unsigned char>(value->value.front())));
    }
    if (const auto* value = value_datum.asSymbol()) {
        return value->name.empty()
            ? Datum::of(0)
            : Datum::of(static_cast<int>(static_cast<unsigned char>(value->name.front())));
    }
    const std::string value = toStringLikeJava(value_datum);
    return value.empty()
        ? Datum::of(0)
        : Datum::of(static_cast<int>(static_cast<unsigned char>(value.front())));
}

enum class FastPrimitiveBuiltin {
    None,
    Length,
    Count,
    Chars,
    BitAnd,
    BitOr,
    BitXor,
    BitNot,
    Abs,
    ListP,
    VoidP,
    CharToNum,
    NumToChar,
    String,
    Random,
    Add
};

FastPrimitiveBuiltin fastPrimitiveBuiltinFor(std::string_view handler_name) {
    switch (handler_name.size()) {
        case 3:
            if (equalsIgnoreCase(handler_name, "abs")) return FastPrimitiveBuiltin::Abs;
            if (equalsIgnoreCase(handler_name, "add")) return FastPrimitiveBuiltin::Add;
            break;
        case 5:
            if (equalsIgnoreCase(handler_name, "count")) return FastPrimitiveBuiltin::Count;
            if (equalsIgnoreCase(handler_name, "chars")) return FastPrimitiveBuiltin::Chars;
            if (equalsIgnoreCase(handler_name, "bitOr")) return FastPrimitiveBuiltin::BitOr;
            if (equalsIgnoreCase(handler_name, "listp")) return FastPrimitiveBuiltin::ListP;
            if (equalsIgnoreCase(handler_name, "voidp")) return FastPrimitiveBuiltin::VoidP;
            break;
        case 6:
            if (equalsIgnoreCase(handler_name, "length")) return FastPrimitiveBuiltin::Length;
            if (equalsIgnoreCase(handler_name, "bitAnd")) return FastPrimitiveBuiltin::BitAnd;
            if (equalsIgnoreCase(handler_name, "bitXor")) return FastPrimitiveBuiltin::BitXor;
            if (equalsIgnoreCase(handler_name, "bitNot")) return FastPrimitiveBuiltin::BitNot;
            if (equalsIgnoreCase(handler_name, "string")) return FastPrimitiveBuiltin::String;
            if (equalsIgnoreCase(handler_name, "random")) return FastPrimitiveBuiltin::Random;
            break;
        case 9:
            if (equalsIgnoreCase(handler_name, "charToNum")) return FastPrimitiveBuiltin::CharToNum;
            if (equalsIgnoreCase(handler_name, "numToChar")) return FastPrimitiveBuiltin::NumToChar;
            break;
        default:
            break;
    }
    return FastPrimitiveBuiltin::None;
}

std::optional<Datum> fastPrimitiveBuiltinCall(ExecutionContext& context,
                                              std::string_view handler_name,
                                              std::span<const Datum> args) {
    if (context.builtins() == nullptr) {
        return std::nullopt;
    }

    switch (fastPrimitiveBuiltinFor(handler_name)) {
    case FastPrimitiveBuiltin::Length: {
        if (args.empty()) {
            return Datum::of(0);
        }
        if (args[0].isList()) {
            return Datum::of(args[0].listValue().count());
        }
        if (args[0].isPropList()) {
            return Datum::of(args[0].propListValue().count());
        }
        if (const auto direct_value = directStringViewLikeJava(args[0])) {
            return Datum::of(static_cast<int>(direct_value->size()));
        }
        std::string storage;
        return Datum::of(static_cast<int>(stringViewLikeJava(args[0], storage).size()));
    }
    case FastPrimitiveBuiltin::Count: {
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
    case FastPrimitiveBuiltin::Chars: {
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
        return Datum::of(std::string(value.substr(static_cast<std::size_t>(start),
                                                  static_cast<std::size_t>(end - start))));
    }
    case FastPrimitiveBuiltin::BitAnd:
        return args.size() < 2 ? Datum::of(0) : Datum::of(toIntLikeJava(args[0]) & toIntLikeJava(args[1]));
    case FastPrimitiveBuiltin::BitOr:
        return args.size() < 2 ? Datum::of(0) : Datum::of(toIntLikeJava(args[0]) | toIntLikeJava(args[1]));
    case FastPrimitiveBuiltin::BitXor:
        return args.size() < 2 ? Datum::of(0) : Datum::of(toIntLikeJava(args[0]) ^ toIntLikeJava(args[1]));
    case FastPrimitiveBuiltin::BitNot:
        return args.empty() ? Datum::of(0) : Datum::of(~toIntLikeJava(args[0]));
    case FastPrimitiveBuiltin::Abs: {
        if (args.empty()) {
            return Datum::of(0);
        }
        if (args[0].isFloat()) {
            return Datum::of(std::fabs(toDoubleLikeJava(args[0])));
        }
        const int value = toIntLikeJava(args[0]);
        return value == std::numeric_limits<int>::min() ? Datum::of(value) : Datum::of(std::abs(value));
    }
    case FastPrimitiveBuiltin::ListP:
        return !args.empty() && (args[0].isList() || args[0].isPropList()) ? Datum::TRUE : Datum::FALSE;
    case FastPrimitiveBuiltin::VoidP:
        return args.empty() || args[0].isVoid() ? Datum::TRUE : Datum::FALSE;
    case FastPrimitiveBuiltin::CharToNum: {
        if (args.empty()) {
            return Datum::of(0);
        }
        return fastCharToNumValue(args[0]);
    }
    case FastPrimitiveBuiltin::NumToChar: {
        if (args.empty()) {
            return Datum::of(std::string());
        }
        const int numeric_value = args[0].asInt() != nullptr
            ? args[0].asInt()->value
            : toIntLikeJava(args[0]);
        return Datum::of(std::string(1, static_cast<char>(numeric_value)));
    }
    case FastPrimitiveBuiltin::String: {
        if (args.empty()) {
            return Datum::of(std::string());
        }
        if (args[0].isString()) {
            return args[0];
        }
        return Datum::of(toStringLikeJava(args[0]));
    }
    case FastPrimitiveBuiltin::Random: {
        if (args.empty()) {
            return Datum::of(1);
        }
        const int max = toIntLikeJava(args[0]);
        if (max <= 0) {
            return Datum::of(1);
        }
        auto* builtin_context = context.builtinContext();
        if (builtin_context != nullptr && builtin_context->random_int_handler) {
            return Datum::of(builtin_context->random_int_handler(max));
        }
        return Datum::of(1);
    }
    case FastPrimitiveBuiltin::Add: {
        if (args.size() >= 2 && args[0].isList()) {
            Datum mutable_target = args[0];
            mutable_target.listValue().items().push_back(args[1]);
            return Datum::voidValue();
        }
        return std::nullopt;
    }
    case FastPrimitiveBuiltin::None:
        return std::nullopt;
    }

    return std::nullopt;
}

bool canUseImmediatePrimitiveExtCall(ExecutionContext& context, std::string_view handler_name) {
    if (context.builtins() == nullptr) {
        return false;
    }
    const auto handler = context.findHandler(handler_name);
    if (!handler) {
        return true;
    }
    return handler->script != nullptr &&
           handler->script_type == chunks::ScriptChunkType::Parent &&
           context.isBuiltin(handler_name);
}

enum class ImmediatePrimitiveExtCall {
    None,
    CharToNum,
    NumToChar,
    Length,
    Count,
    BitAnd,
    BitOr,
    BitXor,
    String,
    Integer,
    Abs,
    ListP,
    VoidP,
    New,
    Min,
    Max,
    Chars
};

ImmediatePrimitiveExtCall immediatePrimitiveExtCallFor(std::string_view handler_name, int arg_count) {
    if (arg_count == 1) {
        switch (handler_name.size()) {
            case 3:
                if (equalsIgnoreCase(handler_name, "abs")) return ImmediatePrimitiveExtCall::Abs;
                if (equalsIgnoreCase(handler_name, "new")) return ImmediatePrimitiveExtCall::New;
                if (equalsIgnoreCase(handler_name, "min")) return ImmediatePrimitiveExtCall::Min;
                if (equalsIgnoreCase(handler_name, "max")) return ImmediatePrimitiveExtCall::Max;
                break;
            case 5:
                if (equalsIgnoreCase(handler_name, "count")) return ImmediatePrimitiveExtCall::Count;
                if (equalsIgnoreCase(handler_name, "listp")) return ImmediatePrimitiveExtCall::ListP;
                if (equalsIgnoreCase(handler_name, "voidp")) return ImmediatePrimitiveExtCall::VoidP;
                break;
            case 6:
                if (equalsIgnoreCase(handler_name, "length")) return ImmediatePrimitiveExtCall::Length;
                if (equalsIgnoreCase(handler_name, "string")) return ImmediatePrimitiveExtCall::String;
                break;
            case 7:
                if (equalsIgnoreCase(handler_name, "integer")) return ImmediatePrimitiveExtCall::Integer;
                break;
            case 9:
                if (equalsIgnoreCase(handler_name, "charToNum")) return ImmediatePrimitiveExtCall::CharToNum;
                if (equalsIgnoreCase(handler_name, "numToChar")) return ImmediatePrimitiveExtCall::NumToChar;
                break;
            default:
                break;
        }
        return ImmediatePrimitiveExtCall::None;
    }
    if (arg_count == 2) {
        switch (handler_name.size()) {
            case 3:
                if (equalsIgnoreCase(handler_name, "min")) return ImmediatePrimitiveExtCall::Min;
                if (equalsIgnoreCase(handler_name, "max")) return ImmediatePrimitiveExtCall::Max;
                break;
            case 5:
                if (equalsIgnoreCase(handler_name, "bitOr")) return ImmediatePrimitiveExtCall::BitOr;
                break;
            case 6:
                if (equalsIgnoreCase(handler_name, "bitAnd")) return ImmediatePrimitiveExtCall::BitAnd;
                if (equalsIgnoreCase(handler_name, "bitXor")) return ImmediatePrimitiveExtCall::BitXor;
                break;
            default:
                break;
        }
        return ImmediatePrimitiveExtCall::None;
    }
    if (arg_count == 3) {
        return equalsIgnoreCase(handler_name, "chars") ? ImmediatePrimitiveExtCall::Chars : ImmediatePrimitiveExtCall::None;
    }
    return ImmediatePrimitiveExtCall::None;
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
        if (const auto parsed_int = parseIntStrictView(trimmed)) {
            return Datum::of(*parsed_int);
        }
        if (const auto parsed_double = parseDoubleStrict(trimmed)) {
            return Datum::of(javaRoundToInt(*parsed_double));
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

Datum fastMaxValue(const Datum& value) {
    if (!value.isList()) {
        return value;
    }
    const auto& items = value.listValue().items();
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

bool tryImmediatePrimitiveExtCall(ExecutionContext& context,
                                  std::string_view handler_name,
                                  int arg_count,
                                  bool no_return) {
    const ImmediatePrimitiveExtCall primitive = immediatePrimitiveExtCallFor(handler_name, arg_count);
    if (primitive == ImmediatePrimitiveExtCall::None) {
        return false;
    }
    if (!canUseImmediatePrimitiveExtCall(context, handler_name)) {
        return false;
    }

    if (primitive == ImmediatePrimitiveExtCall::CharToNum) {
        if (no_return) {
            context.scope().drop(1);
        } else {
            context.scope().replaceTop(fastCharToNumValue(context.peekRef()));
        }
        return true;
    }

    if (primitive == ImmediatePrimitiveExtCall::NumToChar) {
        if (no_return) {
            context.scope().drop(1);
        } else {
            const Datum& value = context.peekRef();
            const int numeric_value = value.asInt() != nullptr ? value.asInt()->value : toIntLikeJava(value);
            context.scope().replaceTop(Datum::of(std::string(1, static_cast<char>(numeric_value))));
        }
        return true;
    }

    if (primitive == ImmediatePrimitiveExtCall::Chars) {
        Datum result = Datum::voidValue();
        if (!no_return) {
            std::string value_storage;
            const std::string_view value = stringViewLikeJava(context.peekRef(2), value_storage);
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
        if (!no_return) {
            context.push(std::move(result));
        }
        return true;
    }

    if (primitive == ImmediatePrimitiveExtCall::Length) {
        if (no_return) {
            context.scope().drop(1);
        } else {
            const Datum& value = context.peekRef();
            Datum result = Datum::of(0);
            if (value.isList()) {
                result = Datum::of(value.listValue().count());
            } else if (value.isPropList()) {
                result = Datum::of(value.propListValue().count());
            } else if (const auto direct_value = directStringViewLikeJava(value)) {
                result = Datum::of(static_cast<int>(direct_value->size()));
            } else {
                std::string storage;
                result = Datum::of(static_cast<int>(stringViewLikeJava(value, storage).size()));
            }
            context.scope().replaceTop(std::move(result));
        }
        return true;
    }

    if (primitive == ImmediatePrimitiveExtCall::Count) {
        if (no_return) {
            context.scope().drop(1);
        } else {
            const Datum& value = context.peekRef();
            Datum result = Datum::of(0);
            if (value.isList()) {
                result = Datum::of(value.listValue().count());
            } else if (value.isPropList()) {
                result = Datum::of(value.propListValue().count());
            }
            context.scope().replaceTop(std::move(result));
        }
        return true;
    }

    if (arg_count == 2) {
        const bool is_bit_and = primitive == ImmediatePrimitiveExtCall::BitAnd;
        const bool is_bit_or = primitive == ImmediatePrimitiveExtCall::BitOr;
        const bool is_bit_xor = primitive == ImmediatePrimitiveExtCall::BitXor;
        if (is_bit_and || is_bit_or || is_bit_xor) {
            if (no_return) {
                context.scope().drop(2);
            } else {
                const Datum& right = context.peekRef(0);
                const Datum& left = context.peekRef(1);
                Datum result;
                if (is_bit_and) {
                    result = Datum::of(toIntLikeJava(left) & toIntLikeJava(right));
                } else if (is_bit_or) {
                    result = Datum::of(toIntLikeJava(left) | toIntLikeJava(right));
                } else {
                    result = Datum::of(toIntLikeJava(left) ^ toIntLikeJava(right));
                }
                context.scope().replaceTopTwo(std::move(result));
            }
            return true;
        }
    }

    if (primitive == ImmediatePrimitiveExtCall::String) {
        if (no_return) {
            context.scope().drop(1);
        } else {
            const Datum& value = context.peekRef();
            Datum result = value.isString() ? value : Datum::of(toStringLikeJava(value));
            context.scope().replaceTop(std::move(result));
        }
        return true;
    }

    if (primitive == ImmediatePrimitiveExtCall::Integer) {
        if (no_return) {
            context.scope().drop(1);
        } else {
            context.scope().replaceTop(fastIntegerValue(context.peekRef()));
        }
        return true;
    }

    if (primitive == ImmediatePrimitiveExtCall::Abs) {
        if (no_return) {
            context.scope().drop(1);
        } else {
            const Datum& value = context.peekRef();
            Datum result;
            if (value.isFloat()) {
                result = Datum::of(std::fabs(toDoubleLikeJava(value)));
            } else {
                const int numeric_value = toIntLikeJava(value);
                result = numeric_value == std::numeric_limits<int>::min()
                    ? Datum::of(numeric_value)
                    : Datum::of(std::abs(numeric_value));
            }
            context.scope().replaceTop(std::move(result));
        }
        return true;
    }

    if (primitive == ImmediatePrimitiveExtCall::ListP) {
        if (no_return) {
            context.scope().drop(1);
        } else {
            const Datum& value = context.peekRef();
            context.scope().replaceTop(value.isList() || value.isPropList() ? Datum::TRUE : Datum::FALSE);
        }
        return true;
    }

    if (primitive == ImmediatePrimitiveExtCall::VoidP) {
        if (no_return) {
            context.scope().drop(1);
        } else {
            context.scope().replaceTop(context.peekRef().isVoid() ? Datum::TRUE : Datum::FALSE);
        }
        return true;
    }

    if (primitive == ImmediatePrimitiveExtCall::New) {
        const Datum& target_ref = context.peekRef(0);
        const auto* script_ref = target_ref.asScriptRef();
        auto* builtin_context = context.builtinContext();
        if (script_ref == nullptr && (builtin_context == nullptr || !builtin_context->new_instance_handler)) {
            return false;
        }

        Datum target = context.pop();
        static const std::vector<Datum> empty_args;
        Datum result = Datum::voidValue();
        if (builtin_context != nullptr && builtin_context->new_instance_handler) {
            result = builtin_context->new_instance_handler(target, empty_args);
        } else {
            result = Datum::scriptInstance("script", script_ref->member_ref);
            initializeDeclaredScriptProperties(context, result, script_ref->member_ref);
            if (builtin_context != nullptr && builtin_context->call_target_handler) {
                Datum handler_result = builtin_context->call_target_handler(result, "new", empty_args);
                if (!handler_result.isVoid()) {
                    result = std::move(handler_result);
                }
            }
        }
        if (!no_return) {
            context.push(std::move(result));
        }
        return true;
    }

    const bool is_min_primitive = primitive == ImmediatePrimitiveExtCall::Min;
    const bool is_max_primitive = primitive == ImmediatePrimitiveExtCall::Max;
    if ((is_min_primitive || is_max_primitive) && (arg_count == 1 || arg_count == 2)) {
        if (arg_count == 1) {
            if (no_return) {
                context.scope().drop(1);
            } else {
                const Datum& value = context.peekRef();
                context.scope().replaceTop(is_min_primitive ? fastMinValue(value) : fastMaxValue(value));
            }
            return true;
        }

        if (no_return) {
            context.scope().drop(2);
        } else {
            const Datum& right = context.peekRef(0);
            const Datum& left = context.peekRef(1);
            Datum result;
            if (left.isFloat() || right.isFloat()) {
                result = Datum::of(is_min_primitive
                    ? std::min(toDoubleLikeJava(left), toDoubleLikeJava(right))
                    : std::max(toDoubleLikeJava(left), toDoubleLikeJava(right)));
            } else {
                result = Datum::of(is_min_primitive
                    ? std::min(toIntLikeJava(left), toIntLikeJava(right))
                    : std::max(toIntLikeJava(left), toIntLikeJava(right)));
            }
            context.scope().replaceTopTwo(std::move(result));
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
                                        int header_index,
                                        const chunks::ScriptChunk::Instruction& collection_load,
                                        const chunks::ScriptChunk::Instruction& index_load) {
    const auto& handler = context.scope().handler();
    const auto& instructions = handler.instructions;
    if (header_index < 4 || header_index >= static_cast<int>(instructions.size())) {
        return false;
    }
    const auto& jmp_if_zero = instructions[static_cast<std::size_t>(header_index)];
    if (jmp_if_zero.opcode != Opcode::JMP_IF_Z) {
        return false;
    }

    const int loop_end_offset = jmp_if_zero.offset + jmp_if_zero.argument;
    const int end_index = handler.getInstructionIndex(loop_end_offset);
    if (end_index < 1 || end_index > static_cast<int>(instructions.size())) {
        return false;
    }
    const auto& end_repeat = instructions[static_cast<std::size_t>(end_index - 1)];
    if (end_repeat.opcode != Opcode::END_REPEAT) {
        return false;
    }

    const int cond_start = handler.getInstructionIndex(end_repeat.offset - end_repeat.argument);
    if (cond_start < 1 || cond_start + 4 != header_index) {
        return false;
    }
    if (!sameVariableInstruction(instructions[static_cast<std::size_t>(cond_start)], index_load) ||
        !sameVariableInstruction(instructions[static_cast<std::size_t>(cond_start + 1)], collection_load)) {
        return false;
    }
    const auto& count_prop = instructions[static_cast<std::size_t>(cond_start + 2)];
    if (count_prop.opcode != Opcode::GET_OBJ_PROP || !equalsIgnoreCase(context.resolveNameRef(count_prop.argument), "count")) {
        return false;
    }
    const auto comparison = instructions[static_cast<std::size_t>(cond_start + 3)].opcode;
    if (comparison != Opcode::LT_EQ && comparison != Opcode::GT_EQ) {
        return false;
    }

    const auto set_opcode = setOpcodeForGetOpcode(index_load.opcode);
    if (!set_opcode.has_value()) {
        return false;
    }
    const auto& initial_set = instructions[static_cast<std::size_t>(cond_start - 1)];
    if (initial_set.opcode != *set_opcode || initial_set.argument != index_load.argument) {
        return false;
    }
    if (end_index < 5) {
        return false;
    }
    const int expected_increment = comparison == Opcode::LT_EQ ? 1 : -1;
    return instructions[static_cast<std::size_t>(end_index - 5)].opcode == Opcode::PUSH_INT8 &&
           instructions[static_cast<std::size_t>(end_index - 5)].argument == expected_increment &&
           sameVariableInstruction(instructions[static_cast<std::size_t>(end_index - 4)], index_load) &&
           instructions[static_cast<std::size_t>(end_index - 3)].opcode == Opcode::ADD &&
           instructions[static_cast<std::size_t>(end_index - 2)].opcode == *set_opcode &&
           instructions[static_cast<std::size_t>(end_index - 2)].argument == index_load.argument;
}

bool isLoopHeaderForIndexedCollection(const ExecutionContext& context,
                                      int header_index,
                                      int current_index,
                                      const chunks::ScriptChunk::Instruction& collection_load,
                                      const chunks::ScriptChunk::Instruction& index_load) {
    if (current_index <= header_index ||
        !indexedCollectionLoopHeaderMatches(context, header_index, collection_load, index_load)) {
        return false;
    }
    const auto& jmp_if_zero = context.scope().handler().instructions[static_cast<std::size_t>(header_index)];
    return jmp_if_zero.offset + jmp_if_zero.argument > context.instructionOffset();
}

bool loopBodyHasIndexedCollectionGetAt(const ExecutionContext& context,
                                       int header_index,
                                       const chunks::ScriptChunk::Instruction& collection_load,
                                       const chunks::ScriptChunk::Instruction& index_load) {
    const auto& handler = context.scope().handler();
    const auto& instructions = handler.instructions;
    const auto& jmp_if_zero = instructions[static_cast<std::size_t>(header_index)];
    const int end_index = handler.getInstructionIndex(jmp_if_zero.offset + jmp_if_zero.argument);
    if (end_index < 1) {
        return false;
    }

    for (int index = header_index + 1; index < end_index - 1; ++index) {
        const auto& instruction = instructions[static_cast<std::size_t>(index)];
        if (instruction.opcode != Opcode::OBJ_CALL || !equalsIgnoreCase(context.resolveNameRef(instruction.argument), "getAt") ||
            index < 3) {
            continue;
        }
        const auto& push_args = instructions[static_cast<std::size_t>(index - 1)];
        if (push_args.opcode == Opcode::PUSH_ARG_LIST && push_args.argument == 2 &&
            sameVariableInstruction(instructions[static_cast<std::size_t>(index - 2)], index_load) &&
            sameVariableInstruction(instructions[static_cast<std::size_t>(index - 3)], collection_load)) {
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
    const int current_index = context.scope().bytecodeIndex();
    if (current_index < 3 || current_index >= static_cast<int>(instructions.size())) {
        return std::nullopt;
    }
    const auto& push_args = instructions[static_cast<std::size_t>(current_index - 1)];
    const auto& index_load = instructions[static_cast<std::size_t>(current_index - 2)];
    const auto& collection_load = instructions[static_cast<std::size_t>(current_index - 3)];
    if (push_args.opcode != Opcode::PUSH_ARG_LIST || push_args.argument != 2 ||
        !isIndexedCollectionLoadOpcode(collection_load.opcode) ||
        !isIndexedCollectionLoadOpcode(index_load.opcode)) {
        return std::nullopt;
    }

    for (int header_index = current_index - 1; header_index >= 0; --header_index) {
        if (isLoopHeaderForIndexedCollection(context, header_index, current_index, collection_load, index_load)) {
            return header_index;
        }
    }
    return std::nullopt;
}

std::optional<int> indexedCollectionCountLoopHeaderUncached(const ExecutionContext& context) {
    const auto& handler = context.scope().handler();
    const auto& instructions = handler.instructions;
    const int current_index = context.scope().bytecodeIndex();
    if (current_index < 2 || current_index + 2 >= static_cast<int>(instructions.size())) {
        return std::nullopt;
    }
    const auto& index_load = instructions[static_cast<std::size_t>(current_index - 2)];
    const auto& collection_load = instructions[static_cast<std::size_t>(current_index - 1)];
    if (!isIndexedCollectionLoadOpcode(collection_load.opcode) ||
        !isIndexedCollectionLoadOpcode(index_load.opcode)) {
        return std::nullopt;
    }
    const int header_index = current_index + 2;
    if (indexedCollectionLoopHeaderMatches(context, header_index, collection_load, index_load) &&
        loopBodyHasIndexedCollectionGetAt(context, header_index, collection_load, index_load)) {
        return header_index;
    }
    return std::nullopt;
}

std::optional<int> cachedIndexedCollectionLoopHeader(const ExecutionContext& context,
                                                     char kind,
                                                     std::optional<int> (*compute)(const ExecutionContext&)) {
    auto* builtin_context = context.builtinContext();
    if (builtin_context == nullptr) {
        return compute(context);
    }

    const std::string key = indexedCollectionLoopHeaderCacheKey(context, kind);
    const auto cached = builtin_context->indexed_collection_loop_header_cache.find(key);
    if (cached != builtin_context->indexed_collection_loop_header_cache.end()) {
        return cached->second >= 0 ? std::optional<int>{cached->second} : std::nullopt;
    }

    const auto result = compute(context);
    builtin_context->indexed_collection_loop_header_cache.emplace(key, result.value_or(-1));
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
                                                    ImmediateObjectMethod method,
                                                    std::span<const Datum> args) {
    if (method != ImmediateObjectMethod::GetAt || args.size() < 2 ||
        (!args[0].isList() && !args[0].isPropList()) ||
        args[1].isString() || args[1].isSymbol()) {
        return std::nullopt;
    }
    const auto loop_header = indexedCollectionLoopHeader(context);
    if (!loop_header.has_value()) {
        return std::nullopt;
    }
    return context.scope().indexedCollectionSnapshotValue(*loop_header,
                                                          collectionIdentity(args[0]),
                                                          args[0],
                                                          toIntLikeJava(args[1]));
}

std::optional<Datum> indexedCollectionSnapshotGetAtDirect(ExecutionContext& context,
                                                          ImmediateObjectMethod method,
                                                          const Datum& collection,
                                                          const Datum& index) {
    if (method != ImmediateObjectMethod::GetAt ||
        (!collection.isList() && !collection.isPropList()) ||
        index.isString() || index.isSymbol()) {
        return std::nullopt;
    }
    const auto loop_header = indexedCollectionLoopHeader(context);
    if (!loop_header.has_value()) {
        return std::nullopt;
    }
    return context.scope().indexedCollectionSnapshotValue(*loop_header,
                                                          collectionIdentity(collection),
                                                          collection,
                                                          toIntLikeJava(index));
}

std::optional<Datum> indexedCollectionSnapshotCount(ExecutionContext& context,
                                                    std::string_view prop_name,
                                                    const Datum& collection) {
    if (!equalsIgnoreCase(prop_name, "count") || (!collection.isList() && !collection.isPropList())) {
        return std::nullopt;
    }
    const auto loop_header = indexedCollectionCountLoopHeader(context);
    if (!loop_header.has_value()) {
        return std::nullopt;
    }
    return Datum::of(context.scope().indexedCollectionSnapshotCount(*loop_header,
                                                                    collectionIdentity(collection),
                                                                    collection));
}

std::optional<Datum> fastListObjectCall(ImmediateObjectMethod method,
                                        std::span<const Datum> args) {
    if (args.empty()) {
        return std::nullopt;
    }
    const Datum& target = args[0];
    if (target.isList()) {
        const auto& read_list = target.listValue();
        const auto& read_items = read_list.items();
        if (method == ImmediateObjectMethod::Count) {
            return Datum::of(static_cast<int>(read_items.size()));
        }
        if (method == ImmediateObjectMethod::GetAt) {
            if (args.size() < 2) {
                return Datum::voidValue();
            }
            if (args[1].isString() || args[1].isSymbol()) {
                if (const auto* prop_list = singlePropListWrapper(read_list)) {
                    const int prop_index = prop_list->findTypedKey(args[1]);
                    return prop_index >= 0 ? prop_list->properties()[static_cast<std::size_t>(prop_index)].second
                                          : Datum::voidValue();
                }
            }
            const int index = toIntLikeJava(args[1]);
            if (index < 1 || index > static_cast<int>(read_items.size())) {
                throw LingoException("getAt: index " + std::to_string(index) +
                                     " out of range (list size: " + std::to_string(read_items.size()) + ")");
            }
            return read_items[static_cast<std::size_t>(index - 1)];
        }
        Datum mutable_target = target;
        auto& list = mutable_target.listValue();
        auto& items = list.items();
        if (method == ImmediateObjectMethod::SetAt) {
            if (args.size() >= 3) {
                auto* prop_list = (args[1].isString() || args[1].isSymbol()) ? singlePropListWrapper(list) : nullptr;
                if (prop_list != nullptr) {
                    prop_list->putTyped(args[1], args[2]);
                } else {
                    const int index = toIntLikeJava(args[1]);
                    if (index >= 1) {
                        const auto zero_index = static_cast<std::size_t>(index - 1);
                        if (zero_index < items.size()) {
                            items[zero_index] = args[2];
                        } else {
                            while (items.size() < zero_index) {
                                items.push_back(Datum::voidValue());
                            }
                            items.push_back(args[2]);
                        }
                    }
                }
            }
            return Datum::voidValue();
        }
        if (method == ImmediateObjectMethod::Append || method == ImmediateObjectMethod::Add) {
            if (args.size() >= 2) {
                items.push_back(args[1]);
            }
            return Datum::voidValue();
        }
        if (method == ImmediateObjectMethod::AddAt) {
            if (args.size() >= 3) {
                int index = toIntLikeJava(args[1]) - 1;
                if (index < 0) {
                    index = 0;
                }
                const auto insert_index = static_cast<std::size_t>(index);
                if (insert_index >= items.size()) {
                    items.push_back(args[2]);
                } else {
                    items.insert(items.begin() + static_cast<std::ptrdiff_t>(insert_index), args[2]);
                }
            }
            return Datum::voidValue();
        }
        if (method == ImmediateObjectMethod::DeleteAt) {
            if (args.size() >= 2) {
                const int index = toIntLikeJava(args[1]) - 1;
                if (index >= 0 && index < static_cast<int>(items.size())) {
                    items.erase(items.begin() + index);
                }
            }
            return Datum::voidValue();
        }
        if (method == ImmediateObjectMethod::Duplicate) {
            return list.deepCopyDatum();
        }
        if (method == ImmediateObjectMethod::GetLast) {
            return items.empty() ? Datum::voidValue() : items.back();
        }
        if (method == ImmediateObjectMethod::GetFirst) {
            return items.empty() ? Datum::voidValue() : items.front();
        }
        return std::nullopt;
    }
    if (!target.isPropList()) {
        return std::nullopt;
    }

    const auto& prop_list = target.propListValue();
    if (method == ImmediateObjectMethod::Count) {
        if (args.size() >= 2) {
            std::string key_name_storage;
            const int index = prop_list.findUntypedKeyName(keyNameLikeJavaView(args[1], key_name_storage));
            if (index < 0) {
                return Datum::of(0);
            }
            const Datum& value = prop_list.properties()[static_cast<std::size_t>(index)].second;
            if (value.isList()) {
                return Datum::of(value.listValue().count());
            }
            if (value.isPropList()) {
                return Datum::of(value.propListValue().count());
            }
            return Datum::of(0);
        }
        return Datum::of(prop_list.count());
    }
    if (method == ImmediateObjectMethod::GetAt) {
        if (args.size() < 2) {
            return Datum::voidValue();
        }
        return propListObjectGetAtValue(prop_list, args[1]);
    }
    if (method == ImmediateObjectMethod::GetAProp || method == ImmediateObjectMethod::GetProp ||
        method == ImmediateObjectMethod::GetProperty || method == ImmediateObjectMethod::GetPropRef) {
        if (args.size() < 2) {
            return Datum::voidValue();
        }
        std::string key_name_storage;
        const int index = prop_list.findUntypedKeyName(keyNameLikeJavaView(args[1], key_name_storage));
        if (index < 0) {
            return Datum::voidValue();
        }
        const Datum& value = prop_list.properties()[static_cast<std::size_t>(index)].second;
        if (args.size() >= 3 && value.isList()) {
            const int item_index = toIntLikeJava(args[2]);
            if (item_index >= 1 && item_index <= value.listValue().count()) {
                return value.listValue().getAt(item_index);
            }
            return Datum::voidValue();
        }
        return value;
    }
    if (method == ImmediateObjectMethod::SetAt) {
        if (args.size() >= 3) {
            Datum mutable_target = target;
            auto& mutable_prop_list = mutable_target.propListValue();
            const int position = toIntLikeJava(args[1]) - 1;
            if (args[1].isInt() && position >= 0 && position < mutable_prop_list.count()) {
                mutable_prop_list.properties()[static_cast<std::size_t>(position)].second = args[2];
            } else if (args[1].isInt()) {
                mutable_prop_list.putTyped(Datum::of(std::to_string(toIntLikeJava(args[1]))), args[2]);
            } else {
                mutable_prop_list.putTyped(args[1], args[2]);
            }
        }
        return Datum::voidValue();
    }
    if (method == ImmediateObjectMethod::SetProp || method == ImmediateObjectMethod::SetAProp) {
        if (args.size() >= 3) {
            Datum mutable_target = target;
            mutable_target.propListValue().putTyped(args[1], args[2]);
        }
        return Datum::voidValue();
    }
    if (method == ImmediateObjectMethod::FindPos) {
        if (args.size() < 2) {
            return Datum::voidValue();
        }
        const int index = prop_list.findUntypedKey(args[1]);
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

    const bool is_count = method == ImmediateObjectMethod::Count;
    const bool is_get_prop = method == ImmediateObjectMethod::GetProp || method == ImmediateObjectMethod::GetPropRef;
    const bool is_set_prop = method == ImmediateObjectMethod::SetProp;
    if (!is_count && !is_get_prop && !is_set_prop) {
        return std::nullopt;
    }

    std::string prop_name_storage;
    const std::string_view prop_name = keyNameLikeJavaView(args[1], prop_name_storage);

    if (is_count) {
        const auto& instance = args.front().scriptInstanceValue();
        return scriptInstancePropertyCountValue(instance, prop_name);
    }

    if (is_get_prop) {
        const auto& instance = args.front().scriptInstanceValue();
        if (args.size() >= 3) {
            return scriptInstanceNestedPropertyValue(instance, prop_name, args[2]);
        }
        return scriptInstancePropertyValue(instance, prop_name);
    }

    if (is_set_prop) {
        auto instance = args.front().scriptInstancePtr();
        if (args.size() == 3) {
            util::setProperty(*instance, prop_name, args[2]);
        } else if (args.size() >= 4) {
            Datum local_prop = util::getProperty(*instance, prop_name);
            if (local_prop.isVoid()) {
                local_prop = Datum::propList();
                util::setProperty(*instance, prop_name, local_prop);
            }
            scriptInstanceSetNestedProperty(local_prop, args[2], args[3]);
            util::setProperty(*instance, prop_name, std::move(local_prop));
        }
        return Datum::voidValue();
    }

    return std::nullopt;
}

std::optional<Datum> fastScriptInstanceObjectCall(std::string_view method_name, std::span<const Datum> args) {
    return fastScriptInstanceObjectCall(classifyImmediateObjectMethod(method_name), args);
}

std::optional<Datum> fastScriptInstanceObjectCall(std::string_view method_name, const std::vector<Datum>& args) {
    return fastScriptInstanceObjectCall(method_name, std::span<const Datum>(args));
}

bool executeObjCallWithArgs(ExecutionContext& context,
                            std::string_view method_name,
                            ImmediateObjectMethod method,
                            std::span<const Datum> args,
                            bool no_return) {
    if (auto snapshot_result = indexedCollectionSnapshotGetAt(context, method, args)) {
        if (!no_return) {
            context.push(std::move(*snapshot_result));
        }
        return true;
    }
    if (auto fast_result = fastListObjectCall(method, args)) {
        if (!no_return) {
            context.push(std::move(*fast_result));
        }
        return true;
    }
    if (auto fast_result = fastScriptInstanceObjectCall(method, args)) {
        if (!no_return) {
            context.push(std::move(*fast_result));
        }
        return true;
    }
    Datum target = args.empty() ? Datum::voidValue() : args.front();
    Datum result = Datum::voidValue();
    if (target.type() == DatumType::ScriptInstanceRef) {
        const std::span<const Datum> method_args(args.data() + 1, args.size() - 1);
        result = scriptInstanceObjectMethod(context, target, method_name, method, method_args);
    } else {
        const std::span<const Datum> method_args = args.size() <= 1
                                                      ? std::span<const Datum>()
                                                      : std::span<const Datum>(args.data() + 1, args.size() - 1);
        result = dispatchObjectMethodSpan(context, std::move(target), method_name, method_args);
    }
    if (!no_return) {
        context.push(std::move(result));
    }
    return true;
}

bool executeObjCallWithArgs(ExecutionContext& context,
                            std::string_view method_name,
                            std::span<const Datum> args,
                            bool no_return) {
    return executeObjCallWithArgs(context, method_name, classifyImmediateObjectMethod(method_name), args, no_return);
}

bool executeObjCallWithArgs(ExecutionContext& context,
                            std::string_view method_name,
                            const std::vector<Datum>& args,
                            bool no_return) {
    return executeObjCallWithArgs(context, method_name, std::span<const Datum>(args), no_return);
}

bool tryImmediateFastObjCall(ExecutionContext& context,
                             std::string_view method_name,
                             ImmediateObjectMethod method,
                             int arg_count,
                             bool no_return) {
    if (arg_count <= 0 || context.scope().stackSize() < arg_count) {
        return false;
    }

    const Datum& target = context.peekRef(arg_count - 1);
    if (target.isList()) {
        if (arg_count == 1 && method == ImmediateObjectMethod::Count) {
            const int count = target.listValue().count();
            context.scope().drop(arg_count);
            if (!no_return) {
                context.push(Datum::of(count));
            }
            return true;
        }

        if (arg_count == 2 && method == ImmediateObjectMethod::GetAt) {
            const Datum& index_datum = context.peekRef(0);
            if (auto snapshot_result =
                    indexedCollectionSnapshotGetAtDirect(context, method, target, index_datum)) {
                context.scope().drop(arg_count);
                if (!no_return) {
                    context.push(std::move(*snapshot_result));
                }
                return true;
            }

            if (index_datum.isString() || index_datum.isSymbol()) {
                if (const auto* prop_list = singlePropListWrapper(target.listValue())) {
                    const int prop_index = prop_list->findTypedKey(index_datum);
                    Datum result = prop_index >= 0
                        ? prop_list->properties()[static_cast<std::size_t>(prop_index)].second
                        : Datum::voidValue();
                    context.scope().drop(arg_count);
                    if (!no_return) {
                        context.push(std::move(result));
                    }
                    return true;
                }
            }

            const int index = toIntLikeJava(index_datum);
            const auto& items = target.listValue().items();
            if (index < 1 || index > static_cast<int>(items.size())) {
                throw LingoException("getAt: index " + std::to_string(index) +
                                     " out of range (list size: " + std::to_string(items.size()) + ")");
            }
            Datum result = items[static_cast<std::size_t>(index - 1)];
            context.scope().drop(arg_count);
            if (!no_return) {
                context.push(std::move(result));
            }
            return true;
        }

        if (arg_count == 1 && method == ImmediateObjectMethod::GetLast) {
            const auto& items = target.listValue().items();
            Datum result = items.empty() ? Datum::voidValue() : items.back();
            context.scope().drop(arg_count);
            if (!no_return) {
                context.push(std::move(result));
            }
            return true;
        }

        if (arg_count == 1 && method == ImmediateObjectMethod::GetFirst) {
            const auto& items = target.listValue().items();
            Datum result = items.empty() ? Datum::voidValue() : items.front();
            context.scope().drop(arg_count);
            if (!no_return) {
                context.push(std::move(result));
            }
            return true;
        }

        if (arg_count == 3 && method == ImmediateObjectMethod::SetAt) {
            Datum value = context.pop();
            Datum index_datum = context.pop();
            Datum mutable_target = context.pop();
            auto* prop_list = (index_datum.isString() || index_datum.isSymbol())
                                 ? singlePropListWrapper(mutable_target.listValue())
                                 : nullptr;
            if (prop_list != nullptr) {
                prop_list->putTyped(std::move(index_datum), std::move(value));
            } else {
                const int index = toIntLikeJava(index_datum);
                if (index >= 1) {
                    auto& items = mutable_target.listValue().items();
                    const auto zero_index = static_cast<std::size_t>(index - 1);
                    if (zero_index < items.size()) {
                        items[zero_index] = std::move(value);
                    } else {
                        while (items.size() < zero_index) {
                            items.push_back(Datum::voidValue());
                        }
                        items.push_back(std::move(value));
                    }
                }
            }
            if (!no_return) {
                context.push(Datum::voidValue());
            }
            return true;
        }

        if (arg_count == 3 && method == ImmediateObjectMethod::AddAt) {
            Datum value = context.pop();
            const int raw_index = toIntLikeJava(context.pop()) - 1;
            Datum mutable_target = context.pop();
            auto& items = mutable_target.listValue().items();
            const int clamped_index = std::max(0, raw_index);
            const auto insert_index = static_cast<std::size_t>(clamped_index);
            if (insert_index >= items.size()) {
                items.push_back(std::move(value));
            } else {
                items.insert(items.begin() + static_cast<std::ptrdiff_t>(insert_index), std::move(value));
            }
            if (!no_return) {
                context.push(Datum::voidValue());
            }
            return true;
        }

        if (arg_count == 2 && method == ImmediateObjectMethod::DeleteAt) {
            const int raw_index = toIntLikeJava(context.pop()) - 1;
            Datum mutable_target = context.pop();
            auto& items = mutable_target.listValue().items();
            if (raw_index >= 0 && raw_index < static_cast<int>(items.size())) {
                items.erase(items.begin() + raw_index);
            }
            if (!no_return) {
                context.push(Datum::voidValue());
            }
            return true;
        }

        if (arg_count == 2 && (method == ImmediateObjectMethod::Append || method == ImmediateObjectMethod::Add)) {
            Datum value = context.pop();
            Datum mutable_target = context.pop();
            mutable_target.listValue().items().push_back(std::move(value));
            if (!no_return) {
                context.push(Datum::voidValue());
            }
            return true;
        }

        if (arg_count == 1 && method == ImmediateObjectMethod::Duplicate) {
            const auto& list = target.listValue();
            Datum result = list.deepCopyDatum();
            context.scope().drop(arg_count);
            if (!no_return) {
                context.push(std::move(result));
            }
            return true;
        }
    }

    if (target.isPropList()) {
        if (arg_count == 1 && method == ImmediateObjectMethod::Count) {
            const int count = target.propListValue().count();
            context.scope().drop(arg_count);
            if (!no_return) {
                context.push(Datum::of(count));
            }
            return true;
        }

        if (arg_count == 2 &&
            (method == ImmediateObjectMethod::GetProp || method == ImmediateObjectMethod::GetPropRef ||
             method == ImmediateObjectMethod::GetAProp || method == ImmediateObjectMethod::GetProperty)) {
            const Datum& key = context.peekRef(0);
            std::string key_name_storage;
            const int prop_index = target.propListValue().findUntypedKeyName(keyNameLikeJavaView(key, key_name_storage));
            Datum result = prop_index >= 0
                ? target.propListValue().properties()[static_cast<std::size_t>(prop_index)].second
                : Datum::voidValue();
            context.scope().drop(arg_count);
            if (!no_return) {
                context.push(std::move(result));
            }
            return true;
        }

        if (arg_count == 2 && method == ImmediateObjectMethod::GetAt) {
            const Datum& key_or_index = context.peekRef(0);
            if (auto snapshot_result =
                    indexedCollectionSnapshotGetAtDirect(context, method, target, key_or_index)) {
                context.scope().drop(arg_count);
                if (!no_return) {
                    context.push(std::move(*snapshot_result));
                }
                return true;
            }
            Datum result = propListObjectGetAtValue(target.propListValue(), key_or_index);
            context.scope().drop(arg_count);
            if (!no_return) {
                context.push(std::move(result));
            }
            return true;
        }

        if (arg_count == 3 && method == ImmediateObjectMethod::SetAt) {
            Datum value = context.pop();
            Datum key_or_index = context.pop();
            Datum mutable_target = context.pop();
            auto& prop_list = mutable_target.propListValue();
            if (key_or_index.isString() || key_or_index.isSymbol()) {
                prop_list.putSameType(std::move(key_or_index), std::move(value));
            } else {
                const int position = toIntLikeJava(key_or_index) - 1;
                if (position >= 0 && position < prop_list.count()) {
                    prop_list.properties()[static_cast<std::size_t>(position)].second = std::move(value);
                }
            }
            if (!no_return) {
                context.push(Datum::voidValue());
            }
            return true;
        }

        if (arg_count == 3 && (method == ImmediateObjectMethod::SetProp || method == ImmediateObjectMethod::SetAProp)) {
            Datum value = context.pop();
            Datum key = context.pop();
            Datum mutable_target = context.pop();
            mutable_target.propListValue().putTyped(std::move(key), std::move(value));
            if (!no_return) {
                context.push(Datum::voidValue());
            }
            return true;
        }
    }

    if (target.type() == DatumType::ScriptInstanceRef) {
        if (arg_count == 1 && method == ImmediateObjectMethod::Count) {
            const auto& instance = target.scriptInstanceValue();
            const int count = static_cast<int>(instance.properties().size()) +
                              (instance.ancestorRaw() != nullptr ? 1 : 0);
            context.scope().drop(arg_count);
            if (!no_return) {
                context.push(Datum::of(count));
            }
            return true;
        }

        if (arg_count == 2 && method == ImmediateObjectMethod::Count) {
            const auto& instance = target.scriptInstanceValue();
            std::string prop_name_storage;
            const std::string_view prop_name = keyNameLikeJavaView(context.peekRef(0), prop_name_storage);
            Datum result = scriptInstancePropertyCountValue(instance, prop_name);
            context.scope().drop(arg_count);
            if (!no_return) {
                context.push(std::move(result));
            }
            return true;
        }

        if ((arg_count == 2 || arg_count == 3) && method == ImmediateObjectMethod::GetAt) {
            const auto& instance = target.scriptInstanceValue();
            std::string prop_name_storage;
            const std::string_view prop_name = keyNameLikeJavaView(context.peekRef(arg_count - 2), prop_name_storage);
            Datum result = arg_count == 3
                ? scriptInstanceNestedPropertyValue(instance, prop_name, context.peekRef(0))
                : scriptInstancePropertyValue(instance, prop_name);
            if (arg_count == 2 && equalsIgnoreCase(prop_name, "ancestor") && result.isVoid()) {
                result = Datum::of(0);
            }
            context.scope().drop(arg_count);
            if (!no_return) {
                context.push(std::move(result));
            }
            return true;
        }

        if (arg_count == 2 && method == ImmediateObjectMethod::GetAProp) {
            const auto& instance = target.scriptInstanceValue();
            std::string prop_name_storage;
            const std::string_view prop_name = keyNameLikeJavaView(context.peekRef(0), prop_name_storage);
            Datum result = scriptInstancePropertyValue(instance, prop_name);
            context.scope().drop(arg_count);
            if (!no_return) {
                context.push(std::move(result));
            }
            return true;
        }

        if ((arg_count == 2 || arg_count == 3) &&
            (method == ImmediateObjectMethod::GetProp || method == ImmediateObjectMethod::GetPropRef)) {
            const auto& instance = target.scriptInstanceValue();
            std::string prop_name_storage;
            const std::string_view prop_name = keyNameLikeJavaView(context.peekRef(arg_count - 2), prop_name_storage);
            Datum result = arg_count == 3
                ? scriptInstanceNestedPropertyValue(instance, prop_name, context.peekRef(0))
                : scriptInstancePropertyValue(instance, prop_name);
            context.scope().drop(arg_count);
            if (!no_return) {
                context.push(std::move(result));
            }
            return true;
        }

        if ((arg_count == 3 || arg_count == 4) &&
            (method == ImmediateObjectMethod::SetAt || method == ImmediateObjectMethod::SetAProp)) {
            auto instance = target.scriptInstancePtr();
            Datum value = context.pop();
            if (arg_count == 3) {
                std::string prop_name_storage;
                const std::string_view prop_name = keyNameLikeJavaView(context.peekRef(0), prop_name_storage);
                util::setProperty(*instance, prop_name, std::move(value));
                context.scope().drop(2);
            } else {
                Datum sub_key = context.pop();
                std::string prop_name_storage;
                const std::string_view prop_name = keyNameLikeJavaView(context.peekRef(0), prop_name_storage);
                Datum local_prop = util::getProperty(*instance, prop_name);
                if (local_prop.isVoid()) {
                    local_prop = Datum::propList();
                    util::setProperty(*instance, prop_name, local_prop);
                }
                scriptInstanceSetNestedProperty(local_prop, sub_key, std::move(value));
                util::setProperty(*instance, prop_name, std::move(local_prop));
                context.scope().drop(2);
            }
            if (!no_return) {
                context.push(Datum::voidValue());
            }
            return true;
        }

        if ((arg_count == 3 || arg_count == 4) && method == ImmediateObjectMethod::SetProp) {
            auto instance = target.scriptInstancePtr();
            Datum value = context.pop();
            if (arg_count == 3) {
                std::string prop_name_storage;
                const std::string_view prop_name = keyNameLikeJavaView(context.peekRef(0), prop_name_storage);
                util::setProperty(*instance, prop_name, std::move(value));
                context.scope().drop(2);
            } else {
                Datum sub_key = context.pop();
                std::string prop_name_storage;
                const std::string_view prop_name = keyNameLikeJavaView(context.peekRef(0), prop_name_storage);
                Datum local_prop = util::getProperty(*instance, prop_name);
                if (local_prop.isVoid()) {
                    local_prop = Datum::propList();
                    util::setProperty(*instance, prop_name, local_prop);
                }
                scriptInstanceSetNestedProperty(local_prop, sub_key, std::move(value));
                util::setProperty(*instance, prop_name, std::move(local_prop));
                context.scope().drop(2);
            }
            if (!no_return) {
                context.push(Datum::voidValue());
            }
            return true;
        }

        if (arg_count == 3 && method == ImmediateObjectMethod::AddProp) {
            auto instance = target.scriptInstancePtr();
            Datum value = context.pop();
            std::string prop_name_storage;
            const std::string_view prop_name = keyNameLikeJavaView(context.peekRef(0), prop_name_storage);
            scriptInstancePutLocalProperty(*instance, prop_name, std::move(value));
            context.scope().drop(2);
            if (!no_return) {
                context.push(Datum::voidValue());
            }
            return true;
        }

        if (arg_count == 2 && method == ImmediateObjectMethod::DeleteProp) {
            auto instance = target.scriptInstancePtr();
            std::string prop_name_storage;
            const std::string_view prop_name = keyNameLikeJavaView(context.peekRef(0), prop_name_storage);
            scriptInstanceDeleteLocalProperty(*instance, prop_name);
            context.scope().drop(arg_count);
            if (!no_return) {
                context.push(Datum::voidValue());
            }
            return true;
        }

        if (arg_count == 1 && method == ImmediateObjectMethod::Ilk) {
            context.scope().drop(arg_count);
            if (!no_return) {
                context.push(Datum::symbol("instance"));
            }
            return true;
        }

        if (arg_count == 1 && method == ImmediateObjectMethod::AddAt) {
            context.scope().drop(arg_count);
            if (!no_return) {
                context.push(Datum::voidValue());
            }
            return true;
        }
    }

    if (target.isString() &&
        (arg_count == 3 || arg_count == 4) &&
        (method == ImmediateObjectMethod::GetProp || method == ImmediateObjectMethod::GetPropRef)) {
        const bool get_prop_ref = method == ImmediateObjectMethod::GetPropRef;
        std::string chunk_name_storage;
        const std::string_view chunk_name = keyNameLikeJavaView(context.peekRef(arg_count - 2), chunk_name_storage);
        const auto chunk_type = stringChunkTypeByNameNoThrow(chunk_name);
        if (!chunk_type.has_value()) {
            return false;
        }

        std::string value_storage;
        const std::string_view value = stringViewLikeJava(target, value_storage);
        const int start = toIntLikeJava(context.peekRef(arg_count - 3));
        const int end = !get_prop_ref && arg_count >= 4 ? toIntLikeJava(context.peekRef(arg_count - 4)) : start;
        Datum result = Datum::voidValue();
        if (*chunk_type == StringChunkType::Char && start == end) {
            result = (start >= 1 && start <= static_cast<int>(value.size()))
                ? Datum::of(std::string(1, value[static_cast<std::size_t>(start - 1)]))
                : Datum::of(std::string());
        }
        if (*chunk_type == StringChunkType::Line) {
            if (const auto* line_index = cachedFieldLineIndex(target, context.builtinContext())) {
                result = Datum::of(util::getLineRange(value, *line_index, start, end));
            }
        }
        if (result.isVoid()) {
            result = Datum::of(util::getChunkRange(value, *chunk_type, start, end, currentItemDelimiter(context)));
        }
        context.scope().drop(arg_count);
        if (!no_return) {
            context.push(std::move(result));
        }
        return true;
    }

    if (target.isString() && arg_count == 2 && method == ImmediateObjectMethod::Char) {
        std::string value_storage;
        const std::string_view value = stringViewLikeJava(target, value_storage);
        const int index = toIntLikeJava(context.peekRef(0));
        Datum result = (index >= 1 && index <= static_cast<int>(value.size()))
            ? Datum::of(std::string(1, value[static_cast<std::size_t>(index - 1)]))
            : Datum::of(std::string());
        context.scope().drop(arg_count);
        if (!no_return) {
            context.push(std::move(result));
        }
        return true;
    }

    if (target.isString() && arg_count == 2 && method == ImmediateObjectMethod::Count) {
        std::string chunk_name_storage;
        const std::string_view chunk_name = keyNameLikeJavaView(context.peekRef(0), chunk_name_storage);
        const auto chunk_type = stringChunkTypeByNameNoThrow(chunk_name);
        if (!chunk_type.has_value()) {
            return false;
        }

        std::string value_storage;
        const std::string_view value = stringViewLikeJava(target, value_storage);
        Datum result = Datum::voidValue();
        if (*chunk_type == StringChunkType::Line) {
            if (const auto* line_index = cachedFieldLineIndex(target, context.builtinContext())) {
                result = Datum::of(util::lineCount(*line_index));
            }
        }
        if (result.isVoid()) {
            result = Datum::of(util::countChunks(value, *chunk_type, currentItemDelimiter(context)));
        }
        context.scope().drop(arg_count);
        if (!no_return) {
            context.push(std::move(result));
        }
        return true;
    }

    return false;
}

bool tryImmediateObjCall(ExecutionContext& context, bool no_return) {
    if (context.instructionTraceEnabled()) {
        return false;
    }

    auto& scope = context.scope();
    const int current_index = scope.bytecodeIndex();
    const int next_index = current_index + 1;
    const auto& instructions = scope.handler().instructions;
    if (next_index < 0 || next_index >= static_cast<int>(instructions.size())) {
        return false;
    }

    const auto& next = instructions[static_cast<std::size_t>(next_index)];
    if (next.opcode != Opcode::OBJ_CALL) {
        return false;
    }

    const int arg_count = context.argument();
    scope.setBytecodeIndex(next_index);
    context.setInstruction(next);
    const std::string& method_name = context.resolveNameRef(next.argument);
    const ImmediateObjectMethod method = classifyImmediateObjectMethod(method_name);
    if (tryImmediateFastObjCall(context, method_name, method, arg_count, no_return)) {
        return true;
    }
    if (arg_count >= 2 && arg_count <= 4 &&
        isFastScriptInstanceObjectMethod(method) &&
        context.peekRef(arg_count - 1).type() == DatumType::ScriptInstanceRef) {
        std::array<Datum, 4> args_storage{};
        for (int index = arg_count - 1; index >= 0; --index) {
            args_storage[static_cast<std::size_t>(index)] = context.pop();
        }
        const std::span<const Datum> args(args_storage.data(), static_cast<std::size_t>(arg_count));
        if (auto fast_result = fastScriptInstanceObjectCall(method, args)) {
            if (!no_return) {
                context.push(std::move(*fast_result));
            }
            return true;
        }
        return executeObjCallWithArgs(context, method_name, method, args, no_return);
    }
    if (arg_count >= 0 && arg_count <= 8) {
        std::array<Datum, 8> args_storage{};
        for (int index = arg_count - 1; index >= 0; --index) {
            args_storage[static_cast<std::size_t>(index)] = context.pop();
        }
        return executeObjCallWithArgs(context,
                                      method_name,
                                      method,
                                      std::span<const Datum>(args_storage.data(), static_cast<std::size_t>(arg_count)),
                                      no_return);
    }

    std::vector<Datum> args = context.popArgs(arg_count);
    return executeObjCallWithArgs(context, method_name, method, args, no_return);
}

bool executeExtCallWithArgsImpl(ExecutionContext& context,
                                std::string_view handler_name,
                                std::span<const Datum> args,
                                bool no_return,
                                const std::vector<Datum>* existing_args) {
    std::vector<Datum> materialized_args;
    const std::vector<Datum>* materialized_args_ptr = existing_args;
    auto vector_args = [&]() -> const std::vector<Datum>& {
        if (materialized_args_ptr == nullptr) {
            materialized_args = std::vector<Datum>(args.begin(), args.end());
            materialized_args_ptr = &materialized_args;
        }
        return *materialized_args_ptr;
    };

    Datum result = Datum::voidValue();
    if (equalsIgnoreCase(handler_name, "return")) {
        context.setReturnValue(args.empty() ? Datum::voidValue() : args[0]);
    } else if (equalsIgnoreCase(handler_name, "voidp")) {
        result = (args.empty() || args[0].isVoid()) ? Datum::TRUE : Datum::FALSE;
    } else if (equalsIgnoreCase(handler_name, "new")) {
        if (auto builtin_result = context.invokeBuiltinIfPresent(handler_name, vector_args())) {
            result = std::move(*builtin_result);
        } else if (const auto handler = context.findHandler(handler_name)) {
            result = safeExecuteHandler(context, *handler, args, Datum::voidValue());
        }
    } else if (const auto handler = context.findHandler(handler_name)) {
        if (handler->script != nullptr && handler->script_type == chunks::ScriptChunkType::Parent) {
            if (auto builtin_result = context.invokeBuiltinIfPresent(handler_name, vector_args())) {
                result = std::move(*builtin_result);
            } else {
                result = safeExecuteHandler(context, *handler, args, Datum::voidValue());
            }
        } else {
            result = safeExecuteHandler(context, *handler, args, Datum::voidValue());
        }
    } else if (auto fast_primitive_result = fastPrimitiveBuiltinCall(context, handler_name, args)) {
        result = std::move(*fast_primitive_result);
    } else if (auto fast_builtin_result = fastListBuiltinCall(handler_name, args)) {
        result = std::move(*fast_builtin_result);
    } else if (auto builtin_result = context.invokeBuiltinIfPresent(handler_name, vector_args())) {
        result = std::move(*builtin_result);
    } else if (!args.empty()) {
        Datum target = args.front();
        const std::span<const Datum> method_args = args.size() <= 1
                                                      ? std::span<const Datum>()
                                                      : std::span<const Datum>(args.data() + 1, args.size() - 1);
        result = dispatchObjectMethodSpan(context, std::move(target), handler_name, method_args);
    } else if (args.empty()) {
        result = builtinConstant(handler_name).value_or(Datum::voidValue());
    }

    if (!no_return) {
        context.push(std::move(result));
    }
    return true;
}

bool executeExtCallWithArgs(ExecutionContext& context,
                            std::string_view handler_name,
                            std::span<const Datum> args,
                            bool no_return) {
    return executeExtCallWithArgsImpl(context, handler_name, args, no_return, nullptr);
}

bool executeExtCallWithArgs(ExecutionContext& context,
                            std::string_view handler_name,
                            const std::vector<Datum>& args,
                            bool no_return) {
    return executeExtCallWithArgsImpl(context, handler_name, std::span<const Datum>(args), no_return, &args);
}

bool tryImmediateCallBuiltin(ExecutionContext& context,
                             std::string_view handler_name,
                             int arg_count,
                             int next_index,
                             const chunks::ScriptChunk::Instruction& next_instruction,
                             bool no_return) {
    if (!equalsIgnoreCase(handler_name, "call") || arg_count < 2) {
        return false;
    }
    auto* builtin_context = context.builtinContext();
    if (builtin_context == nullptr || !builtin_context->call_target_handler) {
        return false;
    }

    const Datum& handler_datum = context.peekRef(arg_count - 1);
    const std::string target_handler_name = handler_datum.asSymbol() != nullptr ? handler_datum.asSymbol()->name
                                                                             : toStringLikeJava(handler_datum);
    const Datum& target_ref = context.peekRef(arg_count - 2);
    static const std::vector<Datum> empty_call_args;
    std::vector<Datum> materialized_call_args;
    const std::vector<Datum>& call_args = arg_count <= 2
                                             ? empty_call_args
                                             : (materialized_call_args = snapshotCallArgsFromStack(context, arg_count));
    Datum last_result = Datum::voidValue();
    context.scope().setBytecodeIndex(next_index);
    context.setInstruction(next_instruction);

    if (target_ref.isList()) {
        std::vector<Datum> snapshot = target_ref.listValue().items();
        context.scope().drop(arg_count);
        for (const auto& item : snapshot) {
            last_result = builtin_context->call_target_handler(item, target_handler_name, call_args);
        }
    } else if (target_ref.isPropList()) {
        std::vector<Datum> snapshot;
        snapshot.reserve(target_ref.propListValue().properties().size());
        for (const auto& entry : target_ref.propListValue().properties()) {
            snapshot.emplace_back(entry.second);
        }
        context.scope().drop(arg_count);
        for (const auto& item : snapshot) {
            last_result = builtin_context->call_target_handler(item, target_handler_name, call_args);
        }
    } else {
        Datum target = target_ref;
        context.scope().drop(arg_count);
        last_result = builtin_context->call_target_handler(target, target_handler_name, call_args);
    }

    if (!no_return) {
        context.push(std::move(last_result));
    }
    return true;
}

bool tryImmediateExtCall(ExecutionContext& context, bool no_return) {
    if (context.instructionTraceEnabled()) {
        return false;
    }

    auto& scope = context.scope();
    const int current_index = scope.bytecodeIndex();
    const int next_index = current_index + 1;
    const auto& instructions = scope.handler().instructions;
    if (next_index < 0 || next_index >= static_cast<int>(instructions.size())) {
        return false;
    }

    const auto& next = instructions[static_cast<std::size_t>(next_index)];
    if (next.opcode != Opcode::EXT_CALL) {
        return false;
    }

    const std::string& handler_name = context.resolveNameRef(next.argument);
    const int arg_count = context.argument();
    if (tryImmediateCallBuiltin(context, handler_name, arg_count, next_index, next, no_return)) {
        return true;
    }
    if (tryImmediatePrimitiveExtCall(context, handler_name, context.argument(), no_return)) {
        scope.setBytecodeIndex(next_index);
        context.setInstruction(next);
        return true;
    }

    if (arg_count >= 0 && arg_count <= 8 &&
        !equalsIgnoreCase(handler_name, "return") &&
        !equalsIgnoreCase(handler_name, "voidp") &&
        !equalsIgnoreCase(handler_name, "new")) {
        if (const auto handler = context.findHandler(handler_name);
            handler.has_value() && handler->script != nullptr &&
            handler->script_type != chunks::ScriptChunkType::Parent) {
            std::array<Datum, 8> args_storage{};
            for (int index = arg_count - 1; index >= 0; --index) {
                args_storage[static_cast<std::size_t>(index)] = context.pop();
            }
            scope.setBytecodeIndex(next_index);
            context.setInstruction(next);
            Datum result = safeExecuteHandler(context,
                                              *handler,
                                              std::span<const Datum>(args_storage.data(),
                                                                     static_cast<std::size_t>(arg_count)),
                                              Datum::voidValue());
            if (!no_return) {
                context.push(std::move(result));
            }
            return true;
        }
    }

    if (arg_count >= 0 && arg_count <= 8) {
        std::array<Datum, 8> args_storage{};
        for (int index = arg_count - 1; index >= 0; --index) {
            args_storage[static_cast<std::size_t>(index)] = context.pop();
        }
        scope.setBytecodeIndex(next_index);
        context.setInstruction(next);
        return executeExtCallWithArgs(context,
                                      handler_name,
                                      std::span<const Datum>(args_storage.data(), static_cast<std::size_t>(arg_count)),
                                      no_return);
    }

    std::vector<Datum> args = context.popArgs(arg_count);
    scope.setBytecodeIndex(next_index);
    context.setInstruction(next);
    return executeExtCallWithArgs(context, handler_name, args, no_return);
}

bool extCall(ExecutionContext& context) {
    const std::string& handler_name = context.resolveNameRef(context.argument());
    const Datum arg_list_datum = context.pop();
    const bool no_return = isNoReturnArgList(arg_list_datum);
    std::vector<Datum> arg_storage;
    const std::vector<Datum>& args = argListItemsRef(arg_list_datum, arg_storage);
    return executeExtCallWithArgs(context, handler_name, args, no_return);
}

bool objCall(ExecutionContext& context) {
    const std::string& method_name = context.resolveNameRef(context.argument());
    const Datum arg_list_datum = context.pop();
    const bool no_return = isNoReturnArgList(arg_list_datum);
    std::vector<Datum> arg_storage;
    const std::vector<Datum>& args = argListItemsRef(arg_list_datum, arg_storage);
    return executeObjCallWithArgs(context, method_name, args, no_return);
}

} // namespace

std::string imageOperationTraceJson() {
    std::ostringstream out;
    out << "{\"events\":[";
    for (std::size_t index = 0; index < image_operation_trace.size(); ++index) {
        if (index > 0) {
            out << ',';
        }
        out << image_operation_trace[index];
    }
    out << "]}";
    return out.str();
}

void clearImageOperationTrace() {
    image_operation_trace.clear();
}

namespace dispatch {

void ImageMethodDispatcher::setImageMutationCallback(std::function<void()> callback) {
    image_mutation_callback_owner = nullptr;
    image_mutation_callback = std::move(callback);
}

void ImageMethodDispatcher::setImageMutationCallback(void* owner, std::function<void()> callback) {
    image_mutation_callback_owner = owner;
    image_mutation_callback = std::move(callback);
}

void ImageMethodDispatcher::clearImageMutationCallback(void* owner) {
    if (image_mutation_callback_owner == owner) {
        image_mutation_callback_owner = nullptr;
        image_mutation_callback = {};
    }
}

Datum ScriptInstanceMethodDispatcher::dispatch(ExecutionContext& context,
                                               Datum& receiver,
                                               std::string_view method_name,
                                               const std::vector<Datum>& args) {
    return scriptInstanceObjectMethod(context, receiver, method_name, args);
}

Datum ImageMethodDispatcher::dispatch(const Datum::ImageRef& image_ref,
                                      std::string_view method_name,
                                      const std::vector<Datum>& args) {
    return imageObjectMethod(image_ref, method_name, args);
}

Datum ImageMethodDispatcher::getProperty(const Datum::ImageRef& image_ref, std::string_view prop_name) {
    return getImageProp(image_ref, prop_name);
}

void ImageMethodDispatcher::setProperty(builtin::BuiltinContext* context,
                                        const Datum::ImageRef& image_ref,
                                        std::string_view prop_name,
                                        const Datum& value) {
    setImageProp(context, image_ref, prop_name, value);
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
    default_raw_handlers_ = raw_handlers_;
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
    return index < raw_handlers_.size() ? raw_handlers_[index] : nullptr;
}

bool OpcodeRegistry::isDefaultRawHandler(Opcode opcode) const {
    const auto index = static_cast<std::size_t>(code(opcode));
    return index < raw_handlers_.size() &&
           raw_handlers_[index] != nullptr &&
           raw_handlers_[index] == default_raw_handlers_[index];
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
        raw_handlers_[index] = handler;
        handlers_[index] = handler;
    }
}

void OpcodeRegistry::registerFunctionHandler(Opcode opcode, OpcodeHandler handler) {
    const auto index = static_cast<std::size_t>(code(opcode));
    if (index < handlers_.size()) {
        raw_handlers_[index] = nullptr;
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
