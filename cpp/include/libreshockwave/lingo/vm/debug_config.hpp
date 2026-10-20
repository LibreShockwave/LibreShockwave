#pragma once

namespace libreshockwave::lingo::vm {

class DebugConfig {
public:
    [[nodiscard]] static bool isDebugPlaybackEnabled();
    static void setDebugPlaybackEnabled(bool enabled);
};

} // namespace libreshockwave::lingo::vm
