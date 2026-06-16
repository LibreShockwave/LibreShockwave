#pragma once

#include <algorithm>
#include <cctype>
#include <compare>
#include <optional>
#include <stdexcept>
#include <string>

namespace libreshockwave::id {

class FrameIndex;

class CastLibId {
public:
    explicit CastLibId(int value) : value_(value) {
        if (value < 1) {
            throw std::invalid_argument("CastLibId must be >= 1, got " + std::to_string(value));
        }
    }

    [[nodiscard]] int value() const { return value_; }
    [[nodiscard]] std::string toString() const { return "CastLibId(" + std::to_string(value_) + ")"; }

    friend auto operator<=>(const CastLibId& lhs, const CastLibId& rhs) = default;

private:
    int value_;
};

class ChannelId {
public:
    explicit ChannelId(int value) : value_(value) {
        if (value < 0) {
            throw std::invalid_argument("ChannelId must be >= 0, got " + std::to_string(value));
        }
    }

    [[nodiscard]] int value() const { return value_; }
    [[nodiscard]] std::string toString() const { return "ChannelId(" + std::to_string(value_) + ")"; }

    friend auto operator<=>(const ChannelId& lhs, const ChannelId& rhs) = default;

private:
    int value_;
};

class ChunkId {
public:
    explicit ChunkId(int value) : value_(value) {
        if (value < 0) {
            throw std::invalid_argument("ChunkId must be >= 0, got " + std::to_string(value));
        }
    }

    [[nodiscard]] int value() const { return value_; }
    [[nodiscard]] std::string toString() const { return "ChunkId(" + std::to_string(value_) + ")"; }

    friend auto operator<=>(const ChunkId& lhs, const ChunkId& rhs) = default;

private:
    int value_;
};

class FrameId {
public:
    explicit FrameId(int value) : value_(value) {
        if (value < 1) {
            throw std::invalid_argument("FrameId must be >= 1, got " + std::to_string(value));
        }
    }

    [[nodiscard]] int value() const { return value_; }
    [[nodiscard]] FrameIndex toIndex() const;
    [[nodiscard]] std::string toString() const { return "FrameId(" + std::to_string(value_) + ")"; }

    friend auto operator<=>(const FrameId& lhs, const FrameId& rhs) = default;

private:
    int value_;
};

class FrameIndex {
public:
    explicit FrameIndex(int value) : value_(value) {
        if (value < 0) {
            throw std::invalid_argument("FrameIndex must be >= 0, got " + std::to_string(value));
        }
    }

    [[nodiscard]] int value() const { return value_; }
    [[nodiscard]] FrameId toFrameId() const { return FrameId(value_ + 1); }
    [[nodiscard]] std::string toString() const { return "FrameIndex(" + std::to_string(value_) + ")"; }

    friend auto operator<=>(const FrameIndex& lhs, const FrameIndex& rhs) = default;

private:
    int value_;
};

inline FrameIndex FrameId::toIndex() const {
    return FrameIndex(value_ - 1);
}

class MemberId {
public:
    explicit MemberId(int value) : value_(value) {
        if (value < 0) {
            throw std::invalid_argument("MemberId must be >= 0, got " + std::to_string(value));
        }
    }

    [[nodiscard]] int value() const { return value_; }
    [[nodiscard]] std::string toString() const { return "MemberId(" + std::to_string(value_) + ")"; }

    friend auto operator<=>(const MemberId& lhs, const MemberId& rhs) = default;

private:
    int value_;
};

class NameId {
public:
    explicit NameId(int value) : value_(value) {
        if (value < 0) {
            throw std::invalid_argument("NameId must be >= 0, got " + std::to_string(value));
        }
    }

    [[nodiscard]] int value() const { return value_; }
    [[nodiscard]] std::string toString() const { return "NameId(" + std::to_string(value_) + ")"; }

    friend auto operator<=>(const NameId& lhs, const NameId& rhs) = default;

private:
    int value_;
};

class PaletteId {
public:
    explicit PaletteId(int value) : value_(value) {}

    [[nodiscard]] int value() const { return value_; }
    [[nodiscard]] bool isBuiltIn() const { return value_ < 0; }
    [[nodiscard]] std::string toString() const { return "PaletteId(" + std::to_string(value_) + ")"; }

    friend auto operator<=>(const PaletteId& lhs, const PaletteId& rhs) = default;

private:
    int value_;
};

class ScriptContextId {
public:
    explicit ScriptContextId(int value) : value_(value) {
        if (value < 1) {
            throw std::invalid_argument("ScriptContextId must be >= 1, got " + std::to_string(value));
        }
    }

    [[nodiscard]] int value() const { return value_; }
    [[nodiscard]] std::string toString() const { return "ScriptContextId(" + std::to_string(value_) + ")"; }

    friend auto operator<=>(const ScriptContextId& lhs, const ScriptContextId& rhs) = default;

private:
    int value_;
};

class SlotId {
public:
    explicit SlotId(int value) : value_(value) {}

    [[nodiscard]] static SlotId of(int castLib, int member) {
        return SlotId((castLib << 16) | (member & 0xFFFF));
    }

    [[nodiscard]] static SlotId of(CastLibId castLib, MemberId member) {
        return of(castLib.value(), member.value());
    }

    [[nodiscard]] int value() const { return value_; }
    [[nodiscard]] int castLib() const { return value_ >> 16; }
    [[nodiscard]] int member() const { return value_ & 0xFFFF; }

    [[nodiscard]] std::optional<CastLibId> castLibId() const {
        const int castLibValue = castLib();
        if (castLibValue >= 1) {
            return CastLibId(castLibValue);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<MemberId> memberId() const {
        const int memberValue = member();
        if (memberValue >= 1) {
            return MemberId(memberValue);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::string toString() const {
        return "SlotId(" + std::to_string(value_) +
               " = castLib " + std::to_string(castLib()) +
               ", member " + std::to_string(member()) + ")";
    }

    friend auto operator<=>(const SlotId& lhs, const SlotId& rhs) = default;

private:
    int value_;
};

enum class InkMode {
    COPY = 0,
    TRANSPARENT = 1,
    REVERSE = 2,
    GHOST = 3,
    NOT_COPY = 4,
    NOT_TRANSPARENT = 5,
    NOT_REVERSE = 6,
    NOT_GHOST = 7,
    MATTE = 8,
    MASK = 9,
    BLEND = 32,
    ADD_PIN = 33,
    ADD = 34,
    SUBTRACT_PIN = 35,
    BACKGROUND_TRANSPARENT = 36,
    LIGHTEST = 37,
    SUBTRACT = 38,
    DARKEST = 39,
    LIGHTEN = 40,
    DARKEN = 41
};

[[nodiscard]] inline int code(InkMode mode) {
    return static_cast<int>(mode);
}

[[nodiscard]] inline bool usesBlend(InkMode mode) {
    switch (mode) {
        case InkMode::BLEND:
        case InkMode::ADD_PIN:
        case InkMode::ADD:
        case InkMode::SUBTRACT_PIN:
        case InkMode::SUBTRACT:
        case InkMode::LIGHTEST:
        case InkMode::DARKEST:
        case InkMode::LIGHTEN:
        case InkMode::DARKEN:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] inline InkMode inkModeFromCode(int value) {
    switch (value) {
        case 0: return InkMode::COPY;
        case 1: return InkMode::TRANSPARENT;
        case 2: return InkMode::REVERSE;
        case 3: return InkMode::GHOST;
        case 4: return InkMode::NOT_COPY;
        case 5: return InkMode::NOT_TRANSPARENT;
        case 6: return InkMode::NOT_REVERSE;
        case 7: return InkMode::NOT_GHOST;
        case 8: return InkMode::MATTE;
        case 9: return InkMode::MASK;
        case 32: return InkMode::BLEND;
        case 33: return InkMode::ADD_PIN;
        case 34: return InkMode::ADD;
        case 35: return InkMode::SUBTRACT_PIN;
        case 36: return InkMode::BACKGROUND_TRANSPARENT;
        case 37: return InkMode::LIGHTEST;
        case 38: return InkMode::SUBTRACT;
        case 39: return InkMode::DARKEST;
        case 40: return InkMode::LIGHTEN;
        case 41: return InkMode::DARKEN;
        default: return InkMode::COPY;
    }
}

[[nodiscard]] inline std::optional<InkMode> inkModeFromName(std::string name) {
    name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char ch) {
        return ch == '_' || ch == '-' || ch == ' ';
    }), name.end());
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (name == "copy") return InkMode::COPY;
    if (name == "transparent") return InkMode::TRANSPARENT;
    if (name == "reverse") return InkMode::REVERSE;
    if (name == "ghost") return InkMode::GHOST;
    if (name == "notcopy") return InkMode::NOT_COPY;
    if (name == "nottransparent") return InkMode::NOT_TRANSPARENT;
    if (name == "notreverse") return InkMode::NOT_REVERSE;
    if (name == "notghost") return InkMode::NOT_GHOST;
    if (name == "matte") return InkMode::MATTE;
    if (name == "mask") return InkMode::MASK;
    if (name == "blend") return InkMode::BLEND;
    if (name == "addpin") return InkMode::ADD_PIN;
    if (name == "add") return InkMode::ADD;
    if (name == "subtractpin") return InkMode::SUBTRACT_PIN;
    if (name == "backgroundtransparent" || name == "bgtransparent") return InkMode::BACKGROUND_TRANSPARENT;
    if (name == "lightest") return InkMode::LIGHTEST;
    if (name == "subtract") return InkMode::SUBTRACT;
    if (name == "darkest") return InkMode::DARKEST;
    if (name == "lighten") return InkMode::LIGHTEN;
    if (name == "darken") return InkMode::DARKEN;
    return std::nullopt;
}

enum class VarType {
    GLOBAL = 0x1,
    GLOBAL2 = 0x2,
    PROPERTY = 0x3,
    PARAM = 0x4,
    LOCAL = 0x5,
    FIELD = 0x6
};

[[nodiscard]] inline int code(VarType type) {
    return static_cast<int>(type);
}

[[nodiscard]] inline VarType varTypeFromCode(int value) {
    switch (value) {
        case 0x1: return VarType::GLOBAL;
        case 0x2: return VarType::GLOBAL2;
        case 0x3: return VarType::PROPERTY;
        case 0x4: return VarType::PARAM;
        case 0x5: return VarType::LOCAL;
        case 0x6: return VarType::FIELD;
        default:
            throw std::invalid_argument("Unknown VarType code: 0x" + std::to_string(value));
    }
}

} // namespace libreshockwave::id
