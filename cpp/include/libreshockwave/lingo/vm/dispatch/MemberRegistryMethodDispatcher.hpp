#pragma once

#include "libreshockwave/lingo/Datum.hpp"

#include <string_view>
#include <vector>

namespace libreshockwave::lingo::builtin {
struct BuiltinContext;
} // namespace libreshockwave::lingo::builtin

namespace libreshockwave::lingo::vm::dispatch {

class MemberRegistryMethodDispatcher {
public:
    struct DispatchResult {
        bool handled{false};
        Datum value{Datum::voidValue()};
    };

    [[nodiscard]] static bool isMethod(std::string_view methodName);
    [[nodiscard]] static DispatchResult prefill(Datum::ScriptInstanceRef& instance,
                                                std::string_view methodName,
                                                const std::vector<Datum>& args,
                                                const builtin::BuiltinContext* context);
    [[nodiscard]] static DispatchResult dispatch(Datum::ScriptInstanceRef& instance,
                                                 std::string_view methodName,
                                                 const std::vector<Datum>& args,
                                                 const builtin::BuiltinContext* context);
};

} // namespace libreshockwave::lingo::vm::dispatch
