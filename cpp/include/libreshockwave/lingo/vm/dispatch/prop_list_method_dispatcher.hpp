#pragma once

#include "libreshockwave/lingo/datum.hpp"

#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

namespace libreshockwave::lingo::vm::dispatch {

class PropListMethodDispatcher {
public:
    [[nodiscard]] static Datum dispatch(Datum::PropList& prop_list,
                                        std::string_view method_name,
                                        std::span<const Datum> args);
    [[nodiscard]] static Datum dispatch(Datum::PropList& prop_list,
                                        std::string_view method_name,
                                        const std::vector<Datum>& args) {
        return dispatch(prop_list, method_name, std::span<const Datum>(args));
    }
    [[nodiscard]] static Datum dispatch(Datum::PropList& prop_list,
                                        std::string_view method_name,
                                        std::initializer_list<Datum> args) {
        return dispatch(prop_list, method_name, std::span<const Datum>(args.begin(), args.size()));
    }
};

} // namespace libreshockwave::lingo::vm::dispatch
