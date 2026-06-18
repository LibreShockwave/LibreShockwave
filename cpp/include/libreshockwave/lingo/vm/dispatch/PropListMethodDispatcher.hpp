#pragma once

#include "libreshockwave/lingo/Datum.hpp"

#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

namespace libreshockwave::lingo::vm::dispatch {

class PropListMethodDispatcher {
public:
    [[nodiscard]] static Datum dispatch(Datum::PropList& propList,
                                        std::string_view methodName,
                                        std::span<const Datum> args);
    [[nodiscard]] static Datum dispatch(Datum::PropList& propList,
                                        std::string_view methodName,
                                        const std::vector<Datum>& args) {
        return dispatch(propList, methodName, std::span<const Datum>(args));
    }
    [[nodiscard]] static Datum dispatch(Datum::PropList& propList,
                                        std::string_view methodName,
                                        std::initializer_list<Datum> args) {
        return dispatch(propList, methodName, std::span<const Datum>(args.begin(), args.size()));
    }
};

} // namespace libreshockwave::lingo::vm::dispatch
