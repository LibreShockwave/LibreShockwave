#pragma once

#include <memory>

namespace libreshockwave::player::audio {
class AudioBackend;
}

namespace libreshockwave::debugger {

/// Creates the desktop debugger's native audio backend. Returns null when the
/// build has no usable audio decoder or output device support.
std::unique_ptr<player::audio::AudioBackend> createDebuggerAudioBackend();

} // namespace libreshockwave::debugger
