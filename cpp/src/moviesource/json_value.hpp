#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace libreshockwave::moviesource {

// A parsed JSON value. It is the data model for the .lsw manifest, config, and
// score descriptions. Objects preserve key order, so the manifest can be read
// back deterministically.
//
// Parse failures are reported through the error string of parse(). Accessors
// are total: they return the fallback or an empty value when the type does not
// match, so callers validate the fields they need.
class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<JsonValue>;
    using Object = std::vector<std::pair<std::string, JsonValue>>;

    [[nodiscard]] static std::optional<JsonValue> parse(std::string_view text,
                                                        std::string& error);

    // Value factories used by the parser and by tests of the manifest layer.
    [[nodiscard]] static JsonValue makeNull() { return JsonValue{}; }
    [[nodiscard]] static JsonValue makeBool(bool value);
    [[nodiscard]] static JsonValue makeNumber(double value);
    [[nodiscard]] static JsonValue makeString(std::string value);
    [[nodiscard]] static JsonValue makeArray(Array value);
    [[nodiscard]] static JsonValue makeObject(Object value);

    [[nodiscard]] Type type() const { return type_; }
    [[nodiscard]] bool isNull() const { return type_ == Type::Null; }
    [[nodiscard]] bool isBool() const { return type_ == Type::Bool; }
    [[nodiscard]] bool isNumber() const { return type_ == Type::Number; }
    [[nodiscard]] bool isString() const { return type_ == Type::String; }
    [[nodiscard]] bool isArray() const { return type_ == Type::Array; }
    [[nodiscard]] bool isObject() const { return type_ == Type::Object; }

    [[nodiscard]] bool asBool(bool fallback = false) const;
    [[nodiscard]] double asNumber(double fallback = 0.0) const;
    [[nodiscard]] int asInt(int fallback = 0) const;
    [[nodiscard]] std::string asString(std::string fallback = {}) const;
    [[nodiscard]] const Array& asArray() const;
    [[nodiscard]] const Object& asObject() const;

    // Object lookup. Returns nullptr when the value is not an object or the
    // key is absent.
    [[nodiscard]] const JsonValue* find(std::string_view key) const;

    // Convenience typed lookup with fallbacks.
    [[nodiscard]] int intField(std::string_view key, int fallback = 0) const;
    [[nodiscard]] double numberField(std::string_view key, double fallback = 0.0) const;
    [[nodiscard]] bool boolField(std::string_view key, bool fallback = false) const;
    [[nodiscard]] std::string stringField(std::string_view key,
                                          std::string fallback = {}) const;

private:
    Type type_ = Type::Null;
    bool bool_value_ = false;
    double number_value_ = 0.0;
    std::string string_value_;
    Array array_value_;
    Object object_value_;
};

} // namespace libreshockwave::moviesource
