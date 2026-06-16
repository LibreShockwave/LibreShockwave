#pragma once

#include "libreshockwave/lingo/Datum.hpp"

#include <string_view>
#include <vector>

namespace libreshockwave::lingo::vm {
class ExecutionContext;
} // namespace libreshockwave::lingo::vm

namespace libreshockwave::lingo::vm::dispatch {

class ScriptInstanceMethodDispatcher {
public:
    [[nodiscard]] static Datum dispatch(ExecutionContext& context,
                                        Datum& receiver,
                                        std::string_view methodName,
                                        const std::vector<Datum>& args);
};

} // namespace libreshockwave::lingo::vm::dispatch
