#pragma once

#include "libreshockwave/lingo/Datum.hpp"

#include <string_view>
#include <vector>

namespace libreshockwave::lingo::vm::dispatch {

class StringMethodDispatcher {
public:
    [[nodiscard]] static Datum dispatch(std::string_view value,
                                        std::string_view methodName,
                                        const std::vector<Datum>& args,
                                        char itemDelimiter = ',');
};

} // namespace libreshockwave::lingo::vm::dispatch
