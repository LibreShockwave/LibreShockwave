#pragma once

#include <map>
#include <optional>
#include <string>

#include "libreshockwave/lingo/Datum.hpp"

namespace libreshockwave::player::debug {

class ExpressionEvaluator {
public:
    using DatumMap = std::map<std::string, lingo::Datum>;

    struct EvaluationContext {
        DatumMap locals;
        DatumMap params;
        DatumMap globals;
        std::optional<lingo::Datum> receiver;

        [[nodiscard]] static EvaluationContext empty();
        [[nodiscard]] lingo::Datum lookupVariable(const std::string& name) const;
    };

    struct EvalResult {
        std::optional<lingo::Datum> value;
        std::optional<std::string> error;

        [[nodiscard]] static EvalResult success(lingo::Datum value);
        [[nodiscard]] static EvalResult failure(std::string message);
        [[nodiscard]] bool succeeded() const;
    };

    [[nodiscard]] EvalResult evaluate(const std::string& expression, const EvaluationContext& context) const;
    [[nodiscard]] bool evaluateCondition(const std::string& expression, const EvaluationContext& context) const;
    [[nodiscard]] std::string interpolateLogMessage(const std::string& message,
                                                    const EvaluationContext& context) const;
};

} // namespace libreshockwave::player::debug
