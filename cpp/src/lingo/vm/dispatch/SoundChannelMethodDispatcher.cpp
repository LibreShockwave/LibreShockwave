#include "libreshockwave/lingo/vm/dispatch/SoundChannelMethodDispatcher.hpp"

#include "libreshockwave/lingo/builtin/BuiltinRegistry.hpp"

#include <utility>

namespace libreshockwave::lingo::vm::dispatch {

Datum SoundChannelMethodDispatcher::dispatch(builtin::BuiltinContext* context,
                                             const Datum::SoundChannel& channel,
                                             std::string_view methodName,
                                             const std::vector<Datum>& args) {
    return context != nullptr ? builtin::SoundBuiltins::handleMethod(*context, channel, methodName, args)
                              : Datum::voidValue();
}

Datum SoundChannelMethodDispatcher::getProperty(builtin::BuiltinContext* context,
                                                const Datum::SoundChannel& channel,
                                                std::string_view propName) {
    return context != nullptr ? builtin::SoundBuiltins::getProperty(*context, channel, propName)
                              : Datum::voidValue();
}

bool SoundChannelMethodDispatcher::setProperty(builtin::BuiltinContext* context,
                                               const Datum::SoundChannel& channel,
                                               std::string_view propName,
                                               Datum value) {
    return context != nullptr && builtin::SoundBuiltins::setProperty(*context, channel, propName, std::move(value));
}

} // namespace libreshockwave::lingo::vm::dispatch
