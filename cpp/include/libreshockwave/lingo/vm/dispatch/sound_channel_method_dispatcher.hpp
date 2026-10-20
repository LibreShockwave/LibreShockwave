#pragma once

#include "libreshockwave/lingo/datum.hpp"

#include <string_view>
#include <vector>

namespace libreshockwave::lingo::builtin {
struct BuiltinContext;
} // namespace libreshockwave::lingo::builtin

namespace libreshockwave::lingo::vm::dispatch {

class SoundChannelMethodDispatcher {
public:
    [[nodiscard]] static Datum dispatch(builtin::BuiltinContext* context,
                                        const Datum::SoundChannel& channel,
                                        std::string_view method_name,
                                        const std::vector<Datum>& args);
    [[nodiscard]] static Datum getProperty(builtin::BuiltinContext* context,
                                           const Datum::SoundChannel& channel,
                                           std::string_view prop_name);
    static bool setProperty(builtin::BuiltinContext* context,
                            const Datum::SoundChannel& channel,
                            std::string_view prop_name,
                            Datum value);
};

} // namespace libreshockwave::lingo::vm::dispatch
