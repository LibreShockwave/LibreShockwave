#pragma once

#include <string_view>

#include "libreshockwave/lingo/datum.hpp"
#include "libreshockwave/lingo/vm/trace_listener.hpp"
#include "libreshockwave/player/sprite/sprite_state.hpp"

namespace libreshockwave::player::debug {

class LifecycleDiagnostics {
public:
    static void setEnabled(bool enabled);
    [[nodiscard]] static bool isEnabled();
    [[nodiscard]] static bool isInterestingHandler(std::string_view handler_name);

    static void logHandlerEnter(const lingo::vm::TraceListener::HandlerInfo& info);
    static void logHandlerExit(const lingo::vm::TraceListener::HandlerInfo& info,
                               const lingo::Datum& return_value);
    static void logExternalCastLoaded(int cast_lib_number, std::string_view file_name);
    static void logSpriteRemoved(std::string_view reason, const sprite::SpriteState& state);
    static void logSpriteMemberCleared(std::string_view reason,
                                       const sprite::SpriteState& state,
                                       int retired_cast_lib,
                                       int retired_member_num);
    static void logSpriteEmptyOverride(std::string_view reason, const sprite::SpriteState& state);
    static void logReleasedEmptyChannel(std::string_view reason, const sprite::SpriteState& state);
    static void logError(std::string_view message, std::string_view error_detail = {});
};

} // namespace libreshockwave::player::debug
