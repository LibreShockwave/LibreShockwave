#include "libreshockwave/player/debug/watch_expression.hpp"

#include <atomic>
#include <utility>

namespace libreshockwave::player::debug {
namespace {

std::atomic<unsigned long long> g_watch_id_counter{1};

} // namespace

WatchExpression WatchExpression::create(std::string expression) {
    const auto id = "watch-" + std::to_string(g_watch_id_counter.fetch_add(1));
    return WatchExpression::create(id, std::move(expression));
}

WatchExpression WatchExpression::create(std::string id, std::string expression) {
    return WatchExpression{std::move(id), std::move(expression), std::nullopt, std::nullopt};
}

bool WatchExpression::hasError() const {
    return last_error.has_value();
}

bool WatchExpression::isEvaluated() const {
    return last_value.has_value() || last_error.has_value();
}

WatchExpression WatchExpression::withExpression(std::string new_expression) const {
    return WatchExpression{id, std::move(new_expression), std::nullopt, std::nullopt};
}

WatchExpression WatchExpression::withValue(lingo::Datum value) const {
    return WatchExpression{id, expression, std::move(value), std::nullopt};
}

WatchExpression WatchExpression::withError(std::string error) const {
    return WatchExpression{id, expression, std::nullopt, std::move(error)};
}

std::string WatchExpression::getResultDisplay() const {
    if (last_error.has_value()) {
        return "<" + *last_error + ">";
    }
    if (last_value.has_value()) {
        return last_value->stringValue();
    }
    return "<not evaluated>";
}

std::string WatchExpression::getTypeName() const {
    if (last_error.has_value()) {
        return "Error";
    }
    if (last_value.has_value()) {
        return last_value->typeString();
    }
    return "-";
}

std::string WatchExpression::toString() const {
    return "Watch[" + expression + " = " + getResultDisplay() + "]";
}

} // namespace libreshockwave::player::debug
