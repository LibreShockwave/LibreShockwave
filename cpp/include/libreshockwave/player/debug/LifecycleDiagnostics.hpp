#pragma once

#include <string_view>

#include "libreshockwave/lingo/Datum.hpp"
#include "libreshockwave/lingo/vm/TraceListener.hpp"
#include "libreshockwave/player/sprite/SpriteState.hpp"

namespace libreshockwave::player::debug {

class LifecycleDiagnostics {
public:
    static void setEnabled(bool enabled);
    [[nodiscard]] static bool isEnabled();
    [[nodiscard]] static bool isInterestingHandler(std::string_view handlerName);

    static void logHandlerEnter(const lingo::vm::TraceListener::HandlerInfo& info);
    static void logHandlerExit(const lingo::vm::TraceListener::HandlerInfo& info,
                               const lingo::Datum& returnValue);
    static void logExternalCastLoaded(int castLibNumber, std::string_view fileName);
    static void logSpriteRemoved(std::string_view reason, const sprite::SpriteState& state);
    static void logSpriteMemberCleared(std::string_view reason,
                                       const sprite::SpriteState& state,
                                       int retiredCastLib,
                                       int retiredMemberNum);
    static void logSpriteEmptyOverride(std::string_view reason, const sprite::SpriteState& state);
    static void logReleasedEmptyChannel(std::string_view reason, const sprite::SpriteState& state);
    static void logError(std::string_view message, std::string_view errorDetail = {});
};

} // namespace libreshockwave::player::debug
