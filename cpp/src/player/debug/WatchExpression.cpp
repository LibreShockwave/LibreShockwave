#include "libreshockwave/player/debug/WatchExpression.hpp"

#include <atomic>
#include <utility>

namespace libreshockwave::player::debug {
namespace {

std::atomic<unsigned long long> gWatchIdCounter{1};

} // namespace

WatchExpression WatchExpression::create(std::string expression) {
    const auto id = "watch-" + std::to_string(gWatchIdCounter.fetch_add(1));
    return WatchExpression::create(id, std::move(expression));
}

WatchExpression WatchExpression::create(std::string id, std::string expression) {
    return WatchExpression{std::move(id), std::move(expression), std::nullopt, std::nullopt};
}

bool WatchExpression::hasError() const {
    return lastError.has_value();
}

bool WatchExpression::isEvaluated() const {
    return lastValue.has_value() || lastError.has_value();
}

WatchExpression WatchExpression::withExpression(std::string newExpression) const {
    return WatchExpression{id, std::move(newExpression), std::nullopt, std::nullopt};
}

WatchExpression WatchExpression::withValue(lingo::Datum value) const {
    return WatchExpression{id, expression, std::move(value), std::nullopt};
}

WatchExpression WatchExpression::withError(std::string error) const {
    return WatchExpression{id, expression, std::nullopt, std::move(error)};
}

std::string WatchExpression::getResultDisplay() const {
    if (lastError.has_value()) {
        return "<" + *lastError + ">";
    }
    if (lastValue.has_value()) {
        return lastValue->stringValue();
    }
    return "<not evaluated>";
}

std::string WatchExpression::getTypeName() const {
    if (lastError.has_value()) {
        return "Error";
    }
    if (lastValue.has_value()) {
        return lastValue->typeString();
    }
    return "-";
}

std::string WatchExpression::toString() const {
    return "Watch[" + expression + " = " + getResultDisplay() + "]";
}

} // namespace libreshockwave::player::debug
