#include "libreshockwave/lingo/vm/dispatch/sound_channel_method_dispatcher.hpp"

#include "libreshockwave/lingo/builtin/builtin_registry.hpp"

#include <utility>

namespace libreshockwave::lingo::vm::dispatch {

Datum SoundChannelMethodDispatcher::dispatch(builtin::BuiltinContext* context,
                                             const Datum::SoundChannel& channel,
                                             std::string_view method_name,
                                             const std::vector<Datum>& args) {
    return context != nullptr ? builtin::SoundBuiltins::handleMethod(*context, channel, method_name, args)
                              : Datum::voidValue();
}

Datum SoundChannelMethodDispatcher::getProperty(builtin::BuiltinContext* context,
                                                const Datum::SoundChannel& channel,
                                                std::string_view prop_name) {
    return context != nullptr ? builtin::SoundBuiltins::getProperty(*context, channel, prop_name)
                              : Datum::voidValue();
}

bool SoundChannelMethodDispatcher::setProperty(builtin::BuiltinContext* context,
                                               const Datum::SoundChannel& channel,
                                               std::string_view prop_name,
                                               Datum value) {
    return context != nullptr && builtin::SoundBuiltins::setProperty(*context, channel, prop_name, std::move(value));
}

} // namespace libreshockwave::lingo::vm::dispatch
