#pragma once

#include "libreshockwave/lingo/datum.hpp"

#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

namespace libreshockwave::lingo::vm::dispatch {

class ListMethodDispatcher {
public:
    [[nodiscard]] static Datum dispatch(Datum::List& list,
                                        std::string_view method_name,
                                        std::span<const Datum> args);
    [[nodiscard]] static Datum dispatch(Datum::List& list,
                                        std::string_view method_name,
                                        const std::vector<Datum>& args) {
        return dispatch(list, method_name, std::span<const Datum>(args));
    }
    [[nodiscard]] static Datum dispatch(Datum::List& list,
                                        std::string_view method_name,
                                        std::initializer_list<Datum> args) {
        return dispatch(list, method_name, std::span<const Datum>(args.begin(), args.size()));
    }
};

} // namespace libreshockwave::lingo::vm::dispatch
