#include "json_value.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace libreshockwave::moviesource {
namespace {

// Reject deeply nested input instead of overflowing the parse stack.
constexpr int kMaxDepth = 64;

// A recursive-descent JSON reader. It is strict: trailing data, bad escapes,
// and truncated input are errors.
class JsonReader {
public:
    JsonReader(std::string_view text, std::string& error) : text_(text), error_(error) {}

    [[nodiscard]] bool read(JsonValue& out) {
        skipWhitespace();
        if (!parseValue(out, 0)) {
            return false;
        }
        skipWhitespace();
        if (position_ != text_.size()) {
            fail("unexpected trailing data");
            return false;
        }
        return true;
    }

private:
    [[nodiscard]] bool parseValue(JsonValue& out, int depth) {
        if (depth > kMaxDepth) {
            fail("nesting is too deep");
            return false;
        }
        if (position_ >= text_.size()) {
            fail("unexpected end of input");
            return false;
        }
        const char c = text_[position_];
        if (c == '{') {
            return parseObject(out, depth);
        }
        if (c == '[') {
            return parseArray(out, depth);
        }
        if (c == '"') {
            std::string value;
            if (!parseString(value)) {
                return false;
            }
            out = JsonValue::makeString(std::move(value));
            return true;
        }
        if (c == 't' || c == 'f') {
            return parseBool(out);
        }
        if (c == 'n') {
            return parseNull(out);
        }
        return parseNumber(out);
    }

    [[nodiscard]] bool parseObject(JsonValue& out, int depth) {
        ++position_; // {
        JsonValue::Object object;
        skipWhitespace();
        if (peek() == '}') {
            ++position_;
            out = JsonValue::makeObject(std::move(object));
            return true;
        }
        while (true) {
            skipWhitespace();
            if (peek() != '"') {
                fail("expected an object key");
                return false;
            }
            std::string key;
            if (!parseString(key)) {
                return false;
            }
            skipWhitespace();
            if (peek() != ':') {
                fail("expected ':' after an object key");
                return false;
            }
            ++position_;
            skipWhitespace();
            JsonValue value;
            if (!parseValue(value, depth + 1)) {
                return false;
            }
            object.emplace_back(std::move(key), std::move(value));
            skipWhitespace();
            const char next = peek();
            if (next == ',') {
                ++position_;
                continue;
            }
            if (next == '}') {
                ++position_;
                out = JsonValue::makeObject(std::move(object));
                return true;
            }
            fail("expected ',' or '}' in an object");
            return false;
        }
    }

    [[nodiscard]] bool parseArray(JsonValue& out, int depth) {
        ++position_; // [
        JsonValue::Array array;
        skipWhitespace();
        if (peek() == ']') {
            ++position_;
            out = JsonValue::makeArray(std::move(array));
            return true;
        }
        while (true) {
            skipWhitespace();
            JsonValue value;
            if (!parseValue(value, depth + 1)) {
                return false;
            }
            array.push_back(std::move(value));
            skipWhitespace();
            const char next = peek();
            if (next == ',') {
                ++position_;
                continue;
            }
            if (next == ']') {
                ++position_;
                out = JsonValue::makeArray(std::move(array));
                return true;
            }
            fail("expected ',' or ']' in an array");
            return false;
        }
    }

    [[nodiscard]] bool parseString(std::string& out) {
        ++position_; // opening quote
        out.clear();
        while (position_ < text_.size()) {
            const char c = text_[position_++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (position_ >= text_.size()) {
                fail("truncated escape sequence");
                return false;
            }
            const char escape = text_[position_++];
            switch (escape) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned int code = 0;
                    if (!parseHex4(code)) {
                        return false;
                    }
                    appendUtf8(out, code);
                    break;
                }
                default:
                    fail("unknown escape sequence");
                    return false;
            }
        }
        fail("unterminated string");
        return false;
    }

    [[nodiscard]] bool parseHex4(unsigned int& out) {
        if (position_ + 4 > text_.size()) {
            fail("truncated \\u escape");
            return false;
        }
        out = 0;
        for (int index = 0; index < 4; ++index) {
            const char c = text_[position_++];
            out <<= 4U;
            if (c >= '0' && c <= '9') {
                out |= static_cast<unsigned int>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                out |= static_cast<unsigned int>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                out |= static_cast<unsigned int>(c - 'A' + 10);
            } else {
                fail("invalid hex digit in \\u escape");
                return false;
            }
        }
        return true;
    }

    static void appendUtf8(std::string& out, unsigned int code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0U | (code >> 6U)));
            out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
        } else {
            out.push_back(static_cast<char>(0xE0U | (code >> 12U)));
            out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
        }
    }

    [[nodiscard]] bool parseBool(JsonValue& out) {
        if (text_.compare(position_, 4, "true") == 0) {
            position_ += 4;
            out = JsonValue::makeBool(true);
            return true;
        }
        if (text_.compare(position_, 5, "false") == 0) {
            position_ += 5;
            out = JsonValue::makeBool(false);
            return true;
        }
        fail("invalid literal");
        return false;
    }

    [[nodiscard]] bool parseNull(JsonValue& out) {
        if (text_.compare(position_, 4, "null") != 0) {
            fail("invalid literal");
            return false;
        }
        position_ += 4;
        out = JsonValue::makeNull();
        return true;
    }

    [[nodiscard]] bool parseNumber(JsonValue& out) {
        const std::size_t start = position_;
        if (peek() == '-') {
            ++position_;
        }
        while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) {
            ++position_;
        }
        if (peek() == '.') {
            ++position_;
            while (position_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                ++position_;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            ++position_;
            if (peek() == '+' || peek() == '-') {
                ++position_;
            }
            while (position_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                ++position_;
            }
        }
        if (position_ == start) {
            fail("invalid number");
            return false;
        }
        const std::string token(text_.substr(start, position_ - start));
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0' || !std::isfinite(value)) {
            fail("invalid number");
            return false;
        }
        out = JsonValue::makeNumber(value);
        return true;
    }

    void skipWhitespace() {
        while (position_ < text_.size() &&
               (text_[position_] == ' ' || text_[position_] == '\t' ||
                text_[position_] == '\n' || text_[position_] == '\r')) {
            ++position_;
        }
    }

    [[nodiscard]] char peek() const {
        return position_ < text_.size() ? text_[position_] : '\0';
    }

    void fail(std::string_view message) {
        error_ = std::string(message) + " at offset " + std::to_string(position_);
    }

    std::string_view text_;
    std::string& error_;
    std::size_t position_ = 0;
};

} // namespace

std::optional<JsonValue> JsonValue::parse(std::string_view text, std::string& error) {
    JsonValue value;
    JsonReader reader(text, error);
    if (!reader.read(value)) {
        return std::nullopt;
    }
    return value;
}

JsonValue JsonValue::makeBool(bool value) {
    JsonValue out;
    out.type_ = Type::Bool;
    out.bool_value_ = value;
    return out;
}

JsonValue JsonValue::makeNumber(double value) {
    JsonValue out;
    out.type_ = Type::Number;
    out.number_value_ = value;
    return out;
}

JsonValue JsonValue::makeString(std::string value) {
    JsonValue out;
    out.type_ = Type::String;
    out.string_value_ = std::move(value);
    return out;
}

JsonValue JsonValue::makeArray(Array value) {
    JsonValue out;
    out.type_ = Type::Array;
    out.array_value_ = std::move(value);
    return out;
}

JsonValue JsonValue::makeObject(Object value) {
    JsonValue out;
    out.type_ = Type::Object;
    out.object_value_ = std::move(value);
    return out;
}

bool JsonValue::asBool(bool fallback) const {
    return type_ == Type::Bool ? bool_value_ : fallback;
}

double JsonValue::asNumber(double fallback) const {
    return type_ == Type::Number ? number_value_ : fallback;
}

int JsonValue::asInt(int fallback) const {
    return type_ == Type::Number ? static_cast<int>(number_value_) : fallback;
}

std::string JsonValue::asString(std::string fallback) const {
    return type_ == Type::String ? string_value_ : std::move(fallback);
}

const JsonValue::Array& JsonValue::asArray() const {
    static const Array empty;
    return type_ == Type::Array ? array_value_ : empty;
}

const JsonValue::Object& JsonValue::asObject() const {
    static const Object empty;
    return type_ == Type::Object ? object_value_ : empty;
}

const JsonValue* JsonValue::find(std::string_view key) const {
    if (type_ != Type::Object) {
        return nullptr;
    }
    for (const auto& [name, value] : object_value_) {
        if (name == key) {
            return &value;
        }
    }
    return nullptr;
}

int JsonValue::intField(std::string_view key, int fallback) const {
    const JsonValue* value = find(key);
    return value != nullptr ? value->asInt(fallback) : fallback;
}

double JsonValue::numberField(std::string_view key, double fallback) const {
    const JsonValue* value = find(key);
    return value != nullptr ? value->asNumber(fallback) : fallback;
}

bool JsonValue::boolField(std::string_view key, bool fallback) const {
    const JsonValue* value = find(key);
    return value != nullptr ? value->asBool(fallback) : fallback;
}

std::string JsonValue::stringField(std::string_view key, std::string fallback) const {
    const JsonValue* value = find(key);
    return value != nullptr ? value->asString(std::move(fallback)) : fallback;
}

} // namespace libreshockwave::moviesource
