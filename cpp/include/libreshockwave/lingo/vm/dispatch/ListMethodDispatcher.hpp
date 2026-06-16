#pragma once

#include "libreshockwave/lingo/Datum.hpp"

#include <string_view>
#include <vector>

namespace libreshockwave::lingo::vm::dispatch {

class ListMethodDispatcher {
public:
    [[nodiscard]] static Datum dispatch(Datum::List& list,
                                        std::string_view methodName,
                                        const std::vector<Datum>& args);
};

} // namespace libreshockwave::lingo::vm::dispatch
