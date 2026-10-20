#pragma once

#include "libreshockwave/lingo/datum.hpp"

#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

namespace libreshockwave::lingo::vm::dispatch {

class StringMethodDispatcher {
public:
    [[nodiscard]] static Datum dispatch(std::string_view value,
                                        std::string_view method_name,
                                        std::span<const Datum> args,
                                        char item_delimiter = ',');
    [[nodiscard]] static Datum dispatch(std::string_view value,
                                        std::string_view method_name,
                                        const std::vector<Datum>& args,
                                        char item_delimiter = ',') {
        return dispatch(value, method_name, std::span<const Datum>(args), item_delimiter);
    }
    [[nodiscard]] static Datum dispatch(std::string_view value,
                                        std::string_view method_name,
                                        std::initializer_list<Datum> args,
                                        char item_delimiter = ',') {
        return dispatch(value, method_name, std::span<const Datum>(args.begin(), args.size()), item_delimiter);
    }
};

} // namespace libreshockwave::lingo::vm::dispatch
