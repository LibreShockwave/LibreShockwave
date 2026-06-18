#pragma once

#include "libreshockwave/lingo/Datum.hpp"

#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

namespace libreshockwave::lingo::vm::dispatch {

class StringMethodDispatcher {
public:
    [[nodiscard]] static Datum dispatch(std::string_view value,
                                        std::string_view methodName,
                                        std::span<const Datum> args,
                                        char itemDelimiter = ',');
    [[nodiscard]] static Datum dispatch(std::string_view value,
                                        std::string_view methodName,
                                        const std::vector<Datum>& args,
                                        char itemDelimiter = ',') {
        return dispatch(value, methodName, std::span<const Datum>(args), itemDelimiter);
    }
    [[nodiscard]] static Datum dispatch(std::string_view value,
                                        std::string_view methodName,
                                        std::initializer_list<Datum> args,
                                        char itemDelimiter = ',') {
        return dispatch(value, methodName, std::span<const Datum>(args.begin(), args.size()), itemDelimiter);
    }
};

} // namespace libreshockwave::lingo::vm::dispatch
