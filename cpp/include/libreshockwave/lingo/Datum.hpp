#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "libreshockwave/id/Ids.hpp"

namespace libreshockwave::bitmap {
class Bitmap;
} // namespace libreshockwave::bitmap

namespace libreshockwave::lingo {

class LingoException : public std::runtime_error {
public:
    explicit LingoException(const std::string& message) : std::runtime_error(message) {}

    [[nodiscard]] static LingoException typeMismatch(const std::string& expected, const std::string& actual);
    [[nodiscard]] static LingoException undefinedHandler(const std::string& name);
    [[nodiscard]] static LingoException undefinedVariable(const std::string& name);
    [[nodiscard]] static LingoException indexOutOfBounds(int index, int size);
};

struct TransparentStringHash {
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }

    [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }

    [[nodiscard]] std::size_t operator()(const char* value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }
};

struct TransparentCaseInsensitiveStringHash {
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept;
    [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept {
        return (*this)(std::string_view(value));
    }
    [[nodiscard]] std::size_t operator()(const char* value) const noexcept {
        return (*this)(std::string_view(value));
    }
};

struct TransparentCaseInsensitiveStringEqual {
    using is_transparent = void;

    [[nodiscard]] bool operator()(std::string_view lhs, std::string_view rhs) const noexcept;
    [[nodiscard]] bool operator()(const std::string& lhs, std::string_view rhs) const noexcept {
        return (*this)(std::string_view(lhs), rhs);
    }
    [[nodiscard]] bool operator()(std::string_view lhs, const std::string& rhs) const noexcept {
        return (*this)(lhs, std::string_view(rhs));
    }
    [[nodiscard]] bool operator()(const std::string& lhs, const std::string& rhs) const noexcept {
        return (*this)(std::string_view(lhs), std::string_view(rhs));
    }
    [[nodiscard]] bool operator()(const char* lhs, std::string_view rhs) const noexcept {
        return (*this)(std::string_view(lhs), rhs);
    }
    [[nodiscard]] bool operator()(std::string_view lhs, const char* rhs) const noexcept {
        return (*this)(lhs, std::string_view(rhs));
    }
    [[nodiscard]] bool operator()(const char* lhs, const std::string& rhs) const noexcept {
        return (*this)(std::string_view(lhs), std::string_view(rhs));
    }
    [[nodiscard]] bool operator()(const std::string& lhs, const char* rhs) const noexcept {
        return (*this)(std::string_view(lhs), std::string_view(rhs));
    }
    [[nodiscard]] bool operator()(const char* lhs, const char* rhs) const noexcept {
        return (*this)(std::string_view(lhs), std::string_view(rhs));
    }
};

enum class DatumType {
    Null,
    Void,
    Int,
    Float,
    String,
    FieldText,
    StringChunk,
    Symbol,
    VarRef,
    ChunkRef,
    List,
    PropList,
    ArgList,
    ArgListNoRet,
    CastLibRef,
    CastLibMemberAccessor,
    CastMemberRef,
    ScriptRef,
    ScriptInstanceRef,
    SpriteRef,
    StageRef,
    PlayerRef,
    MovieRef,
    IntPoint,
    IntRect,
    Vector,
    ColorRef,
    Media,
    ImageRef,
    BitmapRef,
    PaletteRef,
    Matte,
    SoundRef,
    SoundChannel,
    CursorRef,
    TimeoutRef,
    Xtra,
    XtraInstance,
    XmlRef,
    DateRef,
    MathRef
};

[[nodiscard]] std::string_view typeName(DatumType type);

enum class StringChunkType {
    Item = 0x01,
    Word = 0x02,
    Char = 0x03,
    Line = 0x04
};

[[nodiscard]] int code(StringChunkType type);
[[nodiscard]] std::string_view name(StringChunkType type);
[[nodiscard]] StringChunkType stringChunkTypeFromCode(int code);
[[nodiscard]] StringChunkType stringChunkTypeFromName(std::string_view name);

class Datum {
public:
    class List;
    class PropList;
    class ScriptInstanceRef;
    class ArgList;
    class ArgListNoRet;

    struct Null {
        friend bool operator==(const Null&, const Null&) = default;
    };

    struct Void {
        friend bool operator==(const Void&, const Void&) = default;
    };

    struct Int {
        int value;
        friend bool operator==(const Int&, const Int&) = default;
    };

    struct DFloat {
        float value;
        friend bool operator==(const DFloat&, const DFloat&) = default;
    };

    struct Str {
        std::string value;
        friend bool operator==(const Str&, const Str&) = default;
    };

    struct FieldText {
        std::string value;
        int castLib;
        int memberNum;
        std::uint64_t revision = 0;
        friend bool operator==(const FieldText&, const FieldText&) = default;
    };

    struct Symbol {
        std::string name;
        friend bool operator==(const Symbol&, const Symbol&) = default;
    };

    struct StringChunk {
        std::shared_ptr<Datum> source;
        StringChunkType chunkType;
        int start;
        int end;
        char itemDelimiter;
        std::string value;
    };

    struct CastMemberRef {
        int castLib;
        int castMember;

        [[nodiscard]] static CastMemberRef of(id::CastLibId castLib, id::MemberId member);
        [[nodiscard]] std::optional<id::CastLibId> castLibId() const;
        [[nodiscard]] std::optional<id::MemberId> memberId() const;
        [[nodiscard]] int memberNum() const;

        friend bool operator==(const CastMemberRef&, const CastMemberRef&) = default;
    };

    struct CastLibRef {
        int castLib;

        [[nodiscard]] static CastLibRef of(id::CastLibId castLib);
        [[nodiscard]] id::CastLibId castLibId() const;

        friend bool operator==(const CastLibRef&, const CastLibRef&) = default;
    };

    struct CastLibMemberAccessor {
        int castLib;

        [[nodiscard]] static CastLibMemberAccessor of(id::CastLibId castLib);
        [[nodiscard]] id::CastLibId castLibId() const;

        friend bool operator==(const CastLibMemberAccessor&, const CastLibMemberAccessor&) = default;
    };

    struct SpriteRef {
        int channel;

        [[nodiscard]] static SpriteRef of(id::ChannelId channel);
        [[nodiscard]] id::ChannelId channelId() const;
        [[nodiscard]] int spriteNum() const;

        friend bool operator==(const SpriteRef&, const SpriteRef&) = default;
    };

    struct StageRef {
        friend bool operator==(const StageRef&, const StageRef&) = default;
    };

    struct ScriptRef {
        CastMemberRef memberRef;
        friend bool operator==(const ScriptRef&, const ScriptRef&) = default;
    };

    struct Stage {
        friend bool operator==(const Stage&, const Stage&) = default;
    };

    struct PlayerRef {
        friend bool operator==(const PlayerRef&, const PlayerRef&) = default;
    };

    struct MovieRef {
        friend bool operator==(const MovieRef&, const MovieRef&) = default;
    };

    struct IntPoint {
        int x;
        int y;
        friend bool operator==(const IntPoint&, const IntPoint&) = default;
    };

    struct IntRect {
        int left;
        int top;
        int right;
        int bottom;

        [[nodiscard]] int width() const;
        [[nodiscard]] int height() const;

        friend bool operator==(const IntRect&, const IntRect&) = default;
    };

    struct Vector3 {
        float x;
        float y;
        float z;
        friend bool operator==(const Vector3&, const Vector3&) = default;
    };

    struct ColorRef {
        int r;
        int g;
        int b;
        std::optional<int> paletteIndex;

        [[nodiscard]] static ColorRef fromPaletteIndex(int index);
        [[nodiscard]] static ColorRef fromRgb(int r, int g, int b);

        friend bool operator==(const ColorRef&, const ColorRef&) = default;
    };

    struct ImageRef {
        std::shared_ptr<bitmap::Bitmap> bitmap;
        std::function<void(bitmap::Bitmap&)> mutationCallback;
        friend bool operator==(const ImageRef& lhs, const ImageRef& rhs) {
            return lhs.bitmap == rhs.bitmap;
        }
    };

    struct Media {
        std::vector<std::uint8_t> bytes;
        friend bool operator==(const Media&, const Media&) = default;
    };

    struct BitmapRef {
        int bitmapId;
        friend bool operator==(const BitmapRef&, const BitmapRef&) = default;
    };

    struct PaletteRef {
        int paletteId;
        friend bool operator==(const PaletteRef&, const PaletteRef&) = default;
    };

    struct Matte {
        friend bool operator==(const Matte&, const Matte&) = default;
    };

    struct SoundRef {
        int soundNum;
        friend bool operator==(const SoundRef&, const SoundRef&) = default;
    };

    struct SoundChannel {
        int channel;
        friend bool operator==(const SoundChannel&, const SoundChannel&) = default;
    };

    struct CursorRef {
        int cursorId;
        friend bool operator==(const CursorRef&, const CursorRef&) = default;
    };

    struct TimeoutRef {
        std::string name;
        friend bool operator==(const TimeoutRef&, const TimeoutRef&) = default;
    };

    struct Xtra {
        std::string name;
        friend bool operator==(const Xtra&, const Xtra&) = default;
    };

    struct XtraInstance {
        std::string xtraName;
        int instanceId;
        friend bool operator==(const XtraInstance&, const XtraInstance&) = default;
    };

    struct XmlRef {
        int xmlId;
        friend bool operator==(const XmlRef&, const XmlRef&) = default;
    };

    struct DateRef {
        int dateId;
        friend bool operator==(const DateRef&, const DateRef&) = default;
    };

    struct MathRef {
        int mathId;
        friend bool operator==(const MathRef&, const MathRef&) = default;
    };

    struct VarRef {
        id::VarType varType;
        int rawIndex;
        friend bool operator==(const VarRef&, const VarRef&) = default;
    };

    struct ChunkRef {
        id::VarType varType;
        int rawIndex;
        StringChunkType chunkType;
        int start;
        int end;
        friend bool operator==(const ChunkRef&, const ChunkRef&) = default;
    };

    Datum() : value_(Void{}) {}

    [[nodiscard]] static Datum nullValue();
    [[nodiscard]] static Datum voidValue();
    [[nodiscard]] static Datum of(int value);
    [[nodiscard]] static Datum of(float value);
    [[nodiscard]] static Datum of(double value);
    [[nodiscard]] static Datum of(std::string value);
    [[nodiscard]] static Datum fieldText(std::string value, int castLib, int memberNum, std::uint64_t revision = 0);
    [[nodiscard]] static Datum symbol(std::string name);
    [[nodiscard]] static Datum list(std::vector<Datum> items = {}, bool sorted = false);
    [[nodiscard]] static Datum propList(bool sorted = false);
    [[nodiscard]] static Datum stringChunk(Datum source, StringChunkType chunkType, int start, int end, char itemDelimiter, std::string value);
    [[nodiscard]] static Datum castMemberRef(CastMemberRef ref);
    [[nodiscard]] static Datum castMemberRef(id::CastLibId castLib, id::MemberId member);
    [[nodiscard]] static Datum castLibRef(id::CastLibId castLib);
    [[nodiscard]] static Datum castLibMemberAccessor(id::CastLibId castLib);
    [[nodiscard]] static Datum spriteRef(id::ChannelId channel);
    [[nodiscard]] static Datum stageRef();
    [[nodiscard]] static Datum scriptRef(CastMemberRef memberRef);
    [[nodiscard]] static Datum playerRef();
    [[nodiscard]] static Datum movieRef();
    [[nodiscard]] static Datum intPoint(int x, int y);
    [[nodiscard]] static Datum intRect(int left, int top, int right, int bottom);
    [[nodiscard]] static Datum vector3(float x, float y, float z);
    [[nodiscard]] static Datum colorRef(int r, int g, int b);
    [[nodiscard]] static Datum paletteIndexColor(int index);
    [[nodiscard]] static Datum media(std::vector<std::uint8_t> bytes);
    [[nodiscard]] static Datum imageRef(std::shared_ptr<bitmap::Bitmap> bitmap);
    [[nodiscard]] static Datum imageRef(std::shared_ptr<bitmap::Bitmap> bitmap,
                                        std::function<void(bitmap::Bitmap&)> mutationCallback);
    [[nodiscard]] static Datum soundRef(int soundNum = 0);
    [[nodiscard]] static Datum soundChannel(int channel);
    [[nodiscard]] static Datum xtra(std::string name);
    [[nodiscard]] static Datum xtraInstance(std::string xtraName, int instanceId);
    [[nodiscard]] static Datum scriptInstance(std::string scriptName, std::optional<CastMemberRef> scriptRef = std::nullopt);
    [[nodiscard]] static Datum scriptInstanceRef(std::shared_ptr<ScriptInstanceRef> instance);
    [[nodiscard]] static Datum argList(std::vector<Datum> args);
    [[nodiscard]] static Datum argListNoRet(std::vector<Datum> args);
    [[nodiscard]] static Datum timeoutRef(std::string name);
    [[nodiscard]] static Datum varRef(id::VarType varType, int rawIndex);
    [[nodiscard]] static Datum chunkRef(id::VarType varType, int rawIndex, StringChunkType chunkType, int start, int end);

    [[nodiscard]] DatumType type() const;
    [[nodiscard]] std::string typeString() const;

    [[nodiscard]] bool isNull() const;
    [[nodiscard]] bool isVoid() const;
    [[nodiscard]] bool isInt() const;
    [[nodiscard]] bool isFloat() const;
    [[nodiscard]] bool isNumber() const;
    [[nodiscard]] bool isString() const;
    [[nodiscard]] bool isSymbol() const;
    [[nodiscard]] bool isList() const;
    [[nodiscard]] bool isPropList() const;

    [[nodiscard]] int intValue() const;
    [[nodiscard]] float floatValue() const;
    [[nodiscard]] std::string stringValue() const;
    [[nodiscard]] bool boolValue() const;

    [[nodiscard]] const Int* asInt() const;
    [[nodiscard]] const DFloat* asFloat() const;
    [[nodiscard]] const Str* asString() const;
    [[nodiscard]] const FieldText* asFieldText() const;
    [[nodiscard]] const StringChunk* asStringChunk() const;
    [[nodiscard]] const Symbol* asSymbol() const;
    [[nodiscard]] const CastLibRef* asCastLibRef() const;
    [[nodiscard]] const CastLibMemberAccessor* asCastLibMemberAccessor() const;
    [[nodiscard]] const CastMemberRef* asCastMemberRef() const;
    [[nodiscard]] const ScriptRef* asScriptRef() const;
    [[nodiscard]] const SpriteRef* asSpriteRef() const;
    [[nodiscard]] const ColorRef* asColorRef() const;
    [[nodiscard]] const Media* asMedia() const;
    [[nodiscard]] const ImageRef* asImageRef() const;
    [[nodiscard]] const SoundRef* asSoundRef() const;
    [[nodiscard]] const SoundChannel* asSoundChannel() const;
    [[nodiscard]] const Xtra* asXtra() const;
    [[nodiscard]] const XtraInstance* asXtraInstance() const;
    [[nodiscard]] const TimeoutRef* asTimeoutRef() const;
    [[nodiscard]] const VarRef* asVarRef() const;
    [[nodiscard]] const ChunkRef* asChunkRef() const;
    [[nodiscard]] IntPoint* asIntPoint();
    [[nodiscard]] const IntPoint* asIntPoint() const;
    [[nodiscard]] IntRect* asIntRect();
    [[nodiscard]] const IntRect* asIntRect() const;

    [[nodiscard]] List& listValue();
    [[nodiscard]] const List& listValue() const;
    [[nodiscard]] PropList& propListValue();
    [[nodiscard]] const PropList& propListValue() const;
    [[nodiscard]] ScriptInstanceRef& scriptInstanceValue();
    [[nodiscard]] const ScriptInstanceRef& scriptInstanceValue() const;
    [[nodiscard]] std::shared_ptr<ScriptInstanceRef> scriptInstancePtr() const;
    [[nodiscard]] const ArgList& argListValue() const;
    [[nodiscard]] const ArgListNoRet& argListNoRetValue() const;

    [[nodiscard]] Datum deepCopy() const;

    friend bool operator==(const Datum& lhs, const Datum& rhs);
    friend bool operator!=(const Datum& lhs, const Datum& rhs) { return !(lhs == rhs); }

    static const Datum TRUE;
    static const Datum FALSE;

private:
    using ListPtr = std::shared_ptr<List>;
    using PropListPtr = std::shared_ptr<PropList>;
    using ScriptInstancePtr = std::shared_ptr<ScriptInstanceRef>;
    using ArgListPtr = std::shared_ptr<ArgList>;
    using ArgListNoRetPtr = std::shared_ptr<ArgListNoRet>;
    using IntPointPtr = std::shared_ptr<IntPoint>;
    using IntRectPtr = std::shared_ptr<IntRect>;
    using Value = std::variant<
        Null,
        Void,
        Int,
        DFloat,
        Str,
        FieldText,
        Symbol,
        ListPtr,
        PropListPtr,
        StringChunk,
        CastMemberRef,
        CastLibRef,
        CastLibMemberAccessor,
        SpriteRef,
        StageRef,
        ScriptRef,
        ScriptInstancePtr,
        Stage,
        PlayerRef,
        MovieRef,
        IntPointPtr,
        IntRectPtr,
        Vector3,
        ColorRef,
        Media,
        ImageRef,
        BitmapRef,
        PaletteRef,
        Matte,
        SoundRef,
        SoundChannel,
        CursorRef,
        TimeoutRef,
        Xtra,
        XtraInstance,
        XmlRef,
        DateRef,
        MathRef,
        VarRef,
        ChunkRef,
        ArgListPtr,
        ArgListNoRetPtr>;

    explicit Datum(Value value) : value_(std::move(value)) {}

    Value value_;
};

inline Datum Datum::nullValue() {
    return Datum(Null{});
}

inline Datum Datum::voidValue() {
    return Datum(Void{});
}

inline Datum Datum::of(int value) {
    return Datum(Int{value});
}

inline Datum Datum::of(float value) {
    return Datum(DFloat{value});
}

inline Datum Datum::of(double value) {
    return Datum(DFloat{static_cast<float>(value)});
}

inline Datum Datum::of(std::string value) {
    return Datum(Str{std::move(value)});
}

class Datum::List {
public:
    explicit List(std::vector<Datum> items = {}, bool sorted = false);

    void add(Datum item);
    [[nodiscard]] Datum getAt(int index) const;
    void setAt(int index, Datum value);
    [[nodiscard]] int count() const;
    [[nodiscard]] bool sorted() const;
    [[nodiscard]] const std::vector<Datum>& items() const;
    [[nodiscard]] std::vector<Datum>& items();
    [[nodiscard]] Datum deepCopyDatum() const;

    friend bool operator==(const List& lhs, const List& rhs);

private:
    std::vector<Datum> items_;
    bool sorted_;
};

class Datum::PropList {
public:
    explicit PropList(bool sorted = false);

    void put(Datum key, Datum value);
    void putSameType(Datum key, Datum value);
    void putTyped(Datum key, Datum value);
    [[nodiscard]] Datum get(const Datum& key) const;
    [[nodiscard]] bool contains(const Datum& key) const;
    [[nodiscard]] int findSameTypeKey(const Datum& key) const;
    [[nodiscard]] int findTypedKey(const Datum& key) const;
    [[nodiscard]] int findUntypedKey(const Datum& key) const;
    [[nodiscard]] int findUntypedKeyName(std::string_view keyName) const;
    [[nodiscard]] int count() const;
    [[nodiscard]] bool sorted() const;
    [[nodiscard]] const std::vector<std::pair<Datum, Datum>>& properties() const;
    [[nodiscard]] std::vector<std::pair<Datum, Datum>>& properties();
    void appendProperty(Datum key, Datum value);
    bool erasePropertyAt(int zeroBasedIndex);

    friend bool operator==(const PropList& lhs, const PropList& rhs);

private:
    void invalidateIndex() const;
    void indexEntry(std::size_t index) const;
    void rebuildIndex() const;

    std::vector<std::pair<Datum, Datum>> properties_;
    bool sorted_;
    mutable bool indexDirty_{true};
    mutable std::unordered_map<std::string, int> firstSameTypeKeyIndex_;
    mutable std::unordered_map<std::string,
                               int,
                               TransparentCaseInsensitiveStringHash,
                               TransparentCaseInsensitiveStringEqual>
        firstUntypedKeyIndex_;
    mutable std::unordered_map<std::string, int> firstCaseSensitiveKeyIndex_;
};

class Datum::ScriptInstanceRef {
public:
    ScriptInstanceRef(std::string scriptName, std::optional<CastMemberRef> scriptRef = std::nullopt);

    [[nodiscard]] const std::string& scriptName() const;
    [[nodiscard]] const std::optional<CastMemberRef>& scriptRef() const;
    [[nodiscard]] std::uint64_t identityId() const;
    [[nodiscard]] std::shared_ptr<ScriptInstanceRef> ancestor() const;
    [[nodiscard]] ScriptInstanceRef* ancestorRaw();
    [[nodiscard]] const ScriptInstanceRef* ancestorRaw() const;
    void setAncestor(std::shared_ptr<ScriptInstanceRef> ancestor);

    [[nodiscard]] Datum getProperty(const std::string& name) const;
    void setProperty(const std::string& name, Datum value);
    [[nodiscard]] bool hasProperty(const std::string& name) const;
    [[nodiscard]] int findExactPropertyIndex(std::string_view name) const;
    [[nodiscard]] int findCaseInsensitivePropertyIndex(std::string_view name) const;
    [[nodiscard]] int findPropertyIndex(std::string_view name) const;
    [[nodiscard]] Datum* findMutablePropertyValue(std::string_view name);
    [[nodiscard]] const std::vector<std::pair<std::string, Datum>>& properties() const;
    [[nodiscard]] std::vector<std::pair<std::string, Datum>>& properties();
    void reserveLocalProperties(std::size_t additionalCount);
    void appendLocalProperty(std::string name, Datum value);
    void putLocalPropertyExact(std::string name, Datum value);
    void putLocalPropertyExactView(std::string_view name, Datum value);
    bool eraseLocalPropertyExact(std::string_view name);

private:
    void invalidatePropertyIndex() const;
    void indexPropertyEntry(std::size_t index) const;
    void rebuildPropertyIndex() const;

    std::string scriptName_;
    std::optional<CastMemberRef> scriptRef_;
    std::uint64_t identityId_;
    std::vector<std::pair<std::string, Datum>> properties_;
    std::shared_ptr<ScriptInstanceRef> ancestor_;
    mutable bool propertyIndexDirty_{false};
    mutable std::unordered_map<std::string, int, TransparentStringHash, std::equal_to<>> firstExactPropertyIndex_;
    mutable std::unordered_map<std::string,
                               int,
                               TransparentCaseInsensitiveStringHash,
                               TransparentCaseInsensitiveStringEqual>
        firstCaseInsensitivePropertyIndex_;
};

class Datum::ArgList {
public:
    explicit ArgList(std::vector<Datum> args);
    [[nodiscard]] const std::vector<Datum>& args() const;

    friend bool operator==(const ArgList& lhs, const ArgList& rhs);

private:
    std::vector<Datum> args_;
};

class Datum::ArgListNoRet {
public:
    explicit ArgListNoRet(std::vector<Datum> args);
    [[nodiscard]] const std::vector<Datum>& args() const;

    friend bool operator==(const ArgListNoRet& lhs, const ArgListNoRet& rhs);

private:
    std::vector<Datum> args_;
};

} // namespace libreshockwave::lingo
