#include "libreshockwave/lingo/Datum.hpp"

#include "libreshockwave/bitmap/Bitmap.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace libreshockwave::lingo {
namespace {

std::string trim(const std::string& value) {
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

int parseIntSafe(const std::string& value) {
    const auto trimmed = trim(value);
    if (trimmed.empty()) {
        return 0;
    }

    int result = 0;
    const auto* begin = trimmed.data();
    const auto* end = trimmed.data() + trimmed.size();
    const auto parsed = std::from_chars(begin, end, result);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return 0;
    }
    return result;
}

float parseFloatSafe(const std::string& value) {
    const auto trimmed = trim(value);
    if (trimmed.empty()) {
        return 0.0F;
    }

    char* end = nullptr;
    const auto result = std::strtof(trimmed.c_str(), &end);
    if (end == trimmed.c_str() || *end != '\0') {
        return 0.0F;
    }
    return result;
}

std::string floatToString(float value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

std::string fixedFloat(float value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6) << value;
    return out.str();
}

bool equalsIgnoreCase(const std::string& lhs, const std::string& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    if (lhs == rhs) {
        return true;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
            std::tolower(static_cast<unsigned char>(rhs[index]))) {
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

std::string propListKeyName(const Datum& datum) {
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

std::string propListSameTypeKey(const Datum& datum) {
    std::string key = datum.asSymbol() != nullptr ? "s:" : "v:";
    key += lowerAscii(propListKeyName(datum));
    return key;
}

std::uint64_t nextScriptInstanceIdentityId() {
    static std::atomic<std::uint64_t> nextId{1};
    return nextId.fetch_add(1, std::memory_order_relaxed);
}

constexpr int kMaxAncestorDepth = 100;

} // namespace

std::size_t TransparentCaseInsensitiveStringHash::operator()(std::string_view value) const noexcept {
    std::size_t hash = 0;
    for (const unsigned char ch : value) {
        const auto lowered = static_cast<unsigned char>(std::tolower(ch));
        hash ^= std::hash<unsigned char>{}(lowered) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
    }
    return hash;
}

bool TransparentCaseInsensitiveStringEqual::operator()(std::string_view lhs, std::string_view rhs) const noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
            std::tolower(static_cast<unsigned char>(rhs[index]))) {
            return false;
        }
    }
    return true;
}

LingoException LingoException::typeMismatch(const std::string& expected, const std::string& actual) {
    return LingoException("Type mismatch: expected " + expected + ", got " + actual);
}

LingoException LingoException::undefinedHandler(const std::string& name) {
    return LingoException("Handler not defined: " + name);
}

LingoException LingoException::undefinedVariable(const std::string& name) {
    return LingoException("Variable not defined: " + name);
}

LingoException LingoException::indexOutOfBounds(int index, int size) {
    return LingoException("Index " + std::to_string(index) + " out of bounds for size " + std::to_string(size));
}

std::string_view typeName(DatumType type) {
    switch (type) {
        case DatumType::Null: return "null";
        case DatumType::Void: return "void";
        case DatumType::Int: return "int";
        case DatumType::Float: return "float";
        case DatumType::String: return "string";
        case DatumType::FieldText: return "string";
        case DatumType::StringChunk: return "string_chunk";
        case DatumType::Symbol: return "symbol";
        case DatumType::VarRef: return "var_ref";
        case DatumType::ChunkRef: return "chunk_ref";
        case DatumType::List: return "list";
        case DatumType::PropList: return "prop_list";
        case DatumType::ArgList: return "arg_list";
        case DatumType::ArgListNoRet: return "arg_list_no_ret";
        case DatumType::CastLibRef: return "cast_lib";
        case DatumType::CastLibMemberAccessor: return "cast_lib_member_accessor";
        case DatumType::CastMemberRef: return "cast_member";
        case DatumType::ScriptRef: return "script_ref";
        case DatumType::ScriptInstanceRef: return "script_instance";
        case DatumType::SpriteRef: return "sprite_ref";
        case DatumType::StageRef: return "stage";
        case DatumType::PlayerRef: return "player_ref";
        case DatumType::MovieRef: return "movie_ref";
        case DatumType::IntPoint: return "point";
        case DatumType::IntRect: return "rect";
        case DatumType::Vector: return "vector";
        case DatumType::ColorRef: return "color_ref";
        case DatumType::Media: return "media";
        case DatumType::ImageRef: return "image";
        case DatumType::BitmapRef: return "bitmap_ref";
        case DatumType::PaletteRef: return "palette_ref";
        case DatumType::Matte: return "matte";
        case DatumType::SoundRef: return "sound";
        case DatumType::SoundChannel: return "sound_channel";
        case DatumType::CursorRef: return "cursor_ref";
        case DatumType::TimeoutRef: return "timeout";
        case DatumType::Xtra: return "xtra";
        case DatumType::XtraInstance: return "xtra_instance";
        case DatumType::XmlRef: return "xml";
        case DatumType::DateRef: return "date";
        case DatumType::MathRef: return "math";
    }
    return "unknown";
}

int code(StringChunkType type) {
    return static_cast<int>(type);
}

std::string_view name(StringChunkType type) {
    switch (type) {
        case StringChunkType::Item: return "item";
        case StringChunkType::Word: return "word";
        case StringChunkType::Char: return "char";
        case StringChunkType::Line: return "line";
    }
    return "unknown";
}

StringChunkType stringChunkTypeFromCode(int value) {
    switch (value) {
        case 0x01: return StringChunkType::Item;
        case 0x02: return StringChunkType::Word;
        case 0x03: return StringChunkType::Char;
        case 0x04: return StringChunkType::Line;
        default:
            throw std::invalid_argument("Unknown chunk type code: " + std::to_string(value));
    }
}

StringChunkType stringChunkTypeFromName(std::string_view value) {
    std::string lower(value);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (lower == "item") return StringChunkType::Item;
    if (lower == "word") return StringChunkType::Word;
    if (lower == "char") return StringChunkType::Char;
    if (lower == "line") return StringChunkType::Line;
    throw std::invalid_argument("Unknown chunk type: " + std::string(value));
}

Datum::CastMemberRef Datum::CastMemberRef::of(id::CastLibId castLib, id::MemberId member) {
    return CastMemberRef{castLib.value(), member.value()};
}

std::optional<id::CastLibId> Datum::CastMemberRef::castLibId() const {
    if (castLib >= 1) {
        return id::CastLibId(castLib);
    }
    return std::nullopt;
}

std::optional<id::MemberId> Datum::CastMemberRef::memberId() const {
    if (castMember >= 1) {
        return id::MemberId(castMember);
    }
    return std::nullopt;
}

int Datum::CastMemberRef::memberNum() const {
    return castMember;
}

Datum::CastLibRef Datum::CastLibRef::of(id::CastLibId castLib) {
    return CastLibRef{castLib.value()};
}

id::CastLibId Datum::CastLibRef::castLibId() const {
    return id::CastLibId(castLib);
}

Datum::CastLibMemberAccessor Datum::CastLibMemberAccessor::of(id::CastLibId castLib) {
    return CastLibMemberAccessor{castLib.value()};
}

id::CastLibId Datum::CastLibMemberAccessor::castLibId() const {
    return id::CastLibId(castLib);
}

Datum::SpriteRef Datum::SpriteRef::of(id::ChannelId channel) {
    return SpriteRef{channel.value()};
}

id::ChannelId Datum::SpriteRef::channelId() const {
    return id::ChannelId(channel);
}

int Datum::SpriteRef::spriteNum() const {
    return channel;
}

int Datum::IntRect::width() const {
    return right - left;
}

int Datum::IntRect::height() const {
    return bottom - top;
}

Datum::ColorRef Datum::ColorRef::fromPaletteIndex(int index) {
    const int normalized = index & 0xFF;
    return ColorRef{normalized, normalized, normalized, normalized};
}

Datum::ColorRef Datum::ColorRef::fromRgb(int r, int g, int b) {
    return ColorRef{r, g, b, std::nullopt};
}

const Datum Datum::TRUE = Datum::of(1);
const Datum Datum::FALSE = Datum::of(0);

Datum Datum::fieldText(std::string value, int castLib, int memberNum, std::uint64_t revision) {
    return Datum(FieldText{std::move(value), castLib, memberNum, revision});
}

Datum Datum::symbol(std::string name) {
    return Datum(Symbol{std::move(name)});
}

Datum Datum::list(std::vector<Datum> items, bool sorted) {
    return Datum(std::make_shared<List>(std::move(items), sorted));
}

Datum Datum::propList(bool sorted) {
    return Datum(std::make_shared<PropList>(sorted));
}

Datum Datum::stringChunk(Datum source, StringChunkType chunkType, int start, int end, char itemDelimiter, std::string value) {
    return Datum(StringChunk{std::make_shared<Datum>(std::move(source)), chunkType, start, end, itemDelimiter, std::move(value)});
}

Datum Datum::castMemberRef(CastMemberRef ref) {
    return Datum(ref);
}

Datum Datum::castMemberRef(id::CastLibId castLib, id::MemberId member) {
    return Datum(CastMemberRef::of(castLib, member));
}

Datum Datum::castLibRef(id::CastLibId castLib) {
    return Datum(CastLibRef::of(castLib));
}

Datum Datum::castLibMemberAccessor(id::CastLibId castLib) {
    return Datum(CastLibMemberAccessor::of(castLib));
}

Datum Datum::spriteRef(id::ChannelId channel) {
    return Datum(SpriteRef::of(channel));
}

Datum Datum::stageRef() {
    return Datum(StageRef{});
}

Datum Datum::scriptRef(CastMemberRef memberRef) {
    return Datum(ScriptRef{memberRef});
}

Datum Datum::playerRef() {
    return Datum(PlayerRef{});
}

Datum Datum::movieRef() {
    return Datum(MovieRef{});
}

Datum Datum::intPoint(int x, int y) {
    return Datum(std::make_shared<IntPoint>(IntPoint{x, y}));
}

Datum Datum::intRect(int left, int top, int right, int bottom) {
    return Datum(std::make_shared<IntRect>(IntRect{left, top, right, bottom}));
}

Datum Datum::vector3(float x, float y, float z) {
    return Datum(Vector3{x, y, z});
}

Datum Datum::colorRef(int r, int g, int b) {
    return Datum(ColorRef::fromRgb(r, g, b));
}

Datum Datum::paletteIndexColor(int index) {
    return Datum(ColorRef::fromPaletteIndex(index));
}

Datum Datum::media(std::vector<std::uint8_t> bytes) {
    return Datum(Media{std::move(bytes)});
}

Datum Datum::imageRef(std::shared_ptr<bitmap::Bitmap> bitmap) {
    return Datum(ImageRef{std::move(bitmap), {}});
}

Datum Datum::imageRef(std::shared_ptr<bitmap::Bitmap> bitmap,
                      std::function<void(bitmap::Bitmap&)> mutationCallback) {
    return Datum(ImageRef{std::move(bitmap), std::move(mutationCallback)});
}

Datum Datum::soundRef(int soundNum) {
    return Datum(SoundRef{soundNum});
}

Datum Datum::soundChannel(int channel) {
    return Datum(SoundChannel{channel});
}

Datum Datum::xtra(std::string name) {
    return Datum(Xtra{std::move(name)});
}

Datum Datum::xtraInstance(std::string xtraName, int instanceId) {
    return Datum(XtraInstance{std::move(xtraName), instanceId});
}

Datum Datum::scriptInstance(std::string scriptName, std::optional<CastMemberRef> scriptRef) {
    return Datum(std::make_shared<ScriptInstanceRef>(std::move(scriptName), scriptRef));
}

Datum Datum::scriptInstanceRef(std::shared_ptr<ScriptInstanceRef> instance) {
    return Datum(std::move(instance));
}

Datum Datum::argList(std::vector<Datum> args) {
    return Datum(std::make_shared<ArgList>(std::move(args)));
}

Datum Datum::argListNoRet(std::vector<Datum> args) {
    return Datum(std::make_shared<ArgListNoRet>(std::move(args)));
}

Datum Datum::timeoutRef(std::string name) {
    return Datum(TimeoutRef{std::move(name)});
}

Datum Datum::varRef(id::VarType varType, int rawIndex) {
    return Datum(VarRef{varType, rawIndex});
}

Datum Datum::chunkRef(id::VarType varType, int rawIndex, StringChunkType chunkType, int start, int end) {
    return Datum(ChunkRef{varType, rawIndex, chunkType, start, end});
}

DatumType Datum::type() const {
    if (std::holds_alternative<Null>(value_)) return DatumType::Null;
    if (std::holds_alternative<Void>(value_)) return DatumType::Void;
    if (std::holds_alternative<Int>(value_)) return DatumType::Int;
    if (std::holds_alternative<DFloat>(value_)) return DatumType::Float;
    if (std::holds_alternative<Str>(value_)) return DatumType::String;
    if (std::holds_alternative<FieldText>(value_)) return DatumType::FieldText;
    if (std::holds_alternative<Symbol>(value_)) return DatumType::Symbol;
    if (std::holds_alternative<ListPtr>(value_)) return DatumType::List;
    if (std::holds_alternative<PropListPtr>(value_)) return DatumType::PropList;
    if (std::holds_alternative<StringChunk>(value_)) return DatumType::StringChunk;
    if (std::holds_alternative<CastMemberRef>(value_)) return DatumType::CastMemberRef;
    if (std::holds_alternative<CastLibRef>(value_)) return DatumType::CastLibRef;
    if (std::holds_alternative<CastLibMemberAccessor>(value_)) return DatumType::CastLibMemberAccessor;
    if (std::holds_alternative<SpriteRef>(value_)) return DatumType::SpriteRef;
    if (std::holds_alternative<StageRef>(value_)) return DatumType::StageRef;
    if (std::holds_alternative<ScriptRef>(value_)) return DatumType::ScriptRef;
    if (std::holds_alternative<ScriptInstancePtr>(value_)) return DatumType::ScriptInstanceRef;
    if (std::holds_alternative<Stage>(value_)) return DatumType::StageRef;
    if (std::holds_alternative<PlayerRef>(value_)) return DatumType::PlayerRef;
    if (std::holds_alternative<MovieRef>(value_)) return DatumType::MovieRef;
    if (std::holds_alternative<IntPointPtr>(value_)) return DatumType::IntPoint;
    if (std::holds_alternative<IntRectPtr>(value_)) return DatumType::IntRect;
    if (std::holds_alternative<Vector3>(value_)) return DatumType::Vector;
    if (std::holds_alternative<ColorRef>(value_)) return DatumType::ColorRef;
    if (std::holds_alternative<Media>(value_)) return DatumType::Media;
    if (std::holds_alternative<ImageRef>(value_)) return DatumType::ImageRef;
    if (std::holds_alternative<BitmapRef>(value_)) return DatumType::BitmapRef;
    if (std::holds_alternative<PaletteRef>(value_)) return DatumType::PaletteRef;
    if (std::holds_alternative<Matte>(value_)) return DatumType::Matte;
    if (std::holds_alternative<SoundRef>(value_)) return DatumType::SoundRef;
    if (std::holds_alternative<SoundChannel>(value_)) return DatumType::SoundChannel;
    if (std::holds_alternative<CursorRef>(value_)) return DatumType::CursorRef;
    if (std::holds_alternative<TimeoutRef>(value_)) return DatumType::TimeoutRef;
    if (std::holds_alternative<Xtra>(value_)) return DatumType::Xtra;
    if (std::holds_alternative<XtraInstance>(value_)) return DatumType::XtraInstance;
    if (std::holds_alternative<XmlRef>(value_)) return DatumType::XmlRef;
    if (std::holds_alternative<DateRef>(value_)) return DatumType::DateRef;
    if (std::holds_alternative<MathRef>(value_)) return DatumType::MathRef;
    if (std::holds_alternative<VarRef>(value_)) return DatumType::VarRef;
    if (std::holds_alternative<ChunkRef>(value_)) return DatumType::ChunkRef;
    if (std::holds_alternative<ArgListPtr>(value_)) return DatumType::ArgList;
    return DatumType::ArgListNoRet;
}

std::string Datum::typeString() const {
    return std::string(typeName(type()));
}

bool Datum::isNull() const { return std::holds_alternative<Null>(value_); }
bool Datum::isVoid() const { return std::holds_alternative<Void>(value_); }
bool Datum::isInt() const { return std::holds_alternative<Int>(value_); }
bool Datum::isFloat() const { return std::holds_alternative<DFloat>(value_); }
bool Datum::isNumber() const { return isInt() || isFloat(); }
bool Datum::isString() const {
    return std::holds_alternative<Str>(value_) ||
           std::holds_alternative<FieldText>(value_) ||
           std::holds_alternative<StringChunk>(value_);
}
bool Datum::isSymbol() const { return std::holds_alternative<Symbol>(value_); }
bool Datum::isList() const { return std::holds_alternative<ListPtr>(value_); }
bool Datum::isPropList() const { return std::holds_alternative<PropListPtr>(value_); }

int Datum::intValue() const {
    if (const auto* value = std::get_if<Int>(&value_)) return value->value;
    if (const auto* value = std::get_if<DFloat>(&value_)) return static_cast<int>(value->value);
    if (const auto* value = std::get_if<Str>(&value_)) return parseIntSafe(value->value);
    if (const auto* value = std::get_if<FieldText>(&value_)) return parseIntSafe(value->value);
    if (std::holds_alternative<Void>(value_)) return 0;
    throw LingoException("Cannot convert " + typeString() + " to int");
}

float Datum::floatValue() const {
    if (const auto* value = std::get_if<DFloat>(&value_)) return value->value;
    if (const auto* value = std::get_if<Int>(&value_)) return static_cast<float>(value->value);
    if (const auto* value = std::get_if<Str>(&value_)) return parseFloatSafe(value->value);
    if (const auto* value = std::get_if<FieldText>(&value_)) return parseFloatSafe(value->value);
    if (std::holds_alternative<Void>(value_)) return 0.0F;
    throw LingoException("Cannot convert " + typeString() + " to float");
}

std::string Datum::stringValue() const {
    if (const auto* value = std::get_if<Str>(&value_)) return value->value;
    if (const auto* value = std::get_if<FieldText>(&value_)) return value->value;
    if (const auto* value = std::get_if<StringChunk>(&value_)) return value->value;
    if (const auto* value = std::get_if<Int>(&value_)) return std::to_string(value->value);
    if (const auto* value = std::get_if<DFloat>(&value_)) return floatToString(value->value);
    if (const auto* value = std::get_if<Symbol>(&value_)) return value->name;
    if (const auto* value = std::get_if<TimeoutRef>(&value_)) return value->name;
    if (const auto* value = std::get_if<CastLibMemberAccessor>(&value_)) {
        return "castLib(" + std::to_string(value->castLib) + ").member";
    }
    if (const auto* value = std::get_if<ChunkRef>(&value_)) {
        return "<chunkref:" + std::string(name(value->chunkType)) + "[" + std::to_string(value->start) + ".." +
               std::to_string(value->end) + "]>";
    }
    if (std::holds_alternative<Void>(value_)) return "VOID";
    if (std::holds_alternative<Null>(value_)) return "";
    if (const auto* value = std::get_if<IntPointPtr>(&value_)) {
        return "point(" + std::to_string((*value)->x) + ", " + std::to_string((*value)->y) + ")";
    }
    if (const auto* value = std::get_if<IntRectPtr>(&value_)) {
        return "rect(" + std::to_string((*value)->left) + ", " + std::to_string((*value)->top) + ", " +
               std::to_string((*value)->right) + ", " + std::to_string((*value)->bottom) + ")";
    }
    if (const auto* value = std::get_if<Vector3>(&value_)) {
        return "vector(" + fixedFloat(value->x) + ", " + fixedFloat(value->y) + ", " + fixedFloat(value->z) + ")";
    }
    if (const auto* value = std::get_if<Media>(&value_)) {
        return "<media " + std::to_string(value->bytes.size()) + " bytes>";
    }
    throw LingoException("Cannot convert " + typeString() + " to string");
}

bool Datum::boolValue() const {
    if (const auto* value = std::get_if<Int>(&value_)) return value->value != 0;
    if (const auto* value = std::get_if<DFloat>(&value_)) return value->value != 0.0F;
    if (const auto* value = std::get_if<Str>(&value_)) return !value->value.empty();
    if (const auto* value = std::get_if<FieldText>(&value_)) return !value->value.empty();
    if (std::holds_alternative<Symbol>(value_)) return true;
    if (std::holds_alternative<Void>(value_) || std::holds_alternative<Null>(value_)) return false;
    throw LingoException("Cannot convert " + typeString() + " to bool");
}

const Datum::Int* Datum::asInt() const { return std::get_if<Int>(&value_); }
const Datum::DFloat* Datum::asFloat() const { return std::get_if<DFloat>(&value_); }
const Datum::Str* Datum::asString() const { return std::get_if<Str>(&value_); }
const Datum::FieldText* Datum::asFieldText() const { return std::get_if<FieldText>(&value_); }
const Datum::StringChunk* Datum::asStringChunk() const { return std::get_if<StringChunk>(&value_); }
const Datum::Symbol* Datum::asSymbol() const { return std::get_if<Symbol>(&value_); }
const Datum::CastLibRef* Datum::asCastLibRef() const { return std::get_if<CastLibRef>(&value_); }
const Datum::CastLibMemberAccessor* Datum::asCastLibMemberAccessor() const {
    return std::get_if<CastLibMemberAccessor>(&value_);
}
const Datum::CastMemberRef* Datum::asCastMemberRef() const { return std::get_if<CastMemberRef>(&value_); }
const Datum::ScriptRef* Datum::asScriptRef() const { return std::get_if<ScriptRef>(&value_); }
const Datum::SpriteRef* Datum::asSpriteRef() const { return std::get_if<SpriteRef>(&value_); }
const Datum::ColorRef* Datum::asColorRef() const { return std::get_if<ColorRef>(&value_); }
const Datum::Media* Datum::asMedia() const { return std::get_if<Media>(&value_); }
const Datum::ImageRef* Datum::asImageRef() const { return std::get_if<ImageRef>(&value_); }
const Datum::SoundRef* Datum::asSoundRef() const { return std::get_if<SoundRef>(&value_); }
const Datum::SoundChannel* Datum::asSoundChannel() const { return std::get_if<SoundChannel>(&value_); }
const Datum::Xtra* Datum::asXtra() const { return std::get_if<Xtra>(&value_); }
const Datum::XtraInstance* Datum::asXtraInstance() const { return std::get_if<XtraInstance>(&value_); }
const Datum::TimeoutRef* Datum::asTimeoutRef() const { return std::get_if<TimeoutRef>(&value_); }
const Datum::VarRef* Datum::asVarRef() const { return std::get_if<VarRef>(&value_); }
const Datum::ChunkRef* Datum::asChunkRef() const { return std::get_if<ChunkRef>(&value_); }
Datum::IntPoint* Datum::asIntPoint() {
    auto* value = std::get_if<IntPointPtr>(&value_);
    return value != nullptr ? value->get() : nullptr;
}

const Datum::IntPoint* Datum::asIntPoint() const {
    const auto* value = std::get_if<IntPointPtr>(&value_);
    return value != nullptr ? value->get() : nullptr;
}

Datum::IntRect* Datum::asIntRect() {
    auto* value = std::get_if<IntRectPtr>(&value_);
    return value != nullptr ? value->get() : nullptr;
}

const Datum::IntRect* Datum::asIntRect() const {
    const auto* value = std::get_if<IntRectPtr>(&value_);
    return value != nullptr ? value->get() : nullptr;
}

Datum::List& Datum::listValue() {
    auto* value = std::get_if<ListPtr>(&value_);
    if (value == nullptr || !*value) {
        throw LingoException("Cannot convert " + typeString() + " to list");
    }
    return **value;
}

const Datum::List& Datum::listValue() const {
    const auto* value = std::get_if<ListPtr>(&value_);
    if (value == nullptr || !*value) {
        throw LingoException("Cannot convert " + typeString() + " to list");
    }
    return **value;
}

Datum::PropList& Datum::propListValue() {
    auto* value = std::get_if<PropListPtr>(&value_);
    if (value == nullptr || !*value) {
        throw LingoException("Cannot convert " + typeString() + " to prop_list");
    }
    return **value;
}

const Datum::PropList& Datum::propListValue() const {
    const auto* value = std::get_if<PropListPtr>(&value_);
    if (value == nullptr || !*value) {
        throw LingoException("Cannot convert " + typeString() + " to prop_list");
    }
    return **value;
}

Datum::ScriptInstanceRef& Datum::scriptInstanceValue() {
    auto* value = std::get_if<ScriptInstancePtr>(&value_);
    if (value == nullptr || !*value) {
        throw LingoException("Cannot convert " + typeString() + " to script_instance");
    }
    return **value;
}

const Datum::ScriptInstanceRef& Datum::scriptInstanceValue() const {
    const auto* value = std::get_if<ScriptInstancePtr>(&value_);
    if (value == nullptr || !*value) {
        throw LingoException("Cannot convert " + typeString() + " to script_instance");
    }
    return **value;
}

std::shared_ptr<Datum::ScriptInstanceRef> Datum::scriptInstancePtr() const {
    const auto* value = std::get_if<ScriptInstancePtr>(&value_);
    if (value == nullptr || !*value) {
        throw LingoException("Cannot convert " + typeString() + " to script_instance");
    }
    return *value;
}

const Datum::ArgList& Datum::argListValue() const {
    const auto* value = std::get_if<ArgListPtr>(&value_);
    if (value == nullptr || !*value) {
        throw LingoException("Cannot convert " + typeString() + " to arg_list");
    }
    return **value;
}

const Datum::ArgListNoRet& Datum::argListNoRetValue() const {
    const auto* value = std::get_if<ArgListNoRetPtr>(&value_);
    if (value == nullptr || !*value) {
        throw LingoException("Cannot convert " + typeString() + " to arg_list_no_ret");
    }
    return **value;
}

Datum Datum::deepCopy() const {
    if (isList()) {
        return listValue().deepCopyDatum();
    }

    if (isPropList()) {
        const auto& source = propListValue();
        Datum copied = Datum::propList(source.sorted());
        auto& copiedProperties = copied.propListValue().properties();
        copiedProperties.reserve(source.properties().size());
        for (const auto& entry : source.properties()) {
            copiedProperties.emplace_back(entry.first.deepCopy(), entry.second.deepCopy());
        }
        return copied;
    }

    if (const auto* point = asIntPoint()) {
        return Datum::intPoint(point->x, point->y);
    }

    if (const auto* rect = asIntRect()) {
        return Datum::intRect(rect->left, rect->top, rect->right, rect->bottom);
    }

    if (const auto* media = asMedia()) {
        return Datum::media(media->bytes);
    }

    if (const auto* image = asImageRef()) {
        return Datum::imageRef(image->bitmap != nullptr
            ? std::make_shared<bitmap::Bitmap>(image->bitmap->copy())
            : std::shared_ptr<bitmap::Bitmap>{});
    }

    if (const auto* chunk = std::get_if<StringChunk>(&value_)) {
        return Datum::stringChunk(chunk->source != nullptr ? chunk->source->deepCopy() : Datum::voidValue(),
                                  chunk->chunkType,
                                  chunk->start,
                                  chunk->end,
                                  chunk->itemDelimiter,
                                  chunk->value);
    }

    if (std::holds_alternative<ArgListPtr>(value_)) {
        std::vector<Datum> copied;
        copied.reserve(argListValue().args().size());
        for (const auto& arg : argListValue().args()) {
            copied.push_back(arg.deepCopy());
        }
        return Datum::argList(std::move(copied));
    }

    if (std::holds_alternative<ArgListNoRetPtr>(value_)) {
        std::vector<Datum> copied;
        copied.reserve(argListNoRetValue().args().size());
        for (const auto& arg : argListNoRetValue().args()) {
            copied.push_back(arg.deepCopy());
        }
        return Datum::argListNoRet(std::move(copied));
    }

    return *this;
}

Datum::List::List(std::vector<Datum> items, bool sorted)
    : items_(std::move(items)), sorted_(sorted) {}

void Datum::List::add(Datum item) {
    items_.push_back(std::move(item));
}

Datum Datum::List::getAt(int index) const {
    if (index < 1 || index > static_cast<int>(items_.size())) {
        throw LingoException("Index out of bounds: " + std::to_string(index));
    }
    return items_[static_cast<std::size_t>(index - 1)];
}

void Datum::List::setAt(int index, Datum value) {
    if (index < 1 || index > static_cast<int>(items_.size())) {
        throw LingoException("Index out of bounds: " + std::to_string(index));
    }
    items_[static_cast<std::size_t>(index - 1)] = std::move(value);
}

int Datum::List::count() const {
    return static_cast<int>(items_.size());
}

bool Datum::List::sorted() const {
    return sorted_;
}

const std::vector<Datum>& Datum::List::items() const {
    return items_;
}

std::vector<Datum>& Datum::List::items() {
    return items_;
}

Datum Datum::List::deepCopyDatum() const {
    std::vector<Datum> copied;
    copied.reserve(items_.size());
    for (const auto& item : items_) {
        copied.push_back(item.deepCopy());
    }
    return Datum::list(std::move(copied), sorted_);
}

bool operator==(const Datum::List& lhs, const Datum::List& rhs) {
    return lhs.sorted_ == rhs.sorted_ && lhs.items_ == rhs.items_;
}

Datum::PropList::PropList(bool sorted) : sorted_(sorted) {}

void Datum::PropList::put(Datum key, Datum value) {
    for (auto& entry : properties_) {
        if (entry.first == key) {
            entry.second = std::move(value);
            return;
        }
    }
    const bool indexWasClean = !indexDirty_;
    properties_.emplace_back(std::move(key), std::move(value));
    if (indexWasClean) {
        indexEntry(properties_.size() - 1);
    }
}

void Datum::PropList::putSameType(Datum key, Datum value) {
    const int index = findSameTypeKey(key);
    if (index >= 0) {
        properties_[static_cast<std::size_t>(index)].second = std::move(value);
        return;
    }
    properties_.emplace_back(std::move(key), std::move(value));
    indexEntry(properties_.size() - 1);
}

void Datum::PropList::putTyped(Datum key, Datum value) {
    const int index = findTypedKey(key);
    if (index >= 0) {
        properties_[static_cast<std::size_t>(index)].second = std::move(value);
        return;
    }
    properties_.emplace_back(std::move(key), std::move(value));
    indexEntry(properties_.size() - 1);
}

Datum Datum::PropList::get(const Datum& key) const {
    const int sameTypeIndex = findSameTypeKey(key);
    if (sameTypeIndex >= 0 && properties_[static_cast<std::size_t>(sameTypeIndex)].first == key) {
        return properties_[static_cast<std::size_t>(sameTypeIndex)].second;
    }
    for (const auto& entry : properties_) {
        if (entry.first == key) {
            return entry.second;
        }
    }
    return Datum::voidValue();
}

bool Datum::PropList::contains(const Datum& key) const {
    const int sameTypeIndex = findSameTypeKey(key);
    if (sameTypeIndex >= 0 && properties_[static_cast<std::size_t>(sameTypeIndex)].first == key) {
        return true;
    }
    return std::any_of(properties_.begin(), properties_.end(), [&](const auto& entry) {
        return entry.first == key;
    });
}

int Datum::PropList::findSameTypeKey(const Datum& key) const {
    rebuildIndex();
    const auto found = firstSameTypeKeyIndex_.find(propListSameTypeKey(key));
    return found != firstSameTypeKeyIndex_.end() ? found->second : -1;
}

int Datum::PropList::findTypedKey(const Datum& key) const {
    rebuildIndex();
    const auto sameType = firstSameTypeKeyIndex_.find(propListSameTypeKey(key));
    if (sameType != firstSameTypeKeyIndex_.end()) {
        return sameType->second;
    }
    const auto fallback = firstCaseSensitiveKeyIndex_.find(propListKeyName(key));
    return fallback != firstCaseSensitiveKeyIndex_.end() ? fallback->second : -1;
}

int Datum::PropList::findUntypedKey(const Datum& key) const {
    return findUntypedKeyName(propListKeyName(key));
}

int Datum::PropList::findUntypedKeyName(std::string_view keyName) const {
    rebuildIndex();
    const auto found = firstUntypedKeyIndex_.find(keyName);
    return found != firstUntypedKeyIndex_.end() ? found->second : -1;
}

int Datum::PropList::count() const {
    return static_cast<int>(properties_.size());
}

bool Datum::PropList::sorted() const {
    return sorted_;
}

const std::vector<std::pair<Datum, Datum>>& Datum::PropList::properties() const {
    return properties_;
}

std::vector<std::pair<Datum, Datum>>& Datum::PropList::properties() {
    invalidateIndex();
    return properties_;
}

void Datum::PropList::appendProperty(Datum key, Datum value) {
    const bool indexWasClean = !indexDirty_;
    properties_.emplace_back(std::move(key), std::move(value));
    if (indexWasClean) {
        indexEntry(properties_.size() - 1);
    }
}

bool Datum::PropList::erasePropertyAt(int zeroBasedIndex) {
    if (zeroBasedIndex < 0 || zeroBasedIndex >= static_cast<int>(properties_.size())) {
        return false;
    }
    properties_.erase(properties_.begin() + zeroBasedIndex);
    invalidateIndex();
    return true;
}

void Datum::PropList::invalidateIndex() const {
    indexDirty_ = true;
}

void Datum::PropList::indexEntry(std::size_t index) const {
    const int signedIndex = static_cast<int>(index);
    const auto& key = properties_[index].first;
    const std::string keyName = propListKeyName(key);
    firstSameTypeKeyIndex_.try_emplace(propListSameTypeKey(key), signedIndex);
    firstUntypedKeyIndex_.try_emplace(keyName, signedIndex);
    firstCaseSensitiveKeyIndex_.try_emplace(keyName, signedIndex);
}

void Datum::PropList::rebuildIndex() const {
    if (!indexDirty_) {
        return;
    }

    firstSameTypeKeyIndex_.clear();
    firstUntypedKeyIndex_.clear();
    firstCaseSensitiveKeyIndex_.clear();
    firstSameTypeKeyIndex_.reserve(properties_.size());
    firstUntypedKeyIndex_.reserve(properties_.size());
    firstCaseSensitiveKeyIndex_.reserve(properties_.size());
    for (std::size_t index = 0; index < properties_.size(); ++index) {
        indexEntry(index);
    }
    indexDirty_ = false;
}

bool operator==(const Datum::PropList& lhs, const Datum::PropList& rhs) {
    return lhs.sorted_ == rhs.sorted_ && lhs.properties_ == rhs.properties_;
}

Datum::ScriptInstanceRef::ScriptInstanceRef(std::string scriptName, std::optional<CastMemberRef> scriptRef)
    : scriptName_(std::move(scriptName)), scriptRef_(scriptRef), identityId_(nextScriptInstanceIdentityId()) {}

const std::string& Datum::ScriptInstanceRef::scriptName() const {
    return scriptName_;
}

const std::optional<Datum::CastMemberRef>& Datum::ScriptInstanceRef::scriptRef() const {
    return scriptRef_;
}

std::uint64_t Datum::ScriptInstanceRef::identityId() const {
    return identityId_;
}

std::shared_ptr<Datum::ScriptInstanceRef> Datum::ScriptInstanceRef::ancestor() const {
    return ancestor_;
}

void Datum::ScriptInstanceRef::setAncestor(std::shared_ptr<ScriptInstanceRef> ancestor) {
    ancestor_ = std::move(ancestor);
}

Datum Datum::ScriptInstanceRef::getProperty(const std::string& name) const {
    if (equalsIgnoreCase(name, "ancestor")) {
        if (ancestor_) {
            return Datum(ancestor_);
        }
        return Datum::voidValue();
    }

    const auto* current = this;
    for (int depth = 0; current != nullptr && depth < kMaxAncestorDepth; ++depth) {
        const int propertyIndex = current->findPropertyIndex(name);
        if (propertyIndex >= 0) {
            return current->properties_[static_cast<std::size_t>(propertyIndex)].second;
        }
        current = current->ancestor_.get();
    }

    return Datum::voidValue();
}

void Datum::ScriptInstanceRef::setProperty(const std::string& name, Datum value) {
    if (equalsIgnoreCase(name, "ancestor")) {
        if (const auto* ancestor = std::get_if<ScriptInstancePtr>(&value.value_)) {
            ancestor_ = *ancestor;
        } else if (value.isVoid()) {
            ancestor_.reset();
        }
        return;
    }

    const int localIndex = findPropertyIndex(name);
    if (localIndex >= 0) {
        properties_[static_cast<std::size_t>(localIndex)].second = std::move(value);
        return;
    }

    auto* current = ancestor_.get();
    for (int depth = 1; current != nullptr && depth < kMaxAncestorDepth; ++depth) {
        const int inheritedIndex = current->findPropertyIndex(name);
        if (inheritedIndex >= 0) {
            current->properties_[static_cast<std::size_t>(inheritedIndex)].second = std::move(value);
            return;
        }
        current = current->ancestor_.get();
    }

    const bool indexWasClean = !propertyIndexDirty_;
    properties_.emplace_back(name, std::move(value));
    if (indexWasClean) {
        indexPropertyEntry(properties_.size() - 1);
    }
}

bool Datum::ScriptInstanceRef::hasProperty(const std::string& name) const {
    const auto* current = this;
    for (int depth = 0; current != nullptr && depth < kMaxAncestorDepth; ++depth) {
        if (current->findPropertyIndex(name) >= 0) {
            return true;
        }
        current = current->ancestor_.get();
    }

    return false;
}

int Datum::ScriptInstanceRef::findExactPropertyIndex(std::string_view name) const {
    rebuildPropertyIndex();
    const auto found = firstExactPropertyIndex_.find(name);
    return found != firstExactPropertyIndex_.end() ? found->second : -1;
}

int Datum::ScriptInstanceRef::findCaseInsensitivePropertyIndex(std::string_view name) const {
    rebuildPropertyIndex();
    const auto found = firstCaseInsensitivePropertyIndex_.find(name);
    return found != firstCaseInsensitivePropertyIndex_.end() ? found->second : -1;
}

int Datum::ScriptInstanceRef::findPropertyIndex(std::string_view name) const {
    rebuildPropertyIndex();
    const auto exact = firstExactPropertyIndex_.find(name);
    if (exact != firstExactPropertyIndex_.end()) {
        return exact->second;
    }
    const auto caseInsensitive = firstCaseInsensitivePropertyIndex_.find(name);
    return caseInsensitive != firstCaseInsensitivePropertyIndex_.end() ? caseInsensitive->second : -1;
}

Datum* Datum::ScriptInstanceRef::findMutablePropertyValue(std::string_view name) {
    const int index = findPropertyIndex(name);
    return index >= 0 ? &properties_[static_cast<std::size_t>(index)].second : nullptr;
}

const std::vector<std::pair<std::string, Datum>>& Datum::ScriptInstanceRef::properties() const {
    return properties_;
}

std::vector<std::pair<std::string, Datum>>& Datum::ScriptInstanceRef::properties() {
    invalidatePropertyIndex();
    return properties_;
}

void Datum::ScriptInstanceRef::reserveLocalProperties(std::size_t additionalCount) {
    properties_.reserve(properties_.size() + additionalCount);
}

void Datum::ScriptInstanceRef::appendLocalProperty(std::string name, Datum value) {
    const bool indexWasClean = !propertyIndexDirty_;
    properties_.emplace_back(std::move(name), std::move(value));
    if (indexWasClean) {
        indexPropertyEntry(properties_.size() - 1);
    }
}

void Datum::ScriptInstanceRef::putLocalPropertyExact(std::string name, Datum value) {
    const int index = findExactPropertyIndex(name);
    if (index >= 0) {
        properties_[static_cast<std::size_t>(index)].second = std::move(value);
        return;
    }
    appendLocalProperty(std::move(name), std::move(value));
}

bool Datum::ScriptInstanceRef::eraseLocalPropertyExact(std::string_view name) {
    const int index = findExactPropertyIndex(name);
    if (index < 0) {
        return false;
    }
    properties_.erase(properties_.begin() + index);
    invalidatePropertyIndex();
    return true;
}

void Datum::ScriptInstanceRef::invalidatePropertyIndex() const {
    propertyIndexDirty_ = true;
}

void Datum::ScriptInstanceRef::indexPropertyEntry(std::size_t index) const {
    const int signedIndex = static_cast<int>(index);
    const auto& key = properties_[index].first;
    firstExactPropertyIndex_.try_emplace(key, signedIndex);
    firstCaseInsensitivePropertyIndex_.try_emplace(key, signedIndex);
}

void Datum::ScriptInstanceRef::rebuildPropertyIndex() const {
    if (!propertyIndexDirty_) {
        return;
    }

    firstExactPropertyIndex_.clear();
    firstCaseInsensitivePropertyIndex_.clear();
    firstExactPropertyIndex_.reserve(properties_.size());
    firstCaseInsensitivePropertyIndex_.reserve(properties_.size());
    for (std::size_t index = 0; index < properties_.size(); ++index) {
        indexPropertyEntry(index);
    }
    propertyIndexDirty_ = false;
}

Datum::ArgList::ArgList(std::vector<Datum> args) : args_(std::move(args)) {}

const std::vector<Datum>& Datum::ArgList::args() const {
    return args_;
}

bool operator==(const Datum::ArgList& lhs, const Datum::ArgList& rhs) {
    return lhs.args_ == rhs.args_;
}

Datum::ArgListNoRet::ArgListNoRet(std::vector<Datum> args) : args_(std::move(args)) {}

const std::vector<Datum>& Datum::ArgListNoRet::args() const {
    return args_;
}

bool operator==(const Datum::ArgListNoRet& lhs, const Datum::ArgListNoRet& rhs) {
    return lhs.args_ == rhs.args_;
}

bool operator==(const Datum& lhs, const Datum& rhs) {
    if (lhs.value_.index() != rhs.value_.index()) {
        return false;
    }

    if (auto value = std::get_if<Datum::Null>(&lhs.value_)) return *value == std::get<Datum::Null>(rhs.value_);
    if (auto value = std::get_if<Datum::Void>(&lhs.value_)) return *value == std::get<Datum::Void>(rhs.value_);
    if (auto value = std::get_if<Datum::Int>(&lhs.value_)) return *value == std::get<Datum::Int>(rhs.value_);
    if (auto value = std::get_if<Datum::DFloat>(&lhs.value_)) return *value == std::get<Datum::DFloat>(rhs.value_);
    if (auto value = std::get_if<Datum::Str>(&lhs.value_)) return *value == std::get<Datum::Str>(rhs.value_);
    if (auto value = std::get_if<Datum::FieldText>(&lhs.value_)) return *value == std::get<Datum::FieldText>(rhs.value_);
    if (auto value = std::get_if<Datum::Symbol>(&lhs.value_)) return *value == std::get<Datum::Symbol>(rhs.value_);
    if (auto value = std::get_if<Datum::ListPtr>(&lhs.value_)) return **value == **std::get_if<Datum::ListPtr>(&rhs.value_);
    if (auto value = std::get_if<Datum::PropListPtr>(&lhs.value_)) return **value == **std::get_if<Datum::PropListPtr>(&rhs.value_);
    if (auto value = std::get_if<Datum::StringChunk>(&lhs.value_)) {
        const auto& other = std::get<Datum::StringChunk>(rhs.value_);
        const bool sourcesEqual = (!value->source && !other.source) ||
                                  (value->source && other.source && *value->source == *other.source);
        return sourcesEqual &&
               value->chunkType == other.chunkType &&
               value->start == other.start &&
               value->end == other.end &&
               value->itemDelimiter == other.itemDelimiter &&
               value->value == other.value;
    }
    if (auto value = std::get_if<Datum::CastMemberRef>(&lhs.value_)) return *value == std::get<Datum::CastMemberRef>(rhs.value_);
    if (auto value = std::get_if<Datum::CastLibRef>(&lhs.value_)) return *value == std::get<Datum::CastLibRef>(rhs.value_);
    if (auto value = std::get_if<Datum::CastLibMemberAccessor>(&lhs.value_)) {
        return *value == std::get<Datum::CastLibMemberAccessor>(rhs.value_);
    }
    if (auto value = std::get_if<Datum::SpriteRef>(&lhs.value_)) return *value == std::get<Datum::SpriteRef>(rhs.value_);
    if (auto value = std::get_if<Datum::StageRef>(&lhs.value_)) return *value == std::get<Datum::StageRef>(rhs.value_);
    if (auto value = std::get_if<Datum::ScriptRef>(&lhs.value_)) return *value == std::get<Datum::ScriptRef>(rhs.value_);
    if (auto value = std::get_if<Datum::ScriptInstancePtr>(&lhs.value_)) return *value == std::get<Datum::ScriptInstancePtr>(rhs.value_);
    if (auto value = std::get_if<Datum::Stage>(&lhs.value_)) return *value == std::get<Datum::Stage>(rhs.value_);
    if (auto value = std::get_if<Datum::PlayerRef>(&lhs.value_)) return *value == std::get<Datum::PlayerRef>(rhs.value_);
    if (auto value = std::get_if<Datum::MovieRef>(&lhs.value_)) return *value == std::get<Datum::MovieRef>(rhs.value_);
    if (auto value = std::get_if<Datum::IntPointPtr>(&lhs.value_)) {
        const auto& other = std::get<Datum::IntPointPtr>(rhs.value_);
        return **value == *other;
    }
    if (auto value = std::get_if<Datum::IntRectPtr>(&lhs.value_)) {
        const auto& other = std::get<Datum::IntRectPtr>(rhs.value_);
        return **value == *other;
    }
    if (auto value = std::get_if<Datum::Vector3>(&lhs.value_)) return *value == std::get<Datum::Vector3>(rhs.value_);
    if (auto value = std::get_if<Datum::ColorRef>(&lhs.value_)) return *value == std::get<Datum::ColorRef>(rhs.value_);
    if (auto value = std::get_if<Datum::BitmapRef>(&lhs.value_)) return *value == std::get<Datum::BitmapRef>(rhs.value_);
    if (auto value = std::get_if<Datum::PaletteRef>(&lhs.value_)) return *value == std::get<Datum::PaletteRef>(rhs.value_);
    if (auto value = std::get_if<Datum::Matte>(&lhs.value_)) return *value == std::get<Datum::Matte>(rhs.value_);
    if (auto value = std::get_if<Datum::SoundRef>(&lhs.value_)) return *value == std::get<Datum::SoundRef>(rhs.value_);
    if (auto value = std::get_if<Datum::SoundChannel>(&lhs.value_)) return *value == std::get<Datum::SoundChannel>(rhs.value_);
    if (auto value = std::get_if<Datum::CursorRef>(&lhs.value_)) return *value == std::get<Datum::CursorRef>(rhs.value_);
    if (auto value = std::get_if<Datum::TimeoutRef>(&lhs.value_)) return *value == std::get<Datum::TimeoutRef>(rhs.value_);
    if (auto value = std::get_if<Datum::Xtra>(&lhs.value_)) return *value == std::get<Datum::Xtra>(rhs.value_);
    if (auto value = std::get_if<Datum::XtraInstance>(&lhs.value_)) return *value == std::get<Datum::XtraInstance>(rhs.value_);
    if (auto value = std::get_if<Datum::XmlRef>(&lhs.value_)) return *value == std::get<Datum::XmlRef>(rhs.value_);
    if (auto value = std::get_if<Datum::DateRef>(&lhs.value_)) return *value == std::get<Datum::DateRef>(rhs.value_);
    if (auto value = std::get_if<Datum::MathRef>(&lhs.value_)) return *value == std::get<Datum::MathRef>(rhs.value_);
    if (auto value = std::get_if<Datum::VarRef>(&lhs.value_)) return *value == std::get<Datum::VarRef>(rhs.value_);
    if (auto value = std::get_if<Datum::ChunkRef>(&lhs.value_)) return *value == std::get<Datum::ChunkRef>(rhs.value_);
    if (auto value = std::get_if<Datum::ArgListPtr>(&lhs.value_)) return **value == **std::get_if<Datum::ArgListPtr>(&rhs.value_);
    return **std::get_if<Datum::ArgListNoRetPtr>(&lhs.value_) == **std::get_if<Datum::ArgListNoRetPtr>(&rhs.value_);
}

} // namespace libreshockwave::lingo
