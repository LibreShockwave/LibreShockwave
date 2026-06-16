#pragma once

#include <optional>
#include <string>

#include "libreshockwave/lingo/Datum.hpp"

namespace libreshockwave::player::debug {

struct WatchExpression {
    std::string id;
    std::string expression;
    std::optional<lingo::Datum> lastValue;
    std::optional<std::string> lastError;

    [[nodiscard]] static WatchExpression create(std::string expression);
    [[nodiscard]] static WatchExpression create(std::string id, std::string expression);

    [[nodiscard]] bool hasError() const;
    [[nodiscard]] bool isEvaluated() const;
    [[nodiscard]] WatchExpression withExpression(std::string expression) const;
    [[nodiscard]] WatchExpression withValue(lingo::Datum value) const;
    [[nodiscard]] WatchExpression withError(std::string error) const;
    [[nodiscard]] std::string getResultDisplay() const;
    [[nodiscard]] std::string getTypeName() const;
    [[nodiscard]] std::string toString() const;

    friend bool operator==(const WatchExpression&, const WatchExpression&) = default;
};

} // namespace libreshockwave::player::debug
